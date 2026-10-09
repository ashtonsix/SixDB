// Synthetic fragmented mapped inventory isolates trim batching, not DB traffic.
#include "../../shape/native_fixture.h"
#include "internal.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>
#include <sys/mman.h>
#include <time.h>
#include <vector>

namespace oa = orbital::allocation;
namespace d = oa::detail;
using Clock = std::chrono::steady_clock;

void check(bool value, const char* reason,
    std::source_location location = std::source_location::current()) {
  if (value) return;
  std::fprintf(stderr, "%s:%u: %s\n", location.file_name(), location.line(), reason);
  std::abort();
}

template<class T> T take(orbital::Result<T> value) {
  check(bool(value), "operation refused"); return std::move(*value);
}

std::uint64_t cpu(clockid_t id) {
  timespec stamp{}; check(::clock_gettime(id, &stamp) == 0, "CPU clock failed");
  return std::uint64_t(stamp.tv_sec) * 1000000000 + stamp.tv_nsec;
}

#if TRIM_FAULT_PROBE
std::atomic<int> discard_count{}, fail_discard{};
extern "C" int __real_madvise(void*, std::size_t, int);
extern "C" int __wrap_madvise(void* address, std::size_t size, int advice) {
  if (advice == MADV_DONTNEED && ++discard_count == fail_discard.load()) {
    errno = EIO; return -1;
  }
  return __real_madvise(address, size, advice);
}
#endif

d::Group& group(oa::Allocation& value) {
  auto* route = d::route(*value.storage);
  check(route->finite, "fixture must use a finite Group");
  return *static_cast<d::Group*>(route->context);
}

std::size_t idle_pages(d::Group& value) {
  check(bool(value.geometry->live_pages(value.live)), "live coverage refused");
  std::size_t count = 0;
  for (std::size_t p = 0; p < value.geometry->payload_pages(); ++p)
    count += value.geometry->mapped(p) && !(value.live[p / 64] & (std::uint64_t{1} << (p % 64)));
  return count;
}

struct Inventory { std::size_t pages{}, runs{}; };

Inventory inventory(d::Group& value) {
  check(bool(value.geometry->live_pages(value.live)), "inventory live coverage refused");
  Inventory result;
  bool previous = false;
  for (std::size_t p = 0; p < value.geometry->payload_pages(); ++p) {
    const bool idle = value.geometry->mapped(p) && !(value.live[p / 64] & (std::uint64_t{1} << (p % 64)));
    result.pages += idle;
    result.runs += idle && !previous;
    previous = idle;
  }
  return result;
}

void seed(oa::Pool& pool, d::Group& value, bool alternating, bool coalesced = false) {
  // Real payload mappings/charges, with no logical Home in these idle pages.
  // This supplies adversarial range fragmentation independent of row placement.
  for (std::size_t p = 0; p < value.geometry->payload_pages(); ++p) {
    if ((alternating && p % 2 && (!coalesced || p < 32)) || value.geometry->mapped(p)) continue;
    const auto page = value.geometry->native_page();
    check(d::claim(pool, page), "seed charge refused");
    auto* address = value.geometry->page_address(p);
    check(::mprotect(address, page, PROT_READ | PROT_WRITE) == 0, "seed protect failed");
    address[0] = std::byte{0xa7}; // Fault the page in before the measured trim.
    check(bool(value.geometry->mark_mapped({p, 1})), "seed map publication refused");
  }
}

void run(const char* name, int owner_cpu, int helper_cpu, unsigned limit, bool fault) {
  compact_shape::probe::NativeFixture fixture;
  check(bool(fixture.open(1ULL << 30, owner_cpu, helper_cpu)), "fixture open refused");
  std::vector<oa::Allocation> kept;
  const bool mixed = std::strcmp(name, "mixed") == 0 || std::strcmp(name, "floor") == 0;
  const bool palette_b = std::strcmp(name, "fragmented-b") == 0;
  const bool contiguous = std::strcmp(name, "contiguous") == 0;
  const bool coalesced = std::strcmp(name, "coalesced-16") == 0;
  const bool empty = std::strcmp(name, "empty") == 0;
  kept.push_back(take(oa::allocate(*fixture.pool, {.bytes = palette_b ? 1536U : 64U, .alignment = 1})));
  auto* cold = &group(kept.front());
  std::memset(kept.front().address, 0x5a, kept.front().size);
  seed(*fixture.pool, *cold, !contiguous, coalesced);
  d::Group* warm{};
  if (mixed) {
    // Fill A's identities so the ordinary allocator must create a second Group.
    // Size alone does not select a new palette when an existing Group can serve it.
    std::vector<oa::Allocation> blockers;
    for (unsigned i = 1; i < cold->uses.size(); ++i)
      blockers.push_back(take(oa::allocate(*fixture.pool, {.bytes = 64, .alignment = 1})));
    kept.push_back(take(oa::allocate(*fixture.pool, {.bytes = 1536, .alignment = 1})));
    warm = &group(kept.back());
    check(warm != cold, "mixed fixture did not use two palettes");
    std::memset(kept.back().address, 0x5a, kept.back().size);
    seed(*fixture.pool, *warm, true);
    auto hint = take(oa::allocate(*fixture.pool, {.bytes = 1536, .alignment = 1}));
    check(&group(hint) == warm, "warm advice came from another Group");
    blockers.clear();
    oa::release(hint, {oa::Temperature::warm});
  }
  if (empty) kept.clear();
  const auto initial = fixture.statistics();
  const auto keep = std::strcmp(name, "floor") == 0 ? initial.idle_bytes / 2 + 123 : 0;
#if SIXDB_TRIM_COMPARE
  Inventory expected;
  for (auto& value : fixture.pool->internal->groups) {
    const auto part = inventory(*value);
    expected.pages += part.pages; expected.runs += part.runs;
  }
  check(expected.pages * d::page_size() == initial.idle_bytes, "independent idle census disagreed");
  const auto expected_pages = (initial.idle_bytes - keep) / d::page_size();
  fixture.pool->internal->trim_comparison = {};
#endif
#if TRIM_FAULT_PROBE
  discard_count = 0; fail_discard = fault ? 3 : 0;
#else
  check(!fault, "fault probe was not compiled");
#endif
  unsigned calls = 0, failures = 0, max_ranges = 0;
  std::uint64_t replies = 0;
  const auto deadline = Clock::now() + std::chrono::seconds(20);
  const auto wall_start = Clock::now();
  const auto owner_start = cpu(CLOCK_THREAD_CPUTIME_ID), process_start = cpu(CLOCK_PROCESS_CPUTIME_ID);
  for (;;) {
    ++calls;
    const auto progress = oa::trim_cold(*fixture.pool, keep);
    if (progress.kind() == orbital::Progress::Kind::failed) {
      check(fault && progress.failure() == orbital::Error::io && failures++ == 0,
          "unexpected trim failure or repeated failed report");
      std::size_t retained_pages = 0;
      for (const auto& value : fixture.pool->internal->groups) {
        if (value->trim_pending) continue; // Baseline can have another Group in flight.
        for (const auto span : value->trimming)
          for (std::size_t p = span.first; p < span.first + span.count; ++p) {
            check(value->geometry->trimming(p), "failed page lost its exclusion state");
            retained_pages++;
          }
      }
      check(retained_pages == 1, "failed range was lost or successful ranges were retained");
      const auto unsettled = fixture.statistics();
      check(fixture.arena->charged_bytes.load() == unsettled.metadata_bytes + unsettled.mapped_bytes,
          "failed page charge was refunded before confirmation");
      continue;
    }
    for (const auto& value : fixture.pool->internal->groups) {
      max_ranges = std::max(max_ranges, static_cast<unsigned>(value->trimming.size()));
      check(!limit || value->trimming.size() <= limit, "batch exceeded range limit");
    }
#if SIXDB_TRIM_COMPARE
    if (limit && warm && !warm->trimming.empty()) {
      check(!cold->trim_pending && cold->trimming.empty(), "warm work overtook unresolved cold spans");
      check(idle_pages(*cold) == 0, "warm work overtook unselected cold pages");
    }
    if (limit && calls == 1 && warm) {
      ++calls;
      const auto repeated = oa::trim_cold(*fixture.pool, keep);
      check(repeated.kind() == orbital::Progress::Kind::waiting && cold->trim_pending &&
          !warm->trim_pending && warm->trimming.empty(), "pending recheck enrolled warmer work");
    }
    if (limit && calls == 1 && coalesced)
      check(cold->trimming.size() == 16 && cold->trimming.back().first == 32 &&
          cold->trimming.back().count == cold->geometry->payload_pages() - 32,
          "full batch did not extend its final contiguous span");
#endif
    if (progress.kind() == orbital::Progress::Kind::done) break;
    check(progress.kind() == orbital::Progress::Kind::waiting, "trim did not return a reusable wait");
    while (progress.signal()->generation == progress.observed_generation()) {
      check(Clock::now() < deadline, "trim did not settle");
      if (orbital::worker::pending(fixture.lane)) replies += orbital::worker::poll(fixture.lane, 64);
      else std::this_thread::yield();
    }
  }
  const auto process_ns = cpu(CLOCK_PROCESS_CPUTIME_ID) - process_start;
  const auto owner_ns = cpu(CLOCK_THREAD_CPUTIME_ID) - owner_start;
  const auto wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - wall_start).count();
  const auto after = fixture.statistics();
  check(failures == unsigned(fault), "injected fault did not produce exactly one io result");
  check(after.idle_bytes >= keep && after.idle_bytes - keep < d::page_size(), "retention floor or final trim was wrong");
  check(initial.mapped_bytes - after.mapped_bytes == (after.trim_pages - initial.trim_pages) * d::page_size(),
      "confirmed trim/refund accounting diverged");
  check(fixture.arena->charged_bytes.load() == after.metadata_bytes + after.mapped_bytes,
      "settled budget was not conserved");
  for (const auto& value : kept)
    for (const auto byte : oa::bytes(value)) check(byte == std::byte{0x5a}, "trim damaged live bytes");
  std::printf("{\"case\":\"%s\",\"fault\":%s,\"wall_ns\":%lld,\"owner_cpu_ns\":%llu,"
      "\"process_cpu_ns\":%llu,\"driver_calls\":%u,\"owner_replies\":%llu,\"max_ranges\":%u,"
      "\"before_metadata\":%llu,\"before_mapped\":%llu,\"after_mapped\":%llu,\"retained_idle\":%llu",
      name, fault ? "true" : "false", static_cast<long long>(wall_ns),
      static_cast<unsigned long long>(owner_ns), static_cast<unsigned long long>(process_ns), calls,
      static_cast<unsigned long long>(replies), max_ranges,
      static_cast<unsigned long long>(initial.metadata_bytes), static_cast<unsigned long long>(initial.mapped_bytes),
      static_cast<unsigned long long>(after.mapped_bytes), static_cast<unsigned long long>(after.idle_bytes));
#if SIXDB_TRIM_COMPARE
  const auto& c = fixture.pool->internal->trim_comparison;
  check(c.new_pages + c.retire_pages == expected_pages, "selected work disagreed with independent idle census");
  if (!keep && !empty) check(c.new_ranges == expected.runs, "selected ranges disagreed with independent topology");
  if (limit && !contiguous && !coalesced && !empty)
    check(c.batch_overflows && max_ranges == limit, "fragmented fixture did not exercise the full batch guard");
  std::printf(",\"jobs\":%llu,\"retry_jobs\":%llu,\"retire_jobs\":%llu,\"ranges\":%llu,\"pages\":%llu,"
      "\"census_pages\":%llu,\"enrollment_pages\":%llu,\"census_live_builds\":%llu,"
      "\"validation_live_builds\":%llu,\"warm_pass_deferrals\":%llu",
      static_cast<unsigned long long>(c.jobs), static_cast<unsigned long long>(c.retry_jobs),
      static_cast<unsigned long long>(c.retire_jobs), static_cast<unsigned long long>(c.ranges),
      static_cast<unsigned long long>(c.pages), static_cast<unsigned long long>(c.census_pages),
      static_cast<unsigned long long>(c.enrollment_pages), static_cast<unsigned long long>(c.census_live_builds),
      static_cast<unsigned long long>(c.validation_live_builds), static_cast<unsigned long long>(c.warm_pass_deferrals));
#endif
  std::puts("}");
  kept.clear(); check(bool(fixture.close()), "fixture close refused");
}

int main(int argc, char** argv) {
  check(argc >= 4, "usage: probe OWNER_CPU HELPER_CPU RANGE_LIMIT [CASE [fault]]");
  const int owner = std::atoi(argv[1]), helper = std::atoi(argv[2]);
  const unsigned limit = std::strtoul(argv[3], nullptr, 10);
  if (argc > 4) run(argv[4], owner, helper, limit, argc > 5);
  else for (const auto* name : {"fragmented-a", "fragmented-b", "contiguous", "coalesced-16", "mixed", "floor", "empty"})
    run(name, owner, helper, limit, false);
}
