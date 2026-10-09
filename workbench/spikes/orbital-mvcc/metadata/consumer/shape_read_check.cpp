#include "shape_read.h"
#include "../orbital/candidates/shape_policy.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace {
namespace sr = orbital_mvcc::metadata_consumer::shape_read;
namespace cs = compact_shape;
namespace cd = compact_descriptor;
namespace ch = compact_history;
namespace v = orbital_bench::versioned;
using Store = ch::ShapeStore16;
using orbital_bench::Key;
using orbital_bench::Outcome;
using orbital_bench::Record;
using orbital_bench::Row;
constexpr orbital::ProgramId program{19, 3};
constexpr orbital::ObjectId A{17, 1};
constexpr std::array<unsigned, 4> widths{64, 64, 20, 32};
using SelectedRead = orbital::Result<void> (*)(sr::TupleProof, cs::Selection, Key, unsigned, Outcome&) noexcept;
using MappedRead = orbital::Result<void> (*)(sr::TupleProof, std::uint64_t, orbital::Bytes, Key, unsigned, Outcome&) noexcept;
constexpr std::array<SelectedRead, 2> selected_reads{sr::read_checked, sr::read_proven};
constexpr std::array<MappedRead, 2> mapped_reads{sr::read_mapped_checked, sr::read_mapped_proven};

struct Evidence {
  std::uint64_t captures{}, publications{}, shared_shape_captures{}, shape_factories{};
  std::uint64_t schema_instances{}, selected_calls{}, mapped_calls{}, unaligned_mapped_calls{};
  std::uint64_t empty_calls{}, zero_step_calls{}, long_chain_calls{}, cyclic_calls{};
  std::uint64_t refused_calls{}, refused_shapes{}, logical_fields{}, wire_bits{};
  std::uint64_t old_scope_checks{}, conversions{}, adopted_alternative_calls{};
  std::size_t body_live{}, body_peak{}, body_owners_live{}, body_allocation_calls{};
  std::size_t charged_peak{}, charged_allocation_calls{};
  unsigned live_schemas{};
} evidence;

void check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "shared Shape read check: %s\n", message);
  std::abort();
}
template<class T, class E> T take(std::expected<T, E> result, const char* message) {
  if (!result) {
    std::fprintf(stderr, "shared Shape read check: %s (error %u)\n", message, unsigned(result.error()));
    std::abort();
  }
  return std::move(*result);
}
void refusal(orbital::Result<void> result, orbital::Error error, Outcome actual, Outcome before,
             const char* message) {
  check(!result && result.error() == error, message);
  check(actual == before, "refused read changed caller Outcome");
  ++evidence.refused_calls;
}

struct Preparation {
  const v::Schema schema;
  Preparation() { ++evidence.live_schemas; ++evidence.schema_instances; }
  ~Preparation() { --evidence.live_schemas; }
};
struct ShapeOwner {
  cs::Shape* pointer{};
  ShapeOwner() = default;
  explicit ShapeOwner(cs::Shape* value) : pointer(value) {}
  ShapeOwner(const ShapeOwner&) = delete;
  ShapeOwner(ShapeOwner&& other) noexcept : pointer(std::exchange(other.pointer, nullptr)) {}
  ShapeOwner& operator=(ShapeOwner other) noexcept { std::swap(pointer, other.pointer); return *this; }
  ~ShapeOwner() { if (pointer) cs::release(pointer); }
};

// Both payload and allocate_shared's real owner/control block are charged to
// the actual history Memory resource. This is requested storage, not RSS.
struct Body {
  ch::Memory& memory;
  std::byte* address{};
  const std::size_t size, alignment;
  Body(ch::Memory& memory, std::size_t bytes, std::size_t alignment)
      : memory(memory), size(bytes), alignment(std::max(std::size_t{16}, alignment)) {
    if (size) {
      address = static_cast<std::byte*>(memory.allocate(size, this->alignment));
      evidence.body_live += size;
      evidence.body_peak = std::max(evidence.body_peak, evidence.body_live);
      ++evidence.body_allocation_calls;
    }
    ++evidence.body_owners_live;
  }
  ~Body() {
    if (size) { memory.deallocate(address, size, alignment); evidence.body_live -= size; }
    --evidence.body_owners_live;
  }
  orbital::MutableBytes writable() const { return {address, size}; }
  orbital::Bytes bytes() const { return {address, size}; }
};

struct PrivateParts {
  v::Shape physical;
  std::array<std::shared_ptr<Body>, 4> owners;
  std::array<v::WritePart, 4> writes{};
  std::array<cd::PartSource, 4> descriptions{};
  std::array<cs::BodySource, 4> bodies{};
  PrivateParts(ch::Memory& memory, const v::Schema& schema, v::Format format, std::size_t count)
      : physical(take(v::shape(schema, format, count), "existing fixture shape")) {
    for (unsigned part = 0; part < physical.part_count; ++part) {
      const auto& required = physical.parts[part];
      owners[part] = std::allocate_shared<Body>(std::pmr::polymorphic_allocator<Body>{&memory},
          memory, required.body_bytes, required.alignment);
      writes[part] = {required.id, owners[part]->writable()};
      descriptions[part] = {required.id, owners[part]->bytes(), required.interpretation,
          required.readable_padding, required.alignment, owners[part]};
      bodies[part] = {required.id, owners[part]->bytes(), owners[part], nullptr};
    }
  }
  void spare() {
    if (physical.format != v::Format::rows) return;
    for (std::size_t row = 0; row < physical.records; ++row)
      owners[0]->address[row * 23 + 18] |= std::byte(unsigned(row % 15 + 1) << 4);
  }
};

std::uint64_t mix(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}
std::vector<Row> input(std::size_t count, bool cyclic, unsigned revision = 0) {
  std::vector<Row> result(count);
  for (std::size_t row = 0; row < count; ++row) {
    result[row].key = row == 0 ? 0 : row + 1 == count ? UINT64_MAX : 0x8000000000000100ULL + 19 * row;
    result[row].record.value = std::uint32_t(mix(row + 81 + revision * 101)) & orbital_bench::value_mask;
    result[row].record.tag = std::uint32_t(mix(row + 123 + revision * 103));
    if (!revision && row % 5 == 0) { result[row].record.value = 0; result[row].record.tag = 0; }
    if (!revision && row % 5 == 1) {
      result[row].record.value = orbital_bench::value_mask; result[row].record.tag = UINT32_MAX;
    }
  }
  for (std::size_t row = 0; row < count; ++row)
    result[row].record.next = cyclic ? result[(row * 11 + 7) % count].key :
        row + 1 < count ? result[row + 1].key : 1; // Sparse Key1 is absent.
  return result;
}

ShapeOwner make_tuple_shape(ch::Memory& memory) {
  Preparation preparation;
  const auto rows = input(1, false);
  PrivateParts parts(memory, preparation.schema, v::Format::rows, rows.size());
  v::KernelCosts costs;
  check(v::construct_into(preparation.schema, parts.physical, std::span(parts.writes).first(1),
                          rows, costs).has_value(), "Shape seed construction");
  auto* shape = take(cs::prove_fixture(memory, preparation.schema, program, parts.physical.interpretation,
      std::span(parts.descriptions).first(1)), "actual canonical Tuple Shape proof");
  ++evidence.shape_factories;
  return ShapeOwner{shape}; // Seed body, Schema and all source descriptors end here.
}

Store::Owner encode(Store& store, ch::Memory& memory, std::span<const Row> rows,
                    v::Format format, cs::Shape* shared_shape = nullptr) {
  Preparation preparation;
  PrivateParts parts(memory, preparation.schema, format, rows.size());
  v::KernelCosts costs;
  check(v::construct_into(preparation.schema, parts.physical,
                          std::span(parts.writes).first(parts.physical.part_count), rows, costs).has_value(),
        "real fixture construction");
  parts.spare();
  ShapeOwner fresh;
  if (!shared_shape) {
    fresh = ShapeOwner{take(cs::prove_fixture(memory, preparation.schema, program, parts.physical.interpretation,
        std::span(parts.descriptions).first(parts.physical.part_count)), "actual captured Shape proof")};
    shared_shape = fresh.pointer;
    ++evidence.shape_factories;
  } else {
    check(format == v::Format::rows && cs::part_count(shared_shape) == 1,
          "shared Shape fixture is not homogeneous Tuple");
    const auto descriptor = cs::tuple_interpretation(shared_shape);
    check(std::equal(descriptor.begin(), descriptor.end(), parts.physical.parts[0].interpretation.begin(),
                     parts.physical.parts[0].interpretation.end()), "constructed law differs from shared Shape law");
    ++evidence.shared_shape_captures;
  }
  auto owner = take(store.capture({shared_shape, rows.size()},
      std::span(parts.bodies).first(parts.physical.part_count)), "actual ShapeStore capture");
  ++evidence.captures;
  return owner; // Captured Body owners and stored Shape law outlive every preparation object.
}

void publish(Store& store, orbital::ObjectId object, const Store::Owner& owner) {
  Store::Scope basis(store);
  const std::array<Store::Change, 1> changes{{{object, owner}}};
  Store::Prepared prepared(store);
  check(store.prepare(basis, changes, prepared).has_value(), "actual history prepare");
  const auto calls = store.statistics().allocation_calls;
  check(store.install(prepared).has_value(), "actual history install");
  check(store.statistics().allocation_calls == calls, "history install allocated");
  ++evidence.publications;
}

orbital::Bytes body(cs::Selection selected, unsigned part) {
  const auto view = take(selected.part(std::size_t{part}), "protected selected Part");
  const auto extent = take(cs::geometry(selected.shape(), part).extent(selected.records()), "actual captured extent");
  check(extent <= SIZE_MAX, "fixture extent exceeds native address space");
  return {view.address(), static_cast<std::size_t>(extent)};
}
using Image = std::array<std::vector<std::byte>, 4>;
Image image(cs::Selection selected) {
  Image result;
  for (unsigned part = 0; part < selected.part_count(); ++part) {
    const auto bytes = body(selected, part);
    result[part].assign(bytes.begin(), bytes.end());
  }
  return result;
}
std::uint64_t field(const Row& row, unsigned index) {
  switch (index) {
  case 0: return row.key;
  case 1: return row.record.next;
  case 2: return row.record.value;
  case 3: return row.record.tag;
  }
  check(false, "independent field index escaped schema");
  return 0;
}
std::uint64_t decode_bits(cs::Selection selected, unsigned index, std::size_t row) {
  const bool tuple = selected.part_count() == 1;
  const auto bytes = body(selected, tuple ? 0 : index);
  constexpr std::array<unsigned, 4> first{0, 64, 128, 152};
  std::uint64_t value = 0;
  for (unsigned bit = 0; bit < widths[index]; ++bit) {
    std::size_t offset; unsigned shift;
    if (tuple) {
      const auto physical = first[index] + bit;
      offset = row * 23 + physical / 8; shift = physical % 8;
    } else {
      const auto whole = widths[index] / 8, residual = widths[index] % 8;
      offset = row / 8 * widths[index] + (bit < residual ? 8 * whole + bit :
          row % 8 * whole + (bit - residual) / 8);
      shift = bit < residual ? unsigned(row % 8) : (bit - residual) % 8;
    }
    check(offset < bytes.size(), "independent decoder exceeded actual body bounds");
    value |= std::uint64_t((std::to_integer<unsigned>(bytes[offset]) >> shift) & 1) << bit;
    ++evidence.wire_bits;
  }
  return value;
}
std::vector<Row> decode(cs::Selection selected, std::span<const Row> expected) {
  check(evidence.live_schemas == 0, "independent read ran while Schema was live");
  check(selected.records() == expected.size(), "captured count differs from logical input");
  std::vector<Row> actual(expected.size());
  for (std::size_t row = 0; row < expected.size(); ++row) {
    actual[row] = {decode_bits(selected, 0, row),
        {decode_bits(selected, 1, row), std::uint32_t(decode_bits(selected, 2, row)),
         std::uint32_t(decode_bits(selected, 3, row))}};
    for (unsigned index = 0; index < 4; ++index) {
      check(field(actual[row], index) == field(expected[row], index), "independent wire/logical decoder differs");
      ++evidence.logical_fields;
    }
    if (selected.part_count() == 1)
      check(std::to_integer<unsigned>(body(selected, 0)[row * 23 + 18]) >> 4 == row % 15 + 1,
            "nonzero Tuple spare nibble differs");
  }
  return actual;
}

// Linear logical lookup and explicit digest recurrence are independent of
// encoded search, fixed-transfer controls and consume_row/consume_absent.
Outcome oracle(std::span<const Row> rows, Key key, unsigned steps) {
  Outcome result{};
  const auto append = [&](std::uint64_t value) {
    result.digest ^= value + 0x9e3779b97f4a7c15ULL + (result.digest << 6) + (result.digest >> 2);
  };
  for (unsigned step = 0; step < steps; ++step) {
    const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.key == key; });
    append(key);
    if (found == rows.end()) { append(0); break; }
    append(found->record.next); append(found->record.value); append(found->record.tag);
    ++result.rows;
    key = found->record.next;
  }
  return result;
}
struct Request { Key key; unsigned steps; };
std::vector<Request> requests(std::span<const Row> rows, bool cyclic) {
  std::vector<Key> keys{0, 1, UINT64_MAX};
  for (const auto& row : rows) {
    keys.push_back(row.key);
    if (row.key) keys.push_back(row.key - 1);
    if (row.key != UINT64_MAX) keys.push_back(row.key + 1);
  }
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  std::vector<Request> result;
  for (const auto key : keys)
    for (const auto steps : {0u, 1u, 16u, 33u, 97u}) result.push_back({key, steps});
  // A finite chain terminates at absent Key1; no enormous cyclic request.
  if (!cyclic) result.push_back({0, UINT32_MAX});
  return result;
}
void exercise(sr::TupleProof proof, cs::Selection selected, std::span<const Row> rows,
              bool cyclic, bool alternative = false) {
  check(evidence.live_schemas == 0, "complete read retained a construction Schema");
  check(selected && selected.shape() == proof.shape() && selected.records() == rows.size(),
        "protected Selection does not match admitted Shape/count");
  const auto before = image(selected);
  const auto bytes = body(selected, 0);
  for (const auto request : requests(rows, cyclic)) {
    const auto expected = oracle(rows, request.key, request.steps);
    for (const auto read : selected_reads) {
      Outcome actual{99, 101, 103};
      check(read(proof, selected, request.key, request.steps, actual).has_value(), "valid protected selected read");
      check(actual == expected, "selected complete Outcome differs from independent oracle");
      ++evidence.selected_calls;
      evidence.adopted_alternative_calls += alternative;
      evidence.empty_calls += rows.empty(); evidence.zero_step_calls += !request.steps;
      evidence.long_chain_calls += request.steps > 16; evidence.cyclic_calls += cyclic;
    }
    for (const auto read : mapped_reads) {
      Outcome actual{107, 109, 113};
      check(read(proof, selected.records(), bytes, request.key, request.steps, actual).has_value(),
            "valid protected mapping read");
      check(actual == expected, "mapped complete Outcome differs from independent oracle");
      ++evidence.mapped_calls;
    }
  }
  check(image(selected) == before, "complete reads changed selected immutable bodies");
}

void selected_refusals(sr::TupleProof proof, cs::Selection wrong, orbital::Error error) {
  check(evidence.live_schemas == 0, "selected refusal retained Schema");
  for (const auto read : selected_reads) {
    Outcome output{0x123456789abcdef0ULL, 41, 43};
    const auto before = output;
    const auto result = read(proof, wrong, 0, 16, output);
    refusal(result, error, output, before, "invalid protected Selection admitted or returned wrong error");
  }
}
void mapped_boundaries(sr::TupleProof proof, cs::Selection selected, std::span<const Row> rows) {
  const auto bytes = body(selected, 0);
  const auto refuse = [&](std::uint64_t count, orbital::Bytes bad) {
    for (const auto read : mapped_reads) {
      Outcome output{0xfedcba9876543210ULL, 47, 53};
      const auto before = output;
      const auto result = read(proof, count, bad, 0, 1, output);
      refusal(result, orbital::Error::invalid_argument, output, before, "invalid mapping admitted or returned wrong error");
    }
  };
  if (!bytes.empty()) refuse(selected.records(), bytes.first(bytes.size() - 1));
  std::vector<std::byte> extra(bytes.begin(), bytes.end());
  extra.push_back(std::byte{0x5a});
  refuse(selected.records(), extra);
  refuse(selected.records() + 1, bytes);
  refuse(1, {static_cast<const std::byte*>(nullptr), 23});
  refuse(SIZE_MAX / 23 + std::uint64_t{1}, {});
  // Arithmetic guard only: this synthetic range must fail before dereference
  // and grants no backing/transaction authority over the fabricated address.
  refuse(1, {reinterpret_cast<const std::byte*>(UINTPTR_MAX - 11), 23});
  if (!rows.empty()) {
    std::vector<std::byte> shifted(bytes.size() + 1);
    std::copy(bytes.begin(), bytes.end(), shifted.begin() + 1);
    const orbital::Bytes mapping{shifted.data() + 1, bytes.size()};
    const auto key = rows.back().key;
    for (const auto read : mapped_reads) {
      Outcome output{59, 61, 67};
      check(read(proof, rows.size(), mapping, key, 1, output).has_value() && output == oracle(rows, key, 1),
            "exact unaligned mapping or last row read differs");
      ++evidence.unaligned_mapped_calls;
    }
  }
}

void shared_shape_history(ch::Memory& memory, cs::Shape* shared_shape, sr::TupleProof proof) {
  Store store(memory);
  for (const std::size_t count : {0u, 1u, 7u, 8u, 9u, 64u, 65u, 129u}) {
    const auto old_rows = input(count, false);
    auto old_owner = encode(store, memory, old_rows, v::Format::rows, shared_shape);
    publish(store, A, old_owner); old_owner = {};
    {
      Store::Scope old(store);
      const auto bound = take(store.bound(old, A), "actual old-cut binding");
      const auto selected = bound.selection();
      check(bound.binding().object == A && bound.binding().version == old.cut(), "catalog-issued binding differs");
      const auto old_image = image(selected);
      decode(selected, old_rows);
      exercise(proof, selected, old_rows, false);
      mapped_boundaries(proof, selected, old_rows);
      selected_refusals(proof, {}, orbital::Error::absent);
      const auto newer_rows = input(count, true, 1);
      auto newer_owner = encode(store, memory, newer_rows, v::Format::rows, shared_shape);
      publish(store, A, newer_owner); newer_owner = {};
      store.drain();
      Store::Scope current(store);
      const auto newer = take(store.bound(current, A), "actual current-cut binding");
      check(newer.binding().version == current.cut() && newer.binding().object == A &&
                newer.selection().realization != selected.realization, "publication did not issue a new logical cut");
      check(newer.selection().shape() == shared_shape && selected.shape() == shared_shape,
            "capture did not reuse the one admitted Shape");
      decode(newer.selection(), newer_rows);
      exercise(proof, newer.selection(), newer_rows, true);
      check(old.bind(A).realization == selected.realization && image(selected) == old_image,
            "old Scope lost its immutable body after publication/drain");
      exercise(proof, old.bind(A), old_rows, false);
      ++evidence.old_scope_checks;
    }
    store.drain();
  }
}

Store::Owner convert_columns(Store& store, ch::Memory& memory, cs::Selection source) {
  Preparation preparation;
  check(source && source.part_count() == 4 && cs::program(source.shape()) == program,
        "conversion source is not the actual published column fixture");
  check(source.records() <= SIZE_MAX, "conversion source count exceeds native size");
  const auto count = static_cast<std::size_t>(source.records());
  auto source_description = take(v::shape(preparation.schema, v::Format::columns, count), "source codec descriptors");
  std::array<v::ReadPart, 4> source_parts{};
  for (unsigned part = 0; part < 4; ++part) {
    const auto& geometry = cs::geometry(source.shape(), part);
    const auto& declared = source_description.parts[part];
    check(geometry.id == declared.id && geometry.kind == 1 && geometry.tile_rows == 8 &&
              geometry.tile_bytes == widths[part] && geometry.alignment_log2 == 6 && !geometry.readable_padding,
          "actual source Shape does not match the regenerated existing codec descriptor");
    source_parts[part] = {geometry.id, body(source, part), declared.interpretation};
  }
  const auto data = take(v::ReadData::bind(preparation.schema, source_description, source_parts),
                         "actual selected column codec binding");
  PrivateParts target(memory, preparation.schema, v::Format::rows, count);
  v::KernelCosts costs;
  check(v::convert_into(preparation.schema, data, target.physical, std::span(target.writes).first(1), costs).has_value(),
        "actual columns-to-Tuple conversion");
  target.spare();
  ShapeOwner shape{take(cs::prove_fixture(memory, preparation.schema, program, target.physical.interpretation,
      std::span(target.descriptions).first(1)), "converted actual Tuple Shape proof")};
  auto owner = take(store.capture({shape.pointer, count}, std::span(target.bodies).first(1)),
                    "actual converted alternative capture");
  ++evidence.shape_factories; ++evidence.captures; ++evidence.conversions;
  return owner; // Schema, source binding and every generated descriptor end before reads/adoption.
}

void conversion_history(ch::Memory& memory, sr::TupleProof other_tuple_proof) {
  Store store(memory);
  const auto rows = input(129, false);
  auto columns = encode(store, memory, rows, v::Format::columns);
  publish(store, A, columns); columns = {};
  Store::Scope old(store);
  const auto source = take(store.bound(old, A), "actual conversion source binding");
  const auto original = source.selection();
  const auto old_image = image(original);
  const auto decoded_source = decode(original, rows);
  auto wrong_format = sr::admit_tuple_shape(original.shape(), program, cs::geometry(original.shape(), 0).id);
  check(!wrong_format && wrong_format.error() == orbital::Error::invalid_argument, "column Shape admitted as Tuple");
  ++evidence.refused_shapes;
  auto target = take(store.conversion(old, A), "actual conversion authority");
  auto replacement = convert_columns(store, memory, target.source());
  check(evidence.live_schemas == 0, "conversion Schema escaped into adoption/read scope");
  const auto proof = take(sr::admit_tuple_shape(replacement.selection().shape(), program,
      cs::geometry(replacement.selection().shape(), 0).id), "actual converted Tuple admission");
  check(proof.shape() != other_tuple_proof.shape(), "independently proven Shape unexpectedly has shared identity");
  check(store.adopt(target, replacement).has_value(), "actual converted alternative adoption");
  replacement = {};
  const auto alternative = take(old.bind_alternative(A), "actual old-cut alternative capture");
  check(alternative.part_count() == 1 && original.part_count() == 4, "conversion did not select distinct physical formats");
  check(decode(alternative, rows) == decoded_source, "converted logical fields differ from independently decoded old source");
  exercise(proof, alternative, rows, false, true);
  mapped_boundaries(proof, alternative, rows);
  selected_refusals(other_tuple_proof, alternative, orbital::Error::invalid_argument); // Same law, distinct Shape.
  selected_refusals(proof, original, orbital::Error::invalid_argument);
  const auto updated_rows = input(rows.size(), true, 7);
  auto updated = encode(store, memory, updated_rows, v::Format::columns);
  publish(store, A, updated); updated = {};
  store.drain();
  Store::Scope current(store);
  const auto newer = take(store.bound(current, A), "current binding after conversion-source publication");
  decode(newer.selection(), updated_rows);
  check(newer.binding().object == A && newer.binding().version == current.cut() &&
            source.binding().version == old.cut(), "catalog-issued conversion/publication labels changed");
  check(old.bind(A).realization == original.realization && image(original) == old_image,
        "old column source ceased to be visible after conversion/publication");
  check(take(old.bind_alternative(A), "old alternative rediscovery").realization == alternative.realization,
        "old Scope lost the adopted alternative after publication");
  exercise(proof, alternative, rows, false, true);
  check(image(original) == old_image, "alternative reads changed old column bytes");
  ++evidence.old_scope_checks;
}

void shape_refusals(const cs::Shape* shape, orbital::PartId part) {
  for (const auto [candidate, expected_program, expected_part] :
       std::array<std::tuple<const cs::Shape*, orbital::ProgramId, orbital::PartId>, 3>{{
           {nullptr, program, part}, {shape, {}, part}, {shape, program, {}}}}) {
    const auto result = sr::admit_tuple_shape(candidate, expected_program, expected_part);
    check(!result && result.error() == orbital::Error::invalid_argument, "wrong/null Shape identity facts admitted");
    ++evidence.refused_shapes;
  }
}
} // namespace

int main() {
  ch::Memory memory;
  {
    auto shared_shape = make_tuple_shape(memory);
    check(evidence.live_schemas == 0, "seed Schema retained after Shape extraction");
    const auto part = cs::geometry(shared_shape.pointer, 0).id;
    const auto proof = take(sr::admit_tuple_shape(shared_shape.pointer, program, part), "one shared Tuple admission");
    shape_refusals(shared_shape.pointer, part);
    shared_shape_history(memory, shared_shape.pointer, proof);
    conversion_history(memory, proof);
  }
  evidence.charged_peak = memory.peak;
  evidence.charged_allocation_calls = memory.calls;
  check(!memory.live && !evidence.body_live && !evidence.body_owners_live && !evidence.live_schemas,
        "cleanup retained charged metadata/body/owner or transient Schema");
  std::cout << "{\"check\":\"actual_shared_shape_complete_reads\",\"captures\":" << evidence.captures
      << ",\"publications\":" << evidence.publications << ",\"shared_shape_captures\":" << evidence.shared_shape_captures
      << ",\"shape_factories\":" << evidence.shape_factories << ",\"transient_schemas\":" << evidence.schema_instances
      << ",\"selected_calls\":" << evidence.selected_calls << ",\"mapped_calls\":" << evidence.mapped_calls
      << ",\"unaligned_mapped_calls\":" << evidence.unaligned_mapped_calls
      << ",\"empty_calls\":" << evidence.empty_calls << ",\"zero_step_calls\":" << evidence.zero_step_calls
      << ",\"long_chain_calls\":" << evidence.long_chain_calls << ",\"cyclic_calls\":" << evidence.cyclic_calls
      << ",\"refused_calls\":" << evidence.refused_calls << ",\"refused_shapes\":" << evidence.refused_shapes
      << ",\"logical_fields\":" << evidence.logical_fields << ",\"wire_bits\":" << evidence.wire_bits
      << ",\"old_scope_checks\":" << evidence.old_scope_checks << ",\"real_conversions\":" << evidence.conversions
      << ",\"adopted_alternative_calls\":" << evidence.adopted_alternative_calls
      << ",\"body_allocation_calls\":" << evidence.body_allocation_calls << ",\"body_peak_bytes\":" << evidence.body_peak
      << ",\"charged_allocation_calls\":" << evidence.charged_allocation_calls
      << ",\"charged_peak_bytes\":" << evidence.charged_peak
      << ",\"tuple_proof_bytes\":" << sizeof(sr::TupleProof)
      << ",\"schema_retained_for_reads\":false,\"charged_metadata_and_body_zero_after_cleanup\":true"
         ",\"timing_or_lmdb_measurement\":false}\n";
}
