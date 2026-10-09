#include "backend.h"
#include "../../performance/oracle.h"
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <type_traits>

namespace c = orbital_mvcc::composed;
namespace p = orbital_versioned_performance;
namespace v = orbital_bench::versioned;
using namespace orbital_bench;
template<class T> void check(const T& value,
    std::source_location where = std::source_location::current()) {
  if (static_cast<bool>(value)) return;
  std::fprintf(stderr, "check failed: %s:%u\n", where.file_name(), where.line()); std::abort();
}
template<class Backend> void run(const char* name, v::Format initial) {
  static_assert(!std::is_move_constructible_v<typename Backend::Scope>);
  static_assert(!std::is_move_constructible_v<typename Backend::template Bodies<64>>);
  p::Corpus corpus(192, 64, 8132);
  std::vector<p::Golden> original(64);
  for (std::size_t object = 0; object < 64; ++object)
    for (const auto& row : corpus.images[object].rows) original[object].emplace(row.key, row.record);
  auto expected = original;
  std::uint64_t comparisons{}, refusals{};
  {
    Backend owner(corpus, 64 * 1024 * 1024, initial);
    const auto initial_account = owner.accounting();
    check(initial_account.active_allocations == (initial == v::Format::rows ? 64 : 256));
    const auto opposite = initial == v::Format::rows ? v::Format::columns : v::Format::rows;
    std::optional<typename Backend::Scope> old(std::in_place, owner.catalog());
    const auto first_cut = old->cut();
    check(owner.convert(0, opposite));
    const auto selected_alternative = owner.body(*old, 0, true);
    check(selected_alternative.format() == opposite);
    check(selected_alternative.selected.part_count() == (opposite == v::Format::rows ? 1 : 4));
    check(owner.convert(0, initial)); // Source is the actual adopted alternative.
    check(owner.cut() == first_cut && selected_alternative.format() == opposite);
    for (const auto& [key, record] : original[0]) {
      check(selected_alternative.get(key) == record); ++comparisons;
    }

    // Duplicate Object and key, sparse population, offered-order last wins.
    std::array<std::size_t, 4> objects{0, 63, 0, 0};
    std::array<Operation, 4> edits;
    std::array<Outcome, 4> output;
    for (std::size_t i = 0; i < edits.size(); ++i) {
      const auto key = corpus.images[objects[i]].rows[i == 3 ? 2 : 0].key;
      edits[i] = {.kind = Kind::update, .key = key, .record = {0, unsigned(100 + i), unsigned(200 + i)}, .count = 1};
      expected[objects[i]][key].value = edits[i].record.value;
      expected[objects[i]][key].tag = edits[i].record.tag;
    }
    check(owner.update(edits, output, objects));
    check(owner.cut() == first_cut + 1 && owner.write_costs.applied_updates == edits.size());
    for (const auto& result : output) {
      Outcome want; want.changes = 1; detail::mix(want.digest, 1); check(result == want);
    }
    {
      auto current = owner.read();
      for (std::size_t object = 0; object < 64; ++object) {
        // Except Object0, these are the first old-cut discoveries, after both
        // physical adoption and the real multi-Object logical publication.
        const auto before = owner.body(*old, object), after = owner.body(current, object);
        for (const auto& [key, record] : original[object]) {
          check(before.get(key) == record && after.get(key) == expected[object].at(key)); ++comparisons;
        }
        for (auto family : {"point-hit", "point-miss", "dependent-16", "scan-16"}) {
          const auto trace = make_trace(family, 3, 7, 9183);
          for (const auto& op : trace.operations) {
            check(p::read_operation(before, op) == p::oracle(original[object], op));
            check(p::read_operation(after, op) == p::oracle(expected[object], op)); comparisons += 2;
          }
        }
      }
    }
    // No clock: exercise the same stationary acquisition/cache factory that
    // the performance trial will instantiate inside its timed interval.
    const auto trace = make_trace("dependent-16", 3, 131, 762);
    for (std::size_t first = 0; first < trace.operations.size(); first += 64) {
      auto bodies = owner.template bodies<64>();
      for (auto i = first; i < std::min(first + 64, trace.operations.size()); ++i) {
        const auto object = i % 64;
        check(p::read_operation(bodies.body(object), trace.operations[i]) == p::oracle(expected[object], trace.operations[i]));
        ++comparisons;
      }
    }

    const std::array<std::size_t, 2> failed_objects{1, 62};
    std::array<Operation, 2> failed{{edits[0], edits[1]}};
    failed[1].key = 2;
    std::array<Outcome, 2> unchanged{{{.digest = 731}, {.digest = 947}}};
    const auto sentinels = unchanged;
    auto before_failure = owner.accounting(); const auto cut = owner.cut();
    auto missing = owner.update(failed, unchanged, failed_objects);
    check(!missing && missing.error() == orbital::Error::absent && owner.cut() == cut && unchanged == sentinels);
    check(owner.accounting().active_allocations == before_failure.active_allocations); ++refusals;
    failed[1] = edits[1];
    owner.metadata_limit(before_failure.metadata_live);
    auto capacity = owner.update(failed, unchanged, failed_objects);
    check(!capacity && capacity.error() == orbital::Error::capacity && unchanged == sentinels && owner.cut() == cut);
    owner.metadata_limit(before_failure.metadata_limit);
    check(owner.accounting().active_allocations == before_failure.active_allocations); ++refusals;
    auto partial = owner.convert(1, opposite, {.fail_after_rows = 1});
    check(!partial && owner.cut() == cut);
    check(owner.accounting().active_allocations == before_failure.active_allocations); ++refusals;
    {
      auto current = owner.read();
      for (std::size_t object = 0; object < 64; ++object)
        for (const auto& [key, record] : expected[object]) { check(owner.body(current, object).get(key) == record); ++comparisons; }
    }
    // A target pins bytes/Store identity, not discoverability of an obsolete
    // history cut. An old live Scope still authorizes adoption at its own cut.
    {
      std::optional<typename Backend::Conversion> delayed;
      {
        auto current = owner.read();
        delayed.emplace(p::need(owner.prepare_conversion(current, 63, opposite)));
      }
      const std::array<std::size_t, 1> destination{63};
      const std::array<Operation, 1> operation{edits[0]};
      std::array<Outcome, 1> result;
      check(owner.update(operation, result, destination));
      expected[63][operation[0].key].value = operation[0].record.value;
      expected[63][operation[0].key].tag = operation[0].record.tag;
      auto late = owner.adopt(*delayed);
      check(!late && late.error() == orbital::Error::absent); ++refusals;
      auto retained = p::need(owner.prepare_conversion(*old, 3, opposite));
      check(owner.adopt(retained));
      const auto old_converted = owner.body(*old, 3, true);
      check(old_converted.format() == opposite);
      for (const auto& [key, record] : original[3]) { check(old_converted.get(key) == record); ++comparisons; }
    }
    old.reset(); owner.drain(); owner.poll(65536);
    check(owner.statistics().versions == 64 && owner.statistics().scopes == 0);
    check(owner.convert(2, opposite));
    {
      auto current = owner.read();
      for (const auto& [key, record] : expected[2]) { check(owner.body(current, 2, true).get(key) == record); ++comparisons; }
    }
    {
      // Dense publication across all64 Objects; retain the previous mixed
      // representation cut without resolving it until after install.
      auto retained = owner.read();
      const auto before = expected;
      std::array<Operation, 64> dense;
      std::array<std::size_t, 64> destinations;
      std::array<Outcome, 64> results;
      for (std::size_t object = 0; object < 64; ++object) {
        destinations[object] = object;
        dense[object] = {.kind = Kind::update, .key = corpus.images[object].rows[1].key,
            .record = {0, unsigned(900 + object), unsigned(1700 + object)}, .count = 1};
        expected[object][dense[object].key].value = dense[object].record.value;
        expected[object][dense[object].key].tag = dense[object].record.tag;
      }
      const auto cut_before = owner.cut();
      check(owner.update(dense, results, destinations));
      check(owner.cut() == cut_before + 1);
      auto current = owner.read();
      for (std::size_t object = 0; object < 64; ++object)
        for (const auto& [key, record] : expected[object]) {
          check(owner.body(retained, object, true).get(key) == before[object].at(key));
          check(owner.body(current, object).get(key) == record); comparisons += 2;
        }
    }
    owner.drain(); owner.poll(65536);
    check(owner.statistics().versions == 64 && owner.statistics().scopes == 0);
    const auto final = owner.accounting();
    const auto held_extent = final.held_extent_bytes ? std::to_string(*final.held_extent_bytes) : "null";
    std::printf("{\"backend\":\"%s\",\"initial\":%u,\"comparisons\":%llu,\"refusals\":%llu,"
        "\"initial_pmr_live\":%zu,\"initial_pmr_peak\":%zu,\"initial_pool_metadata\":%llu,"
        "\"pmr_live\":%zu,\"pmr_peak\":%zu,\"pmr_calls\":%zu,\"shared_shapes\":%zu,"
        "\"write_shape_heap\":%zu,\"shared_schema\":%zu,\"backend_record\":%zu,"
        "\"scope\":%zu,\"body\":%zu,\"bodies1\":%zu,\"bodies64\":%zu,"
        "\"write_arrays\":%zu,\"private_parts\":%zu,\"pool_metadata\":%llu,"
        "\"pool_mapped\":%llu,\"pool_reserved\":%llu,\"arena_charge\":%llu,"
        "\"active_allocations\":%zu,\"live_capacity\":%zu,\"held_extent\":%s,"
        "\"borrowed_write_shape_record\":%zu,\"pool_setup_and_inventory\":%zu}\n",
        name, unsigned(initial), static_cast<unsigned long long>(comparisons), static_cast<unsigned long long>(refusals),
        initial_account.metadata_live, initial_account.metadata_peak, static_cast<unsigned long long>(initial_account.pool.metadata_bytes),
        final.metadata_live, final.metadata_peak, final.metadata_calls, final.shared_shape_bytes,
        final.write_shape_heap_bytes, final.schema_bytes, sizeof(Backend), sizeof(typename Backend::Scope), sizeof(c::Body),
        sizeof(typename Backend::template Bodies<1>), sizeof(typename Backend::template Bodies<64>), Backend::write_caller_arrays,
        sizeof(c::PrivateParts), static_cast<unsigned long long>(final.pool.metadata_bytes),
        static_cast<unsigned long long>(final.pool.mapped_bytes), static_cast<unsigned long long>(final.pool.reserved_bytes),
        static_cast<unsigned long long>(final.arena_charge), final.active_allocations, final.live_capacity_bytes, held_extent.c_str(),
        final.borrowed_write_shape_record_bytes, final.pool_setup_and_inventory_bytes);
  }
  // Backend teardown checks zero PMR; actual Runtime shutdown checks zero Arena.
}
template<class Backend> void check_window_extent(const char* name) {
  p::Corpus corpus(8192, 1, 918);
  check(corpus.images[0].encoded.size() > 65535);
  Backend owner(corpus, 64 * 1024 * 1024);
  const auto accounting = owner.accounting();
  check(accounting.active_allocations == 1);
  if (accounting.held_extent_bytes)
    check(*accounting.held_extent_bytes == corpus.images[0].encoded.size());
  const auto extent = accounting.held_extent_bytes ? std::to_string(*accounting.held_extent_bytes) : "null";
  std::printf("{\"backend\":\"%s\",\"wide_window\":true,\"held_extent\":%s,\"encoded_bytes\":%zu}\n",
      name, extent.c_str(), corpus.images[0].encoded.size());
}
int main() {
  for (auto format : {v::Format::rows, v::Format::columns}) {
    run<c::Inline>("inline16", format);
    run<c::Directory>("directory16", format);
  }
  check_window_extent<c::Inline>("inline16");
  check_window_extent<c::Directory>("directory16");
  std::puts("{\"passed\":true,\"teardown_checked\":true,\"timings\":false}");
}
