#include "compact_descriptor_adapter.h"
#include "compact_operations.h"
#include "canonical_descriptor_adapter.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <memory_resource>
#include <vector>

namespace c = orbital_mvcc::metadata_consumer;
namespace law = c::compact_law;
namespace canonical = c::canonical_law;
namespace cd = compact_descriptor;
namespace v = orbital_bench::versioned;
using orbital_bench::Key;
using orbital_bench::Outcome;
using orbital_bench::Row;

namespace {
struct Memory : std::pmr::memory_resource {
  std::size_t live{}, peak{};
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live += bytes; peak = std::max(peak, live); return result;
  }
  void do_deallocate(void* value, std::size_t bytes, std::size_t alignment) override {
    assert(live >= bytes); live -= bytes;
    std::pmr::new_delete_resource()->deallocate(value, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};
struct Body {
  std::pmr::vector<std::byte> bytes;
  std::size_t& destroyed;
  Body(Memory& memory, std::size_t count, std::size_t& counter)
      : bytes(count * 23, &memory), destroyed(counter) {}
  ~Body() { ++destroyed; }
};
template<class T, class E> T take(std::expected<T, E> value) {
  assert(value); return std::move(*value);
}
std::uint64_t calls{}, exported_calls{}, refusals{}, captures{}, peak_metadata{}, peak_external{};

// Linear logical lookup and explicit workload digest; neither decoder nor
// consume helper participates in this expected result.
Outcome expected(std::span<const Row> rows, Key key, unsigned steps) {
  Outcome result{};
  const auto append = [&](std::uint64_t value) {
    result.digest ^= value + UINT64_C(0x9e3779b97f4a7c15) +
        (result.digest << 6) + (result.digest >> 2);
  };
  for (unsigned i = 0; i < steps; ++i) {
    append(key);
    const auto found = std::find_if(rows.begin(), rows.end(),
        [&](const auto& row) { return row.key == key; });
    if (found == rows.end()) { append(0); break; }
    append(found->record.next); append(found->record.value); append(found->record.tag);
    ++result.rows; key = found->record.next;
  }
  return result;
}
void compare(const law::Control& control, law::Cursor cursor,
             std::span<const Row> rows, bool exported) {
  for (Key key : {Key{0}, Key{1}, Key{2}, Key{0x8000000000000017}, UINT64_MAX}) {
    for (unsigned steps : {0u, 1u, 2u, 16u, 33u}) {
      Outcome output{99, 99, 99};
      law::read_chain8_into(control, cursor, key, steps, output);
      assert(output == expected(rows, key, steps)); ++calls;
      if (steps == 1) {
        law::read_point8_into(control, cursor, key, output);
        assert(output == expected(rows, key, 1)); ++calls;
      }
      if (exported) ++exported_calls;
    }
  }
}
void compare_canonical(const canonical::Control& control, canonical::Cursor cursor,
                       std::span<const Row> rows, bool exported) {
  for (Key key : {Key{0}, Key{1}, Key{2}, Key{0x8000000000000017}, UINT64_MAX}) {
    for (unsigned steps : {0u, 1u, 2u, 16u, 33u}) {
      Outcome output{99, 99, 99};
      canonical::read_chain8_into(control, cursor, key, steps, output);
      assert(output == expected(rows, key, steps)); ++calls;
      if (steps == 1) {
        canonical::read_point8_into(control, cursor, key, output);
        assert(output == expected(rows, key, 1)); ++calls;
      }
      if (exported) ++exported_calls;
    }
  }
}
void one(std::size_t count) {
  Memory metadata, external;
  std::size_t destroyed{};
  std::vector<Row> rows;
  for (std::size_t i = 0; i < count; ++i) {
    const Key key = !i ? 0 : i + 1 == count ? UINT64_MAX : UINT64_C(0x8000000000000000) + 23 * i;
    rows.push_back({key, {1, std::uint32_t((i * 17713 + 97) & orbital_bench::value_mask),
                        std::uint32_t(i * 27913 + 17)}});
  }
  for (std::size_t i = 0; i + 1 < count; ++i) rows[i].record.next = rows[i + 1].key;
  if (count > 2) rows[1].record.next = 1; // A real middle-chain miss.
  const orbital::ObjectId object{12, 93};
  constexpr std::uint64_t version = 7;
  const orbital::PartId requested{0x56455253494f4e44ULL, 0};
  law::ReadLaw read_law = law::ReadLaw::canonical();
  cd::Descriptor* descriptor{};
  {
    const v::Schema schema;
    read_law = take(law::ReadLaw::from_schema(schema));
    auto shape = take(v::shape(schema, v::Format::rows, count));
    auto body = std::allocate_shared<Body>(std::pmr::polymorphic_allocator<Body>(&external),
                                          external, count, destroyed);
    const v::WritePart target{requested, body->bytes};
    v::KernelCosts costs;
    assert(v::construct_into(schema, shape, {&target, 1}, rows, costs));
    for (std::size_t i = 0; i < count; ++i) body->bytes[i * 23 + 18] |= std::byte{0xf0};
    const auto logical = c::detail::count_bytes(count);
    cd::RepresentationSpec spec{object, version, {3, 1}, v::data_program,
                                logical, shape.interpretation, {}};
    cd::PartSource source{requested, body->bytes, shape.parts[0].interpretation, 0, 1, body};
    descriptor = take(cd::capture(metadata, spec, {&source, 1})); ++captures;
    const v::TupleControl full_control{schema, count};
    const auto check_bad = [&](const cd::RepresentationSpec& description,
                               std::span<const cd::PartSource> parts) {
      auto* bad = take(cd::capture(metadata, description, parts)); ++captures;
      const cd::Selection selection{bad};
      const law::Control compact_control{read_law, count};
      assert(!c::qualify_descriptor_tuple8(full_control, selection, object, version, requested));
      assert(!c::qualify_descriptor_borrowed_tuple8(full_control, selection, object, version, requested));
      assert(!law::qualify_descriptor_cursor(compact_control, selection, object, version, requested));
      assert(!canonical::qualify_descriptor_cursor({count}, selection, object, version, requested));
      refusals += 4; cd::release(bad);
    };
    check_bad(spec, {}); // Actual missing directory, parent still claims one Part.
    auto sibling = source; sibling.id.low = 1;
    const std::array parts{source, sibling};
    check_bad(spec, parts); // Actual extra Part, with independently valid body.
    auto wrong_id = source; wrong_id.id = {9, 9}; check_bad(spec, {&wrong_id, 1});
    auto bad_descriptor = shape.parts[0].interpretation;
    bad_descriptor[bad_descriptor.size() - 1] ^= std::byte{1};
    auto wrong_part = source; wrong_part.interpretation = bad_descriptor;
    check_bad(spec, {&wrong_part, 1});
    if (count) {
      wrong_part = source; wrong_part.body = source.body.first(source.body.size() - 1);
      check_bad(spec, {&wrong_part, 1});
    }
  } // Schema, Shape, ordinary input descriptions and local backing owner ended.
  assert(destroyed == 0);
  auto copied_law = read_law;
  const law::Control control{copied_law, count};
  const cd::Selection selected{descriptor};
  const auto cursor = take(law::qualify_descriptor_cursor(control, selected, object, version, requested));
  compare(control, cursor, rows, false);
  const canonical::Control canonical_control{count};
  const auto canonical_cursor = take(canonical::qualify_descriptor_cursor(
      canonical_control, selected, object, version, requested));
  compare_canonical(canonical_control, canonical_cursor, rows, false);
  assert(!law::qualify_descriptor_cursor(control, selected, {12, 94}, version, requested));
  assert(!law::qualify_descriptor_cursor(control, selected, object, version + 1, requested));
  assert(!law::qualify_descriptor_cursor(control, selected, object, version, {1, 2}));
  refusals += 3;
  assert(!canonical::qualify_descriptor_cursor(canonical_control, selected, {12, 94}, version, requested));
  assert(!canonical::qualify_descriptor_cursor(canonical_control, selected, object, version + 1, requested));
  assert(!canonical::qualify_descriptor_cursor(canonical_control, selected, object, version, {1, 2}));
  refusals += 3;
  auto* exported = take(cd::retain_part(selected, 0));
  cd::release(descriptor); // Only Export now owns the immutable slab and body.
  const auto view = cd::view(exported);
  const auto retained = take(law::qualify_cursor(control, view.body(), view.interpretation()));
  assert(destroyed == 0);
  compare(control, retained, rows, true);
  const auto canonical_retained = take(canonical::qualify_cursor(
      canonical_control, view.body(), view.interpretation()));
  compare_canonical(canonical_control, canonical_retained, rows, true);
  cd::release(exported);
  assert(destroyed == 1 && metadata.live == 0 && external.live == 0);
  peak_metadata = std::max(peak_metadata, std::uint64_t(metadata.peak));
  peak_external = std::max(peak_external, std::uint64_t(external.peak));
}
} // namespace
int main() {
  for (std::size_t count : {0u, 1u, 2u, 17u, 129u}) one(count);
  std::printf("{\"status\":\"pass\",\"scope\":\"full compact selected admission and exported complete reads; no timing\","
      "\"schema_and_source_metadata_ended_before_compact_calls\":true,\"cases\":5,"
      "\"complete_calls\":%llu,\"exported_chain_cases\":%llu,\"refusals\":%llu,\"captures\":%llu,"
      "\"read_law_bytes\":%zu,\"shared_control_bytes\":%zu,\"cursor_bytes\":%zu,"
      "\"fixture_live_read_law_copies\":2,\"canonical_retained_law_bytes\":0,"
      "\"canonical_shared_control_bytes\":%zu,\"canonical_materialized_tag_bytes\":%zu,"
      "\"actual_metadata_peak_with_refusal_controls\":%llu,\"external_owner_payload_peak\":%llu,"
      "\"final_resource_charge\":0}\n",
      (unsigned long long)calls, (unsigned long long)exported_calls, (unsigned long long)refusals,
      (unsigned long long)captures, sizeof(law::ReadLaw), sizeof(law::Control), sizeof(law::Cursor),
      sizeof(canonical::Control), sizeof(canonical::Law),
      (unsigned long long)peak_metadata, (unsigned long long)peak_external);
}
