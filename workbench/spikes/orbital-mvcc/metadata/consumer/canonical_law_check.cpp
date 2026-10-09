#include "canonical_law.h"
#include "compact_operations.h"
#if defined(CONSUMER_METADATA_FROZEN) && CONSUMER_METADATA_FROZEN
#include "workbench/benchmarks/orbital/versioned/native.h"
#include "workbench/benchmarks/orbital/versioned-performance/read.h"
#else
#include "consumer/native.h"
#include "performance/read.h"
#endif
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
namespace canonical = orbital_mvcc::metadata_consumer::canonical_law;
namespace compact = orbital_mvcc::metadata_consumer::compact_law;
namespace v = orbital_bench::versioned;
namespace perf = orbital_versioned_performance;
namespace a = orbital::allocation;
using orbital_bench::Key;
using orbital_bench::Outcome;
using orbital_bench::Row;

struct Evidence {
  std::uint64_t fixtures{}, full_schema_calls{}, compact_law_calls{}, canonical_calls{};
  std::uint64_t point_calls{}, zero_step_calls{}, long_chain_calls{}, empty_part_calls{};
  std::uint64_t after_schema_calls{}, escaped_count_calls{}, after_allocation_release_calls{};
  std::uint64_t exact_extent_calls{}, rows{}, wire_bits{}, refusals{};
  std::uint64_t schemas{}, constructor_calls{}, allocations{}, payload_bytes{};
} evidence;
void check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "canonical Tuple law check: %s\n", message);
  std::abort();
}
template<class T, class E> T take(std::expected<T, E> result, const char* message) {
  if (!result) {
    std::fprintf(stderr, "canonical Tuple law check: %s (error %u)\n", message, unsigned(result.error()));
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
    evidence.allocations = a::statistics(*pool).allocations;
    bool closed = false;
    for (unsigned attempt = 0; attempt < 65536; ++attempt) {
      const auto progress = a::close(*pool);
      check(progress.kind() != orbital::Progress::Kind::failed, "Pool close failed");
      if (progress.kind() == orbital::Progress::Kind::done) { closed = true; break; }
      orbital::worker::poll(lane, 1024);
      std::this_thread::yield();
    }
    check(closed, "physical close did not finish after all backing protection ended");
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
std::vector<Row> input(std::size_t count, bool cyclic) {
  std::vector<Row> result(count);
  for (std::size_t row = 0; row < count; ++row) {
    auto& item = result[row];
    item.key = row == 0 ? 0 : row + 1 == count ? UINT64_MAX : 0x8000000000000100ULL + 19 * row;
    item.record.value = std::uint32_t(mix(row + 81)) & orbital_bench::value_mask;
    item.record.tag = std::uint32_t(mix(row + 123));
    if (row % 5 == 0) { item.record.value = 0; item.record.tag = 0; }
    if (row % 5 == 1) { item.record.value = orbital_bench::value_mask; item.record.tag = UINT32_MAX; }
  }
  for (std::size_t row = 0; row < count; ++row)
    result[row].record.next = cyclic ? result[(row * 11 + 7) % count].key :
        row + 1 < count ? result[row + 1].key : 1;
  return result;
}
std::uint64_t field(const Row& row, unsigned index) {
  switch (index) {
  case 0: return row.key;
  case 1: return row.record.next;
  case 2: return row.record.value;
  case 3: return row.record.tag;
  }
  check(false, "independent wire oracle escaped field law");
  return 0;
}
void wire(orbital::Bytes bytes, std::span<const Row> rows) {
  constexpr std::array<unsigned, 4> first{0, 64, 128, 152};
  constexpr std::array<unsigned, 4> widths{64, 64, 20, 32};
  check(bytes.size() == rows.size() * 23, "wire oracle extent");
  for (std::size_t row = 0; row < rows.size(); ++row) {
    for (unsigned index = 0; index < 4; ++index)
      for (unsigned bit = 0; bit < widths[index]; ++bit) {
        const auto physical = first[index] + bit;
        const auto byte = std::to_integer<unsigned>(bytes[row * 23 + physical / 8]);
        check(((byte >> (physical % 8)) & 1) == ((field(rows[row], index) >> bit) & 1),
              "wire differs from independent input record");
        ++evidence.wire_bits;
      }
    check(std::to_integer<unsigned>(bytes[row * 23 + 18]) >> 4 == row % 15 + 1,
          "spare nibble differs");
  }
}
// Independent expected operation uses logical linear lookup and the digest
// recurrence, never encoded reads, binary search or implementation consume_*.
Outcome oracle(std::span<const Row> rows, Key key, unsigned steps) {
  Outcome result{};
  const auto append = [&](std::uint64_t value) {
    result.digest ^= value + 0x9e3779b97f4a7c15ULL + (result.digest << 6) + (result.digest >> 2);
  };
  for (unsigned step = 0; step < steps; ++step) {
    const auto found = std::find_if(rows.begin(), rows.end(), [&](const Row& row) { return row.key == key; });
    append(key);
    if (found == rows.end()) { append(0); break; }
    append(found->record.next); append(found->record.value); append(found->record.tag);
    ++result.rows; key = found->record.next;
  }
  return result;
}
struct Request { Key key; unsigned steps; Outcome expected, full_schema; };
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
    for (const auto steps : std::array<unsigned, 7>{0, 1, 2, 15, 16, 17, 33})
      result.push_back({key, steps, oracle(rows, key, steps), {}});
  if (!cyclic) result.push_back({0, UINT32_MAX, oracle(rows, 0, UINT32_MAX), {}});
  return result;
}

struct Encoded {
  a::Allocation backing;
  std::array<std::byte, 75> descriptor;
  compact::ReadLaw comparison_law; // Explicitly retained only by this comparator fixture.
  canonical::Cursor cursor;
};
Encoded encode(a::Pool& pool, std::span<const Row> rows, std::span<Request> offered) {
  const v::Schema schema;
  ++evidence.schemas;
  auto law = take(compact::ReadLaw::from_schema(schema), "actual ReadLaw64 preparation");
  const auto shape = take(v::shape(schema, v::Format::rows, rows.size()), "existing Tuple shape");
  a::Allocation backing;
  if (shape.parts[0].body_bytes)
    backing = take(a::allocate(pool, {.bytes = shape.parts[0].body_bytes, .alignment = shape.parts[0].alignment,
        .readable_padding = shape.parts[0].readable_padding,
        .initialization = a::Initialization::uninitialized}), "actual Part allocation");
  const v::WritePart destination{shape.parts[0].id, a::bytes(backing)};
  v::KernelCosts costs;
  check(v::construct_into(schema, shape, {&destination, 1}, rows, costs).has_value(), "existing Tuple constructor");
  evidence.constructor_calls += costs.constructor_calls;
  evidence.payload_bytes += backing.size;
  for (std::size_t row = 0; row < rows.size(); ++row)
    backing.address[row * 23 + 18] |= std::byte(unsigned(row % 15 + 1) << 4);
  std::array<std::byte, 75> descriptor;
  std::copy(shape.parts[0].interpretation.begin(), shape.parts[0].interpretation.end(), descriptor.begin());
  const auto bytes = a::bytes(backing);
  wire(bytes, rows);
  const v::TupleControl control{schema, rows.size()};
  const v::ReadPart encoded{shape.parts[0].id, bytes, descriptor};
  const auto baseline = take(v::qualify_tuple(control, shape.interpretation, {&encoded, 1}), "frozen body qualification");
  for (auto& request : offered) {
    request.full_schema = perf::read_operation(baseline.open(control),
        {.kind = orbital_bench::Kind::chain, .key = request.key, .record = {}, .count = request.steps});
    check(request.full_schema == request.expected, "existing body Outcome differs from independent oracle");
    ++evidence.full_schema_calls;
  }
  const auto cursor = take(canonical::qualify_cursor({rows.size()}, bytes, descriptor), "stateless canonical admission");
  return {std::move(backing), descriptor, std::move(law), cursor};
} // Full Schema, construction records, view/endpoint and control end here.

void compare(Outcome actual, const Request& request, std::size_t count, bool point = false) {
  check(actual == request.expected && actual == request.full_schema, "canonical complete Outcome differs");
  ++evidence.canonical_calls; ++evidence.after_schema_calls;
  if (point) ++evidence.point_calls;
  else {
    if (!request.steps) ++evidence.zero_step_calls;
    if (request.steps > 16) ++evidence.long_chain_calls;
  }
  if (!count) ++evidence.empty_part_calls;
}
void complete_calls(canonical::Cursor cursor, canonical::Part part, std::size_t count,
                    std::span<const Request> offered) {
  const canonical::Control control{count};
  for (const auto& request : offered) {
    Outcome from_cursor{99, 99, 99}, from_part{77, 77, 77};
    canonical::read_chain8_into(control, cursor, request.key, request.steps, from_cursor);
    canonical::read_chain_into(part, request.key, request.steps, from_part);
    compare(from_cursor, request, count); compare(from_part, request, count);
    if (request.steps == 1) {
      Outcome point{55, 55, 55};
      canonical::read_point8_into(control, cursor, request.key, point);
      compare(point, request, count, true);
    }
  }
}
void refusals(const canonical::Control& control, orbital::Bytes body, orbital::Bytes descriptor) {
  const auto refuse = [&](canonical::Control required, orbital::Bytes bytes, orbital::Bytes description) {
    check(!canonical::qualify_part(required, bytes, description) &&
              !canonical::qualify_cursor(required, bytes, description), "malformed canonical Part admitted");
    evidence.refusals += 2;
  };
  std::array<std::byte, 76> malformed{};
  std::copy(descriptor.begin(), descriptor.end(), malformed.begin());
  refuse(control, body, std::span(malformed).first(74));
  refuse(control, body, malformed);
  for (unsigned i = 0; i < 75; ++i) {
    malformed[i] ^= std::byte{1};
    refuse(control, body, std::span(malformed).first(75));
    malformed[i] ^= std::byte{1};
  }
  refuse({control.records + 1}, body, descriptor);
  refuse({SIZE_MAX}, body, descriptor);
  const auto invalid_address = reinterpret_cast<const std::byte*>(UINTPTR_MAX - 1);
  refuse({1}, {invalid_address, 23}, descriptor);
  refuse(control, body, {invalid_address, 75});
  refuse(control, body, {static_cast<const std::byte*>(nullptr), 75});
  refuse({1}, {static_cast<const std::byte*>(nullptr), 23}, descriptor);
  if (!body.empty()) {
    refuse(control, body.first(body.size() - 1), descriptor);
    std::vector<std::byte> extra(body.begin(), body.end());
    extra.push_back(std::byte{0});
    refuse(control, extra, descriptor);
  }
}
void run(a::Pool& pool, std::size_t count, bool cyclic) {
  const auto rows = input(count, cyclic);
  auto offered = requests(rows, cyclic);
  auto source = encode(pool, rows, offered); // Every call below runs after full Schema destruction.
  const auto bytes = a::bytes(source.backing);
  const std::vector<std::byte> original(bytes.begin(), bytes.end());
  const canonical::Control control{count};
  const auto part = take(canonical::qualify_part(control, bytes, source.descriptor), "post-Schema canonical Part qualification");
  const compact::Control comparison_control{source.comparison_law, count};
  const auto comparison_part = take(compact::qualify_part(comparison_control, bytes, source.descriptor),
                                     "ReadLaw64 comparison Part qualification");
  const auto comparison_cursor = take(compact::qualify_cursor(comparison_control, bytes, source.descriptor),
                                       "ReadLaw64 comparison cursor qualification");
  const auto compare_transfer = [&]<unsigned Field>() {
    constexpr auto fixed = canonical::Law::transfer<Field>();
    const auto& prepared = source.comparison_law.transfers()[Field];
    check(fixed.mask == prepared.mask && fixed.offset == prepared.offset &&
              fixed.shift == prepared.shift && fixed.bytes == prepared.bytes,
          "stateless compile controls differ from actual Ikea preparation");
  };
  compare_transfer.template operator()<0>(); compare_transfer.template operator()<1>();
  compare_transfer.template operator()<2>(); compare_transfer.template operator()<3>();
  for (std::size_t row = 0; row < rows.size(); ++row) {
    const auto actual = take(part.row(row), "canonical row");
    check(actual == rows[row] && actual == take(comparison_part.row(source.comparison_law, row), "ReadLaw64 row"),
          "canonical row differs from independent record or ReadLaw64 body");
    ++evidence.rows;
  }
  check(!part.row(count) && !part.row(SIZE_MAX), "out-of-range canonical row admitted");
  evidence.refusals += 2;
  for (const auto& request : offered) {
    Outcome cursor_result{99, 99, 99}, part_result{77, 77, 77};
    compact::read_chain8_into(comparison_control, comparison_cursor, request.key, request.steps, cursor_result);
    compact::read_chain_into(source.comparison_law, comparison_part, request.key, request.steps, part_result);
    check(cursor_result == request.expected && part_result == request.expected,
          "existing compact-law complete Outcome differs from independent oracle");
    evidence.compact_law_calls += 2;
  }
  complete_calls(source.cursor, part, count, offered);
  refusals(control, bytes, source.descriptor);

  auto copied = source.cursor;
  auto moved = std::move(copied);
  const auto escaped = [&] {
    const canonical::Control local{count};
    const auto local_cursor = moved;
    return local_cursor.with(local);
  }();
  auto before = evidence.canonical_calls;
  complete_calls(moved, escaped, count, offered);
  evidence.escaped_count_calls += evidence.canonical_calls - before;

  a::Hold retained;
  if (count) {
    retained = take(a::hold(source.backing, {0, source.backing.size}), "retain exact encoded backing");
    source.backing = {};
    before = evidence.canonical_calls;
    complete_calls(moved, escaped, count, offered);
    evidence.after_allocation_release_calls += evidence.canonical_calls - before;
    // ASan sees these exact extents independently of allocator slack; offset1
    // also tests unaligned keys and the last short value/tag transfer.
    std::vector<std::byte> exact(bytes.begin(), bytes.end());
    std::vector<std::byte> shifted(bytes.size() + 1);
    std::copy(bytes.begin(), bytes.end(), shifted.begin() + 1);
    before = evidence.canonical_calls;
    for (const auto body : std::array<orbital::Bytes, 2>{exact, std::span(shifted).subspan(1)}) {
      const auto exact_cursor = take(canonical::qualify_cursor(control, body, source.descriptor), "exact cursor qualification");
      complete_calls(exact_cursor, exact_cursor.with(control), count, offered);
    }
    evidence.exact_extent_calls += evidence.canonical_calls - before;
  }
  check(std::equal(bytes.begin(), bytes.end(), original.begin(), original.end()), "canonical operations changed source");
  ++evidence.fixtures;
}
} // namespace

int main() {
  {
    Runtime runtime;
    for (const auto count : std::array<std::size_t, 6>{0, 1, 2, 17, 128, 129}) {
      run(*runtime.pool, count, false); run(*runtime.pool, count, true);
    }
  }
  std::cout << "{\"scope\":\"stateless canonical Tuple law; complete point/chain; no timings\","
               "\"existing_kernel\":\"tp::detail::read_gpr_transfer<1,Bytes>\","
               "\"retained_schema_or_transfer_array_in_operations\":false,\"record_array_scratch_bytes\":0,"
               "\"source_constants\":{\"canonical_descriptor_bytes\":75,\"fields\":4,"
               "\"transient_reconstructed_controls_type_bytes\":" << sizeof(ikea::tuplepack::detail::gpr_read)
            << ",\"code_and_rodata_costs\":\"must be charged from compiler output; not measured by this check\"},"
               "\"actual_type_bytes\":{\"empty_law_tag_if_materialized\":" << sizeof(canonical::Law)
            << ",\"count_control\":" << sizeof(canonical::Control)
            << ",\"cursor\":" << sizeof(canonical::Cursor) << ",\"part\":" << sizeof(canonical::Part)
            << ",\"result_cursor\":" << sizeof(orbital::Result<canonical::Cursor>)
            << ",\"result_part\":" << sizeof(orbital::Result<canonical::Part>)
            << ",\"comparison_owned_ReadLaw\":" << sizeof(compact::ReadLaw)
            << ",\"comparison_full_transient_Schema\":" << sizeof(v::Schema)
            << "},\"correctness\":{\"fixtures\":" << evidence.fixtures
            << ",\"full_Schema_complete_calls\":" << evidence.full_schema_calls
            << ",\"ReadLaw_complete_calls\":" << evidence.compact_law_calls
            << ",\"canonical_complete_calls\":" << evidence.canonical_calls
            << ",\"point_calls\":" << evidence.point_calls << ",\"zero_step_calls\":" << evidence.zero_step_calls
            << ",\"over16_step_calls\":" << evidence.long_chain_calls
            << ",\"empty_part_calls\":" << evidence.empty_part_calls
            << ",\"after_Schema_ended_calls\":" << evidence.after_schema_calls
            << ",\"escaped_count_control_calls\":" << evidence.escaped_count_calls
            << ",\"after_Allocation_release_with_Hold_calls\":" << evidence.after_allocation_release_calls
            << ",\"exact_unaligned_extent_calls\":" << evidence.exact_extent_calls
            << ",\"row_checks\":" << evidence.rows << ",\"wire_bits\":" << evidence.wire_bits
            << ",\"refusals\":" << evidence.refusals
            << "},\"actual_fixture_work\":{\"transient_Schemas\":" << evidence.schemas
            << ",\"constructor_calls\":" << evidence.constructor_calls
            << ",\"allocations\":" << evidence.allocations << ",\"payload_bytes\":" << evidence.payload_bytes
            << "},\"compile_controls_match_actual_preparation\":true,\"physical_close_zero_charge\":true}\n";
}
