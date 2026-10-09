#include "data.h"
#include "native.h"
#include <orbital/allocation.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
namespace v = orbital_bench::versioned;
namespace a = orbital::allocation;
using orbital_bench::Key;
using orbital_bench::Record;
using orbital_bench::Row;
constexpr std::array<unsigned, 4> widths{64, 64, 20, 32};

struct Evidence {
  std::uint64_t cases{}, logical_fields{}, wire_bits{}, conversions{}, updates{};
  std::uint64_t refusals{}, metadata_refusals{}, lookup_checks{}, scans{}, dependent_steps{};
  std::uint64_t ended_scope_bindings{}, misaligned_column_refusals{};
  std::uint64_t thin_cursor_cases{}, thin_qualification_refusals{};
  v::KernelCosts costs;
} evidence;

void check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "versioned codec data check: %s\n", message);
  std::abort();
}

template<class T, class E> T take(std::expected<T, E> value, const char* message) {
  check(value.has_value(), message);
  return std::move(*value);
}

void add(const v::KernelCosts& costs) {
  evidence.costs.decoded_values += costs.decoded_values;
  evidence.costs.encoded_values += costs.encoded_values;
  evidence.costs.bindings += costs.bindings;
  evidence.costs.constructor_calls += costs.constructor_calls;
  evidence.costs.issued_bytes += costs.issued_bytes;
  evidence.costs.issued_spans += costs.issued_spans;
  evidence.costs.completed_parts += costs.completed_parts;
  evidence.costs.completed_rows += costs.completed_rows;
  evidence.costs.scratch_bound_bytes += costs.scratch_bound_bytes;
}

struct Runtime {
  orbital::worker::Lane lane;
  orbital::Ref<a::Arena> arena;
  a::Pool* pool{};
  bool closed{};
  explicit Runtime(std::uint64_t limit) {
    check(orbital::worker::adopt(lane).has_value(), "Lane adoption failed");
    arena = take(a::open_arena(limit), "Arena open failed");
    pool = take(a::add_pool(*arena, lane), "Pool creation failed");
  }
  void finish() {
    if (closed) return;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
      const auto progress = a::close(*pool);
      check(progress.kind() != orbital::Progress::Kind::failed, "physical close failed");
      if (progress.kind() == orbital::Progress::Kind::done) break;
      check(std::chrono::steady_clock::now() < deadline, "physical close did not drain");
      orbital::worker::poll(lane, 1024);
      std::this_thread::yield();
    }
    check(arena->charged_bytes.load() == 0, "Pool close retained charge");
    check(a::statistics(*pool).metadata_bytes == 0, "Pool close retained metadata");
    check(a::remove_pool(*pool).has_value(), "closed Pool removal failed");
    pool = nullptr;
    orbital::worker::relinquish(lane);
    arena.reset();
    closed = true;
  }
  ~Runtime() { finish(); }
};

struct Parts {
  v::Shape shape;
  std::array<a::Allocation, v::field_count> backing;
  Parts(a::Pool& pool, const v::Schema& schema, v::Format format, std::size_t count)
      : shape(take(v::shape(schema, format, count), "valid shape failed")) {
    check(shape.part_count == (format == v::Format::rows ? 1 : v::field_count),
          "wrong physical Part count");
    for (unsigned part = 0; part < shape.part_count; ++part) {
      const auto& required = shape.parts[part];
      if (!required.body_bytes) continue; // The real allocator rejects zero bytes.
      backing[part] = take(a::allocate(pool,
          {.bytes = required.body_bytes, .alignment = required.alignment,
           .readable_padding = required.readable_padding,
           .initialization = a::Initialization::uninitialized}), "Part allocation failed");
      check(backing[part].size == required.body_bytes &&
                backing[part].readable_padding == required.readable_padding &&
                reinterpret_cast<std::uintptr_t>(backing[part].address) % required.alignment == 0,
            "actual Allocation does not meet Part shape");
    }
  }
  std::array<v::WritePart, v::field_count> writes() {
    std::array<v::WritePart, v::field_count> result;
    for (unsigned part = 0; part < shape.part_count; ++part)
      result[part] = {shape.parts[part].id, a::bytes(backing[part])};
    return result;
  }
  std::array<v::ReadPart, v::field_count> reads() const {
    std::array<v::ReadPart, v::field_count> result;
    for (unsigned part = 0; part < shape.part_count; ++part)
      result[part] = {shape.parts[part].id, a::bytes(backing[part]), shape.parts[part].interpretation};
    return result;
  }
  v::ReadData bind(const v::Schema& schema) const {
    const auto parts = reads();
    return take(v::ReadData::bind(schema, shape, std::span(parts).first(shape.part_count)),
                "completed data binding failed");
  }
  void fill(std::byte value) {
    for (unsigned part = 0; part < shape.part_count; ++part)
      std::fill(a::bytes(backing[part]).begin(), a::bytes(backing[part]).end(), value);
  }
};

struct Image {
  std::array<std::vector<std::byte>, v::field_count> bytes;
  explicit Image(const Parts& parts) {
    for (unsigned part = 0; part < parts.shape.part_count; ++part) {
      const auto body = a::bytes(parts.backing[part]);
      bytes[part].assign(body.begin(), body.end());
    }
  }
  bool equals(const Parts& parts) const {
    for (unsigned part = 0; part < parts.shape.part_count; ++part) {
      const auto body = a::bytes(parts.backing[part]);
      if (!std::equal(body.begin(), body.end(), bytes[part].begin(), bytes[part].end())) return false;
    }
    return true;
  }
};

std::uint64_t scramble(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

// Expected records are generated independently of all codec/lookup bodies.
std::vector<Row> arbitrary(std::size_t count) {
  std::vector<Row> result(count);
  for (std::size_t row = 0; row < count; ++row) {
    auto& item = result[row];
    switch (row % 9) {
    case 0: item = {0, {0, 0, 0}}; break;
    case 1: item = {UINT64_MAX, {UINT64_MAX, orbital_bench::value_mask, UINT32_MAX}}; break;
    case 2: item = {std::uint64_t{1} << (row % 64),
                   {std::uint64_t{1} << ((row + 17) % 64),
                    std::uint32_t{1} << (row % 20), std::uint32_t{1} << (row % 32)}}; break;
    case 3: item = {0xaaaaaaaaaaaaaaaaULL,
                   {0x5555555555555555ULL, 0xaaaaa, 0x55555555}}; break;
    default: item = {scramble(row + 19),
                    {scramble(row + 0x100001), std::uint32_t(scramble(row + 81)) & orbital_bench::value_mask,
                     std::uint32_t(scramble(row + 123))}}; break;
    }
  }
  return result;
}

std::vector<Row> ordered(std::size_t count) {
  auto result = arbitrary(count);
  for (std::size_t row = 0; row < count; ++row)
    result[row].key = 0x8000000000000100ULL + row * 19;
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
  default: check(false, "oracle field index escaped fixed schema"); return 0;
  }
}

void equal_row(const Row& actual, const Row& expected) {
  for (unsigned index = 0; index < v::field_count; ++index) {
    check(field(actual, index) == field(expected, index), "decoded field differs from independent Row");
    ++evidence.logical_fields;
  }
}

unsigned octet(orbital::Bytes bytes, std::size_t offset) {
  check(offset < bytes.size(), "wire oracle exceeded physical extent");
  return std::to_integer<unsigned>(bytes[offset]);
}

void row_at(orbital::Bytes bytes, std::size_t row, const Row& expected, unsigned spare = 0) {
  constexpr std::array<unsigned, 4> first_bit{0, 64, 128, 152};
  for (unsigned index = 0; index < v::field_count; ++index)
    for (unsigned bit = 0; bit < widths[index]; ++bit) {
      const auto physical = first_bit[index] + bit;
      check(((octet(bytes, row * v::tuple_bytes + physical / 8) >> (physical % 8)) & 1) ==
                ((field(expected, index) >> bit) & 1), "Tuple wire bit differs from independent Row");
      ++evidence.wire_bits;
    }
  check((octet(bytes, row * v::tuple_bytes + 18) >> 4) == spare,
        "Tuple spare high nibble changed");
}

void column_prefix(orbital::Bytes bytes, unsigned index, std::span<const Row> expected,
                   std::size_t initialized_rows, bool final_slack) {
  const auto width = widths[index], whole = width / 8, residual = width % 8;
  const auto through = final_slack ? (initialized_rows + 7) / 8 * 8 : initialized_rows;
  for (std::size_t row = 0; row < through; ++row) {
    const auto value = row < initialized_rows ? field(expected[row], index) : 0;
    for (unsigned bit = 0; bit < width; ++bit) {
      const auto offset = row / 8 * width + (bit < residual ? 8 * whole + bit :
          row % 8 * whole + (bit - residual) / 8);
      const auto physical_bit = bit < residual ? row % 8 : (bit - residual) % 8;
      check(((octet(bytes, offset) >> physical_bit) & 1) == ((value >> bit) & 1),
            "Series wire bit or initialized final-tile slack differs");
      ++evidence.wire_bits;
    }
  }
}

void wire(const Parts& parts, std::span<const Row> expected,
          std::span<const std::uint8_t> spare = {}) {
  check(parts.shape.records == expected.size(), "shape logical count differs");
  if (parts.shape.format == v::Format::rows) {
    const auto& required = parts.shape.parts[0];
    check(required.body_bytes == expected.size() * v::tuple_bytes,
          "Tuple occupied body extent differs from fixed wire");
    check(required.interpretation.size() == 75, "Tuple interpretation extent differs");
    check(octet(required.interpretation, 0) == 'T' && octet(required.interpretation, 1) == 'P' &&
              octet(required.interpretation, 2) == 1 && octet(required.interpretation, 3) == 23 &&
              octet(required.interpretation, 4) == 23 && octet(required.interpretation, 5) == 0,
          "Tuple interpretation header differs from fixed schema");
    for (unsigned code = 0; code < 23; ++code)
      check(octet(required.interpretation, 6 + 3 * code) == code &&
                octet(required.interpretation, 7 + 3 * code) == 0 &&
                octet(required.interpretation, 8 + 3 * code) == (code == 18 ? 4 : 8),
            "Tuple code rank or byte placement differs from fixed schema");
    check(spare.empty() || spare.size() == expected.size(), "oracle spare extent differs");
    const auto bytes = a::bytes(parts.backing[0]);
    for (std::size_t row = 0; row < expected.size(); ++row)
      row_at(bytes, row, expected[row], spare.empty() ? 0 : spare[row]);
  } else {
    for (unsigned index = 0; index < v::field_count; ++index) {
      const auto& required = parts.shape.parts[index];
      const auto bytes = a::bytes(parts.backing[index]);
      check(required.body_bytes == (expected.size() + 7) / 8 * widths[index],
            "Series body extent differs from complete tight Local tiles");
      check(required.interpretation.size() == 40, "Series descriptor extent differs");
      auto description = v::sp::decode_descriptor({
          reinterpret_cast<const std::uint8_t*>(required.interpretation.data()), 40});
      check(description.has_value() && description->width == widths[index] &&
                description->heads == 0 && description->storage == v::sp::geometry::local &&
                description->count == expected.size() && description->strides[0] == widths[index] &&
                !description->strides[1] && !description->strides[2],
            "Series descriptor does not match fixed headless Local wire");
      column_prefix(bytes, index, expected, expected.size(), true);
    }
  }
  for (unsigned part = 0; part < parts.shape.part_count; ++part) {
    const auto& allocation = parts.backing[part];
    if (!allocation) continue;
    for (std::size_t byte = 0; byte < allocation.readable_padding; ++byte)
      check(allocation.address[allocation.size + byte] == std::byte{}, "allocator padding changed");
  }
}

void verify(const v::Schema& schema, const Parts& parts, std::span<const Row> expected,
            std::span<const std::uint8_t> spare = {}) {
  auto data = parts.bind(schema);
  check(data.size() == expected.size() && data.format() == parts.shape.format,
        "bound data changed count or format");
  for (std::size_t row = 0; row < expected.size(); ++row) {
    equal_row(take(data.row(row), "valid checked row read failed"), expected[row]);
    equal_row(data.row_unchecked(row), expected[row]);
  }
  for (unsigned index = 0; index < v::field_count; ++index) {
    std::vector<std::uint64_t> output(expected.size(), UINT64_MAX);
    check(data.field(v::Field(index), 0, output).has_value(), "valid field range failed");
    for (std::size_t row = 0; row < expected.size(); ++row) {
      check(output[row] == field(expected[row], index), "field range differs from independent oracle");
      ++evidence.logical_fields;
    }
  }
  check(!data.row(expected.size()), "end row was accepted");
  std::array<std::uint64_t, 2> guarded{0x123456789abcdef0ULL, 0xfedcba9876543210ULL};
  const auto before = guarded;
  check(!data.field(v::Field::value, expected.empty() ? 0 : expected.size() - 1, guarded),
        "surplus field output range was accepted");
  check(guarded == before, "rejected field range changed output");
  std::span<std::uint64_t> empty;
  check(data.field(v::Field::value, expected.size(), empty).has_value(), "empty end field range failed");
  wire(parts, expected, spare);
}

void construct(const v::Schema& schema, Parts& target, std::span<const Row> input) {
  auto parts = target.writes();
  v::KernelCosts costs;
  check(v::construct_into(schema, target.shape, std::span(parts).first(target.shape.part_count),
                          input, costs).has_value(), "valid private construction failed");
  add(costs);
}

void convert(const v::Schema& schema, const Parts& source, Parts& target) {
  auto input = source.bind(schema);
  auto parts = target.writes();
  v::KernelCosts costs;
  const auto converted = v::convert_into(schema, input, target.shape,
      std::span(parts).first(target.shape.part_count), costs);
  if (!converted)
    std::fprintf(stderr, "conversion records=%zu source=%u target=%u error=%u encoded=%llu\n",
        input.size(), unsigned(input.format()), unsigned(target.shape.format), unsigned(converted.error()),
        static_cast<unsigned long long>(costs.encoded_values));
  check(converted.has_value(), "valid conversion failed");
  add(costs);
  ++evidence.conversions;
}

void matrix(a::Pool& pool, const v::Schema& schema) {
  const std::array<std::size_t, 18> counts{0, 1, 7, 8, 9, 15, 16, 17, 63, 64, 65,
                                        127, 128, 129, 255, 256, 257, 1025};
  for (const auto count : counts) {
    const auto input = arbitrary(count);
    for (const auto format : {v::Format::rows, v::Format::columns}) {
      ++evidence.cases;
      Parts source(pool, schema, format, count);
      construct(schema, source, input);
      verify(schema, source, input);
      const Image before(source);
      const auto alternative = format == v::Format::rows ? v::Format::columns : v::Format::rows;
      Parts target(pool, schema, alternative, count);
      convert(schema, source, target);
      verify(schema, target, input);
      check(before.equals(source), "conversion changed source physical bytes");
      Parts roundtrip(pool, schema, format, count);
      convert(schema, target, roundtrip);
      verify(schema, roundtrip, input);
    }
  }
}

void shape_equal(const v::Shape& left, const v::Shape& right) {
  check(left.format == right.format && left.records == right.records &&
            left.part_count == right.part_count && left.interpretation == right.interpretation,
        "decoded parent shape differs");
  for (unsigned part = 0; part < left.part_count; ++part)
    check(left.parts[part].id == right.parts[part].id &&
              left.parts[part].body_bytes == right.parts[part].body_bytes &&
              left.parts[part].alignment == right.parts[part].alignment &&
              left.parts[part].readable_padding == right.parts[part].readable_padding &&
              left.parts[part].interpretation == right.parts[part].interpretation,
          "decoded physical Part shape differs");
}

void metadata(a::Pool& pool, const v::Schema& schema) {
  const auto input = arbitrary(17);
  for (const auto format : {v::Format::rows, v::Format::columns}) {
    Parts source(pool, schema, format, input.size());
    construct(schema, source, input);
    const Image before(source);
    shape_equal(source.shape, take(v::decode_shape(schema, source.shape.interpretation),
                                  "canonical shape decode failed"));
    for (const auto at : {std::size_t{0}, source.shape.interpretation.size() - 1}) {
      auto corrupted = source.shape.interpretation;
      corrupted[at] ^= std::byte{0xff};
      check(!v::decode_shape(schema, corrupted), "corrupted parent interpretation was accepted");
      ++evidence.metadata_refusals;
    }
    auto short_parent = source.shape.interpretation;
    short_parent.pop_back();
    check(!v::decode_shape(schema, short_parent), "short parent interpretation was accepted");
    ++evidence.metadata_refusals;
    auto long_parent = source.shape.interpretation;
    long_parent.push_back(std::byte{});
    check(!v::decode_shape(schema, long_parent), "trailing parent interpretation was accepted");
    ++evidence.metadata_refusals;
    const auto original = source.reads();
    auto reject = [&](std::span<const v::ReadPart> parts) {
      check(!v::ReadData::bind(schema, source.shape, parts), "malformed physical Parts were accepted");
      ++evidence.metadata_refusals;
    };
    reject(std::span(original).first(source.shape.part_count - 1));
    std::vector<v::ReadPart> excess(original.begin(), original.begin() + source.shape.part_count);
    excess.push_back(original[0]);
    reject(excess);
    auto parts = original;
    parts[0].id.low ^= 0x100;
    reject(std::span(parts).first(source.shape.part_count));
    parts = original;
    parts[0].bytes = parts[0].bytes.first(parts[0].bytes.size() - 1);
    reject(std::span(parts).first(source.shape.part_count));
    parts = original;
    std::vector<std::byte> wrong(parts[0].interpretation.begin(), parts[0].interpretation.end());
    wrong[0] ^= std::byte{0xff};
    parts[0].interpretation = wrong;
    reject(std::span(parts).first(source.shape.part_count));
    parts = original;
    parts[0].interpretation = parts[0].interpretation.first(parts[0].interpretation.size() - 1);
    reject(std::span(parts).first(source.shape.part_count));
    Parts differently_sized(pool, schema, format, 16);
    parts = original;
    parts[0].interpretation = differently_sized.shape.parts[0].interpretation;
    if (format == v::Format::columns) reject(std::span(parts).first(source.shape.part_count));
    // The row layout is count-independent, so its valid metadata need not fail.
    if (format == v::Format::columns) {
      parts = original;
      parts[0].interpretation = original[2].interpretation;
      reject(std::span(parts).first(source.shape.part_count));
      // Same-width byte substitution cannot be authenticated by a codec. Prove
      // our logical oracle catches it even though shape/layout admission passes.
      parts = original;
      std::swap(parts[0].bytes, parts[1].bytes);
      auto swapped = take(v::ReadData::bind(schema, source.shape, std::span(parts).first(4)),
                          "physically valid same-width substitution failed binding");
      bool distinct = false;
      for (std::size_t row = 0; row < input.size(); ++row) {
        const auto observed = take(swapped.row(row), "swapped physical read failed");
        check(observed.key == input[row].record.next && observed.record.next == input[row].key,
              "same-width substitution test did not reach the actual payloads");
        distinct |= observed != input[row];
      }
      check(distinct, "independent logical oracle did not distinguish swapped 64-bit columns");
    }
    check(before.equals(source), "metadata refusal changed source payload");
    verify(schema, source, input);
  }
}

std::size_t expected_lower(std::span<const Row> input, Key key) {
  std::size_t row = 0;
  while (row < input.size() && input[row].key < key) ++row;
  return row;
}

void lookups(a::Pool& pool, const v::Schema& schema) {
  for (const auto count : {std::size_t{0}, std::size_t{1}, std::size_t{17},
                            std::size_t{65}, std::size_t{257}, std::size_t{1025}}) {
    const auto input = ordered(count);
    for (const auto format : {v::Format::rows, v::Format::columns}) {
      Parts source(pool, schema, format, count);
      construct(schema, source, input);
      auto data = source.bind(schema);
      std::vector<Key> queries{0, UINT64_MAX};
      for (const auto& row : input) {
        queries.push_back(row.key);
        queries.push_back(row.key + 1);
      }
      for (const auto key : queries) {
        const auto first = expected_lower(input, key);
        check(data.lower_bound(key) == first, "lower_bound differs from independent linear oracle");
        const auto actual = data.get(key);
        const auto hit = first < input.size() && input[first].key == key;
        check(actual.has_value() == hit, "get hit/miss differs from independent key oracle");
        if (hit) check(*actual == input[first].record, "get record differs from independent oracle");
        ++evidence.lookup_checks;
        std::array<Row, 16> output;
        const Row sentinel{0x123456789abcdef0ULL, {0xfedcba9876543210ULL, 0xdead, 0xbeef}};
        output.fill(sentinel);
        const auto returned = data.scan(key, output);
        const auto wanted = std::min(output.size(), input.size() - first);
        check(returned == wanted, "scan16 returned the wrong count");
        for (std::size_t row = 0; row < wanted; ++row) equal_row(output[row], input[first + row]);
        for (std::size_t row = wanted; row < output.size(); ++row)
          check(output[row] == sentinel, "scan16 changed surplus output rows");
        ++evidence.scans;
      }
      if (input.empty()) continue;
      for (const auto start : {std::size_t{0}, input.size() / 2, input.size() - 1}) {
        auto key = input[start].key;
        for (unsigned step = 0; step < 16; ++step) {
          const auto expected = std::find_if(input.begin(), input.end(),
              [&](const Row& row) { return row.key == key; });
          check(expected != input.end(), "dependent oracle chain escaped input");
          const auto actual = data.get(key);
          check(actual.has_value() && *actual == expected->record,
                "dependent16 step differs from independent record oracle");
          key = actual->next;
          ++evidence.dependent_steps;
        }
      }
    }
  }
}

// Force named source/copy/move records to die on a distinct stack frame. A
// cached endpoint borrowing one of their view addresses would escape here;
// only the stable Schema and actual Allocation-backed bytes remain live.
[[gnu::noinline]] v::ReadData binding_after_scope(const v::Schema& schema, const Parts& source) {
  const auto original = source.bind(schema);
  auto copied = original;
  auto moved = v::ReadData(std::move(copied));
  return v::ReadData(std::move(moved));
}

void moved_bindings(a::Pool& pool, const v::Schema& schema) {
  const Row sentinel{0x123456789abcdef0ULL, {0xfedcba9876543210ULL, 0xdead, 0xbeef}};
  for (const auto count : {std::size_t{0}, std::size_t{1}, std::size_t{129}}) {
    const auto input = ordered(count);
    for (const auto format : {v::Format::rows, v::Format::columns}) {
      Parts source(pool, schema, format, count);
      construct(schema, source, input);
      const Image before(source);
      auto data = binding_after_scope(schema, source);
      check(data.size() == input.size() && data.format() == format,
            "moved bound data lost count/format after source scope ended");
      for (std::size_t row = 0; row < input.size(); ++row) {
        equal_row(take(data.row(row), "moved checked row failed"), input[row]);
        equal_row(data.row_unchecked(row), input[row]);
      }
      for (unsigned index = 0; index < v::field_count; ++index) {
        std::vector<std::uint64_t> output(input.size(), UINT64_MAX);
        check(data.field(v::Field(index), 0, output).has_value(), "moved field range failed");
        for (std::size_t row = 0; row < input.size(); ++row) {
          check(output[row] == field(input[row], index), "moved field differs from independent oracle");
          ++evidence.logical_fields;
        }
      }
      for (const auto at : {input.size(), SIZE_MAX}) {
        check(!data.row(at), "moved out-of-range row was accepted");
        std::array<std::uint64_t, 2> output{0x123456789abcdef0ULL, 0xfedcba9876543210ULL};
        const auto unchanged = output;
        check(!data.field(v::Field::value, at, output), "moved out-of-range field was accepted");
        check(output == unchanged, "moved rejected field range changed output");
        ++evidence.refusals;
      }
      std::span<std::uint64_t> empty;
      check(data.field(v::Field::tag, input.size(), empty).has_value(),
            "moved empty end field range failed");
      std::array<std::uint64_t, 1> invalid_field_output{0x123456789abcdef0ULL};
      check(!data.field(v::Field(v::field_count), 0, invalid_field_output) &&
                invalid_field_output[0] == 0x123456789abcdef0ULL,
            "moved invalid field changed output or was accepted");
      ++evidence.refusals;
      std::vector<Key> queries{0, UINT64_MAX};
      for (const auto& row : input) {
        queries.push_back(row.key);
        queries.push_back(row.key + 1);
      }
      for (const auto key : queries) {
        const auto first = expected_lower(input, key);
        check(data.lower_bound(key) == first, "moved seek differs from independent linear oracle");
        const auto got = data.get(key);
        const bool hit = first < input.size() && input[first].key == key;
        check(got.has_value() == hit, "moved get hit/miss differs");
        if (hit) check(*got == input[first].record, "moved get record differs");
        ++evidence.lookup_checks;
        std::array<Row, 16> output;
        output.fill(sentinel);
        const auto returned = data.scan(key, output);
        const auto wanted = std::min(output.size(), input.size() - first);
        check(returned == wanted, "moved scan16 returned wrong count");
        for (std::size_t row = 0; row < wanted; ++row) equal_row(output[row], input[first + row]);
        for (std::size_t row = wanted; row < output.size(); ++row)
          check(output[row] == sentinel, "moved scan16 changed surplus output");
        ++evidence.scans;
      }
      std::span<Row> empty_scan;
      check(data.scan(0, empty_scan) == 0, "moved empty scan returned rows");
      check(before.equals(source), "moved native reads changed retained source bytes");
      ++evidence.ended_scope_bindings;
    }
  }
}

[[gnu::noinline]] v::TupleCursor tuple_cursor_after_scope(const v::TupleControl& control,
                                                        const Parts& source) {
  const auto parts = source.reads();
  const auto original = take(v::qualify_tuple(control, source.shape.interpretation,
      std::span(parts).first(source.shape.part_count)), "thin Tuple qualification failed");
  auto copied = original;
  auto moved = v::TupleCursor(std::move(copied));
  return v::TupleCursor(std::move(moved));
}
[[gnu::noinline]] v::ColumnCursors column_cursors_after_scope(const v::Schema& schema,
    const v::ColumnControl& control, const Parts& source) {
  const auto parts = source.reads();
  const auto original = take(v::qualify_columns(schema, control, source.shape.interpretation,
      std::span(parts).first(source.shape.part_count)), "thin column qualification failed");
  auto copied = original;
  auto moved = v::ColumnCursors(std::move(copied));
  return v::ColumnCursors(std::move(moved));
}
template<class Cursor, class Control>
[[gnu::noinline]] auto thin_body_after_scope(Cursor cursor, const Control& control) {
  const auto original = cursor.open(control);
  auto copied = original;
  using Body = decltype(copied);
  auto moved = Body(std::move(copied));
  return Body(std::move(moved));
}

template<class Body> void exercise_thin_body(const Body& body, std::span<const Row> input) {
  check(body.size() == input.size(), "thin body changed qualified count");
  for (std::size_t row = 0; row < input.size(); ++row) {
    equal_row(take(body.row(row), "thin checked row failed"), input[row]);
    equal_row(body.row_unchecked(row), input[row]);
    for (unsigned index = 0; index < v::field_count; ++index) {
      check(body.field_unchecked(v::Field(index), row) == field(input[row], index),
            "thin unchecked field differs from independent oracle");
      ++evidence.logical_fields;
    }
  }
  for (unsigned index = 0; index < v::field_count; ++index) {
    std::vector<std::uint64_t> output(input.size(), UINT64_MAX);
    check(body.field(v::Field(index), 0, output).has_value(), "thin field range failed");
    for (std::size_t row = 0; row < input.size(); ++row) {
      check(output[row] == field(input[row], index), "thin field range differs from independent oracle");
      ++evidence.logical_fields;
    }
  }
  for (const auto at : {input.size(), SIZE_MAX}) {
    check(!body.row(at), "thin out-of-range row accepted");
    std::array<std::uint64_t, 2> output{0x123456789abcdef0ULL, 0xfedcba9876543210ULL};
    const auto before = output;
    check(!body.field(v::Field::next, at, output), "thin out-of-range field accepted");
    check(output == before, "thin rejected field changed output");
    ++evidence.refusals;
  }
  std::span<std::uint64_t> empty;
  check(body.field(v::Field::tag, input.size(), empty).has_value(), "thin empty end field failed");
  std::array<std::uint64_t, 1> invalid{0x123456789abcdef0ULL};
  check(!body.field(v::Field(v::field_count), 0, invalid) && invalid[0] == 0x123456789abcdef0ULL,
        "thin invalid field accepted or changed output");
  ++evidence.refusals;
  const Row sentinel{0x123456789abcdef0ULL, {0xfedcba9876543210ULL, 0xdead, 0xbeef}};
  std::vector<Key> queries{0, UINT64_MAX};
  for (const auto& row : input) {
    queries.push_back(row.key);
    queries.push_back(row.key + 1);
  }
  for (const auto key : queries) {
    const auto first = expected_lower(input, key);
    check(body.lower_bound(key) == first, "thin seek differs from independent unsigned oracle");
    const auto actual = body.get(key);
    const bool hit = first < input.size() && input[first].key == key;
    check(actual.has_value() == hit, "thin get hit/miss differs");
    if (hit) check(*actual == input[first].record, "thin get record differs from independent oracle");
    ++evidence.lookup_checks;
    std::array<Row, 16> output;
    output.fill(sentinel);
    const auto returned = body.scan(key, output);
    const auto wanted = std::min(output.size(), input.size() - first);
    check(returned == wanted, "thin scan16 count differs");
    for (std::size_t row = 0; row < wanted; ++row) equal_row(output[row], input[first + row]);
    for (std::size_t row = wanted; row < output.size(); ++row)
      check(output[row] == sentinel, "thin scan16 changed surplus rows");
    ++evidence.scans;
  }
  std::span<Row> empty_scan;
  check(body.scan(0, empty_scan) == 0, "thin empty scan returned records");
}

void thin_cursors(a::Pool& pool, const v::Schema& schema) {
  check(sizeof(v::TupleCursor) == 8 && sizeof(v::ColumnCursors) == 32 &&
            sizeof(v::TupleBody) == 24 && sizeof(v::ColumnBody) == 40,
        "thin cursor/body ABI differs from bounded representation");
  for (const auto count : {std::size_t{0}, std::size_t{1}, std::size_t{129}}) {
    const auto input = ordered(count);
    for (const auto format : {v::Format::rows, v::Format::columns}) {
      Parts source(pool, schema, format, count);
      construct(schema, source, input);
      const Image before(source);
      const v::TupleControl tuple_control{schema, count};
      const v::ColumnControl column_control{count};
      const auto original = source.reads();
      const auto selected = std::span(original).first(source.shape.part_count);
      // Cursor and body copies both leave ended stack frames before native use;
      // the exact matching control, Schema and backing remain live here.
      if (format == v::Format::rows) {
        const auto cursor = tuple_cursor_after_scope(tuple_control, source);
        const auto body = thin_body_after_scope(cursor, tuple_control);
        exercise_thin_body(body, input);
        const v::TupleControl wrong_count{schema, count + 1};
        check(!v::qualify_tuple(wrong_count, source.shape.interpretation, selected),
              "thin Tuple qualified mismatched control count");
        check(!v::qualify_columns(schema, column_control, source.shape.interpretation, selected),
              "thin columns qualified row-format metadata");
      } else {
        const auto cursor = column_cursors_after_scope(schema, column_control, source);
        const auto body = thin_body_after_scope(cursor, column_control);
        exercise_thin_body(body, input);
        const v::ColumnControl wrong_count{count + 1};
        check(!v::qualify_columns(schema, wrong_count, source.shape.interpretation, selected),
              "thin columns qualified mismatched control count");
        check(!v::qualify_tuple(tuple_control, source.shape.interpretation, selected),
              "thin Tuple qualified column-format metadata");
      }
      evidence.thin_qualification_refusals += 2;
      auto reject = [&](orbital::Bytes parent, std::span<const v::ReadPart> parts) {
        if (format == v::Format::rows)
          check(!v::qualify_tuple(tuple_control, parent, parts), "thin Tuple admitted corrupt closure");
        else
          check(!v::qualify_columns(schema, column_control, parent, parts),
                "thin columns admitted corrupt closure");
        ++evidence.thin_qualification_refusals;
      };
      auto wrong_parts = original;
      wrong_parts[0].id.low ^= 0x100;
      reject(source.shape.interpretation, std::span(wrong_parts).first(source.shape.part_count));
      reject(source.shape.interpretation, selected.first(selected.size() - 1));
      auto parent = source.shape.interpretation;
      parent.back() = std::byte{1};
      reject(parent, selected);
      auto metadata = std::vector<std::byte>(original[0].interpretation.begin(), original[0].interpretation.end());
      metadata[0] ^= std::byte{1};
      wrong_parts = original;
      wrong_parts[0].interpretation = metadata;
      reject(source.shape.interpretation, std::span(wrong_parts).first(source.shape.part_count));
      if (count) {
        wrong_parts = original;
        wrong_parts[0].bytes = wrong_parts[0].bytes.first(wrong_parts[0].bytes.size() - 1);
        reject(source.shape.interpretation, std::span(wrong_parts).first(source.shape.part_count));
      }
      check(before.equals(source), "thin qualification/native reads changed backing");
      wire(source, input);
      ++evidence.thin_cursor_cases;
    }
  }
}

void misaligned_column(a::Pool& pool, const v::Schema& schema) {
  const auto input = arbitrary(17);
  Parts source(pool, schema, v::Format::columns, input.size());
  construct(schema, source, input);
  const Image before(source);
  // Exact extent, correct ID/descriptor/count and initialized accessible bytes;
  // only the late value-column's required 64-byte placement is violated.
  const auto extent = source.shape.parts[2].body_bytes;
  auto shifted = take(a::allocate(pool,
      {.bytes = extent + 64, .alignment = 64, .initialization = a::Initialization::uninitialized}),
      "misaligned admission fixture Allocation failed");
  std::memcpy(shifted.address + 1, source.backing[2].address, extent);
  auto parts = source.reads();
  parts[2].bytes = {shifted.address + 1, extent};
  check(parts[2].bytes.size() == extent &&
            reinterpret_cast<std::uintptr_t>(parts[2].bytes.data()) % 64 == 1,
        "misalignment fixture did not isolate address admission");
  const auto selected = std::span(parts).first(source.shape.part_count);
  check(!v::ReadData::bind(schema, source.shape, selected),
        "Shape bind accepted exact-sized misaligned Series column");
  check(!v::ReadData::bind(schema, orbital::Bytes{source.shape.interpretation}, selected),
        "parent interpretation bind accepted exact-sized misaligned Series column");
  const v::ColumnControl control{input.size()};
  check(!v::qualify_columns(schema, control, source.shape.interpretation, selected),
        "thin cursor qualification accepted exact-sized misaligned Series column");
  ++evidence.thin_qualification_refusals;
  evidence.metadata_refusals += 2;
  evidence.misaligned_column_refusals += 2;
  check(before.equals(source), "misalignment admission changed original payload");
  verify(schema, source, input);
}

void updates(a::Pool& pool, const v::Schema& schema) {
  const auto input = arbitrary(65);
  for (const auto format : {v::Format::rows, v::Format::columns}) {
    Parts source(pool, schema, format, input.size());
    construct(schema, source, input);
    const Image source_image(source);
    for (const auto copied : {false, true}) {
      Parts target(pool, schema, format, input.size());
      if (copied) {
        for (unsigned part = 0; part < target.shape.part_count; ++part)
          std::copy(a::bytes(source.backing[part]).begin(), a::bytes(source.backing[part]).end(),
                    a::bytes(target.backing[part]).begin());
      } else construct(schema, target, input);
      auto expected = input;
      std::vector<std::uint8_t> spare(input.size());
      if (format == v::Format::rows) {
        for (const auto row : {std::size_t{0}, std::size_t{32}, std::size_t{64}}) {
          spare[row] = std::uint8_t(1 + row % 15);
          target.backing[0].address[row * v::tuple_bytes + 18] |= std::byte(spare[row] << 4);
        }
      }
      auto parts = target.writes();
      for (const auto row : {std::size_t{0}, std::size_t{7}, std::size_t{8}, std::size_t{32},
                             std::size_t{63}, std::size_t{64}}) {
        const Record replacement{scramble(row + 300), std::uint32_t(scramble(row + 400)) &
            orbital_bench::value_mask, std::uint32_t(scramble(row + 500))};
        v::KernelCosts costs;
        check(v::update_into(schema, target.shape, std::span(parts).first(target.shape.part_count),
                             row, replacement, costs).has_value(), "valid private update failed");
        expected[row].record = replacement;
        add(costs);
        ++evidence.updates;
        verify(schema, target, expected, format == v::Format::rows ? std::span(spare) :
                                                                    std::span<const std::uint8_t>{});
      }
      const Image before(target);
      v::KernelCosts invalid_costs;
      check(!v::update_into(schema, target.shape, std::span(parts).first(target.shape.part_count),
                            64, {UINT64_MAX, orbital_bench::value_mask + 1, UINT32_MAX}, invalid_costs),
            "out-of-width private update was accepted");
      check(before.equals(target), "invalid value update changed private bytes");
      ++evidence.refusals;
      for (const auto row : {input.size(), SIZE_MAX}) {
        v::KernelCosts costs;
        check(!v::update_into(schema, target.shape, std::span(parts).first(target.shape.part_count),
                              row, {}, costs), "out-of-range private update was accepted");
        check(before.equals(target), "invalid range update changed private bytes");
        ++evidence.refusals;
      }
      check(source_image.equals(source), "private update changed its source copy");
    }
  }
}

void construction_refusals(a::Pool& pool, const v::Schema& schema) {
  const auto input = arbitrary(17);
  for (const auto format : {v::Format::rows, v::Format::columns}) {
    Parts target(pool, schema, format, input.size());
    target.fill(std::byte{0xa5});
    const Image before(target);
    auto parts = target.writes();
    auto invalid = input;
    invalid.back().record.value = orbital_bench::value_mask + 1;
    v::KernelCosts costs;
    check(!v::construct_into(schema, target.shape, std::span(parts).first(target.shape.part_count),
                             invalid, costs), "late out-of-width construction value was accepted");
    check(before.equals(target), "invalid late construction value changed private bytes");
    ++evidence.refusals;
    costs = {};
    check(!v::construct_into(schema, target.shape, std::span(parts).first(target.shape.part_count),
                             std::span(input).first(16), costs), "short construction input was accepted");
    check(before.equals(target), "invalid construction extent changed private bytes");
    ++evidence.refusals;
    auto shortened = parts;
    shortened[0].bytes = shortened[0].bytes.first(shortened[0].bytes.size() - 1);
    costs = {};
    check(!v::construct_into(schema, target.shape, std::span(shortened).first(target.shape.part_count),
                             input, costs), "short private Part body was accepted");
    check(before.equals(target), "short target Part changed private bytes");
    ++evidence.refusals;
  }
}

void partial_failures(a::Pool& pool, const v::Schema& schema) {
  const auto input = arbitrary(257);
  for (const auto initial : {v::Format::rows, v::Format::columns}) {
    Parts source(pool, schema, initial, input.size());
    construct(schema, source, input);
    const Image source_image(source);
    const auto format = initial == v::Format::rows ? v::Format::columns : v::Format::rows;
    for (const auto converting : {false, true}) {
      for (const auto options : {v::ConstructionOptions{.fail_after_rows = 128},
                                 v::ConstructionOptions{.fail_after_parts = 0},
                                 v::ConstructionOptions{.fail_after_parts = 1}}) {
        if (format == v::Format::rows && options.fail_after_parts == 1) continue;
        {
          Parts private_target(pool, schema, format, input.size());
          private_target.fill(std::byte{0xa5});
          const Image untouched(private_target);
          auto parts = private_target.writes();
          v::KernelCosts costs;
          auto data = source.bind(schema);
          const auto result = converting ? v::convert_into(schema, data, private_target.shape,
              std::span(parts).first(private_target.shape.part_count), costs, options) :
              v::construct_into(schema, private_target.shape,
                  std::span(parts).first(private_target.shape.part_count), input, costs, options);
          check(!result, "injected private construction returned complete");
          if (options.fail_after_parts == 0) {
            check(untouched.equals(private_target), "zero completed Parts limit issued private writes");
          } else {
            check(!untouched.equals(private_target) && costs.encoded_values != 0 &&
                      costs.issued_bytes != 0, "injection did not follow actual private construction");
            if (options.fail_after_rows == 128) {
              if (format == v::Format::rows) {
                check(costs.completed_rows == 128, "row prefix frontier differs from injection");
                for (std::size_t row = 0; row < 128; ++row)
                  row_at(a::bytes(private_target.backing[0]), row, input[row]);
              } else {
                column_prefix(a::bytes(private_target.backing[0]), 0, input, 128, false);
              }
            } else {
              check(costs.completed_parts == 1, "column completion frontier differs from injection");
              column_prefix(a::bytes(private_target.backing[0]), 0, input, input.size(), true);
            }
          }
          add(costs);
          ++evidence.refusals;
          check(source_image.equals(source), "partial private construction changed source");
        } // Discard incomplete private bytes; no rollback/publication claim.
        Parts retry(pool, schema, format, input.size());
        if (converting) convert(schema, source, retry); else construct(schema, retry, input);
        verify(schema, retry, input);
      }
    }
    verify(schema, source, input);
  }
}

void budget_refusal(a::Pool& pool, const v::Schema& schema) {
  Runtime constrained(1);
  const auto input = arbitrary(65);
  for (const auto format : {v::Format::rows, v::Format::columns}) {
    Parts source(pool, schema, format, input.size());
    construct(schema, source, input);
    const Image before(source);
    const auto target_format = format == v::Format::rows ? v::Format::columns : v::Format::rows;
    const auto target = take(v::shape(schema, target_format, input.size()), "target shape failed");
    const auto& first = target.parts[0];
    const auto refused = a::allocate(*constrained.pool,
        {.bytes = first.body_bytes, .alignment = first.alignment,
         .readable_padding = first.readable_padding,
         .initialization = a::Initialization::uninitialized});
    check(!refused, "real constrained allocation unexpectedly succeeded");
    ++evidence.refusals;
    check(before.equals(source), "target allocation refusal changed source");
    Parts retry(pool, schema, target_format, input.size());
    convert(schema, source, retry);
    verify(schema, retry, input);
  }
  constrained.finish();
}
} // namespace

int main() {
  Runtime runtime(128 * 1024 * 1024);
  const v::Schema schema;
  matrix(*runtime.pool, schema);
  metadata(*runtime.pool, schema);
  lookups(*runtime.pool, schema);
  moved_bindings(*runtime.pool, schema);
  thin_cursors(*runtime.pool, schema);
  misaligned_column(*runtime.pool, schema);
  updates(*runtime.pool, schema);
  construction_refusals(*runtime.pool, schema);
  partial_failures(*runtime.pool, schema);
  budget_refusal(*runtime.pool, schema);
  const auto allocations = a::statistics(*runtime.pool).allocations;
  runtime.finish();
  const auto& costs = evidence.costs;
  std::cout << "{\"status\":\"pass\",\"scope\":\"codec data with actual allocation; no Objects/Views qualification\""
            << ",\"format_count_cases\":" << evidence.cases
            << ",\"logical_field_checks\":" << evidence.logical_fields
            << ",\"independent_wire_bit_checks\":" << evidence.wire_bits
            << ",\"conversions\":" << evidence.conversions
            << ",\"private_updates\":" << evidence.updates
            << ",\"refusals\":" << evidence.refusals
            << ",\"metadata_refusals\":" << evidence.metadata_refusals
            << ",\"lookup_checks\":" << evidence.lookup_checks
            << ",\"scan16_checks\":" << evidence.scans
            << ",\"dependent16_steps\":" << evidence.dependent_steps
            << ",\"ended_scope_bindings\":" << evidence.ended_scope_bindings
            << ",\"misaligned_column_refusals\":" << evidence.misaligned_column_refusals
            << ",\"thin_cursor_cases\":" << evidence.thin_cursor_cases
            << ",\"thin_qualification_refusals\":" << evidence.thin_qualification_refusals
            << ",\"allocation_requests\":" << allocations
            << ",\"final_charged_bytes\":0,\"kernel_cost_totals\":{\"decoded_values\":" << costs.decoded_values
            << ",\"encoded_values\":" << costs.encoded_values
            << ",\"bindings\":" << costs.bindings
            << ",\"constructor_calls\":" << costs.constructor_calls
            << ",\"issued_bytes\":" << costs.issued_bytes
            << ",\"issued_spans\":" << costs.issued_spans
            << ",\"completed_parts\":" << costs.completed_parts
            << ",\"completed_rows\":" << costs.completed_rows
            << ",\"sum_call_scratch_bounds_bytes\":" << costs.scratch_bound_bytes << "}}\n";
}
