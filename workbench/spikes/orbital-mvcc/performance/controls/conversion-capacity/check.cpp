#include "workbench/spikes/orbital-mvcc/metadata/composed/backend.h"
#include "workbench/spikes/orbital-mvcc/metadata/orbital/shape/shape_private.h"
#include "orbital/src/allocation/internal.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <source_location>
#include <vector>

namespace c = orbital_mvcc::composed;
namespace p = orbital_versioned_performance;
namespace v = orbital_bench::versioned;
namespace oa = orbital::allocation;
namespace cs = compact_shape;
template<class T> void check(const T& ok, std::source_location at = std::source_location::current()) {
  if (bool(ok)) return;
  std::fprintf(stderr, "check failed at %s:%u\n", at.file_name(), at.line()); std::abort();
}
struct Part {
  std::size_t object, index, extent;
  const char* role;
  cs::Realization* realization;
  oa::Storage* storage;
  const std::byte* address;
};
std::size_t row_comparisons{};
void rows(const c::Body& body, const p::Corpus& corpus, std::size_t object) {
  check(body.size() == 16384);
  for (std::size_t i = 0; i < body.size(); ++i) {
    const auto actual = body.row_unchecked(i);
    const auto& expected = corpus.images[object].rows[i];
    check(actual.key == expected.key && actual.record == expected.record);
    ++row_comparisons;
  }
}
struct Snapshot {
  std::vector<Part> parts;
  std::size_t occupied{}, live_capacity{}, live{}, idle{}, alternatives{};
};
Snapshot snapshot(const char* phase, c::Inline& owner, const c::Material& material,
    const p::Corpus& corpus, std::size_t expected_alternatives) {
  Snapshot out;
  {
    auto scope = owner.read();
    for (std::size_t object = 0; object < 2; ++object) {
      auto primary = scope.bind(corpus.images[object].address.object);
      check(primary);
      auto alternative = scope.bind_alternative(corpus.images[object].address.object);
      check(alternative || alternative.error() == orbital::Error::absent);
      if (alternative) { ++out.alternatives; check(alternative->realization != primary.realization); }
      for (const auto& [selected, role] : std::array{
          std::pair{primary, "primary"}, std::pair{alternative ? *alternative : cs::Selection{}, "alternative"}}) {
        if (!selected) continue;
        rows({&material, selected}, corpus, object);
        check(selected.realization->owners == orbital::PartOwners::independent_allocation);
        const auto* bodies = orbital::object_detail::allocated_bodies(selected.realization);
        for (std::size_t i = 0; i < selected.part_count(); ++i)
          out.parts.push_back({object, i, p::need(selected.control().extent(i)), role,
              selected.realization, bodies[i].owner, bodies[i].address});
      }
    }
  } // A diagnostic Scope must not retain a replaced alternative in the next phase.
  const auto stats = owner.statistics();
  const auto account = owner.accounting();
  check(stats.alternative_count == out.alternatives && out.alternatives == expected_alternatives);
  check(stats.versions == 2 && stats.objects == 2 && owner.cut() == 1);
  const auto& pool = *material.runtime.pool;
  check(pool.internal->groups.empty());
  std::map<oa::Storage*, const Part*> by_storage;
  for (const auto& part : out.parts) {
    check(by_storage.emplace(part.storage, &part).second); // Independent complete bodies never alias here.
    out.occupied += part.extent;
  }
  std::printf("{\"phase\":\"%s\",\"cut\":%llu,\"versions\":%zu,\"scopes\":%zu,\"alternative_count\":%zu,\"allocations\":%llu,\"reuses\":%llu,\"windows\":[",
      phase, (unsigned long long)owner.cut(), stats.versions, stats.scopes, out.alternatives,
      (unsigned long long)account.pool.allocations, (unsigned long long)account.pool.reuses);
  bool first = true;
  for (const auto& owned : pool.internal->windows) {
    const auto& window = *owned;
    auto* storage = const_cast<oa::Storage*>(&window.uses.slots[0]);
    const auto users = storage->users.load(std::memory_order_relaxed);
    const auto found = by_storage.find(storage);
    const auto* part = found == by_storage.end() ? nullptr : found->second;
    check(users == (part ? 1u : 0u));
    check(!window.trim_pending && !window.preparation && window.charged == window.prepared);
    check(!window.uses.domain.queued.load(std::memory_order_relaxed));
    check(oa::detail::route(*storage)->context == &window);
    if (part) {
      check(part->address == window.address && part->extent <= window.prepared);
      ++out.live; out.live_capacity += window.prepared;
    } else out.idle += window.prepared;
    std::printf("%s{\"window\":%zu,\"storage\":\"%p\",\"address\":\"%p\",\"users\":%u,\"prepared\":%zu,\"reserved\":%zu,\"role\":\"%s\",\"extent\":%zu",
        first ? "" : ",", window.owner_index, (void*)storage, (void*)window.address,
        users, window.prepared, window.reserved, part ? part->role : "idle", part ? part->extent : 0);
    if (part) std::printf(",\"object\":%zu,\"part\":%zu,\"realization\":\"%p\",\"realization_references\":%u",
        part->object, part->index, (void*)part->realization, part->realization->references);
    std::printf("}"); first = false;
  }
  check(out.live == by_storage.size() && out.live == account.active_allocations);
  check(out.live_capacity == account.live_capacity_bytes && out.idle == account.pool.idle_bytes);
  check(out.live_capacity + out.idle == account.pool.mapped_bytes);
  check(account.pool.preparing_bytes == 0 && account.pool.uncertain_bytes == 0 && account.pool.foreign_returns == 0);
  std::printf("],\"occupied_bytes\":%zu,\"live_capacity_bytes\":%zu,\"active_allocations\":%zu,\"idle_bytes\":%zu,\"mapped_bytes\":%llu,\"capacity_slack_bytes\":%zu,\"rows_checked_cumulative\":%zu}\n",
      out.occupied, out.live_capacity, out.live, out.idle, (unsigned long long)account.pool.mapped_bytes,
      out.live_capacity - out.occupied, row_comparisons);
  return out;
}
const Part& part(const Snapshot& state, std::size_t object, const char* role, std::size_t index) {
  for (const auto& candidate : state.parts)
    if (candidate.object == object && candidate.index == index && std::string_view(candidate.role) == role) return candidate;
  check(false); std::abort();
}
int main() {
  p::Corpus corpus(32768, 2, 8132);
  orbital::Ref<oa::Arena> retained_arena;
  {
    c::Inline owner(corpus, 64 * 1024 * 1024, v::Format::columns);
    std::optional<c::Inline::Scope> old(std::in_place, owner.catalog());
    const std::array primary{owner.body(*old, 0), owner.body(*old, 1)};
    const auto& material = *primary[0].material;
    retained_arena = material.runtime.arena;
    const auto initial = snapshot("initial_series", owner, material, corpus, 0);
    check(initial.occupied == 737280 && initial.live_capacity == 737280 && initial.live == 8);
    check(owner.convert(0, v::Format::rows));
    const auto tuple0 = snapshot("object0_tuple", owner, material, corpus, 1);
    check(owner.convert(1, v::Format::rows));
    const auto tuples = snapshot("both_tuple", owner, material, corpus, 2);
    check(owner.convert(0, v::Format::columns));
    const auto series0 = snapshot("object0_series", owner, material, corpus, 2);
    check(series0.idle == 376832 && series0.live_capacity == series0.occupied);
    check(owner.convert(1, v::Format::columns));
    const auto final = snapshot("both_series_reader_live", owner, material, corpus, 2);
    const auto& reused = part(final, 1, "alternative", 0);
    const auto& old_tuple0 = part(tuple0, 0, "alternative", 0);
    check(reused.storage == old_tuple0.storage && reused.address == old_tuple0.address);
    check(reused.extent == 131072 && old_tuple0.extent == 376832);
    const auto& old_tuple1 = part(tuples, 1, "alternative", 0);
    check(old_tuple1.storage->users.load() == 0);
    check(static_cast<oa::detail::Window*>(oa::detail::route(*old_tuple1.storage)->context)->prepared == 376832);
    check(final.live == 16 && final.occupied == 1474560 && final.live_capacity == 1720320 && final.idle == 376832);
    check(owner.accounting().pool.reuses == 1);
    for (std::size_t object = 0; object < 2; ++object) {
      rows(primary[object], corpus, object); // Borrow made before all four conversions stays valid.
      check(primary[object].selected.realization == part(final, object, "primary", 0).realization);
    }
    old.reset(); owner.drain(); owner.poll(128);
    const auto released = snapshot("reader_released", owner, material, corpus, 2);
    check(released.live_capacity == final.live_capacity && released.occupied == final.occupied);
    check(owner.statistics().scopes == 0);
  } // Production catalog/material/runtime destructors enforce zero PMR live bytes and drained Pool.
  check(retained_arena->charged_bytes.load() == 0);
  std::printf("{\"passed\":true,\"row_comparisons\":%zu,\"conversions\":4,\"oversized_reuses\":1,\"idle_tuple_windows\":1,\"after_owner_destruction_arena_charge\":0}\n", row_comparisons);
}
