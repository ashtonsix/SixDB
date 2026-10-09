#include "compact_law.h"
#include <orbital/allocation.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <type_traits>
#include <vector>

namespace {
namespace c = orbital_mvcc::metadata_consumer::compact_law;
namespace v = orbital_bench::versioned;
namespace tp = ikea::tuplepack;
namespace a = orbital::allocation;
using orbital_bench::Row;

struct Evidence {
  std::uint64_t cases{}, rows{}, fields{}, wire_bits{}, refusals{}, ended_schema_reads{};
  std::uint64_t preparation_schemas{}, reader_plans{}, writer_plans{}, constructors{};
  std::uint64_t allocation_calls{}, payload_bytes{}, descriptor_bytes{}, constructor_calls{};
  std::uint64_t binding_calls{}, encoded_values{}, issued_bytes{}, issued_spans{};
  std::size_t construction_scratch_bound{};
} evidence;

void check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "compact Tuple law check: %s\n", message);
  std::abort();
}
template<class T, class E> T take(std::expected<T, E> result, const char* message) {
  if (!result) {
    std::fprintf(stderr, "compact Tuple law check: %s (error %u)\n", message, unsigned(result.error()));
    std::abort();
  }
  return std::move(*result);
}

struct Runtime {
  orbital::worker::Lane lane;
  orbital::Ref<a::Arena> arena;
  a::Pool* pool{};
  Runtime() {
    check(orbital::worker::adopt(lane).has_value(), "Lane adoption");
    arena = take(a::open_arena(16 * 1024 * 1024), "Arena open");
    pool = take(a::add_pool(*arena, lane), "Pool open");
  }
  ~Runtime() {
    evidence.allocation_calls = a::statistics(*pool).allocations;
    bool closed = false;
    for (unsigned attempt = 0; attempt < 65536; ++attempt) {
      const auto progress = a::close(*pool);
      check(progress.kind() != orbital::Progress::Kind::failed, "Pool close failed");
      if (progress.kind() == orbital::Progress::Kind::done) { closed = true; break; }
      orbital::worker::poll(lane, 1024);
      std::this_thread::yield();
    }
    check(closed, "physical close did not finish after all Part backing ended");
    check(arena->charged_bytes.load() == 0 && a::statistics(*pool).metadata_bytes == 0,
          "physical close retained charge/metadata");
    check(a::remove_pool(*pool).has_value(), "closed Pool removal");
    arena.reset();
    orbital::worker::relinquish(lane);
  }
};

std::uint64_t mix(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}
// Independent logical input; arbitrary keys avoid accidentally introducing a
// sorted-key/container condition into this fixed Part read-law experiment.
std::vector<Row> input(std::size_t count) {
  std::vector<Row> result(count);
  for (std::size_t row = 0; row < count; ++row) {
    switch (row % 7) {
    case 0: result[row] = {0, {0, 0, 0}}; break;
    case 1: result[row] = {UINT64_MAX, {UINT64_MAX, orbital_bench::value_mask, UINT32_MAX}}; break;
    case 2: result[row] = {std::uint64_t{1} << (row % 64),
        {std::uint64_t{1} << ((row + 17) % 64), std::uint32_t{1} << (row % 20),
         std::uint32_t{1} << (row % 32)}}; break;
    case 3: result[row] = {0xaaaaaaaaaaaaaaaaULL, {0x5555555555555555ULL, 0xaaaaa, 0x55555555}}; break;
    default: result[row] = {mix(row + 19), {mix(row + 0x100001),
        std::uint32_t(mix(row + 81)) & orbital_bench::value_mask, std::uint32_t(mix(row + 123))}};
    }
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
  check(false, "oracle field escaped fixed schema");
  return 0;
}
void equal(Row actual, const Row& expected) {
  for (unsigned index = 0; index < 4; ++index) {
    check(field(actual, index) == field(expected, index), "field differs from independent record");
    ++evidence.fields;
  }
  ++evidence.rows;
}
void equal_laws(const c::ReadLaw& extracted, const c::ReadLaw& derived) {
  for (unsigned field = 0; field < 4; ++field) {
    const auto& left = extracted.transfers()[field];
    const auto& right = derived.transfers()[field];
    check(left.mask == right.mask && left.offset == right.offset &&
              left.shift == right.shift && left.bytes == right.bytes,
          "compile-law derivation differs from actual Ikea prepared transfer");
  }
}

struct Encoded {
  a::Allocation backing;
  std::array<std::byte, 75> descriptor;
  c::ReadLaw law;
};
// The full preparation Schema, Shape/metadata vectors, construction operation
// and WritePart array all end before the returned compact law is used. The
// actual allocation and exact serialized Part descriptor are retained openly.
Encoded encode(a::Pool& pool, std::span<const Row> rows) {
  const v::Schema schema;
  ++evidence.preparation_schemas;
  evidence.reader_plans += schema.readers.size();
  evidence.writer_plans += schema.writers.size();
  ++evidence.constructors;
  auto law = take(c::ReadLaw::from_schema(schema), "canonical Schema read-law extraction");
  auto shape = take(v::shape(schema, v::Format::rows, rows.size()), "frozen Tuple shape");
  check(shape.part_count == 1 && shape.parts[0].interpretation.size() == 75,
        "canonical fixture Part metadata differs");
  a::Allocation backing;
  if (shape.parts[0].body_bytes)
    backing = take(a::allocate(pool, {.bytes = shape.parts[0].body_bytes,
        .alignment = shape.parts[0].alignment,
        .readable_padding = shape.parts[0].readable_padding,
        .initialization = a::Initialization::uninitialized}), "actual Part allocation");
  const std::array<v::WritePart, 1> writes{{{shape.parts[0].id, a::bytes(backing)}}};
  v::KernelCosts costs;
  check(v::construct_into(schema, shape, writes, rows, costs).has_value(), "existing Tuple constructor");
  evidence.constructor_calls += costs.constructor_calls;
  evidence.binding_calls += costs.bindings;
  evidence.encoded_values += costs.encoded_values;
  evidence.issued_bytes += costs.issued_bytes;
  evidence.issued_spans += costs.issued_spans;
  evidence.construction_scratch_bound = std::max(evidence.construction_scratch_bound, costs.scratch_bound_bytes);
  std::array<std::byte, 75> descriptor;
  std::copy(shape.parts[0].interpretation.begin(), shape.parts[0].interpretation.end(), descriptor.begin());
  for (std::size_t row = 0; row < rows.size(); ++row)
    backing.address[row * 23 + 18] |= std::byte(unsigned(row % 15 + 1) << 4);
  evidence.payload_bytes += backing.size;
  evidence.descriptor_bytes += descriptor.size();
  return {std::move(backing), descriptor, std::move(law)};
}

void wire(orbital::Bytes bytes, std::span<const Row> rows) {
  // Bitwise oracle is independent of both the existing encoder and transfer
  // reader. The high spare nibble is deliberately nonzero and must be ignored.
  constexpr std::array<unsigned, 4> first{0, 64, 128, 152};
  constexpr std::array<unsigned, 4> widths{64, 64, 20, 32};
  check(bytes.size() == rows.size() * 23, "wire oracle exact extent");
  for (std::size_t row = 0; row < rows.size(); ++row) {
    for (unsigned field_index = 0; field_index < 4; ++field_index)
      for (unsigned bit = 0; bit < widths[field_index]; ++bit) {
        const auto physical = first[field_index] + bit;
        const auto byte = std::to_integer<unsigned>(bytes[row * 23 + physical / 8]);
        check(((byte >> (physical % 8)) & 1) == ((field(rows[row], field_index) >> bit) & 1),
              "encoded Tuple bits differ from independent record");
        ++evidence.wire_bits;
      }
    check(std::to_integer<unsigned>(bytes[row * 23 + 18]) >> 4 == row % 15 + 1,
          "spare nibble differs");
  }
}

void reads(const c::Part& part, const c::ReadLaw& law, std::span<const Row> rows) {
  check(part.size() == rows.size(), "qualified Part count differs");
  for (std::size_t row = 0; row < rows.size(); ++row) {
    equal(take(part.row(law, row), "bounded compact row"), rows[row]);
    ++evidence.ended_schema_reads;
  }
  check(!part.row(law, rows.size()) && !part.row(law, SIZE_MAX), "out-of-range row admitted");
  evidence.refusals += 2;
  std::array<std::uint64_t, 17> values;
  constexpr std::uint64_t sentinel = 0xfeedfacebeefdeadULL;
  for (unsigned field_index = 0; field_index < 4; ++field_index) {
    for (std::size_t first = 0; first < rows.size(); first += values.size()) {
      const auto count = std::min(values.size(), rows.size() - first);
      values.fill(sentinel);
      check(part.field(law, v::Field(field_index), first, std::span(values).first(count)).has_value(),
            "bounded compact field");
      for (std::size_t i = 0; i < count; ++i) {
        check(values[i] == field(rows[first + i], field_index), "field range differs from independent record");
        ++evidence.fields; ++evidence.ended_schema_reads;
      }
      for (std::size_t i = count; i < values.size(); ++i)
        check(values[i] == sentinel, "field range overwrote surplus output");
    }
    check(part.field(law, v::Field(field_index), rows.size(), {}).has_value(), "empty end range refused");
    values.fill(sentinel);
    check(!part.field(law, v::Field(field_index), rows.size(), std::span(values).first(1)) &&
              !part.field(law, v::Field(field_index), SIZE_MAX, {}), "out-of-range field admitted");
    check(std::ranges::all_of(values, [&](auto value) { return value == sentinel; }),
          "refused field changed output");
    evidence.refusals += 2;
  }
  check(!part.field(law, v::Field(4), 0, {}) && !part.field(law, v::Field(UINT32_MAX), 0, {}),
        "invalid field admitted");
  evidence.refusals += 2;
}

void refusals(const c::Control& control, orbital::Bytes body, orbital::Bytes descriptor) {
  const auto refuse = [&](const c::Control& required, orbital::Bytes bytes, orbital::Bytes interpretation) {
    check(!c::qualify_part(required, bytes, interpretation) &&
              !c::qualify_cursor(required, bytes, interpretation), "malformed direct Part admitted");
    evidence.refusals += 2;
  };
  std::array<std::byte, 76> malformed{};
  std::copy(descriptor.begin(), descriptor.end(), malformed.begin());
  refuse(control, body, std::span(malformed).first(74));
  refuse(control, body, malformed);
  for (unsigned byte = 0; byte < 75; ++byte) {
    malformed[byte] ^= std::byte{1};
    refuse(control, body, std::span(malformed).first(75));
    malformed[byte] ^= std::byte{1};
  }
  refuse({control.law, control.records + 1}, body, descriptor);
  refuse({control.law, SIZE_MAX}, body, descriptor);
  // Synthetic spans are rejected before dereference, solely exercising address
  // and multiplication guards. They do not assert authority over fake storage.
  const auto invalid_address = reinterpret_cast<const std::byte*>(UINTPTR_MAX - 1);
  refuse({control.law, 1}, {invalid_address, 23}, descriptor);
  refuse(control, body, {invalid_address, 75});
  refuse(control, body, {static_cast<const std::byte*>(nullptr), 75});
  refuse({control.law, 1}, {static_cast<const std::byte*>(nullptr), 23}, descriptor);
  if (!body.empty()) {
    refuse(control, body.first(body.size() - 1), descriptor);
    std::vector<std::byte> extra(body.begin(), body.end());
    extra.push_back(std::byte{0});
    refuse(control, extra, descriptor);
  }
}

void run(a::Pool& pool, std::size_t count) {
  const auto expected = input(count);
  auto source = encode(pool, expected); // Full Schema and construction records ended here.
  const auto body = a::bytes(source.backing);
  const std::vector<std::byte> original(body.begin(), body.end());
  wire(body, expected);
  constexpr auto derived = c::ReadLaw::canonical();
  equal_laws(source.law, derived);
  for (const auto* law : std::array<const c::ReadLaw*, 2>{&source.law, &derived}) {
    const c::Control control{*law, count};
    const auto part = take(c::qualify_part(control, body, source.descriptor), "direct compact Part qualification");
    auto cursor = take(c::qualify_cursor(control, body, source.descriptor), "direct pointer-only qualification");
    auto copied = cursor;
    auto moved = std::move(copied);
    reads(part, *law, expected);
    // Local control/cursor addresses do not escape with Part value.
    const auto opened = [&] {
      const c::Control local{*law, count};
      auto local_cursor = moved;
      return local_cursor.with(local);
    }();
    reads(opened, *law, expected);
    refusals(control, body, source.descriptor);
  }
  check(std::equal(body.begin(), body.end(), original.begin(), original.end()), "read-law changed encoded source");

  // Exact vector extent exercises the last three/four-byte field reads under
  // ASan independently of allocator page slack. Deliberate offset1 exercises
  // unaligned key/next loads without a fabricated stricter Part alignment.
  if (count) {
    std::vector<std::byte> exact(body.begin(), body.end());
    std::vector<std::byte> shifted(body.size() + 1);
    std::copy(body.begin(), body.end(), shifted.begin() + 1);
    const c::Control control{derived, count};
    reads(take(c::qualify_part(control, exact, source.descriptor), "exact extent qualification"), derived, expected);
    reads(take(c::qualify_part(control, std::span(shifted).subspan(1), source.descriptor),
               "unaligned exact extent qualification"), derived, expected);
  }
  ++evidence.cases;
}

template<class T> void size(const char* name, bool comma = true) {
  std::cout << '"' << name << "\":{\"bytes\":" << sizeof(T) << ",\"alignment\":" << alignof(T)
            << ",\"trivially_copyable\":" << (std::is_trivially_copyable_v<T> ? "true" : "false") << '}';
  if (comma) std::cout << ',';
}
} // namespace

int main() {
  {
    Runtime runtime;
    for (const auto count : std::array<std::size_t, 6>{0, 1, 2, 17, 128, 129}) run(*runtime.pool, count);
  } // Actual physical close/poll/remove and zero charge precede the report.
  std::cout << "{\"scope\":\"fixed canonical Tuple Part read law; no timings\","
               "\"internal_authoring_seam\":\"tp::detail::read_gpr_transfer<1,Bytes>\","
               "\"schema_can_end_before_reads\":true,\"owned_read_law\":{";
  size<c::ReadLaw>("ReadLaw"); size<tp::detail::word_transfer>("WordTransfer", false);
  std::cout << "},\"complete_retained_law_and_placement_bytes\":{\"law_plus_cursor_plus_shared_control\":"
            << sizeof(c::ReadLaw) + sizeof(c::Cursor) + sizeof(c::Control)
            << ",\"law_plus_part_explicit_call_argument\":" << sizeof(c::ReadLaw) + sizeof(c::Part)
            << "},\"placement\":{";
  size<c::Control>("SharedControl"); size<c::Cursor>("Cursor"); size<c::Part>("Part");
  size<orbital::Result<c::Cursor>>("ResultCursor"); size<orbital::Result<c::Part>>("ResultPart", false);
  std::cout << "},\"transient_type_extents_not_optimized_frame_costs\":{";
  size<v::Schema>("Schema"); size<tp::layout>("Layout"); size<tp::constructor>("Constructor");
  size<tp::reader<8>>("Reader8"); size<tp::writer<8>>("Writer8");
  size<tp::detail::gpr_read>("PerReadGprControls"); size<tp::const_view>("ExtractionView");
  size<tp::read_operation<8, const tp::byte>>("ExtractionReadOperation");
  size<orbital::Result<c::ReadLaw>>("ExtractionResult");
  size<std::array<tp::byte, 75>>("ExtractionDescriptor", false);
  std::cout << "},\"preparation\":{\"extraction_schema_instances\":" << evidence.preparation_schemas
            << ",\"reader_plan_instances\":" << evidence.reader_plans
            << ",\"writer_plan_instances\":" << evidence.writer_plans
            << ",\"constructor_instances\":" << evidence.constructors
            << ",\"compile_law_schema_instances\":0,\"compile_law_reader_plan_instances\":0,"
               "\"compile_law_writer_plan_instances\":0,\"compile_law_constructor_instances\":0},"
               "\"actual_encoder_work\":{\"constructor_calls\":" << evidence.constructor_calls
            << ",\"bindings\":" << evidence.binding_calls << ",\"encoded_values\":" << evidence.encoded_values
            << ",\"issued_bytes\":" << evidence.issued_bytes << ",\"issued_spans\":" << evidence.issued_spans
            << ",\"scratch_bound_bytes\":" << evidence.construction_scratch_bound
            << "},\"fixture_storage\":{\"allocation_calls\":" << evidence.allocation_calls
            << ",\"payload_bytes\":" << evidence.payload_bytes
            << ",\"serialized_descriptor_bytes\":" << evidence.descriptor_bytes
            << "},\"correctness\":{\"cases\":" << evidence.cases << ",\"rows\":" << evidence.rows
            << ",\"fields\":" << evidence.fields << ",\"wire_bits\":" << evidence.wire_bits
            << ",\"ended_schema_reads\":" << evidence.ended_schema_reads
            << ",\"refusals\":" << evidence.refusals
            << "},\"physical_close_zero_charge\":true}\n";
}
