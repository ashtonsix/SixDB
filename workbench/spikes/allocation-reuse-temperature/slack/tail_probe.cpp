// Diagnostic only: out-of-band madvise deliberately leaves Orbital's charge
// unchanged. No old readers/Holds exist; only page-aligned private slack is advised.
// Async advice must settle before growth exposes this range: advice dispatched
// after new stores can lose those stores. Stores after completed MADV_FREE cancel
// lazy reclaim on the pages written; FREE does not promise immediate eviction.
#include <orbital/allocation.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>

namespace {
using namespace orbital;
namespace a = allocation;
using Clock = std::chrono::steady_clock;
constexpr std::size_t large = 65536, small = 4096;

void require(bool condition, const char* reason) {
  if (!condition) { std::fprintf(stderr, "%s\n", reason); std::abort(); }
}

template<class T> T take(Result<T> result) {
  require(result.has_value(), "Orbital operation refused");
  return std::move(*result);
}

long faults() {
  rusage usage{};
  require(::getrusage(RUSAGE_THREAD, &usage) == 0, "getrusage failed");
  return usage.ru_minflt;
}

long long elapsed(Clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}

std::size_t resident(std::byte* address, std::size_t extent, std::size_t page) {
  std::array<unsigned char, 16> state{};
  require(extent % page == 0 && extent / page <= state.size(), "invalid residency range");
  require(::mincore(address, extent, state.data()) == 0, "mincore failed");
  std::size_t total = 0;
  for (std::size_t index = 0; index < extent / page; ++index) total += state[index] & 1;
  return total; // No mapped data is read or written by this observation.
}

void row(const char* mode, const char* stage, const a::Allocation& value,
    const a::Pool& pool, std::size_t page, long long ns = 0, long minor = 0) {
  const auto s = a::statistics(pool);
  const auto count = value ? resident(value.address, a::capacity(value), page) : 0;
  std::printf("%s,%s,%zu,%zu,%zu,%llu,%llu,%llu,%llu,%llu,%zu,%lld,%ld\n",
      mode, stage, value.size, a::capacity(value), a::reserved_bytes(value),
      static_cast<unsigned long long>(s.mapped_bytes),
      static_cast<unsigned long long>(s.reserved_bytes),
      static_cast<unsigned long long>(s.idle_bytes),
      static_cast<unsigned long long>(s.metadata_bytes),
      static_cast<unsigned long long>(pool.arena->charged_bytes.load()), count, ns, minor);
}

void prefix(const a::Allocation& value) {
  for (std::size_t offset = 0; offset < small; ++offset)
    require(value.address[offset] == std::byte{0x5a}, "live prefix changed");
}
} // namespace

int main(int argc, char** argv) {
  if (argc != 2 || (std::strcmp(argv[1], "control") &&
      std::strcmp(argv[1], "dontneed") && std::strcmp(argv[1], "free"))) {
    std::fprintf(stderr, "usage: tail_probe control|dontneed|free\n"); return 2;
  }
  const char* mode = argv[1];
  const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  if (page != 4096) {
    std::fprintf(stderr, "this 64KiB/4KiB diagnostic requires 4KiB base pages\n"); return 77;
  }
  auto arena = take(a::open_arena(16 * 1024 * 1024));
  worker::Lane lane;
  require(worker::adopt(lane).has_value(), "Lane adoption refused");
  auto* pool = take(a::add_pool(*arena, lane));
  auto value = take(a::allocate(*pool, {.bytes = large}));
  require(a::capacity(value) == large, "unexpected large capacity");
  for (std::size_t offset = 0; offset < large; offset += page)
    *static_cast<volatile std::byte*>(value.address + offset) = std::byte{0x37};
  auto* original_address = value.address;
  const auto* original_storage = value.storage;
  std::puts("mode,stage,size,capacity,reserved,pool_mapped,pool_reserved,pool_idle,pool_metadata,arena_charged,resident_pages,operation_ns,minor_faults");
  row(mode, "large_dirty", value, *pool, page);
  a::release(value, {.temperature = a::Temperature::hot});
  value = take(a::allocate(*pool, {.bytes = small}));
  require(value.address == original_address && value.storage == original_storage,
      "small allocation did not reuse the large Window");
  std::memset(value.address, 0x5a, small);
  row(mode, "small_reused", value, *pool, page);
  const auto before = a::statistics(*pool);
  const auto charge = arena->charged_bytes.load();
  const auto keep = ((value.size + value.readable_padding + page - 1) / page) * page;
  const auto tail_size = a::capacity(value) - keep;
  require(tail_size == large - small, "unexpected rounded tail");

  (void)Clock::now(); // Exclude first clock-symbol/page setup from fault deltas.
  (void)faults();
  auto prior_faults = faults();
  auto start = Clock::now();
  if (std::strcmp(mode, "control")) {
    const int advice = !std::strcmp(mode, "dontneed") ? MADV_DONTNEED : MADV_FREE;
    require(::madvise(value.address + keep, tail_size, advice) == 0, "madvise failed");
  }
  const auto advice_ns = elapsed(start);
  const auto advice_faults = faults() - prior_faults;
  row(mode, "after_advice", value, *pool, page, advice_ns, advice_faults);
  prefix(value);
  const auto after = a::statistics(*pool);
  require(before.mapped_bytes == after.mapped_bytes &&
      before.reserved_bytes == after.reserved_bytes &&
      before.metadata_bytes == after.metadata_bytes && arena->charged_bytes.load() == charge,
      "raw advice unexpectedly changed allocator accounting");

  prior_faults = faults(); start = Clock::now();
  require(a::grow(value, {.bytes = large}).has_value(), "regrowth refused");
  const auto grow_ns = elapsed(start);
  const auto grow_faults = faults() - prior_faults;
  require(value.address == original_address, "regrowth relocated");
  row(mode, "grown_before_touch", value, *pool, page, grow_ns, grow_faults);
  prior_faults = faults(); start = Clock::now();
  for (std::size_t offset = keep; offset < large; offset += page)
    *static_cast<volatile std::byte*>(value.address + offset) = std::byte{0x63};
  const auto touch_ns = elapsed(start);
  const auto touch_faults = faults() - prior_faults;
  row(mode, "grown_after_touch", value, *pool, page, touch_ns, touch_faults);
  prefix(value);
  for (std::size_t offset = keep; offset < large; offset += page)
    require(value.address[offset] == std::byte{0x63}, "regrown tail write lost");

  a::release(value);
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  for (;;) {
    const auto progress = a::close(*pool);
    require(progress.kind() != Progress::Kind::failed, "Pool close failed");
    if (progress.kind() == Progress::Kind::done) break;
    require(Clock::now() < deadline, "Pool close timed out");
    worker::poll(lane, 64); std::this_thread::yield();
  }
  row(mode, "closed", value, *pool, page);
  const auto closed = a::statistics(*pool);
  require(!closed.mapped_bytes && !closed.reserved_bytes && !closed.metadata_bytes &&
      !arena->charged_bytes.load() && !worker::pending(lane), "close stranded resources");
  require(a::remove_pool(*pool).has_value(), "Pool removal refused");
  worker::relinquish(lane);
}
