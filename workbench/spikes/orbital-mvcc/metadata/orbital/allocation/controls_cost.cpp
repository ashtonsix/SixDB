// Matched Stage1/control-layout traces. Batch quantiles are not point latency.
#include <orbital/allocation.h>
#include "finite.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace oa = orbital::allocation;
namespace ow = orbital::worker;
namespace ad = orbital::allocation::detail;
using Clock = std::chrono::steady_clock;
constexpr std::size_t width = 64;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (false)
template<class T> T need(orbital::Result<T> value) { CHECK(value); return std::move(*value); }
void need(orbital::Result<void> value) { CHECK(value); }
double elapsed(Clock::time_point start) {
  return std::chrono::duration<double, std::nano>(Clock::now() - start).count();
}

struct Samples {
  std::vector<double> ns;
  explicit Samples(std::size_t count) { ns.reserve(count); }
  void emit(const char* phase, char palette, std::size_t units,
      std::uint64_t metadata = 0, std::uint64_t pages = 0) {
    CHECK(!ns.empty());
    std::sort(ns.begin(), ns.end());
    const auto q = [&](double fraction) {
      return ns[static_cast<std::size_t>((ns.size() - 1) * fraction)] / units;
    };
    double total = 0; for (const auto n : ns) total += n;
    std::printf("{\"phase\":\"%s\",\"palette\":\"%c\",\"samples\":%zu,"
        "\"units_per_sample\":%zu,\"p50_ns_per_unit\":%.3f,\"p99_ns_per_unit\":%.3f,"
        "\"total_ns\":%.0f,\"metadata_bytes\":%llu,\"trim_pages\":%llu}\n",
        phase, palette, ns.size(), units, q(.50), q(.99), total,
        static_cast<unsigned long long>(metadata), static_cast<unsigned long long>(pages));
  }
};

struct Fixture {
  ow::Lane lane;
  orbital::Ref<oa::Arena> arena;
  oa::Pool* pool{};
  Fixture(int owner, int helper) {
    need(ow::adopt(lane, owner));
    arena = need(oa::open_arena(1ull << 30, helper));
    pool = need(oa::add_pool(*arena, lane));
  }
  void settle(bool closing) {
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    for (;;) {
      auto result = closing ? oa::close(*pool) : oa::trim_cold(*pool, 0);
      CHECK(result.kind() != orbital::Progress::Kind::failed);
      if (result.kind() == orbital::Progress::Kind::done) break;
      ow::poll(lane, 64);
      CHECK(Clock::now() < deadline);
      std::this_thread::yield();
    }
  }
  ~Fixture() {
    settle(true); need(oa::remove_pool(*pool)); arena.reset(); ow::relinquish(lane);
  }
};

using Allocations = std::array<oa::Allocation, width>;
using Holds = std::array<oa::Hold, width>;
oa::AllocationRequest request(char palette) {
  return {.bytes = palette == 'A' ? 64u : 256u, .alignment = palette == 'A' ? 16u : 64u};
}
oa::ReleaseHint hint(std::size_t index, Clock::time_point stamp) {
  return {static_cast<oa::Temperature>(index % 3), stamp};
}
void populate(Fixture& f, char palette, Allocations& allocations, Holds& holds) {
  const auto r = request(palette);
  for (std::size_t i = 0; i < width; ++i) {
    allocations[i] = need(oa::allocate(*f.pool, r));
    CHECK(allocations[i].size == r.bytes &&
          reinterpret_cast<std::uintptr_t>(allocations[i].address) % r.alignment == 0);
    allocations[i].address[0] = static_cast<std::byte>(i);
    holds[i] = need(oa::hold(allocations[i], {0, r.bytes}));
    CHECK(holds[i].address == allocations[i].address);
  }
}
void release_allocations(Allocations& allocations, Clock::time_point stamp) {
  for (std::size_t i = 0; i < width; ++i) oa::release(allocations[i], hint(i, stamp));
}
void release_holds(Holds& holds, Clock::time_point stamp) {
  for (std::size_t i = 0; i < width; ++i) {
    CHECK(holds[i].address[0] == static_cast<std::byte>(i));
    oa::release(holds[i], hint(i, stamp));
  }
}

void local(char palette, std::size_t count, int owner, int helper) {
  Fixture f(owner, helper); Allocations allocations; Holds holds;
  const auto stamp = Clock::now();
  populate(f, palette, allocations, holds);
  release_allocations(allocations, stamp); release_holds(holds, stamp);
  const auto before = oa::statistics(*f.pool);
  Samples timings(count);
  for (std::size_t n = 0; n < count; ++n) {
    const auto start = Clock::now();
    populate(f, palette, allocations, holds);
    release_allocations(allocations, stamp); release_holds(holds, stamp);
    timings.ns.push_back(elapsed(start));
  }
  const auto after = oa::statistics(*f.pool);
  CHECK(after.allocations - before.allocations == count * width);
  CHECK(after.reuses - before.reuses == count * width);
  CHECK(after.mapped_bytes == before.mapped_bytes && after.metadata_bytes == before.metadata_bytes);
  timings.emit("owner_allocate_hold_release_reuse", palette, width, after.metadata_bytes);
}

struct Foreign {
  Holds holds;
  Clock::time_point stamp{Clock::now()};
  std::atomic<std::uint64_t> request{}, completed{};
  double nanoseconds{};
  static void run(void* pointer) noexcept {
    auto& self = *static_cast<Foreign*>(pointer);
    std::uint64_t previous = 0;
    for (;;) {
      const auto next = self.request.load(std::memory_order_acquire);
      if (next == std::numeric_limits<std::uint64_t>::max()) return;
      if (next == previous) { std::this_thread::yield(); continue; }
      const auto start = Clock::now();
      release_holds(self.holds, self.stamp);
      self.nanoseconds = elapsed(start);
      previous = next; self.completed.store(next, std::memory_order_release);
    }
  }
};
void foreign(char palette, std::size_t count, int owner, int sender, int helper) {
  Fixture f(owner, helper); Allocations allocations; Foreign job; ow::Thread thread;
  need(ow::spawn(thread, Foreign::run, &job, sender));
  Samples send(count), drain(count);
  const auto before = oa::statistics(*f.pool);
  for (std::size_t n = 1; n <= count; ++n) {
    populate(f, palette, allocations, job.holds);
    release_allocations(allocations, job.stamp);
    job.request.store(n, std::memory_order_release);
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (job.completed.load(std::memory_order_acquire) != n) {
      CHECK(Clock::now() < deadline); std::this_thread::yield();
    }
    send.ns.push_back(job.nanoseconds);
    const auto start = Clock::now();
    while (ow::pending(f.lane)) ow::poll(f.lane, 64);
    drain.ns.push_back(elapsed(start));
    CHECK(oa::statistics(*f.pool).foreign_returns - before.foreign_returns == n * width);
  }
  job.request.store(std::numeric_limits<std::uint64_t>::max(), std::memory_order_release);
  need(ow::join(thread));
  const auto after = oa::statistics(*f.pool);
  CHECK(after.allocations - before.allocations == count * width);
  CHECK(after.reuses >= (count - 1) * width);
  send.emit("foreign_final_release", palette, width, after.metadata_bytes);
  drain.emit("owner_handle_foreign_returns", palette, width, after.metadata_bytes);
}

void trimming(char palette, std::size_t count, int owner, int helper) {
  Fixture f(owner, helper); Allocations allocations; Holds holds;
  Samples submit(count), complete(count); std::uint64_t pages = 0, metadata = 0;
  for (std::size_t n = 0; n < count; ++n) {
    populate(f, palette, allocations, holds);
    // Fault every allocated body page; both controls see the same actual bytes.
    for (auto& value : allocations) std::fill_n(value.address + 1, value.size - 1, std::byte{0x71});
    const auto stamp = Clock::now();
    release_allocations(allocations, stamp); release_holds(holds, stamp);
    const auto before = oa::statistics(*f.pool);
    CHECK(before.mapped_bytes && before.idle_bytes == before.mapped_bytes);
    const auto start = Clock::now();
    const auto result = oa::trim_cold(*f.pool, 0);
    const auto submitted = elapsed(start);
    CHECK(result.kind() != orbital::Progress::Kind::failed);
    f.settle(false);
    complete.ns.push_back(elapsed(start)); submit.ns.push_back(submitted);
    const auto after = oa::statistics(*f.pool);
    CHECK(after.mapped_bytes == 0 && after.idle_bytes == 0 && after.trim_pages > before.trim_pages);
    pages += after.trim_pages - before.trim_pages; metadata = after.metadata_bytes;
  }
  submit.emit("trim_owner_submission", palette, 1, metadata, pages);
  complete.emit("trim_background_completion", palette, 1, metadata, pages);
}

void mapping(char palette, std::size_t count) {
  Samples create(count), retire(count);
  std::uint64_t controls = 0;
  for (std::size_t n = 0; n < count; ++n) {
    const auto start = Clock::now();
    auto group = need(ad::FiniteGroup::create({palette == 'A' ? ad::Palette::a : ad::Palette::b, 256}));
    need(group->prepare()); create.ns.push_back(elapsed(start));
    CHECK(group->mapped_pages() == 0); controls = group->control_bytes();
    const auto end = Clock::now(); need(group->retire()); retire.ns.push_back(elapsed(end));
  }
  create.emit("finite_control_create_prepare", palette, 1, controls);
  retire.emit("finite_control_retire", palette, 1, controls);
}

int main(int argc, char** argv) {
  CHECK(argc == 6);
  const auto samples = std::strtoul(argv[1], nullptr, 10), cold = std::strtoul(argv[2], nullptr, 10);
  const auto owner = std::atoi(argv[3]), sender = std::atoi(argv[4]), helper = std::atoi(argv[5]);
  CHECK(samples > 1 && cold > 1 && owner != sender && owner != helper && sender != helper);
  for (const auto palette : {'A', 'B'}) {
    local(palette, samples, owner, helper);
    foreign(palette, samples, owner, sender, helper);
    trimming(palette, cold, owner, helper);
    mapping(palette, cold);
  }
}
