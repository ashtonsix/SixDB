// Actual allocator timings are whole-loop means. Model rows count retained pages;
// they do not time alternative implementations or establish cache residence.
#include <orbital/allocation.h>
#include "internal.h" // Layout census, compiled against the same source as the library.
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>
#include <vector>

#ifndef SIXDB_LIVE_TAIL_API
#define SIXDB_LIVE_TAIL_API 1
#endif
namespace {
namespace a = orbital::allocation;
namespace w = orbital::worker;
using Clock = std::chrono::steady_clock;
const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
int helper_cpu = -1;
void require(bool ok, const char* why) {
  if (!ok) { std::fprintf(stderr, "%s\n", why); std::abort(); }
}
template<class T> T need(orbital::Result<T> value) {
  require(bool(value), "allocator refused benchmark operation"); return std::move(*value);
}
void done(orbital::Result<void> value) { require(bool(value), "allocator operation refused"); }
long long elapsed(Clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
long faults() {
  rusage usage{}; require(!::getrusage(RUSAGE_THREAD, &usage), "getrusage"); return usage.ru_minflt;
}
struct Fixture {
  orbital::Ref<a::Arena> arena;
  w::Lane lane;
  a::Pool* pool;
  Fixture() : arena(need(a::open_arena(256ull << 20, helper_cpu))) {
    done(w::adopt(lane)); pool = need(a::add_pool(*arena, lane));
  }
  template<class F> void settle(F operation) {
    const auto limit = Clock::now() + std::chrono::seconds(20);
    for (;;) {
      auto progress = operation();
      require(progress.kind() != orbital::Progress::Kind::failed, "background operation failed");
      if (progress.kind() == orbital::Progress::Kind::done) break;
      w::poll(lane, 4096); std::this_thread::yield();
      require(Clock::now() < limit, "background settlement timeout");
    }
  }
  void trim() { settle([&] { return a::trim_cold(*pool, 0); }); }
  ~Fixture() {
    settle([&] { return a::close(*pool); });
    require(arena->charged_bytes.load() == 0, "close retained charge");
    done(a::remove_pool(*pool)); w::relinquish(lane);
  }
};
void row(const char* kind, const char* name, std::size_t n, long long ns,
    long minor, const Fixture& f, long resident = -1, long model = -1) {
  const auto s = a::statistics(*f.pool);
  std::printf("%s,%s,%zu,%lld,%.4f,%ld,%llu,%llu,%llu,%llu,%llu,%ld,%ld,%zu,%zu,%zu\n",
      kind, name, n, ns, n ? double(ns) / n : 0, minor,
      static_cast<unsigned long long>(f.arena->charged_bytes.load()),
      static_cast<unsigned long long>(s.mapped_bytes),
      static_cast<unsigned long long>(s.reserved_bytes),
      static_cast<unsigned long long>(s.metadata_bytes),
      static_cast<unsigned long long>(s.trim_pages), resident, model,
      sizeof(a::detail::Window), sizeof(a::Hold), sizeof(a::Storage));
}
[[maybe_unused]] long resident(const std::vector<a::Allocation>& values) {
  std::array<unsigned char, 16> state{}; long count = 0;
  for (const auto& value : values) {
    require(a::reserved_bytes(value) == 16 * page, "unexpected reservation");
    require(!::mincore(value.address, 16 * page, state.data()), "mincore");
    for (auto byte : state) count += byte & 1;
  }
  return count;
}
void ordinary(std::size_t loops, std::size_t bytes, const char* name) {
  Fixture f;
  auto warm = need(a::allocate(*f.pool, {.bytes = bytes})); a::release(warm);
  const auto start = Clock::now();
  for (std::size_t i = 0; i < loops; ++i) {
    auto value = need(a::allocate(*f.pool, {.bytes = bytes})); a::release(value);
  }
  row("runtime", name, loops, elapsed(start), 0, f);
}
void holds(std::size_t loops, std::size_t bytes, const char* name) {
  Fixture f;
  auto value = need(a::allocate(*f.pool, {.bytes = bytes}));
  const auto footprint = std::min(bytes, page);
  row("runtime", bytes == 64 ? "before_finite_hold" : "before_window_hold", 1, 0, 0, f);
  auto start = Clock::now(); auto first = need(a::hold(value, {0, footprint}));
  auto ns = elapsed(start); row("runtime", bytes == 64 ? "first_finite_hold" : "first_window_hold", 1, ns, 0, f);
  a::release(first);
  start = Clock::now();
  for (std::size_t i = 0; i < loops; ++i) {
    auto one = need(a::hold(value, {0, footprint})); auto two = need(a::retain(one));
    a::release(two); a::release(one);
  }
  row("runtime", name, loops, elapsed(start), 0, f);
  a::release(value);
}
#if SIXDB_LIVE_TAIL_API
std::vector<a::Allocation> batch(Fixture& f, std::size_t n) {
  std::vector<a::Allocation> values; values.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    values.push_back(need(a::allocate(*f.pool, {.bytes = 16 * page, .reserved_bytes = 16 * page})));
    std::memset(values.back().address, 0x59, 16 * page);
  }
  return values;
}
void prefix(const std::vector<a::Allocation>& values) {
  for (const auto& value : values)
    for (std::size_t i = 0; i < page; ++i)
      require(value.address[i] == std::byte{0x59}, "prefix changed");
}
void tails(std::size_t n, bool reclaim) {
  Fixture f; auto values = batch(f, n);
  std::vector<std::byte*> original; for (auto& value : values) original.push_back(value.address);
  (void)faults(); row("runtime", "touched_before", n, 0, 0, f, resident(values));
  auto start = Clock::now();
  for (auto& value : values) done(a::relinquish_tail(value, page));
  auto ns = elapsed(start);
  row("runtime", "relinquish_only", n, ns, 0, f, resident(values));
  auto previous_faults = faults(); start = Clock::now();
  if (reclaim) f.trim();
  ns = elapsed(start); auto minor = faults() - previous_faults;
  row("runtime", reclaim ? "async_trim_settled" : "retained_control", n, ns, minor, f, resident(values));
  prefix(values);
  previous_faults = faults(); start = Clock::now();
  for (auto& value : values) done(a::grow(value, {.bytes = 16 * page, .reserved_bytes = 16 * page}));
  ns = elapsed(start); minor = faults() - previous_faults;
  row("runtime", "regrow_mapping", n, ns, minor, f, resident(values));
  previous_faults = faults(); start = Clock::now();
  for (auto& value : values)
    for (std::size_t i = page; i < 16 * page; i += page)
      *static_cast<volatile std::byte*>(value.address + i) = std::byte{0x63};
  ns = elapsed(start); minor = faults() - previous_faults;
  row("runtime", "regrow_first_write", n, ns, minor, f, resident(values));
  prefix(values);
  for (std::size_t i = 0; i < n; ++i) {
    require(values[i].address == original[i], "regrowth relocated");
    for (std::size_t at = page; at < 16 * page; at += page)
      require(values[i].address[at] == std::byte{0x63}, "tail write lost");
  }
}
void continuous_prefix(std::size_t n) {
  Fixture f; auto values = batch(f, n);
  std::vector<a::Hold> wide, old_prefix, new_prefix;
  for (auto& value : values) {
    wide.push_back(need(a::hold(value, {0, 5 * page})));
    old_prefix.push_back(need(a::hold(value, {0, page})));
    done(a::relinquish_tail(value, page));
    new_prefix.push_back(need(a::hold(value, {0, page})));
  }
  f.trim(); row("runtime", "wide_reader_retained", n, 0, 0, f, resident(values));
  for (const auto& held : wide)
    for (std::size_t i = 0; i < held.footprint.size; ++i)
      require(held.address[i] == std::byte{0x59}, "wide reader changed");
  for (auto& held : wide) a::release(held);
  const auto start = Clock::now(); f.trim(); const auto ns = elapsed(start);
  row("runtime", "wide_retired_prefix_live", n, ns, 0, f, resident(values)); prefix(values);
  // Trace model: old narrow + wide form the old cohort; new narrow is separate.
  for (const auto* policy : {"watermark", "cohort", "exact", "binned"}) {
    const long pages = !std::strcmp(policy, "watermark") || !std::strcmp(policy, "cohort") ? 5 : 1;
    row("model", policy, n, 0, 0, f, -1, long(n) * pages);
  }
  for (auto& held : old_prefix) a::release(held);
  row("model", "watermark_after_old_cohort", n, 0, 0, f, -1, long(n) * 5);
  row("model", "cohort_after_old_cohort", n, 0, 0, f, -1, long(n));
  // Exact endpoint 5 pages rounds to 8 in the selected logarithmic bins.
  require((std::size_t{1} << std::bit_width(std::size_t{4})) == 8, "bin model");
  row("model", "binned_wide_before_release", n, 0, 0, f, -1, long(n) * 8);
}
#endif
} // namespace
int main(int argc, char** argv) {
  require(argc >= 2 && argc <= 5, "usage: bench ordinary|retain|reclaim|continuous [loops] [batch] [helper_cpu]");
  const auto loops = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1048576;
  const auto n = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 128;
  helper_cpu = argc > 4 ? std::atoi(argv[4]) : -1;
  require(page > 0 && loops && n && n * 16 * page < (192ull << 20), "invalid operation count/batch");
  (void)Clock::now(); (void)faults();
  std::puts("kind,case,operations,total_ns,ns_per_cycle,minor_faults,arena_charged,pool_mapped,pool_reserved,metadata,trim_pages,resident_pages,model_pages,window_bytes,hold_bytes,storage_bytes");
  if (!std::strcmp(argv[1], "ordinary")) {
    ordinary(loops, 64, "prepared_finite_allocate_release");
    ordinary(loops, 2 * page, "prepared_window_allocate_release");
    holds(loops, 64, "finite_hold_retain_release_cycle");
    holds(loops, 16 * page, "window_hold_retain_release_cycle");
  }
#if SIXDB_LIVE_TAIL_API
  else if (!std::strcmp(argv[1], "retain")) tails(n, false);
  else if (!std::strcmp(argv[1], "reclaim")) tails(n, true);
  else if (!std::strcmp(argv[1], "continuous")) continuous_prefix(n);
#endif
  else { std::fprintf(stderr, "unknown or unavailable case\n"); return 2; }
}
