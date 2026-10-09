#include "compact_operations.h"
#include "operations.h"
#if defined(CONSUMER_METADATA_FROZEN) && CONSUMER_METADATA_FROZEN
#include "workbench/benchmarks/orbital/versioned-performance/read.h"
#else
#include "performance/read.h"
#endif
#include <orbital/allocation.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

namespace {
namespace compact = orbital_mvcc::metadata_consumer::compact_law;
namespace baseline = orbital_mvcc::metadata_consumer;
namespace v = orbital_bench::versioned;
namespace perf = orbital_versioned_performance;
namespace a = orbital::allocation;
using orbital_bench::Key;
using orbital_bench::Outcome;
using orbital_bench::Row;

struct Evidence {
  std::uint64_t fixtures{}, baseline_complete_calls{}, compact_complete_calls{};
  std::uint64_t point_calls{}, chain_calls{}, zero_step_calls{}, long_chain_calls{};
  std::uint64_t empty_part_calls{}, ended_schema_calls{}, escaped_frame_calls{}, retained_backing_calls{};
  std::uint64_t wire_bits{}, refusals{}, schema_instances{}, constructor_calls{}, allocation_calls{};
  std::uint64_t payload_bytes{}, issued_bytes{}, issued_spans{};
} evidence;

void check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "compact Tuple complete operation check: %s\n", message);
  std::abort();
}
template<class T, class E> T take(std::expected<T, E> result, const char* message) {
  if (!result) {
    std::fprintf(stderr, "compact Tuple complete operation check: %s (error %u)\n",
                 message, unsigned(result.error()));
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
    check(closed, "physical close did not finish after backing protection ended");
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
        row + 1 < count ? result[row + 1].key : 1; // Key1 is absent in this sparse fixture.
  return result;
}
std::uint64_t field(const Row& row, unsigned index) {
  switch (index) {
  case 0: return row.key;
  case 1: return row.record.next;
  case 2: return row.record.value;
  case 3: return row.record.tag;
  }
  check(false, "wire oracle field escaped fixed schema");
  return 0;
}
void wire(orbital::Bytes bytes, std::span<const Row> rows) {
  constexpr std::array<unsigned, 4> first{0, 64, 128, 152};
  constexpr std::array<unsigned, 4> widths{64, 64, 20, 32};
  check(bytes.size() == rows.size() * 23, "independent wire oracle extent");
  for (std::size_t row = 0; row < rows.size(); ++row) {
    for (unsigned index = 0; index < 4; ++index)
      for (unsigned bit = 0; bit < widths[index]; ++bit) {
        const auto physical = first[index] + bit;
        const auto byte = std::to_integer<unsigned>(bytes[row * 23 + physical / 8]);
        check(((byte >> (physical % 8)) & 1) == ((field(rows[row], index) >> bit) & 1),
              "encoded bytes differ from independently generated record");
        ++evidence.wire_bits;
      }
    check(std::to_integer<unsigned>(bytes[row * 23 + 18]) >> 4 == row % 15 + 1,
          "nonzero spare nibble differs");
  }
}

// Independent expected operation: linear logical lookup and explicit digest
// recurrence. No encoded reads, binary-search helper or consume_* call is used.
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
    ++result.rows;
    key = found->record.next;
  }
  return result;
}
struct Request {
  Key key{};
  unsigned steps{};
  Outcome expected{}, baseline{};
};
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
  // UINT_MAX tests step-counter admission/termination without requesting an
  // unbounded cyclic walk. Finite input reaches absent Key1 within count+1.
  if (!cyclic) result.push_back({0, UINT32_MAX, oracle(rows, 0, UINT32_MAX), {}});
  return result;
}

struct Encoded {
  a::Allocation backing;
  std::array<std::byte, 75> descriptor;
  compact::ReadLaw law;
  compact::Cursor cursor;
};
Encoded encode(a::Pool& pool, std::span<const Row> rows, std::span<Request> offered) {
  const v::Schema schema; // Actual complete read/write construction Schema is transient here.
  ++evidence.schema_instances;
  auto law = take(compact::ReadLaw::from_schema(schema), "prepared compact read-law extraction");
  const auto shape = take(v::shape(schema, v::Format::rows, rows.size()), "real Tuple shape");
  a::Allocation backing;
  if (shape.parts[0].body_bytes)
    backing = take(a::allocate(pool, {.bytes = shape.parts[0].body_bytes,
        .alignment = shape.parts[0].alignment, .readable_padding = shape.parts[0].readable_padding,
        .initialization = a::Initialization::uninitialized}), "actual Part allocation");
  const v::WritePart destination{shape.parts[0].id, a::bytes(backing)};
  v::KernelCosts costs;
  check(v::construct_into(schema, shape, {&destination, 1}, rows, costs).has_value(), "existing Tuple construction");
  evidence.constructor_calls += costs.constructor_calls;
  evidence.issued_bytes += costs.issued_bytes; evidence.issued_spans += costs.issued_spans;
  evidence.payload_bytes += backing.size;
  for (std::size_t row = 0; row < rows.size(); ++row)
    backing.address[row * 23 + 18] |= std::byte(unsigned(row % 15 + 1) << 4);
  std::array<std::byte, 75> descriptor;
  std::copy(shape.parts[0].interpretation.begin(), shape.parts[0].interpretation.end(), descriptor.begin());
  const auto bytes = a::bytes(backing);
  wire(bytes, rows);

  // The full-Schema comparator deliberately retains Schema during its calls.
  // Capture only caller Outcomes for later comparisons; no baseline Schema,
  // read endpoint, descriptor pointer or cursor leaves this function.
  const v::TupleControl control{schema, rows.size()};
  const auto full_cursor = take(baseline::qualify_tuple_part8(control, bytes, descriptor), "full-Schema Part qualification");
  const v::ReadPart encoded{shape.parts[0].id, bytes, descriptor};
  const auto frozen_cursor = take(v::qualify_tuple(control, shape.interpretation, {&encoded, 1}),
                                  "frozen consumer qualification");
  for (auto& request : offered) {
    request.baseline = {99, 99, 99};
    baseline::read_chain8_into(control, full_cursor, request.key, request.steps, request.baseline);
    check(request.baseline == request.expected, "complete full-Schema call differs from independent Outcome");
    const auto native = perf::read_operation(frozen_cursor.open(control),
        {.kind = orbital_bench::Kind::chain, .key = request.key, .record = {}, .count = request.steps});
    check(native == request.expected, "frozen complete operation differs from independent Outcome");
    evidence.baseline_complete_calls += 2;
    if (request.steps == 1) {
      Outcome point{99, 99, 99};
      baseline::read_point8_into(control, full_cursor, request.key, point);
      check(point == request.expected, "complete full-Schema point call differs");
      ++evidence.baseline_complete_calls;
    }
  }
  const compact::Control compact_control{law, rows.size()};
  auto cursor = take(compact::qualify_cursor(compact_control, bytes, descriptor), "compact cursor qualification");
  // Returned law owns its transfer values. Cursor borrows only backing, so both
  // survive the local law/control move and the full Schema/Shape scope ending.
  return {std::move(backing), descriptor, std::move(law), cursor};
}

void compare(Outcome actual, const Request& request, std::size_t count, bool point = false) {
  check(actual == request.expected && actual == request.baseline, "compact complete Outcome differs");
  ++evidence.compact_complete_calls; ++evidence.ended_schema_calls;
  if (point) ++evidence.point_calls;
  else {
    ++evidence.chain_calls;
    if (!request.steps) ++evidence.zero_step_calls;
    if (request.steps > 16) ++evidence.long_chain_calls;
  }
  if (!count) ++evidence.empty_part_calls;
}
void complete_calls(const compact::ReadLaw& law, compact::Cursor cursor, compact::Part part,
                    std::size_t count, std::span<const Request> offered) {
  const compact::Control control{law, count};
  for (const auto& request : offered) {
    Outcome from_cursor{99, 99, 99}, from_part{77, 77, 77};
    compact::read_chain8_into(control, cursor, request.key, request.steps, from_cursor);
    compact::read_chain_into(law, part, request.key, request.steps, from_part);
    compare(from_cursor, request, count); compare(from_part, request, count);
    if (request.steps == 1) {
      Outcome point{55, 55, 55};
      compact::read_point8_into(control, cursor, request.key, point);
      compare(point, request, count, true);
    }
  }
}
void run(a::Pool& pool, std::size_t count, bool cyclic) {
  const auto rows = input(count, cyclic);
  auto offered = requests(rows, cyclic);
  auto source = encode(pool, rows, offered); // Schema and full baseline read state ended here.
  const auto bytes = a::bytes(source.backing);
  const std::vector<std::byte> original(bytes.begin(), bytes.end());
  constexpr auto derived = compact::ReadLaw::canonical();
  for (unsigned field = 0; field < 4; ++field) {
    const auto& actual = source.law.transfers()[field];
    const auto& expected = derived.transfers()[field];
    check(actual.mask == expected.mask && actual.offset == expected.offset &&
              actual.shift == expected.shift && actual.bytes == expected.bytes,
          "canonical compiled law differs from existing prepared transfer");
  }
  for (const auto* law : std::array<const compact::ReadLaw*, 2>{&source.law, &derived}) {
    const compact::Control control{*law, count};
    const auto part = take(compact::qualify_part(control, bytes, source.descriptor), "post-Schema compact Part qualification");
    complete_calls(*law, source.cursor, part, count, offered);
    check(!compact::qualify_cursor({*law, count + 1}, bytes, source.descriptor), "wrong count admitted");
    ++evidence.refusals;
  }

  // Value copies have no borrowed preparation/control/cursor self address.
  auto copied_law = source.law;
  auto moved_law = std::move(copied_law);
  auto copied_cursor = source.cursor;
  auto moved_cursor = std::move(copied_cursor);
  const auto escaped_part = [&] {
    const compact::Control local{moved_law, count};
    const auto local_cursor = moved_cursor;
    return local_cursor.with(local);
  }();
  const auto before_escape = evidence.compact_complete_calls;
  complete_calls(moved_law, moved_cursor, escaped_part, count, offered);
  evidence.escaped_frame_calls += evidence.compact_complete_calls - before_escape;

  a::Hold retained;
  if (count) {
    retained = take(a::hold(source.backing, {0, source.backing.size}), "retain exact Part backing");
    source.backing = {}; // Only the actual Hold now protects the admitted pointer.
    const auto before_retained = evidence.compact_complete_calls;
    complete_calls(moved_law, moved_cursor, escaped_part, count, offered);
    evidence.retained_backing_calls += evidence.compact_complete_calls - before_retained;
  }
  check(std::equal(bytes.begin(), bytes.end(), original.begin(), original.end()), "complete reads changed encoded source");
  ++evidence.fixtures;
} // Local Hold/backing ends before Runtime's real physical close.
} // namespace

int main() {
  {
    Runtime runtime;
    for (const auto count : std::array<std::size_t, 6>{0, 1, 2, 17, 128, 129}) {
      run(*runtime.pool, count, false);
      run(*runtime.pool, count, true);
    }
  }
  std::cout << "{\"scope\":\"fixed canonical Tuple complete point/chain operations; no timings\","
               "\"internal_authoring_seam\":\"existing compact_law Ikea fixed-transfer body\","
               "\"retained_schema_in_compact_operations\":false,\"record_array_scratch_bytes\":0,"
               "\"metadata_type_bytes\":{\"full_preparation_schema\":" << sizeof(v::Schema)
            << ",\"owned_compact_law\":" << sizeof(compact::ReadLaw)
            << ",\"shared_compact_control\":" << sizeof(compact::Control)
            << ",\"cursor\":" << sizeof(compact::Cursor) << ",\"part\":" << sizeof(compact::Part)
            << ",\"caller_outcome\":" << sizeof(Outcome)
            << "},\"correctness\":{\"fixtures\":" << evidence.fixtures
            << ",\"baseline_complete_calls\":" << evidence.baseline_complete_calls
            << ",\"compact_complete_calls\":" << evidence.compact_complete_calls
            << ",\"point_calls\":" << evidence.point_calls << ",\"chain_calls\":" << evidence.chain_calls
            << ",\"zero_step_calls\":" << evidence.zero_step_calls
            << ",\"over16_step_calls\":" << evidence.long_chain_calls
            << ",\"empty_part_calls\":" << evidence.empty_part_calls
            << ",\"after_schema_ended_calls\":" << evidence.ended_schema_calls
            << ",\"escaped_local_control_calls\":" << evidence.escaped_frame_calls
            << ",\"after_allocation_release_with_hold_calls\":" << evidence.retained_backing_calls
            << ",\"wire_bits\":" << evidence.wire_bits << ",\"count_refusals\":" << evidence.refusals
            << "},\"actual_fixture_work\":{\"transient_schema_instances\":" << evidence.schema_instances
            << ",\"constructor_calls\":" << evidence.constructor_calls
            << ",\"issued_bytes\":" << evidence.issued_bytes << ",\"issued_spans\":" << evidence.issued_spans
            << ",\"allocation_calls\":" << evidence.allocation_calls << ",\"payload_bytes\":" << evidence.payload_bytes
            << "},\"canonical_derivation_matches_preparation\":true,\"physical_close_zero_charge\":true}\n";
}
