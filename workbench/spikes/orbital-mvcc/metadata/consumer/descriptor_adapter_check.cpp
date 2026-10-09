#include "descriptor_adapter.h"
#include "operations.h"
#if CONSUMER_METADATA_FROZEN
#include "workbench/benchmarks/orbital/versioned-performance/read.h"
#else
#include "performance/read.h"
#endif
#include <cassert>
#include <cstdio>
#include <memory_resource>
#include <vector>

namespace c = orbital_mvcc::metadata_consumer;
namespace cd = compact_descriptor;
namespace v = orbital_bench::versioned;
namespace perf = orbital_versioned_performance;
using orbital_bench::Key;
using orbital_bench::Outcome;
using orbital_bench::Record;
using orbital_bench::Row;
struct Memory : std::pmr::memory_resource {
  std::size_t live{}, peak{}, calls{};
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live += bytes; peak = std::max(peak, live); ++calls;
    return result;
  }
  void do_deallocate(void* value, std::size_t bytes, std::size_t alignment) override {
    assert(live >= bytes); live -= bytes;
    std::pmr::new_delete_resource()->deallocate(value, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};
struct Body {
  std::pmr::vector<std::byte> bytes;
  std::size_t* destroyed;
  Body(std::pmr::memory_resource* resource, std::size_t size, std::size_t& counter)
      : bytes(size, resource), destroyed(&counter) {}
  ~Body() { ++*destroyed; }
};
std::uint64_t queries{}, refusals{}, descriptor_calls{}, descriptor_bytes{}, exported_reads{};
template<class T, class E> T take(std::expected<T, E> result) {
  assert(result); return std::move(*result);
}
void one(std::size_t count) {
  Memory metadata, external;
  std::size_t destroyed{};
  const v::Schema schema;
  const v::TupleControl control{schema, count};
  auto shape = take(v::shape(schema, v::Format::rows, count));
  std::vector<Row> rows;
  for (std::size_t i = 0; i < count; ++i)
    rows.push_back({3 + i * 17, {i + 1 < count ? 3 + (i + 1) * 17 : UINT64_MAX,
                               std::uint32_t((i * 8191 + 91) & orbital_bench::value_mask),
                               std::uint32_t(i * 12931 + 17)}});
  auto body = std::allocate_shared<Body>(std::pmr::polymorphic_allocator<Body>(&external),
                                        &external, shape.parts[0].body_bytes, destroyed);
  const v::WritePart destination{shape.parts[0].id, body->bytes};
  v::KernelCosts costs;
  assert(v::construct_into(schema, shape, {&destination, 1}, rows, costs));
  const auto logical = c::detail::count_bytes(count);
  cd::RepresentationSpec spec{{9, 73}, 7, {3, 1}, v::data_program, logical, shape.interpretation, {}};
  cd::PartSource source{shape.parts[0].id, body->bytes, shape.parts[0].interpretation,
                         0, 1, body};
  auto* descriptor = take(cd::capture(metadata, spec, {&source, 1}));
  const cd::Selection selected{descriptor};
  ++descriptor_calls; descriptor_bytes += cd::footprint(descriptor).requested_bytes;
  const auto qualify = [&](bool borrowed, cd::Selection selection, orbital::ObjectId object,
                           std::uint64_t version, orbital::PartId part) {
    return borrowed ? c::qualify_descriptor_borrowed_tuple8(control, selection, object, version, part)
                    : c::qualify_descriptor_tuple8(control, selection, object, version, part);
  };
  const std::array cursors{
      take(qualify(false, selected, spec.object, spec.version, source.id)),
      take(qualify(true, selected, spec.object, spec.version, source.id))};
  const v::ReadPart encoded{source.id, source.body, source.interpretation};
  auto baseline = take(v::qualify_tuple(control, shape.interpretation, {&encoded, 1}));
  for (const auto cursor : cursors)
  for (Key key : {Key{0}, Key{3}, Key{4}, Key{20}, Key{21}, Key{3+128*17}, UINT64_MAX}) {
    for (unsigned steps : {0u, 1u, 16u}) {
      Outcome actual{99, 99, 99};
      c::read_chain8_into(control, cursor, key, steps, actual);
      const auto expected = perf::read_operation(baseline.open(control),
          {.kind=orbital_bench::Kind::chain, .key=key, .record={}, .count=steps});
      assert(actual == expected); ++queries;
    }
  }
  for (bool borrowed : {false, true}) {
    assert(!qualify(borrowed, selected, {9, 74}, spec.version, source.id));
    assert(!qualify(borrowed, selected, spec.object, spec.version+1, source.id));
    assert(!qualify(borrowed, selected, spec.object, spec.version, {1, 2}));
    assert(!qualify(borrowed, {}, spec.object, spec.version, source.id));
    refusals += 4;
  }
  const auto wrong = [&](cd::RepresentationSpec malformed) {
    auto* bad = take(cd::capture(metadata, malformed, {&source, 1}));
    const cd::Selection bad_selected{bad};
    for (bool borrowed : {false, true}) {
      assert(!qualify(borrowed, bad_selected, spec.object, spec.version, source.id));
      ++refusals;
    }
    // This mechanism intentionally admits only Part facts. Full admission above
    // must reject bad parents even when their individually valid Part is usable.
    assert(c::qualify_selected_part8(control, bad_selected, source.id));
    cd::release(bad); ++descriptor_calls;
  };
  auto malformed = spec; malformed.format.low ^= 1; wrong(malformed);
  auto bad_count = logical; bad_count[0] ^= std::byte{1};
  malformed = spec; malformed.logical_metadata = bad_count; wrong(malformed);
  malformed = spec; malformed.logical_metadata = std::span(logical).first(7); wrong(malformed);
  auto bad_parent = shape.interpretation; bad_parent[20] = std::byte{1};
  malformed = spec; malformed.interpretation = bad_parent; wrong(malformed);

  auto* exported = take(cd::retain_part(selected, 0));
  cd::release(descriptor);
  source.backing.reset();
  body.reset();
  // Only the actual Export now protects payload. The baseline cursor borrows
  // those same bytes; both readers rely on this protection during comparison.
  auto export_view = cd::view(exported);
  const auto retained = take(c::qualify_borrowed_part8(control, export_view, source.id));
  Outcome output;
  c::read_chain8_into(control, retained, 3, 16, output);
  assert(output == perf::read_operation(baseline.open(control),
      {.kind=orbital_bench::Kind::chain, .key=3, .record={}, .count=16}));
  ++exported_reads;
  cd::release(exported);
  assert(metadata.live == 0 && external.live == 0 && destroyed == 1);
}
int main() {
  for (auto count : {0u, 1u, 129u}) one(count);
  std::printf("{\"scope\":\"actual compact descriptor full-parent and Part-only consumer adapters; no timing\","
      "\"cases\":3,\"same_body_complete_chain_checks\":%llu,\"wrong_parent_or_address_refusals\":%llu,"
      "\"descriptor_captures\":%llu,\"valid_descriptor_requested_bytes_sum\":%llu,\"export_reads\":%llu,"
      "\"control_parent_description_materialized_bytes\":%zu,"
      "\"borrowed_parent_materialized_bytes\":0,\"final_descriptor_charge\":0,"
      "\"external_backing_owner_storage_separate\":true}\n",
      (unsigned long long)queries, (unsigned long long)refusals, (unsigned long long)descriptor_calls,
      (unsigned long long)descriptor_bytes, (unsigned long long)exported_reads, sizeof(cd::Description));
}
