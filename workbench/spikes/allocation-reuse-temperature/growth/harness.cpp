// Whole-cohort means. Counters use a separate, instrumented Window TU.
#include <orbital/allocation.h>
#include "internal.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sched.h>
#include <source_location>
#include <string_view>
#include <thread>
#include <vector>

#ifdef GROWTH_COUNTERS
extern "C" {
extern std::uint64_t growth_head_probes, growth_list_visits;
extern std::uint64_t growth_ineligible_visits, growth_map_entries;
}
#endif

namespace {
namespace oa = orbital::allocation;
namespace ow = orbital::worker;
using Clock = std::chrono::steady_clock;

void check(bool condition, const char* message,
    std::source_location where = std::source_location::current()) {
  if (condition) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), message);
  std::abort();
}

template<class T> T take(orbital::Result<T> result,
    std::source_location where = std::source_location::current()) {
  if (!result) {
    std::fprintf(stderr, "%s:%u: refusal %u\n", where.file_name(), where.line(), unsigned(result.error()));
    std::abort();
  }
  return std::move(*result);
}

struct Options { int cpu{}, reclaimer{-1}; std::size_t operations{1048576}; bool checks_only{}; };

struct Fixture {
  orbital::Ref<oa::Arena> arena;
  ow::Lane lane;
  oa::Pool* pool{};
  explicit Fixture(const Options& options) : arena(take(oa::open_arena(1ULL << 30, options.reclaimer))) {
    check(bool(ow::adopt(lane, options.cpu)), "Lane adoption failed");
    pool = take(oa::add_pool(*arena, lane));
  }
  ~Fixture() {
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    for (;;) {
      const auto state = oa::close(*pool);
      check(state.kind() != orbital::Progress::Kind::failed, "close failed");
      if (state.kind() == orbital::Progress::Kind::done) break;
      check(Clock::now() < deadline, "close did not settle");
      ow::poll(lane, 128);
      std::this_thread::yield();
    }
    check(arena->charged_bytes.load() == 0, "close retained a charge");
    check(bool(oa::remove_pool(*pool)), "closed Pool removal failed");
    ow::relinquish(lane);
  }
};

struct Counters { std::uint64_t head{}, list{}, ineligible{}, entries{}; };

Counters counters() {
#ifdef GROWTH_COUNTERS
  return {growth_head_probes, growth_list_visits, growth_ineligible_visits, growth_map_entries};
#else
  return {};
#endif
}

std::size_t index_nodes(const oa::Pool& pool) {
#if GROWTH_HAS_INDEX
  return pool.internal->window_sizes.size();
#else
  (void)pool;
  return 0;
#endif
}

struct Observation { oa::Statistics stats; Counters visits; std::size_t nodes; };
Observation observe(const oa::Pool& pool) { return {oa::statistics(pool), counters(), index_nodes(pool)}; }

void emit(std::string_view name, std::size_t n, std::size_t from, std::size_t to,
    std::size_t reservation, std::size_t operations, const oa::Pool& pool,
    const Observation& before, Clock::time_point start, Clock::time_point stop,
    std::uint64_t signature = 0) {
  const auto after = observe(pool);
#ifdef GROWTH_COUNTERS
  constexpr bool instrumented = true;
  const std::uint64_t ns = 0;
  (void)start; (void)stop;
#else
  constexpr bool instrumented = false;
  const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
#endif
  std::printf("%.*s,%zu,%zu,%zu,%zu,%zu,%u,%llu,%.3f",
      int(name.size()), name.data(), n, from, to, reservation, operations, instrumented,
      static_cast<unsigned long long>(ns), double(ns) / operations);
  const std::array<std::uint64_t, 21> values{
    after.visits.head - before.visits.head, after.visits.list - before.visits.list,
    after.visits.ineligible - before.visits.ineligible, after.visits.entries - before.visits.entries,
    before.stats.mapped_bytes, after.stats.mapped_bytes,
    before.stats.reserved_bytes, after.stats.reserved_bytes,
    before.stats.idle_bytes, after.stats.idle_bytes,
    before.stats.metadata_bytes, after.stats.metadata_bytes, before.nodes, after.nodes,
    sizeof(oa::detail::Window), sizeof(oa::detail::PoolState),
    after.stats.allocations - before.stats.allocations, after.stats.reuses - before.stats.reuses,
    after.stats.growths - before.stats.growths, after.stats.relocations - before.stats.relocations,
    signature};
  for (auto value : values) std::printf(",%llu", static_cast<unsigned long long>(value));
  std::putchar('\n');
}

unsigned old_pattern(std::size_t i) { return (i * 17 + 23) & 255; }
unsigned new_pattern(std::size_t i) { return (i * 13 + 91) & 255; }

void verify(const oa::Allocation& value, std::size_t from, std::size_t to, std::size_t i) {
  check(value.size == to && oa::capacity(value) >= to, "grown geometry is invalid");
  for (std::size_t j = 0; j < from; ++j)
    check(value.address[j] == std::byte(old_pattern(i)), "old bytes changed");
  for (std::size_t j = from; j < from + 64; ++j)
    check(value.address[j] == std::byte(new_pattern(i)), "appended bytes changed");
}

void grow_append(oa::Allocation& value, std::size_t from, std::size_t to, std::size_t i) {
  if (!oa::grow(value, {.bytes = to, .alignment = 8})) std::abort();
  std::memset(value.address + from, int(new_pattern(i)), 64);
}

void uniform(const Options& options, std::size_t n, std::size_t from, std::size_t to,
    std::size_t reservation) {
  Fixture fixture(options);
  auto& pool = *fixture.pool;
  std::vector<oa::Allocation> values;
  std::vector<std::byte*> addresses;
  values.reserve(n); addresses.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    auto value = take(oa::allocate(pool, {.bytes = from, .reserved_bytes = reservation, .alignment = 8}));
    check(oa::capacity(value) == from && oa::reserved_bytes(value) == (reservation ? reservation : from),
        "fresh Window geometry changed");
    std::memset(value.address, int(old_pattern(i)), from);
    addresses.push_back(value.address);
    values.push_back(std::move(value));
  }
  check(!pool.internal->idle_windows, "fresh cohort has idle inventory");
  const auto before = observe(pool);
  const auto start = Clock::now();
  for (std::size_t i = 0; i < n; ++i) grow_append(values[i], from, to, i);
  const auto stop = Clock::now();
  std::size_t moved{};
  for (std::size_t i = 0; i < n; ++i) {
    verify(values[i], from, to, i);
    check(oa::capacity(values[i]) == to, "uniform growth acquired unexpected slack");
    moved += values[i].address != addresses[i];
  }
  check(moved == (reservation ? 0 : n), "uniform relocation changed");
#ifdef GROWTH_COUNTERS
  const auto after = counters();
#if GROWTH_HAS_INDEX
  check(after.list - before.visits.list == 0, "indexed growth scanned an idle list");
#else
  check(after.list - before.visits.list == (reservation ? 0 : n * (n - 1) / 2),
      "baseline growth did not reproduce triangular scan");
#endif
#endif
  emit("uniform", n, from, to, reservation, n, pool, before, start, stop);
}

void buried(const Options& options, std::size_t n) {
  Fixture fixture(options);
  auto& pool = *fixture.pool;
  auto source = take(oa::allocate(pool, {.bytes = 4096, .alignment = 8}));
  std::memset(source.address, int(old_pattern(0)), 4096);
  auto fitting = take(oa::allocate(pool, {.bytes = 8192, .alignment = 8}));
  const auto expected = fitting.address;
  std::vector<oa::Allocation> blockers;
  blockers.reserve(n);
  for (std::size_t i = 0; i < n; ++i)
    blockers.push_back(take(oa::allocate(pool, {.bytes = 4096, .alignment = 8})));
  oa::release(fitting);
  for (auto& value : blockers) oa::release(value);
  const auto before = observe(pool);
  const auto start = Clock::now();
  grow_append(source, 4096, 8192, 0);
  const auto stop = Clock::now();
  verify(source, 4096, 8192, 0);
  check(source.address == expected, "buried fitting Window was not reused");
#ifdef GROWTH_COUNTERS
  const auto after = counters();
  check(after.list - before.visits.list ==
#if GROWTH_HAS_INDEX
      1,
#else
      n + 1,
#endif
      "buried candidate search changed");
#endif
  emit("buried-fit", n, 4096, 8192, 0, 1, pool, before, start, stop, 1);
}

void mixed(const Options& options, std::size_t n) {
  Fixture fixture(options);
  auto& pool = *fixture.pool;
  constexpr std::array<std::size_t, 6> seed_sizes{4096, 8192, 12288, 16384, 24576, 32768};
  constexpr std::array<std::size_t, 4> targets{8192, 16384, 32768, 24576};
  struct Candidate { std::byte* address; std::size_t capacity, ordinal; };
  std::vector<oa::Allocation> values, seeds;
  std::vector<Candidate> idle, expected;
  values.reserve(n); seeds.reserve(n); idle.reserve(n); expected.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    auto value = take(oa::allocate(pool, {.bytes = 4096, .alignment = 8}));
    std::memset(value.address, int(old_pattern(i)), 4096);
    values.push_back(std::move(value));
  }
  for (std::size_t i = 0; i < n; ++i) {
    seeds.push_back(take(oa::allocate(pool, {.bytes = seed_sizes[i % seed_sizes.size()], .alignment = 8})));
    idle.push_back({seeds.back().address, oa::capacity(seeds.back()), i + 1});
  }
  // Independent LIFO model: all seeds are fully prepared and aligned. Old
  // 4 KiB source Windows cannot satisfy any target, so they do not alter choices.
  for (std::size_t i = 0; i < n; ++i) {
    auto found = std::find_if(idle.rbegin(), idle.rend(), [&](const auto& value) {
      return value.capacity >= targets[i % targets.size()];
    });
    if (found == idle.rend()) expected.push_back({nullptr, targets[i % targets.size()], 0});
    else { expected.push_back(*found); idle.erase(std::next(found).base()); }
  }
  for (auto& value : seeds) oa::release(value);
  const auto before = observe(pool);
  const auto start = Clock::now();
  for (std::size_t i = 0; i < n; ++i) grow_append(values[i], 4096, targets[i % targets.size()], i);
  const auto stop = Clock::now();
  std::uint64_t signature = 1469598103934665603ULL;
  std::size_t reused{};
  for (std::size_t i = 0; i < n; ++i) {
    verify(values[i], 4096, targets[i % targets.size()], i);
    check(oa::capacity(values[i]) == expected[i].capacity, "mixed capacity choice changed");
    if (expected[i].address) {
      ++reused;
      check(values[i].address == expected[i].address, "mixed LIFO choice changed");
    }
    signature = (signature ^ expected[i].ordinal) * 1099511628211ULL;
  }
  check(oa::statistics(pool).reuses - before.stats.reuses == reused, "mixed reuse count changed");
#ifdef GROWTH_COUNTERS
#if GROWTH_HAS_INDEX
  check(counters().ineligible == before.visits.ineligible, "indexed mixed search entered undersized list");
#endif
#endif
  emit("mixed-classes", n, 4096, 0, 0, n, pool, before, start, stop, signature);
}

void prepared(const Options& options, std::size_t size) {
  Fixture fixture(options);
  auto& pool = *fixture.pool;
  auto initial = take(oa::allocate(pool, {.bytes = size, .alignment = 8}));
  const auto address = initial.address;
  std::memset(initial.address, 0x73, size);
  oa::release(initial);
  for (std::size_t i = 0; i < 64; ++i) {
    auto value = take(oa::allocate(pool, {.bytes = size, .alignment = 8}));
    oa::release(value);
  }
  const auto before = observe(pool);
  const auto start = Clock::now();
  for (std::size_t i = 0; i < options.operations; ++i) {
    auto value = oa::allocate(pool, {.bytes = size, .alignment = 8});
    if (!value) std::abort();
    std::memset(value->address, 0x73, 64);
    oa::release(*value);
  }
  const auto stop = Clock::now();
  const auto after = oa::statistics(pool);
  check(after.allocations - before.stats.allocations == options.operations &&
      after.reuses - before.stats.reuses == options.operations &&
      after.mapped_bytes == before.stats.mapped_bytes, "prepared path did not reuse backing");
  emit("prepared", 1, size, size, 0, options.operations, pool, before, start, stop);
  auto verify_live = take(oa::allocate(pool, {.bytes = size, .alignment = 8}));
  check(verify_live.address == address, "prepared path changed address");
  // Reestablish public readable bytes; uninitialized allocation promises no contents.
  std::memset(verify_live.address, 0x73, size);
  for (auto byte : oa::bytes(verify_live)) check(byte == std::byte{0x73}, "prepared payload changed");
}

Options parse(int argc, char** argv) {
  Options result;
  result.cpu = ::sched_getcpu();
  for (int i = 1; i < argc; ++i) {
    const std::string_view key{argv[i]};
    if (key == "--check") { result.checks_only = true; continue; }
    check(++i < argc, "option needs value");
    char* end{};
    const auto value = std::strtol(argv[i], &end, 10);
    check(end && *end == 0 && value >= 0, "invalid integer option");
    if (key == "--cpu") result.cpu = int(value);
    else if (key == "--reclaimer-cpu") result.reclaimer = int(value);
    else if (key == "--operations") result.operations = std::size_t(value);
    else check(false, "unknown option");
  }
  check(result.operations, "positive operation count required");
  cpu_set_t allowed;
  check(::sched_getaffinity(0, sizeof(allowed), &allowed) == 0 &&
      result.cpu < CPU_SETSIZE && CPU_ISSET(result.cpu, &allowed), "caller CPU not allowed");
  check(result.reclaimer < 0 || (result.reclaimer < CPU_SETSIZE && CPU_ISSET(result.reclaimer, &allowed)),
      "reclaimer CPU not allowed");
  cpu_set_t selected;
  CPU_ZERO(&selected); CPU_SET(result.cpu, &selected);
  check(::sched_setaffinity(0, sizeof(selected), &selected) == 0, "caller pin failed");
  check(oa::detail::page_size() == 4096, "study requires 4 KiB base pages");
  if (result.checks_only) result.operations = 128;
  return result;
}
} // namespace

int main(int argc, char** argv) {
  const auto options = parse(argc, argv);
  std::puts("case,n,from_bytes,to_bytes,reservation,operations,counter_run,duration_ns,ns_per_op,"
      "selector_head_probes,selector_list_visits,selector_ineligible_visits,selector_map_entries,"
      "before_mapped,after_mapped,before_reserved,after_reserved,before_idle,after_idle,"
      "before_metadata,after_metadata,before_index_nodes,after_index_nodes,sizeof_window,sizeof_pool_state,"
      "allocations,reuses,growths,relocations,choice_signature");
  for (auto n : std::array<std::size_t, 4>{64, 256, 1024, 6144}) {
    if (options.checks_only && n != 64) continue;
    for (auto [from, to] : std::array<std::pair<std::size_t, std::size_t>, 2>{{{4096, 8192}, {12288, 16384}}}) {
      uniform(options, n, from, to, 0);
      uniform(options, n, from, to, to * 2);
    }
    buried(options, n);
    mixed(options, n);
  }
  prepared(options, 4096);
  prepared(options, 16384);
}
