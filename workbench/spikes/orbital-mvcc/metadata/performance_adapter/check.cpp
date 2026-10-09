#include "../../performance/candidate_runtime.h"
#include "../../performance/oracle.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <type_traits>

namespace perf = orbital_versioned_performance;
namespace data = orbital_bench::versioned;
namespace cd = compact_descriptor;
using namespace orbital_bench;

struct Counts { std::size_t reads{}, refusals{}, acquisitions{}, late_discoveries{}; } counts;

perf::Golden golden(std::span<const Row> rows) {
  perf::Golden result;
  for (const auto& row : rows) result.emplace(row.key, row.record);
  return result;
}

// This test changes an actually owned mutable Store through its benchmark's
// const catalog accessor. The read adapter remains the real Tooling adapter;
// there is no second runtime or alternative acquisition implementation here.
template<class Store>
void replace(perf::Candidate<Store>& candidate, std::size_t object,
             std::span<const Row> rows) {
  auto& store = const_cast<Store&>(candidate.catalog());
  typename Store::Scope basis(store);
  const auto& corpus = candidate.corpus();
  const auto& image = corpus.images.at(object);
  auto backing = std::make_shared<std::vector<std::byte>>(image.encoded.size());
  const data::WritePart target{image.address.part, *backing};
  data::KernelCosts costs;
  perf::need(data::construct_into(corpus.schema, image.shape, {&target, 1}, rows, costs));
  const auto logical = perf::codec::detail::count_bytes(image.shape.records);
  const cd::RepresentationSpec spec{image.address.object, basis.cut() + 1, {},
      data::data_program, logical, image.shape.interpretation, {}};
  const cd::PartSource part{image.address.part, *backing,
      image.shape.parts[0].interpretation, 0, 1, backing};
  const compact_history::Change change{image.address.object,
      perf::need(store.capture(spec, {&part, 1}))};
  auto prepared = perf::need(store.prepare(basis, {&change, 1}));
  perf::need(store.install(prepared));
}

template<class Store, bool Direct>
void reads(perf::Candidate<Store>& candidate) {
  using Bodies = perf::CandidateBodies<Store, Direct>;
  static_assert(!std::is_copy_constructible_v<Bodies>);
  static_assert(!std::is_move_constructible_v<Bodies>);
  const auto& corpus = candidate.corpus();
  const auto acquire = [&] { return Bodies{candidate}; };
  const auto allocations = candidate.memory_calls();
  assert(candidate.statistics().scopes == 0);
  {
    auto selected = acquire();
    assert(candidate.statistics().scopes == 1);
    ++counts.acquisitions;
    for (std::size_t object = 0; object < corpus.images.size(); ++object) {
      const auto& rows = corpus.images[object].rows;
      const auto expected = golden(rows);
      for (const auto key : {Key{0}, rows.front().key, rows[rows.size()/2].key, UINT64_MAX}) {
        for (const auto kind : {Kind::read, Kind::chain, Kind::scan}) {
          const Operation op{.kind=kind, .key=key, .record={}, .count=16};
          const auto result = perf::read_operation(selected.body(object), op);
          assert(result == perf::oracle(expected, op));
          ++counts.reads;
        }
      }
    }
    assert(selected.bindings == corpus.images.size());
    try { (void)selected.body(corpus.images.size()); assert(false); }
    catch (const std::exception&) { ++counts.refusals; }
  }
  assert(candidate.statistics().scopes == 0);
  assert(candidate.memory_calls() == allocations);
}

template<class Store> void one(const char* name) {
  perf::Corpus corpus(1032, 8, 8132);
  perf::Candidate<Store> candidate(corpus, 64 * 1024 * 1024);
  const auto initial = candidate.statistics();
  assert(initial.objects == corpus.images.size() && initial.versions == initial.objects);
  assert(initial.requested_bytes && initial.allocation_calls && candidate.charged_bytes());
  {
    typename Store::Scope scope(candidate.catalog());
    for (const auto& image : corpus.images) {
      const auto selected = scope.bind(image.address.object);
      const auto part = perf::need(selected.part(image.address.part));
      assert(part.body().data() != image.encoded.data());
      assert(std::ranges::equal(part.body(), image.encoded));
    }
  }
  // Native access must rely on the published owner, not the fixture's payload.
  auto saved = corpus.images[0].encoded;
  std::fill(corpus.images[0].encoded.begin(), corpus.images[0].encoded.end(), std::byte{0});
  reads<Store, false>(candidate);
  reads<Store, true>(candidate);
  corpus.images[0].encoded = std::move(saved);

  const auto changed_object = corpus.images.size()-1;
  auto changed = corpus.images[changed_object].rows;
  changed.front().record.value ^= value_mask;
  changed.front().record.tag ^= UINT32_MAX;
  const Operation probe{.kind=Kind::chain, .key=changed.front().key, .record={}, .count=16};
  const auto old_expected = perf::oracle(golden(corpus.images[changed_object].rows), probe);
  const auto new_expected = perf::oracle(golden(changed), probe);
  assert(old_expected != new_expected);
  {
    auto old = perf::CandidateBodies<Store, true>{candidate};
    (void)old.body(0); // Deliberately do not bind the changed Object yet.
    replace(candidate, changed_object, changed);
    auto& store = const_cast<Store&>(candidate.catalog());
    (void)store.drain();
    assert(candidate.statistics().versions == initial.versions+1);
    assert(perf::read_operation(old.body(changed_object), probe) == old_expected);
    auto current = perf::CandidateBodies<Store, true>{candidate};
    assert(perf::read_operation(current.body(changed_object), probe) == new_expected);
    assert(candidate.statistics().scopes == 2);
    ++counts.late_discoveries;
  }
  auto& store = const_cast<Store&>(candidate.catalog());
  (void)store.drain();
  assert(candidate.statistics().scopes == 0 && candidate.statistics().versions == initial.versions);
  const auto final = candidate.statistics();
  std::printf("{\"candidate\":\"%s\",\"initial_metadata_live\":%zu,\"initial_metadata_peak\":%zu,"
      "\"initial_metadata_allocations\":%zu,\"final_metadata_live\":%zu,\"final_metadata_peak\":%zu,"
      "\"final_metadata_allocations\":%zu,\"initial_objects\":%zu,\"node_bytes\":%zu,"
      "\"core_bytes\":%zu,\"history_capacity_bytes\":%zu,\"descriptor_directory_capacity_bytes\":%zu,"
      "\"copied_metadata_bytes\":%zu,\"captured_descriptor_bytes\":%zu,\"arena_charged_bytes\":%llu,"
      "\"candidate_resident_bytes\":%zu,\"ordinary_bodies_bytes\":%zu,\"direct_bodies_bytes\":%zu,"
      "\"scope_bytes\":%zu,\"shared_schema_bytes\":%zu,\"scope_and_bind_pmr_allocations\":0}",
      name, initial.requested_bytes, initial.peak_bytes, initial.allocation_calls,
      final.requested_bytes, final.peak_bytes, final.allocation_calls, initial.objects,
      initial.node_bytes, initial.core_bytes, initial.history_capacity_bytes,
      initial.descriptor_directory_capacity_bytes, initial.copied_metadata_bytes,
      initial.captured_descriptor_bytes, (unsigned long long)candidate.charged_bytes(),
      sizeof(candidate), sizeof(perf::CandidateBodies<Store, false>),
      sizeof(perf::CandidateBodies<Store, true>), sizeof(typename Store::Scope), sizeof(corpus.schema));
}
int main() {
  std::printf("{\"scope\":\"actual Tooling compact read adapters; independent correctness and accounting; no timing\",\"candidates\":[");
  one<compact_history::Store8>("8"); std::printf(",");
  one<compact_history::Store16>("16"); std::printf(",");
  one<compact_history::Store32>("32");
  std::printf("],\"independent_read_results\":%zu,\"bound_refusals\":%zu,\"stationary_acquisitions\":%zu,"
      "\"late_old_scope_discoveries\":%zu,\"exclusions\":\"Corpus schema/images/rows/encoded/control storage; "
      "physical Arena/Pool metadata and allocator overhead; replacement-only test vector/shared owner is external\"}\n",
      counts.reads, counts.refusals, counts.acquisitions, counts.late_discoveries);
}
