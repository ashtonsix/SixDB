// Synthetic fragmented mapped inventory isolates trim batching, not DB traffic.
#include "fixture.h"
#include "internal.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <array>
#include <bit>
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

// Wrappers exist only in consequence binaries; plain timing has no wrappers.
#if TRIM_FAULT_PROBE
std::atomic<bool> metering{};
std::atomic<unsigned> discard_count{}, protect_count{}, fail_discard{};
std::atomic<void*> target_address{};
std::atomic<bool> partial_protect{}, target_discarded{}, pause_discard{}, paused{}, resume{};
std::atomic<unsigned> partial_failures{};
struct Meter {
  std::uint64_t jobs{}, retry_jobs{}, retire_jobs{}, ranges{}, pages{}, retire_pages{};
  std::vector<d::Group*> scheduled;
} meter;
void reset_meter() {
  meter = {}; discard_count = protect_count = fail_discard = 0;
  target_address = nullptr; partial_protect = target_discarded = pause_discard = paused = resume = false;
  partial_failures = 0; metering = true;
}
void print_meter() {
  std::printf(",\"jobs\":%llu,\"retry_jobs\":%llu,\"retire_jobs\":%llu,"
      "\"ranges\":%llu,\"pages\":%llu,\"retire_pages\":%llu,"
      "\"madvise_runs\":%u,\"mprotect_runs\":%u",
      static_cast<unsigned long long>(meter.jobs), static_cast<unsigned long long>(meter.retry_jobs),
      static_cast<unsigned long long>(meter.retire_jobs), static_cast<unsigned long long>(meter.ranges),
      static_cast<unsigned long long>(meter.pages), static_cast<unsigned long long>(meter.retire_pages),
      discard_count.load(), protect_count.load());
}
#endif


d::Group& group(oa::Allocation& value) {
  auto* route = d::route(*value.storage);
  check(route->finite, "fixture must use a finite Group");
  return *static_cast<d::Group*>(route->context);
}

// Independent topology decoder. Deliberately does not call the runtime iterator.
std::vector<d::PageSpan> pending_spans(const d::Group& value) {
  std::vector<d::PageSpan> result;
#if TRIM_BITMAP
  const auto bits = value.geometry->trim_membership();
  for (std::size_t p = 0; p < value.geometry->payload_pages(); ++p) {
    if (!(bits[p / 64] & (std::uint64_t{1} << (p % 64)))) continue;
    if (!result.empty() && result.back().first + result.back().count == p) ++result.back().count;
    else result.push_back({p, 1});
  }
#else
  for (const auto span : value.trimming) result.push_back({span.first, span.count});
#endif
  return result;
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
#if TRIM_FAULT_PROBE
  reset_meter(); fail_discard = fault ? 3 : 0;
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
        for (const auto span : pending_spans(*value))
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
#if TRIM_FAULT_PROBE
    for (const auto& value : fixture.pool->internal->groups) {
      if (value->retiring) continue;
      const auto ranges = pending_spans(*value).size();
      max_ranges = std::max(max_ranges, static_cast<unsigned>(ranges));
      check(!limit || ranges <= limit, "batch exceeded range limit");
    }
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
#if TRIM_FAULT_PROBE
  check(meter.ranges == discard_count.load(), "helper syscall ranges differ from submitted topology");
  check(protect_count.load() + unsigned(fault) == discard_count.load(), "protection call count lost a run");
  check(meter.retry_jobs == unsigned(fault), "failed full batch was not retried exactly once");
  print_meter(); metering = false;
#endif
  std::puts("}");
  kept.clear(); check(bool(fixture.close()), "fixture close refused");
}

#if TRIM_FAULT_PROBE
namespace orbital::allocation::detail { void group_trim_run(Job&) noexcept; }
extern "C" void __real__ZN7orbital10allocation6detail8scheduleERNS0_5ArenaERNS1_3JobE(
    oa::Arena&, d::Job&) noexcept;
extern "C" void __wrap__ZN7orbital10allocation6detail8scheduleERNS0_5ArenaERNS1_3JobE(
    oa::Arena& arena, d::Job& job) noexcept {
  if (metering && job.run == d::group_trim_run) {
    auto& value = *static_cast<d::Group*>(job.context);
    ++meter.jobs;
    if (std::find(meter.scheduled.begin(), meter.scheduled.end(), &value) != meter.scheduled.end())
      ++meter.retry_jobs;
    else meter.scheduled.push_back(&value);
    if (value.retiring) {
      ++meter.retire_jobs;
      meter.retire_pages += value.retire_mapped / value.geometry->native_page();
    } else {
      const auto spans = pending_spans(value);
      meter.ranges += spans.size();
      for (const auto span : spans) meter.pages += span.count;
    }
  }
  __real__ZN7orbital10allocation6detail8scheduleERNS0_5ArenaERNS1_3JobE(arena, job);
}

extern "C" int __real_madvise(void*, std::size_t, int) noexcept;
extern "C" int __real_mprotect(void*, std::size_t, int) noexcept;
extern "C" int __wrap_madvise(void* address, std::size_t size, int advice) noexcept {
  if (metering && advice == MADV_DONTNEED) {
    const auto count = ++discard_count;
    if (count == fail_discard.load()) { errno = EIO; return -1; }
    if (address == target_address.load() && pause_discard.exchange(false)) {
      paused.store(true, std::memory_order_release);
      const auto deadline = Clock::now() + std::chrono::seconds(20);
      while (!resume.load(std::memory_order_acquire)) {
        check(Clock::now() < deadline, "paused helper was not released");
        std::this_thread::yield();
      }
    }
  }
  const auto result = __real_madvise(address, size, advice);
  if (metering && advice == MADV_DONTNEED && address == target_address.load()) {
    check(size == 5 * d::page_size(), "cross-word run was split before discard");
    target_discarded.store(result == 0, std::memory_order_release);
  }
  return result;
}

extern "C" int __wrap_mprotect(void* address, std::size_t size, int rights) noexcept {
  if (metering && rights == PROT_NONE) {
    ++protect_count;
    if (address == target_address.load() && partial_protect.exchange(false)) {
      check(size == 5 * d::page_size(), "cross-word run was split before protection");
      check(target_discarded.load(std::memory_order_acquire), "partial protection did not follow successful discard");
      check(__real_mprotect(address, d::page_size(), PROT_NONE) == 0,
          "could not produce a real partial protection change");
      ++partial_failures;
      errno = EIO;
      return -1;
    }
  }
  return __real_mprotect(address, size, rights);
}

template<class Predicate> void await(Predicate ready, const char* reason) {
  const auto deadline = Clock::now() + std::chrono::seconds(20);
  while (!ready()) {
    check(Clock::now() < deadline, reason);
    std::this_thread::yield();
  }
}

void seed_cross_word(oa::Pool& pool, d::Group& value) {
  for (std::size_t p = 62; p < 67; ++p) {
    const auto page = value.geometry->native_page();
    check(!value.geometry->mapped(p), "cross-word idle page was already mapped");
    check(d::claim(pool, page), "cross-word seed charge refused");
    auto* address = value.geometry->page_address(p);
    check(::mprotect(address, page, PROT_READ | PROT_WRITE) == 0, "cross-word seed protect failed");
    address[0] = std::byte{0xa7};
    check(bool(value.geometry->mark_mapped({p, 1})), "cross-word seed publication refused");
  }
}

void cross_word(const char* name, int owner_cpu, int helper_cpu) {
  compact_shape::probe::NativeFixture fixture;
  check(bool(fixture.open(1ULL << 30, owner_cpu, helper_cpu)), "cross-word fixture open refused");
  auto kept = take(oa::allocate(*fixture.pool, {.bytes = 64, .alignment = 1}));
  std::memset(kept.address, 0x5a, kept.size);
  auto& value = group(kept);
  seed_cross_word(*fixture.pool, value);
  const auto initial = fixture.statistics();
  const auto expected = inventory(value);
  check(expected.pages == 5 && expected.runs == 1, "cross-word fixture topology is wrong");
  const auto page = value.geometry->native_page();
  const bool partial = std::strcmp(name, "cross-word-partial-protect") == 0;
  const bool pause = std::strcmp(name, "cross-word-paused") == 0;
  reset_meter();
  target_address = value.geometry->page_address(62);
  partial_protect = partial; pause_discard = pause;
  auto progress = oa::trim_cold(*fixture.pool, 0);
  check(progress.kind() == orbital::Progress::Kind::waiting, "cross-word trim was not admitted");
  const auto admitted = pending_spans(value);
  check(admitted.size() == 1 && admitted[0].first == 62 && admitted[0].count == 5,
      "cross-word membership did not preserve the whole run");
  if (pause) {
    await([] { return paused.load(std::memory_order_acquire); }, "helper did not pause on cross-word run");
    const auto charged = fixture.arena->charged_bytes.load();
    for (unsigned i = 0; i < 20; ++i) {
      const auto current = fixture.statistics();
      check(current.mapped_bytes == initial.mapped_bytes && current.metadata_bytes == initial.metadata_bytes,
          "owner census changed unresolved trim accounting");
    }
    auto other = take(oa::allocate(*fixture.pool, {.bytes = 64, .alignment = 1}));
    check(&group(other) == &value, "paused helper prevented compatible reuse in the same Group");
    const auto* first = value.geometry->page_address(62);
    const auto* end = value.geometry->page_address(67);
    check(other.address < first || other.address >= end, "allocation claimed a pending trim page");
    std::memset(other.address, 0x39, other.size);
    check(fixture.arena->charged_bytes.load() == charged, "compatible paused reuse grew the charge");
    const auto retained = pending_spans(value);
    check(retained.size() == 1 && retained[0].first == 62 && retained[0].count == 5,
        "owner operations overwrote immutable helper membership");
    for (const auto byte : oa::bytes(other)) check(byte == std::byte{0x39}, "paused allocation bytes changed");
    oa::release(other);
    resume.store(true, std::memory_order_release);
  }
  unsigned failures = 0;
  const auto deadline = Clock::now() + std::chrono::seconds(20);
  for (;;) {
    if (progress.kind() == orbital::Progress::Kind::failed) {
      check(partial && progress.failure() == orbital::Error::io && failures++ == 0,
          "partial protection produced the wrong failure result");
      const auto retained = pending_spans(value);
      check(retained.size() == 1 && retained[0].first == 62 && retained[0].count == 5,
          "partially protected run was shortened or lost");
      for (std::size_t p = 62; p < 67; ++p)
        check(value.geometry->trimming(p), "failed run lost exclusion on one of its pages");
      const auto unsettled = fixture.statistics();
      check(unsettled.mapped_bytes == initial.mapped_bytes && unsettled.trim_pages == initial.trim_pages,
          "partial failure refunded or counted unconfirmed pages");
      check(fixture.arena->charged_bytes.load() == unsettled.metadata_bytes + unsettled.mapped_bytes,
          "partially protected run lost its complete charge");
      // Grid 5's 512-byte homes cover pages 62 and 63. Its claim must skip that run.
      auto identity = take(value.geometry->claim_identity((std::uint64_t{1} << 5) - 1));
      auto home = take(value.geometry->claim_home(identity, 512, 1));
      check(home.pages.first + home.pages.count <= 62 || home.pages.first >= 67,
          "failed protection allowed a Home to overlap reserved pages");
      check(bool(value.geometry->release_home(home)), "failed-run probe home release refused");
      check(bool(value.geometry->release_identity(identity)), "failed-run probe identity release refused");
    } else if (progress.kind() == orbital::Progress::Kind::done) break;
    else check(progress.kind() == orbital::Progress::Kind::waiting, "cross-word trim returned invalid progress");
    check(Clock::now() < deadline, "cross-word trim did not settle");
    orbital::worker::poll(fixture.lane, 64);
    std::this_thread::yield();
    progress = oa::trim_cold(*fixture.pool, 0);
  }
  const auto after = fixture.statistics();
  check(failures == unsigned(partial) && partial_failures == unsigned(partial),
      "partial protection was not injected and reported exactly once");
  check(pending_spans(value).empty(), "settled run retained membership");
  for (std::size_t p = 62; p < 67; ++p)
    check(!value.geometry->mapped(p) && !value.geometry->trimming(p), "confirmed cross-word page stayed available or pending");
  check(initial.mapped_bytes - after.mapped_bytes == 5 * page && after.trim_pages - initial.trim_pages == 5,
      "cross-word retry did not refund exactly five pages");
  check(fixture.arena->charged_bytes.load() == after.metadata_bytes + after.mapped_bytes,
      "cross-word settled budget was not conserved");
  check(meter.jobs == 1 + unsigned(partial) && meter.retry_jobs == unsigned(partial) &&
      meter.ranges == 1 + unsigned(partial) && discard_count == 1 + unsigned(partial) &&
      protect_count == 1 + unsigned(partial) && meter.pages == 5 * (1 + unsigned(partial)),
      "cross-word run was split or retried with different endpoints");
  for (const auto byte : oa::bytes(kept)) check(byte == std::byte{0x5a}, "cross-word trim damaged live bytes");
  std::printf("{\"case\":\"%s\",\"fault\":%s,\"paused_owner_statistics_and_allocation\":%s,"
      "\"before_metadata\":%llu,\"before_mapped\":%llu,\"after_mapped\":%llu,\"passed\":true",
      name, partial ? "true" : "false", pause ? "true" : "false",
      static_cast<unsigned long long>(initial.metadata_bytes), static_cast<unsigned long long>(initial.mapped_bytes),
      static_cast<unsigned long long>(after.mapped_bytes));
  print_meter(); std::puts("}"); metering = false;
  oa::release(kept); check(bool(fixture.close()), "cross-word fixture close refused");
}
#endif

int main(int argc, char** argv) {
  check(argc >= 4, "usage: probe OWNER_CPU HELPER_CPU RANGE_LIMIT [CASE [fault]]");
  const int owner = std::atoi(argv[1]), helper = std::atoi(argv[2]);
  const unsigned limit = std::strtoul(argv[3], nullptr, 10);
  if (argc > 4) {
#if TRIM_FAULT_PROBE
    if (std::strncmp(argv[4], "cross-word", 10) == 0) cross_word(argv[4], owner, helper);
    else
#endif
      run(argv[4], owner, helper, limit, argc > 5);
  }
  else for (const auto* name : {"fragmented-a", "fragmented-b", "contiguous", "coalesced-16", "mixed", "floor", "empty"})
    run(name, owner, helper, limit, false);
}
