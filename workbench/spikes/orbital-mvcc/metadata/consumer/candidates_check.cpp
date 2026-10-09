#include "candidates.h"
#include <orbital/allocation.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {
namespace c = orbital_mvcc::metadata_consumer;
namespace v = orbital_bench::versioned;
namespace a = orbital::allocation;
using orbital_bench::Key;
using orbital_bench::Record;
using orbital_bench::Row;
constexpr std::array<unsigned, 4> widths{64, 64, 20, 32};
struct Evidence {
  std::uint64_t cases{}, fields{}, wire_bits{}, lookups{}, scans{}, chains{}, refusals{};
  std::uint64_t payload_bytes{}, serialized_metadata_bytes{}, allocations{};
  std::uint64_t status_out_refusals{}, selected_wrappers{}, part_only_wrappers{};
} evidence;

void check(bool valid, const char* message) {
  if (valid) return;
  std::fprintf(stderr, "metadata consumer candidates: %s\n", message);
  std::abort();
}
template<class T, class E> T take(std::expected<T, E> result, const char* message) {
  if (!result) {
    std::fprintf(stderr, "metadata consumer candidates: %s (error %u)\n", message, unsigned(result.error()));
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
    arena = take(a::open_arena(64 * 1024 * 1024), "Arena open");
    pool = take(a::add_pool(*arena, lane), "Pool open");
  }
  ~Runtime() {
    evidence.allocations = a::statistics(*pool).allocations;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
      const auto progress = a::close(*pool);
      check(progress.kind() != orbital::Progress::Kind::failed, "Pool close failed");
      if (progress.kind() == orbital::Progress::Kind::done) break;
      check(std::chrono::steady_clock::now() < deadline, "live physical use at close deadline");
      orbital::worker::poll(lane, 1024);
      std::this_thread::yield();
    }
    check(arena->charged_bytes.load() == 0 && a::statistics(*pool).metadata_bytes == 0,
          "physical close retained charge/metadata");
    check(a::remove_pool(*pool).has_value(), "closed Pool removal");
    arena.reset();
    orbital::worker::relinquish(lane);
  }
};
struct Parts {
  v::Shape shape;
  std::array<a::Allocation, 4> allocation;
  Parts(a::Pool& pool, v::Format format, std::span<const Row> input)
      : shape(take(v::shape(c::canonical_tuple_schema, format, input.size()), "canonical shape")) {
    std::array<v::WritePart, 4> writes;
    evidence.serialized_metadata_bytes += shape.interpretation.size();
    for (unsigned index = 0; index < shape.part_count; ++index) {
      const auto& part = shape.parts[index];
      if (part.body_bytes)
        allocation[index] = take(a::allocate(pool,
            {.bytes = part.body_bytes, .alignment = part.alignment,
             .initialization = a::Initialization::uninitialized}), "actual Part allocation");
      writes[index] = {part.id, a::bytes(allocation[index])};
      evidence.payload_bytes += part.body_bytes;
      evidence.serialized_metadata_bytes += part.interpretation.size();
    }
    v::KernelCosts costs;
    check(v::construct_into(c::canonical_tuple_schema, shape, std::span(writes).first(shape.part_count),
                           input, costs).has_value(), "frozen encoder construction");
    if (format == v::Format::rows)
      for (std::size_t row = 0; row < input.size(); ++row)
        allocation[0].address[row * 23 + 18] |= std::byte(unsigned(row % 15 + 1) << 4);
  }
  std::array<v::ReadPart, 4> reads() const {
    std::array<v::ReadPart, 4> result;
    for (unsigned index = 0; index < shape.part_count; ++index)
      result[index] = {shape.parts[index].id, a::bytes(allocation[index]), shape.parts[index].interpretation};
    return result;
  }
};
struct Image {
  std::array<std::vector<std::byte>, 4> bytes;
  explicit Image(const Parts& source) {
    for (unsigned index = 0; index < source.shape.part_count; ++index) {
      const auto data = a::bytes(source.allocation[index]);
      bytes[index].assign(data.begin(), data.end());
    }
  }
  void unchanged(const Parts& source) const {
    for (unsigned index = 0; index < source.shape.part_count; ++index) {
      const auto data = a::bytes(source.allocation[index]);
      check(std::equal(data.begin(), data.end(), bytes[index].begin(), bytes[index].end()),
            "native candidate changed encoded source");
    }
  }
};
std::uint64_t mix(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}
std::vector<Row> input(std::size_t count) {
  std::vector<Row> result(count);
  for (std::size_t row = 0; row < count; ++row) {
    result[row] = {0x8000000000000100ULL + 19 * row,
        {0, std::uint32_t(mix(row + 49)) & orbital_bench::value_mask, std::uint32_t(mix(row + 89))}};
    if (row % 7 == 0) result[row].record.value = 0;
    if (row % 7 == 1) result[row].record.value = orbital_bench::value_mask;
    if (row % 7 == 2) result[row].record.tag = UINT32_MAX;
  }
  for (std::size_t row = 0; row < count; ++row)
    result[row].record.next = result[(row * 11 + 7) % count].key;
  return result;
}
std::uint64_t field(const Row& row, unsigned index) {
  switch (index) {
  case 0: return row.key;
  case 1: return row.record.next;
  case 2: return row.record.value;
  case 3: return row.record.tag;
  default: check(false, "oracle field escaped fixed schema"); return 0;
  }
}
void equal(Row actual, Row expected) {
  for (unsigned index = 0; index < 4; ++index) {
    check(field(actual, index) == field(expected, index), "logical field differs from independent record");
    ++evidence.fields;
  }
}
unsigned byte(orbital::Bytes bytes, std::size_t offset) {
  check(offset < bytes.size(), "wire oracle escaped exact physical extent");
  return std::to_integer<unsigned>(bytes[offset]);
}
void wire(const Parts& source, std::span<const Row> rows) {
  if (source.shape.format == v::Format::rows) {
    const auto bytes = a::bytes(source.allocation[0]);
    constexpr std::array<unsigned, 4> first{0, 64, 128, 152};
    for (std::size_t row = 0; row < rows.size(); ++row) {
      for (unsigned index = 0; index < 4; ++index)
        for (unsigned bit = 0; bit < widths[index]; ++bit) {
          const auto physical = first[index] + bit;
          check(((byte(bytes, row * 23 + physical / 8) >> (physical % 8)) & 1) ==
                    ((field(rows[row], index) >> bit) & 1), "Tuple wire differs from per-bit oracle");
          ++evidence.wire_bits;
        }
      check(byte(bytes, row * 23 + 18) >> 4 == row % 15 + 1, "initialized spare nibble changed");
    }
  } else {
    for (unsigned index = 0; index < 4; ++index) {
      const auto bytes = a::bytes(source.allocation[index]);
      const auto width = widths[index], whole = width / 8, residual = width % 8;
      for (std::size_t row = 0; row < (rows.size() + 7) / 8 * 8; ++row) {
        const auto value = row < rows.size() ? field(rows[row], index) : 0;
        for (unsigned bit = 0; bit < width; ++bit) {
          const auto offset = row / 8 * width + (bit < residual ? 8 * whole + bit :
              row % 8 * whole + (bit - residual) / 8);
          const auto position = bit < residual ? row % 8 : (bit - residual) % 8;
          check(((byte(bytes, offset) >> position) & 1) == ((value >> bit) & 1),
                "Series wire/tail slack differs from per-bit oracle");
          ++evidence.wire_bits;
        }
      }
    }
  }
}
std::size_t seek(std::span<const Row> rows, Key key) {
  std::size_t index = 0;
  while (index < rows.size() && rows[index].key < key) ++index;
  return index;
}
template<class Read> void rows_and_fields(std::span<const Row> rows, Read&& read) {
  for (std::size_t row = 0; row < rows.size(); ++row) equal(take(read(row), "in-range candidate row"), rows[row]);
  check(!read(rows.size()) && !read(SIZE_MAX), "candidate admitted out-of-range row");
  evidence.refusals += 2;
}
template<class Get, class Lower, class Scan>
void lookups(std::span<const Row> rows, Get&& get, Lower&& lower, Scan&& scan) {
  std::vector<Key> queries{0, UINT64_MAX};
  for (const auto& row : rows) { queries.push_back(row.key); queries.push_back(row.key + 1); }
  const Row sentinel{0x123456789abcdef0ULL, {0xfedcba9876543210ULL, 0xdead, 0xbeef}};
  for (const auto key : queries) {
    const auto first = seek(rows, key);
    const auto actual = get(key);
    const bool hit = first < rows.size() && rows[first].key == key;
    check(lower(key) == first && actual.has_value() == hit, "unsigned get/seek differs from independent oracle");
    if (hit) check(*actual == rows[first].record, "candidate get record differs");
    ++evidence.lookups;
    std::array<Row, 16> output;
    output.fill(sentinel);
    const auto returned = scan(key, std::span<Row>{output});
    const auto wanted = std::min(output.size(), rows.size() - first);
    check(returned == wanted, "candidate scan count differs");
    for (std::size_t row = 0; row < wanted; ++row) equal(output[row], rows[first + row]);
    for (std::size_t row = wanted; row < output.size(); ++row)
      check(output[row] == sentinel, "candidate scan overwrote surplus output");
    ++evidence.scans;
  }
  check(scan(0, std::span<Row>{}) == 0, "empty scan returned rows");
}
template<class Field> void fields(std::span<const Row> rows, Field&& read) {
  for (unsigned index = 0; index < 4; ++index) {
    std::vector<std::uint64_t> output(rows.size(), UINT64_MAX);
    check(read(v::Field(index), 0, std::span<std::uint64_t>{output}).has_value(), "candidate field range failed");
    for (std::size_t row = 0; row < rows.size(); ++row) {
      check(output[row] == field(rows[row], index), "candidate field differs from independent oracle");
      ++evidence.fields;
    }
  }
  for (const auto first : {rows.size(), SIZE_MAX}) {
    std::array<std::uint64_t, 2> output{0x123456789abcdef0ULL, 0xfedcba9876543210ULL};
    const auto before = output;
    check(!read(v::Field::value, first, std::span<std::uint64_t>{output}) && output == before,
          "candidate invalid field range admitted/wrote output");
    ++evidence.refusals;
  }
  std::array<std::uint64_t, 1> output{0x123456789abcdef0ULL};
  check(!read(v::Field(4), 0, std::span<std::uint64_t>{output}) && output[0] == 0x123456789abcdef0ULL,
        "candidate invalid Field admitted/wrote output");
  check(read(v::Field::tag, rows.size(), std::span<std::uint64_t>{}).has_value(), "empty end field failed");
  ++evidence.refusals;
}
template<class Chain> void chains(std::span<const Row> rows, Chain&& chain) {
  std::vector<Key> starts{0, UINT64_MAX};
  if (!rows.empty()) starts.insert(starts.end(), {rows.front().key, rows[rows.size() / 2].key, rows.back().key});
  const Record sentinel{0xfedcba9876543210ULL, 0xdead, 0xbeef};
  for (const auto start : starts) {
    std::array<Record, 16> output;
    output.fill(sentinel);
    auto expected_key = start;
    std::size_t expected_count = 0;
    std::array<Record, 16> expected;
    expected.fill(sentinel);
    for (; expected_count < expected.size(); ++expected_count) {
      const auto position = std::find_if(rows.begin(), rows.end(), [&](const Row& row) { return row.key == expected_key; });
      if (position == rows.end()) break;
      expected[expected_count] = position->record;
      expected_key = position->record.next;
    }
    const auto actual = chain(start, std::span<Record>{output});
    check(actual.completed == expected_count && actual.next == expected_key && output == expected,
          "whole-call dependent chain differs from independent records/prefix oracle");
    ++evidence.chains;
    const auto empty = chain(start, std::span<Record>{});
    check(empty.completed == 0 && empty.next == start, "empty chain changed logical position");
  }
}

[[gnu::noinline]] c::DefaultTuple16 default_after_scope(const Parts& source) {
  const auto parts = source.reads();
  const auto original = take(c::DefaultTuple16::qualify(source.shape.records, source.shape.interpretation,
      std::span(parts).first(source.shape.part_count)), "default candidate qualification");
  auto copied = original;
  auto moved = c::DefaultTuple16(std::move(copied));
  return c::DefaultTuple16(std::move(moved));
}
[[gnu::noinline]] c::ExplicitTuple16 explicit_after_scope(const Parts& source) {
  const auto parts = source.reads();
  const v::TupleControl control{c::canonical_tuple_schema, source.shape.records};
  const auto original = take(c::ExplicitTuple16::qualify(control, source.shape.interpretation,
      std::span(parts).first(source.shape.part_count)), "explicit candidate qualification");
  auto copied = original;
  auto moved = c::ExplicitTuple16(std::move(copied));
  return c::ExplicitTuple16(std::move(moved));
}
[[gnu::noinline]] c::QualifiedTuple8 pointer_after_scope(const Parts& source) {
  const auto parts = source.reads();
  const v::TupleControl control{c::canonical_tuple_schema, source.shape.records};
  const auto original = take(c::qualify_tuple_part8(control, parts[0].bytes, parts[0].interpretation),
                             "pointer8 direct Part qualification");
  auto copied = original;
  auto moved = c::QualifiedTuple8(std::move(copied));
  return c::QualifiedTuple8(std::move(moved));
}
template<class State, class Qualify> void status_preserves(State seed, Qualify&& qualify) {
  const auto before = std::bit_cast<std::array<std::byte, sizeof(State)>>(seed);
  const auto status = qualify(seed);
  check(!status && before == std::bit_cast<std::array<std::byte, sizeof(State)>>(seed),
        "cold status refusal changed caller-owned state");
  ++evidence.status_out_refusals;
  ++evidence.refusals;
}
void direct_part_checks(const Parts& source, std::span<const Row> rows, c::ExplicitTuple16 seed16,
                        c::QualifiedTuple8 seed8) {
  const auto parts = source.reads();
  const v::TupleControl control{c::canonical_tuple_schema, rows.size()};
  auto reject = [&](orbital::Bytes body, orbital::Bytes interpretation) {
    check(!c::qualify_tuple_part16(control, body, interpretation), "direct16 Part admitted malformed facts");
    check(!c::qualify_tuple_part8(control, body, interpretation), "direct8 Part admitted malformed facts");
    status_preserves(seed16, [&](auto& output) { return c::qualify_tuple_part16(control, body, interpretation, output); });
    status_preserves(seed8, [&](auto& output) { return c::qualify_tuple_part8(control, body, interpretation, output); });
    auto baseline_parts = parts;
    baseline_parts[0].bytes = body;
    baseline_parts[0].interpretation = interpretation;
    check(!v::qualify_tuple(control, source.shape.interpretation, std::span(baseline_parts).first(1)),
          "direct Part refusal differs from frozen cold acceptance");
    evidence.refusals += 2;
  };
  for (std::size_t offset = 0; offset < parts[0].interpretation.size(); ++offset) {
    auto corrupted = std::vector<std::byte>(parts[0].interpretation.begin(), parts[0].interpretation.end());
    corrupted[offset] ^= std::byte{1};
    reject(parts[0].bytes, corrupted);
  }
  reject(parts[0].bytes, parts[0].interpretation.first(74));
  if (!rows.empty()) reject(parts[0].bytes.first(parts[0].bytes.size() - 1), parts[0].interpretation);
  const v::TupleControl wrong{c::canonical_tuple_schema, rows.size() + 1};
  status_preserves(seed16, [&](auto& output) {
    return c::qualify_tuple_part16(wrong, parts[0].bytes, parts[0].interpretation, output);
  });
  status_preserves(seed8, [&](auto& output) {
    return c::qualify_tuple_part8(wrong, parts[0].bytes, parts[0].interpretation, output);
  });
  const v::TupleControl overflow{c::canonical_tuple_schema, SIZE_MAX};
  status_preserves(seed16, [&](auto& output) {
    return c::qualify_tuple_part16(overflow, parts[0].bytes, parts[0].interpretation, output);
  });
  status_preserves(seed8, [&](auto& output) {
    return c::qualify_tuple_part8(overflow, parts[0].bytes, parts[0].interpretation, output);
  });
  c::ExplicitTuple16 out16;
  c::QualifiedTuple8 out8;
  check(c::qualify_tuple_part16(control, parts[0].bytes, parts[0].interpretation, out16).has_value() &&
        c::qualify_tuple_part8(control, parts[0].bytes, parts[0].interpretation, out8).has_value(),
        "direct Part status success failed");
  chains(rows, [&](Key key, std::span<Record> out) {
    c::ChainResult outcome{SIZE_MAX, UINT64_MAX};
    c::tuple_chain_into(control.schema, out16, key, out, outcome);
    return outcome;
  });
  chains(rows, [&](Key key, std::span<Record> out) {
    c::ChainResult outcome{SIZE_MAX, UINT64_MAX};
    c::tuple_chain8_into(control, out8, key, out, outcome);
    return outcome;
  });
  std::vector<Key> queries{0, UINT64_MAX};
  for (const auto& row : rows) { queries.push_back(row.key); queries.push_back(row.key + 1); }
  const Record sentinel{0xfedcba9876543210ULL, 0xdead, 0xbeef};
  for (const auto key : queries) {
    const auto position = seek(rows, key);
    const bool hit = position < rows.size() && rows[position].key == key;
    c::LookupOutcome first{sentinel, true}, second{sentinel, true};
    c::tuple_get_into(control.schema, out16, key, first);
    c::tuple_get8_into(control, out8, key, second);
    const auto expected = hit ? rows[position].record : sentinel;
    check(first.found == hit && second.found == hit && first.record == expected && second.record == expected,
          "caller-owned point outcome differs or miss changed record");
    evidence.lookups += 2;
  }
}
void tuple_cases(const Parts& source, std::span<const Row> rows) {
  const auto& schema = c::canonical_tuple_schema;
  const auto first = default_after_scope(source);
  const auto second = explicit_after_scope(source);
  const auto third = pointer_after_scope(source);
  const v::TupleControl control{schema, rows.size()};
  check(first.size() == rows.size() && second.size() == rows.size(), "candidate count changed after stack scope");
  rows_and_fields(rows, [&](std::size_t row) { return first.row(row); });
  rows_and_fields(rows, [&](std::size_t row) { return c::tuple_row(schema, second, row); });
  fields(rows, [&](v::Field field, std::size_t at, std::span<std::uint64_t> out) { return first.field(field, at, out); });
  fields(rows, [&](v::Field field, std::size_t at, std::span<std::uint64_t> out) { return c::tuple_field(schema, second, field, at, out); });
  lookups(rows, [&](Key key) { return first.get(key); }, [&](Key key) { return first.lower_bound(key); },
          [&](Key key, std::span<Row> out) { return first.scan(key, out); });
  lookups(rows, [&](Key key) { return c::tuple_get(schema, second, key); },
          [&](Key key) { return c::tuple_lower_bound(schema, second, key); },
          [&](Key key, std::span<Row> out) { return c::tuple_scan(schema, second, key, out); });
  chains(rows, [&](Key key, std::span<Record> out) { return first.chain(key, out); });
  chains(rows, [&](Key key, std::span<Record> out) { return c::tuple_chain(schema, second, key, out); });
  lookups(rows, [&](Key key) { return c::tuple_get8(control, third, key); },
          [&](Key key) { return c::tuple_lower_bound(schema, third.with(control), key); },
          [&](Key key, std::span<Row> out) { return c::tuple_scan(schema, third.with(control), key, out); });
  chains(rows, [&](Key key, std::span<Record> out) { return c::tuple_chain8(control, third, key, out); });
  direct_part_checks(source, rows, second, third);
  const auto parts = source.reads();
  const auto selected = std::span(parts).first(source.shape.part_count);
  check(c::qualify_tuple_parent(control, source.shape.interpretation, parts[0].id, 1).has_value(),
        "shared legacy parent qualification failed");
  const auto full8 = take(c::qualify_tuple8(control, source.shape.interpretation, selected),
                          "full raw-parent pointer8 qualification");
  chains(rows, [&](Key key, std::span<Record> out) { return c::tuple_chain8(control, full8, key, out); });
  auto reject = [&](orbital::Bytes parent, std::span<const v::ReadPart> input) {
    check(!c::DefaultTuple16::qualify(rows.size(), parent, input), "default candidate admitted malformed closure");
    check(!c::ExplicitTuple16::qualify({schema, rows.size()}, parent, input),
          "explicit candidate admitted malformed closure");
    check(!c::qualify_tuple8(control, parent, input), "raw-parent pointer8 admitted malformed closure");
    evidence.refusals += 3;
    status_preserves(second, [&](auto& output) { return c::qualify_tuple16(control, parent, input, output); });
    status_preserves(third, [&](auto& output) { return c::qualify_tuple8(control, parent, input, output); });
  };
  check(!c::DefaultTuple16::qualify(rows.size() + 1, source.shape.interpretation, selected),
        "default candidate admitted wrong count");
  check(!c::ExplicitTuple16::qualify({schema, rows.size() + 1}, source.shape.interpretation, selected),
        "explicit candidate admitted wrong count");
  evidence.refusals += 2;
  for (std::size_t offset = 0; offset < source.shape.interpretation.size(); ++offset) {
    auto altered = source.shape.interpretation;
    altered[offset] ^= std::byte{1};
    check(!c::qualify_tuple_parent(control, altered, parts[0].id, 1),
          "shared parent helper admitted changed canonical byte");
    reject(altered, selected);
    ++evidence.refusals;
  }
  check(!c::qualify_tuple_parent(control, source.shape.interpretation, parts[0].id, 0) &&
        !c::qualify_tuple_parent(control, source.shape.interpretation, parts[0].id, 2) &&
        !c::qualify_tuple_parent(control, source.shape.interpretation,
                               {parts[0].id.high, parts[0].id.low + 1}, 1),
        "shared parent helper admitted wrong Part count/ID");
  evidence.refusals += 3;
  reject(source.shape.interpretation, selected.first(0));
  auto wrong_parts = parts;
  wrong_parts[0].id.low ^= 0x100;
  reject(source.shape.interpretation, std::span(wrong_parts).first(1));
  auto metadata = source.shape.interpretation;
  metadata.back() = std::byte{1};
  reject(metadata, selected);
  auto descriptor = std::vector<std::byte>(parts[0].interpretation.begin(), parts[0].interpretation.end());
  descriptor[0] ^= std::byte{1};
  wrong_parts = parts;
  wrong_parts[0].interpretation = descriptor;
  reject(source.shape.interpretation, std::span(wrong_parts).first(1));
  if (!rows.empty()) {
    wrong_parts = parts;
    wrong_parts[0].bytes = wrong_parts[0].bytes.first(wrong_parts[0].bytes.size() - 1);
    reject(source.shape.interpretation, std::span(wrong_parts).first(1));
  }
}
void column_cases(const Parts& source, std::span<const Row> rows) {
  const auto parts = source.reads();
  const auto selected = std::span(parts).first(source.shape.part_count);
  const v::ColumnControl control{rows.size()};
  const auto cursors = take(v::qualify_columns(c::canonical_tuple_schema, control,
      source.shape.interpretation, selected), "independent four-column qualification");
  const auto body = cursors.open(control);
  rows_and_fields(rows, [&](std::size_t row) { return body.row(row); });
  fields(rows, [&](v::Field field, std::size_t at, std::span<std::uint64_t> out) { return body.field(field, at, out); });
  lookups(rows, [&](Key key) { return body.get(key); }, [&](Key key) { return body.lower_bound(key); },
          [&](Key key, std::span<Row> out) { return body.scan(key, out); });
  chains(rows, [&](Key key, std::span<Record> out) { return c::columns_chain(control, cursors, key, out); });
  check(!c::DefaultTuple16::qualify(rows.size(), source.shape.interpretation, selected),
        "default candidate admitted column encoding");
  check(!c::ExplicitTuple16::qualify({c::canonical_tuple_schema, rows.size()}, source.shape.interpretation, selected),
        "explicit candidate admitted column encoding");
  evidence.refusals += 2;
}
void selected_wrappers(Runtime& runtime) {
  namespace obj = orbital::object;
  for (const auto count : {std::size_t{0}, std::size_t{1}, std::size_t{129}}) {
    const auto rows = input(count);
    const auto& schema = c::canonical_tuple_schema;
    const v::TupleControl control{schema, count};
    auto store = take(obj::Store::open(runtime.lane, 1024 * 1024), "wrapper Store open");
    auto backing = std::make_shared<Parts>(*runtime.pool, v::Format::rows, rows);
    const auto read = backing->reads()[0];
    auto draft = take(store.edit(), "wrapper Draft");
    const orbital::ObjectId id{0x4d45544143414e44ULL, count + 1};
    const auto logical = c::detail::count_bytes(count);
    const obj::RepresentationSpec spec{id, draft.version(), {}, v::data_program, logical,
                                       backing->shape.interpretation, {}};
    const std::array parts{obj::PartSource{read.id, read.bytes, read.interpretation, 0, 1, backing}};
    auto rep = take(store.capture(spec, parts), "actual wrapper immutable capture");
    check(draft.put(rep).has_value(), "wrapper Draft insertion");
    auto prepared = take(store.prepare(draft), "wrapper prepare");
    check(store.install(prepared).has_value(), "wrapper installation");
    prepared.retire();
    {
      auto context = take(store.read(), "wrapper ReadContext");
      const auto selected = take(obj::select(context, id), "wrapper selected closure");
      const auto view = take(selected.part(std::size_t{0}), "wrapper complete Tuple View");
      const auto from_selected = take(c::qualify_tuple16(control, selected), "direct selected qualifier");
      const auto from_rep = take(c::qualify_tuple16(control, rep), "direct Representation qualifier");
      const auto from_view = take(c::qualify_tuple16(control, view), "prior-parent-admitted direct View qualifier");
      const auto part16 = take(c::qualify_tuple_part16(control, view), "independent actual Part16 qualifier");
      const auto part8 = take(c::qualify_tuple_part8(control, view), "independent actual Part8 qualifier");
      for (const auto state : {from_selected, from_rep, from_view, part16})
        chains(rows, [&](Key key, std::span<Record> output) { return c::tuple_chain(schema, state, key, output); });
      chains(rows, [&](Key key, std::span<Record> output) { return c::tuple_chain8(control, part8, key, output); });
      c::ExplicitTuple16 output;
      check(c::qualify_tuple16(control, selected, output).has_value() &&
                c::qualify_tuple16(control, rep, output).has_value() &&
                c::qualify_tuple16(control, view, output).has_value(), "selected status+out success failed");
      evidence.selected_wrappers += 6;
      evidence.part_only_wrappers += 2;
      const v::TupleControl wrong{schema, count + 1};
      status_preserves(output, [&](auto& out) { return c::qualify_tuple16(wrong, selected, out); });
      status_preserves(output, [&](auto& out) { return c::qualify_tuple16(wrong, rep, out); });
      status_preserves(output, [&](auto& out) { return c::qualify_tuple16(wrong, view, out); });
      status_preserves(output, [&](auto& out) { return c::qualify_tuple_part16(control, obj::View{}, out); });
      status_preserves(part8, [&](auto& out) { return c::qualify_tuple_part8(control, obj::View{}, out); });
      auto refuse_full_parent = [&](const obj::Representation& wrong_rep) {
        const auto wrong_view = take(wrong_rep.part(std::size_t{0}), "malformed wrapper Part");
        check(!c::qualify_tuple16(control, wrong_rep) && !c::qualify_tuple16(control, wrong_view),
              "legacy wrapper admitted wrong parent/Part association");
        status_preserves(output, [&](auto& out) { return c::qualify_tuple16(control, wrong_rep, out); });
        status_preserves(output, [&](auto& out) { return c::qualify_tuple16(control, wrong_view, out); });
        // The independent Part entry deliberately does not certify parent
        // ProgramId/count/ID. Its byte+descriptor facts remain valid here.
        check(c::qualify_tuple_part16(control, wrong_view).has_value() &&
              c::qualify_tuple_part8(control, wrong_view).has_value(),
              "Part helper silently inherited legacy parent obligations");
        evidence.part_only_wrappers += 2;
        evidence.refusals += 2;
      };
      auto wrong_program = spec;
      wrong_program.format = {0, 99};
      const auto program_rep = take(store.capture(wrong_program, parts), "wrong ProgramId capture");
      refuse_full_parent(program_rep);
      auto wrong_count = spec;
      const auto bad_logical = c::detail::count_bytes(count + 1);
      wrong_count.logical_metadata = bad_logical;
      const auto count_rep = take(store.capture(wrong_count, parts), "wrong logical count capture");
      refuse_full_parent(count_rep);
      auto wrong_parts = parts;
      wrong_parts[0].id.low ^= 0x100;
      const auto id_rep = take(store.capture(spec, wrong_parts), "wrong requested PartId capture");
      refuse_full_parent(id_rep);
      auto malformed_parent = backing->shape.interpretation;
      malformed_parent.back() = std::byte{1};
      auto wrong_metadata = spec;
      wrong_metadata.interpretation = malformed_parent;
      const auto metadata_rep = take(store.capture(wrong_metadata, parts), "wrong parent metadata capture");
      refuse_full_parent(metadata_rep);

      // A resolved View cannot certify directory completeness. Keep every
      // available row/parent fact canonical, but capture an actual extra Part.
      // The View admission below intentionally omits the required prior parent
      // proof to falsify any claim that it discovers or rejects that extra Part;
      // no native operation is performed through this unadmitted parent.
      auto multi_draft = take(store.edit(), "extra-Part private Draft");
      auto multi_spec = spec;
      multi_spec.version = multi_draft.version();
      const std::array<obj::PartSource, 2> multi_parts{{parts[0],
          {{read.id.high, read.id.low + 1}, {}, {}, 0, 1, {}}}};
      const auto multi_rep = take(store.capture(multi_spec, multi_parts), "actual two-Part capture");
      check(multi_rep.part_count() == 2, "two-Part falsifier lost its extra Part");
      check(multi_draft.put(multi_rep).has_value(), "two-Part private insertion");
      const auto multi_selected = take(multi_draft.select(id), "two-Part private Selection");
      const auto multi_view = take(multi_selected.part(std::size_t{0}), "two-Part resolved row View");
      check(!c::qualify_tuple16(control, multi_rep) && !c::qualify_tuple16(control, multi_selected),
            "whole-parent admission ignored actual extra Part");
      status_preserves(output, [&](auto& out) { return c::qualify_tuple16(control, multi_rep, out); });
      status_preserves(output, [&](auto& out) { return c::qualify_tuple16(control, multi_selected, out); });
      check(c::qualify_tuple16(control, multi_view).has_value() &&
                c::qualify_tuple_part16(control, multi_view).has_value() &&
                c::qualify_tuple_part8(control, multi_view).has_value(),
            "resolved Part incorrectly claimed to discover parent directory count");
      evidence.refusals += 2;
      evidence.part_only_wrappers += 3;
    }
    for (unsigned pass = 0; pass < 128; ++pass) if (!store.drain(4096)) break;
    check(store.statistics().retirement_records == 0, "wrapper metadata retirement did not drain");
  }
}
void cases(a::Pool& pool) {
  for (const auto count : {std::size_t{0}, std::size_t{1}, std::size_t{7}, std::size_t{8},
                           std::size_t{9}, std::size_t{127}, std::size_t{128}, std::size_t{129}, std::size_t{257}}) {
    const auto rows = input(count);
    for (const auto format : {v::Format::rows, v::Format::columns}) {
      Parts source(pool, format, rows);
      const Image before(source);
      wire(source, rows);
      if (format == v::Format::rows) tuple_cases(source, rows);
      else column_cases(source, rows);
      before.unchanged(source);
      ++evidence.cases;
    }
  }
  auto rows = input(9);
  rows[0].record.next = rows[1].key;
  rows[1].record.next = UINT64_MAX;
  for (const auto format : {v::Format::rows, v::Format::columns}) {
    Parts source(pool, format, rows);
    const Image before(source);
    if (format == v::Format::rows) tuple_cases(source, rows);
    else column_cases(source, rows);
    before.unchanged(source);
    wire(source, rows);
    ++evidence.cases;
  }
}
} // namespace

int main() {
  {
    Runtime runtime;
    cases(*runtime.pool);
    selected_wrappers(runtime);
  }
  const auto& f = c::footprint;
  std::cout << "{\"status\":\"pass\",\"frozen_revision\":\"7f73c81\",\"scope\":\"same-kernel consumer spike; no MVCC or timing qualification\""
#if defined(CONSUMER_METADATA_FROZEN) && CONSUMER_METADATA_FROZEN
      << ",\"input_mode\":\"frozen-capture\""
#else
      << ",\"input_mode\":\"relocated-spike-baseline\""
#endif
      << ",\"format_count_cases\":" << evidence.cases << ",\"logical_fields\":" << evidence.fields
      << ",\"wire_bits\":" << evidence.wire_bits << ",\"lookups\":" << evidence.lookups
      << ",\"scans\":" << evidence.scans << ",\"chains\":" << evidence.chains
      << ",\"refusals\":" << evidence.refusals << ",\"allocation_requests\":" << evidence.allocations
      << ",\"status_out_refusals\":" << evidence.status_out_refusals
      << ",\"selected_wrapper_checks\":" << evidence.selected_wrappers
      << ",\"part_only_wrapper_checks\":" << evidence.part_only_wrappers
      << ",\"constructed_payload_bytes_total\":" << evidence.payload_bytes
      << ",\"serialized_parent_and_part_metadata_bytes_total\":" << evidence.serialized_metadata_bytes
      << ",\"static_cpp_record_bytes\":{\"default_state\":" << f.default_tuple_state
      << ",\"explicit_state\":" << f.explicit_tuple_state << ",\"program_shared_schema\":" << f.schema
      << ",\"qualified_pointer8\":" << sizeof(c::QualifiedTuple8)
      << ",\"pointer8_result\":" << sizeof(orbital::Result<c::QualifiedTuple8>)
      << ",\"pointer_count16_result\":" << sizeof(orbital::Result<c::ExplicitTuple16>)
      << ",\"status_result\":" << sizeof(orbital::Result<void>)
      << ",\"caller_lookup_outcome\":" << sizeof(c::LookupOutcome)
      << ",\"optional_lookup_result\":" << sizeof(std::optional<Record>)
      << ",\"tuple_control\":" << f.tuple_control << ",\"column_control\":" << f.column_control
      << ",\"tuple_cursor\":" << f.tuple_cursor << ",\"four_column_cursors\":" << f.column_cursors
      << ",\"tuple_body\":" << f.tuple_body << ",\"column_body\":" << f.column_body
      << ",\"fused_tuple_view\":" << f.tuple_view << ",\"tuple_endpoint\":" << f.tuple_endpoint
      << ",\"four_tuple_endpoints\":" << f.four_tuple_endpoints
      << ",\"schema_layout\":" << f.schema_layout << ",\"schema_constructor\":" << f.schema_constructor
      << ",\"schema_readers\":" << f.schema_readers << ",\"schema_writers\":" << f.schema_writers
      << "},\"candidate_heap_box_bytes\":0,\"native_output_bytes_for_chain16\":" << sizeof(std::array<Record, 16>)
      << ",\"ownership\":\"external real Allocation plus immutable program Schema; serialized metadata consumed at cold qualification\""
      << ",\"final_arena_charge\":0}\n";
}
