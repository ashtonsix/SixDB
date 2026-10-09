#include "heat.h"
#include "../shape/native_fixture.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <source_location>
#include <thread>
#include <vector>

namespace h = orbital_heat;
namespace oa = orbital::allocation;
namespace d = oa::detail;
using Clock = std::chrono::steady_clock;
const auto boundary = Clock::time_point{Clock::duration{INT64_C(1) << 50}};
const auto period = Clock::duration{1000000};

template<class T> void check(const T& value, std::source_location location = std::source_location::current()) {
  if (value) return;
  std::fprintf(stderr, "heat check failed at %s:%u\n", location.file_name(), location.line());
  std::abort();
}
template<class T> T take(orbital::Result<T> r) { check(r); return std::move(*r); }

struct Item { oa::Allocation allocation; d::Home home; d::Group* group; };
struct Trace {
  compact_shape::probe::NativeFixture native;
  std::vector<Item> items;
  Trace(unsigned bytes, unsigned count) {
    check(native.open());
    items.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
      auto allocation = take(oa::allocate(*native.pool, {.bytes = bytes, .alignment = 1}));
      auto* group = static_cast<d::Group*>(allocation.storage->domain->context);
      check(allocation.storage->domain->finite);
      auto home = take(group->geometry->bound_home({allocation.storage->identity}));
      if (i) check(items.front().group == group);
      items.push_back({std::move(allocation), home, group});
    }
  }
  d::Group& group() { return *items.front().group; }
  void drain() {
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (orbital::worker::pending(native.lane)) {
      check(Clock::now() < deadline);
      orbital::worker::poll(native.lane, 128);
    }
  }
  std::vector<std::size_t> eligible() {
    auto& g = group();
    check(g.geometry->live_pages(g.live));
    std::vector<std::size_t> result;
    for (std::size_t page = 0; page < g.geometry->payload_pages(); ++page)
      if (g.geometry->mapped(page) && !g.geometry->trimming(page) &&
          !(g.live[page / 64] & (std::uint64_t{1} << (page % 64)))) result.push_back(page);
    return result;
  }
  // An observation oracle over captured physical spans, not a proposed runtime
  // page index. Its traversal cost cannot be quoted as production sweep cost.
  unsigned page_heat(h::EpochBanks& banks, std::size_t page) const {
    unsigned result{};
    for (const auto& item : items)
      if (page >= item.home.pages.first && page - item.home.pages.first < item.home.pages.count)
        result = std::max(result, banks.heat(item.home));
    return result;
  }
};

void neighbours(unsigned bytes, unsigned count) {
  Trace trace(bytes, count);
  auto& group = trace.group();
  h::GroupAdvice aggregate(group.domain.mutex, trace.native.lane);
  h::EpochBanks spatial(*group.geometry, group.palette, group.domain.mutex, boundary, period);
  h::EpochBanks group_epochs(*group.geometry, group.palette, group.domain.mutex, boundary, period, true);
  const auto hot_page = trace.items.front().home.pages.first;
  for (const auto& item : trace.items) {
    const bool hot = item.home.pages.first == hot_page;
    const oa::ReleaseHint hint{hot ? oa::Temperature::hot : oa::Temperature::cold};
    aggregate.local(hint); spatial.local(item.home, hint); group_epochs.local(item.home, hint);
  }
  for (auto& item : trace.items) oa::release(item.allocation);
  const auto eligible = trace.eligible();
  check(!eligible.empty());
  const auto selected = *std::min_element(eligible.begin(), eligible.end(), [&](auto a, auto b) {
    return trace.page_heat(spatial, a) < trace.page_heat(spatial, b);
  });
  if (eligible.size() > 1) {
    check(eligible.front() == hot_page && selected != hot_page);
    check(trace.page_heat(spatial, selected) == 0 && trace.page_heat(spatial, hot_page) == 2);
    check(trace.page_heat(group_epochs, selected) == 2);
  }
  std::printf("{\"case\":\"neighbours\",\"body_bytes\":%u,\"allocations\":%u,\"mapped_body_pages\":%zu,"
      "\"reserved_body_bytes\":%zu,\"aggregate_header\":%zu,\"epoch_header\":%zu,"
      "\"spatial_banks\":%zu,\"spatial_usable\":%zu,\"group_epoch_banks\":%zu,\"group_epoch_usable\":%zu,\"observer_table_request\":%zu,\"observer_table_usable\":%zu,\"aggregate_chosen_hot\":%s,\"spatial_chosen_hot\":%s}\n",
      bytes, count, group.geometry->mapped_pages(), group.geometry->reserved_bytes(), sizeof(aggregate), sizeof(spatial),
      spatial.bank_bytes(), spatial.heap_usable(), group_epochs.bank_bytes(), group_epochs.heap_usable(),
      trace.items.capacity() * sizeof(Item), ::malloc_usable_size(trace.items.data()), eligible.front() == hot_page ? "true" : "false",
      selected == hot_page ? "true" : "false");
}

void physical_cells_and_rollover() {
  Trace trace(48, 4);
  auto& group = trace.group();
  h::EpochBanks banks(*group.geometry, group.palette, group.domain.mutex, boundary, period);
  banks.local(trace.items[0].home, {.temperature = {}, .last_access = boundary - period / 2});
  check(banks.heat(trace.items[0].home) == 1);
  const auto offset = trace.items[2].home.address - group.geometry->page_address(0);
  check(offset % 128 == 96);
  banks.local(trace.items[2].home, {oa::Temperature::hot});
  check(banks.heat(trace.items[1].home) == 2 && banks.heat(trace.items[3].home) == 2);
  banks.local(trace.items[1].home, {oa::Temperature::cold});
  banks.local(trace.items[3].home, {.temperature = {}, .last_access = boundary - period * 4});
  check(banks.heat(trace.items[2].home) == 2);
  check(banks.rollover(boundary + period));
  check(banks.heat(trace.items[2].home) == 1);
  banks.local(trace.items[2].home, {.temperature = {}, .last_access = boundary});
  check(banks.heat(trace.items[2].home) == 1);
  check(banks.rollover(boundary + period * 2));
  check(banks.heat(trace.items[2].home) == 0);
  banks.foreign(trace.items[2].home, {.temperature = oa::Temperature::hot, .last_access = boundary});
  check(banks.heat(trace.items[2].home) == 0);
  banks.local(trace.items[2].home, {oa::Temperature::warm});
  check(banks.heat(trace.items[2].home) == 2); // Missing time is release-time recency.
  const auto old_home = trace.items[2].home;
  const auto old_identity = trace.items[2].allocation.storage->identity;
  oa::release(trace.items[2].allocation);
  auto replacement = take(oa::allocate(*trace.native.pool, {.bytes = 64, .alignment = 1}));
  check(replacement.storage->domain->context == &group);
  check(replacement.storage->identity == old_identity);
  const auto new_home = take(group.geometry->bound_home({replacement.storage->identity}));
  check(new_home.address != old_home.address);
  check(banks.heat(old_home) == 2); // Heat remains with the old physical range.
  oa::release(replacement);
}

void stalled_foreign_and_shared_word() {
  Trace trace(48, 4);
  auto& group = trace.group();
  h::EpochBanks banks(*group.geometry, group.palette, group.domain.mutex, boundary, period);
  std::atomic<bool> selected{}, resume{};
  std::thread foreign([&] {
    std::lock_guard lock(group.domain.mutex);
    const auto bank = banks.select_foreign_locked({oa::Temperature::hot});
    selected.store(true, std::memory_order_release);
    while (!resume.load(std::memory_order_acquire)) std::this_thread::yield();
    const auto protected_home = take(group.geometry->bound_home({trace.items[0].allocation.storage->identity}));
    banks.mark_foreign_locked(protected_home, bank);
  });
  const auto deadline = Clock::now() + std::chrono::seconds(5);
  while (!selected.load(std::memory_order_acquire)) { check(Clock::now() < deadline); std::this_thread::yield(); }
  check(!banks.rollover(boundary + period));
  banks.local(trace.items[3].home, {oa::Temperature::hot});
  check(banks.heat(trace.items[3].home) == 2);
  resume.store(true, std::memory_order_release); foreign.join();
  check(banks.heat(trace.items[0].home) == 2 && banks.heat(trace.items[3].home) == 2);
  check(banks.rollover(boundary + period));
  check(banks.heat(trace.items[0].home) == 1 && banks.heat(trace.items[3].home) == 1);
  check(banks.rollover(boundary + period * 2));
  check(!banks.heat(trace.items[0].home) && !banks.heat(trace.items[3].home));
}

void foreign_aggregate_and_authority() {
  Trace trace(64, 128);
  auto& group = trace.group();
  h::GroupAdvice aggregate(group.domain.mutex, trace.native.lane);
  h::EpochBanks banks(*group.geometry, group.palette, group.domain.mutex, boundary, period);
  aggregate.local({.temperature = oa::Temperature::hot, .last_access = boundary});
  std::thread foreign([&] { aggregate.foreign({.temperature = {}, .last_access = boundary + Clock::duration{1}}); });
  foreign.join(); trace.drain();
  check(aggregate.advice().temperature == oa::Temperature::hot && aggregate.advice().last_access == boundary + Clock::duration{1});
  aggregate.local({.temperature = oa::Temperature::cold, .last_access = boundary});
  check(aggregate.advice().temperature == oa::Temperature::hot);
  auto held = take(oa::hold(trace.items[0].allocation, {0, 64}));
  for (auto& item : trace.items) oa::release(item.allocation);
  auto eligible = trace.eligible();
  check(eligible.size() == 1 && eligible[0] != trace.items[0].home.pages.first);
  const auto candidate = eligible[0];
  banks.local(trace.items.back().home, {oa::Temperature::hot});
  const auto before = banks.heat(trace.items.back().home);
  check(group.geometry->begin_trim({candidate, 1}));
  check(trace.eligible().empty());
  check(group.geometry->finish_trim({candidate, 1}, false));
  check(trace.eligible().size() == 1 && banks.heat(trace.items.back().home) == before);
  oa::release(held);
  check(trace.eligible().size() == 2);
}

void repeated_shared_word() {
  Trace trace(48, 4);
  auto& group = trace.group();
  h::EpochBanks banks(*group.geometry, group.palette, group.domain.mutex, boundary, period);
  std::atomic<bool> start{};
  std::thread foreign([&] {
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    for (unsigned i = 0; i < 100000; ++i) banks.foreign(trace.items[0].home, {oa::Temperature::hot});
  });
  start.store(true, std::memory_order_release);
  for (unsigned i = 0; i < 100000; ++i) banks.local(trace.items[3].home, {oa::Temperature::warm});
  foreign.join();
  check(banks.heat(trace.items[0].home) == 2 && banks.heat(trace.items[3].home) == 2);
  check(banks.rollover(boundary + period * 3));
  check(!banks.heat(trace.items[0].home) && !banks.heat(trace.items[3].home));
  // A delayed hint without a supplied access time intentionally means arrival
  // recency. It cannot be distinguished from genuinely recent activity.
  banks.foreign(trace.items[0].home, {oa::Temperature::hot});
  check(banks.heat(trace.items[0].home) == 2);
}

int main() {
  for (auto count : {8u, 256u}) for (auto bytes : {48u, 64u, 1536u}) neighbours(bytes, count);
  physical_cells_and_rollover(); stalled_foreign_and_shared_word(); foreign_aggregate_and_authority(); repeated_shared_word();
  std::puts("{\"case\":\"histories\",\"passed\":true}");
}
