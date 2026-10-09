#include "fixture.h"
#include <ikea/seriespack/representation.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>

namespace {
namespace f = orbital_conversion_fixture;
namespace a = orbital::allocation;

struct Evidence {
  std::uint64_t cases{}, constructions{}, conversions{}, record_checks{}, bit_checks{};
  std::uint64_t failures{}, malformed_descriptions{}, retained_closures{};
  f::Costs construction, columns_to_rows, rows_to_columns, rejected;
} evidence;

void check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "orbital representation conversion: %s\n", message);
  std::abort();
}

template<class T, class E> T take(std::expected<T, E> result, const char* message) {
  check(result.has_value(), message);
  return std::move(*result);
}

void add(f::Costs& destination, const f::Costs& source) {
  destination.source_values += source.source_values;
  destination.target_values += source.target_values;
  destination.decode_bindings += source.decode_bindings;
  destination.construction_bindings += source.construction_bindings;
  destination.allocations += source.allocations;
  destination.allocated_body_bytes += source.allocated_body_bytes;
  destination.padding_bytes += source.padding_bytes;
  destination.issued_bytes += source.issued_bytes;
  destination.issued_spans += source.issued_spans;
  destination.completed_units += source.completed_units;
  destination.scratch_bytes += source.scratch_bytes;
  destination.descriptor_bytes += source.descriptor_bytes;
  destination.construction_calls += source.construction_calls;
  destination.guard_initialization_bytes += source.guard_initialization_bytes;
}

struct Runtime {
  orbital::worker::Lane lane;
  orbital::Ref<a::Arena> arena;
  a::Pool* pool{};
  bool closed{};
  explicit Runtime(std::uint64_t limit) {
    check(orbital::worker::adopt(lane).has_value(), "Lane adoption failed");
    arena = take(a::open_arena(limit), "Arena creation failed");
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
    check(arena->charged_bytes.load() == 0, "physical close retained a byte charge");
    check(a::statistics(*pool).metadata_bytes == 0, "physical close retained allocator metadata");
    check(a::remove_pool(*pool).has_value(), "closed Pool removal failed");
    pool = nullptr;
    orbital::worker::relinquish(lane);
    arena.reset();
    closed = true;
  }
  ~Runtime() { finish(); }
};

std::uint64_t mask(unsigned width) {
  return width == 64 ? UINT64_MAX : (std::uint64_t{1} << width) - 1;
}

std::uint64_t scramble(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

// This oracle never calls either pack, the converter, or a representation reader.
std::vector<f::Record> records(const f::Schema& schema, std::size_t count,
                               std::uint64_t seed) {
  std::vector<f::Record> result(count);
  for (std::size_t row = 0; row < count; ++row) {
    result[row].fill(UINT64_MAX); // Inactive application slots must be ignored.
    for (std::size_t field = 0; field < schema.widths.size(); ++field) {
      const auto width = schema.widths[field];
      std::uint64_t value;
      switch (row % 11) {
      case 0: value = 0; break;
      case 1: value = mask(width); break;
      case 2: value = std::uint64_t{1} << (width - 1); break;
      case 3: value = mask(width) ^ 1; break;
      case 4: value = 1; break;
      case 5: value = 0xaaaaaaaaaaaaaaaaULL; break;
      case 6: value = 0x5555555555555555ULL; break;
      default: value = scramble(seed + row * 0x10001 + field * 0x100000001ULL); break;
      }
      result[row][field] = value & mask(width);
    }
  }
  return result;
}

std::uint64_t little64(orbital::Bytes bytes, std::size_t first) {
  check(first <= bytes.size() && bytes.size() - first >= 8, "short interpretation integer");
  std::uint64_t value = 0;
  for (unsigned byte = 0; byte < 8; ++byte)
    value |= std::uint64_t(std::to_integer<unsigned>(bytes[first + byte])) << (8 * byte);
  return value;
}

unsigned octet(orbital::Bytes bytes, std::size_t index) {
  check(index < bytes.size(), "wire oracle exceeded an admitted extent");
  return std::to_integer<unsigned>(bytes[index]);
}

void equal_records(std::span<const f::Record> actual, std::span<const f::Record> expected,
                   const f::Schema& schema) {
  check(actual.size() == expected.size(), "logical record count differs from oracle");
  for (std::size_t row = 0; row < expected.size(); ++row)
    for (std::size_t field = 0; field < schema.widths.size(); ++field) {
      check(actual[row][field] == expected[row][field], "logical field differs from oracle");
      ++evidence.record_checks;
    }
}

void padding(const f::Part& part) {
  if (part.body.empty()) return; // Empty Parts form no payload or padding address.
  check(part.readable_padding == 64, "fixture padding extent changed");
  for (std::size_t byte = 0; byte < part.readable_padding; ++byte)
    check(part.body.data()[part.body.size() + byte] == std::byte{},
          "initialized allocator padding changed");
}

void row_wire(const f::Description& description, const f::Schema& schema,
              std::span<const f::Record> expected) {
  const auto& part = description.parts[0];
  check(part.codec == f::Codec::tuplepack && part.width == 0,
        "row Part has the wrong interpretation kind");
  std::size_t bits = 0, codes = 0;
  for (const auto width : schema.widths) {
    codes += (bits % 8 + width + 7) / 8;
    bits += width;
  }
  const auto unit = (bits + 7) / 8;
  check(part.body.size() == expected.size() * unit, "row Part occupied extent differs");
  check(part.interpretation.size() == 6 + 3 * codes, "Tuple description length differs");
  check(octet(part.interpretation, 0) == 'T' && octet(part.interpretation, 1) == 'P' &&
            octet(part.interpretation, 2) == 1 && octet(part.interpretation, 3) == unit &&
            (octet(part.interpretation, 4) | (octet(part.interpretation, 5) << 8)) == codes,
        "Tuple description header differs from independent schema");
  std::size_t position = 0, rank = 0;
  for (const auto width : schema.widths) {
    unsigned remaining = width;
    while (remaining) {
      const auto fragment = std::min<unsigned>(remaining, 8 - position % 8);
      check(octet(part.interpretation, 6 + 3 * rank) == position / 8 &&
                octet(part.interpretation, 7 + 3 * rank) == position % 8 &&
                octet(part.interpretation, 8 + 3 * rank) == fragment,
            "Tuple code rank differs from independent physical bit order");
      position += fragment;
      remaining -= fragment;
      ++rank;
    }
  }
  for (std::size_t row = 0; row < expected.size(); ++row) {
    std::size_t first_bit = 0;
    for (std::size_t field = 0; field < schema.widths.size(); ++field) {
      for (unsigned bit = 0; bit < schema.widths[field]; ++bit) {
        const auto physical = first_bit + bit;
        check(((octet(part.body, row * unit + physical / 8) >> (physical % 8)) & 1) ==
                  ((expected[row][field] >> bit) & 1),
              "Tuple wire bit differs from independent record oracle");
        ++evidence.bit_checks;
      }
      first_bit += schema.widths[field];
    }
    for (std::size_t bit = bits; bit < 8 * unit; ++bit)
      check(((octet(part.body, row * unit + bit / 8) >> (bit % 8)) & 1) == 0,
            "Tuple constructor left nonzero unit spare bits");
  }
  padding(part);
}

void column_wire(const f::Description& description, const f::Schema& schema,
                 std::span<const f::Record> expected) {
  for (std::size_t field = 0; field < schema.widths.size(); ++field) {
    const auto& part = description.parts[field];
    const auto width = schema.widths[field];
    const unsigned heads = width >= 20 ? 16 : width >= 9 ? 8 : 0;
    const unsigned payload = width - heads, whole = payload / 8, residual = payload % 8;
    check(part.codec == f::Codec::seriespack && part.width == width,
          "column Part has the wrong width or codec");
    check(part.interpretation.size() == 88, "Series interpretation closure is incomplete");
    const auto* encoded = reinterpret_cast<const std::uint8_t*>(part.interpretation.data());
    auto format = ikea::seriespack::decode_descriptor({encoded, 40});
    check(format.has_value() && format->width == width && format->heads == heads &&
              format->storage == ikea::seriespack::geometry::local &&
              format->count == expected.size(),
          "Series descriptor differs from independently selected fixture format");
    std::array<std::size_t, 3> offsets{}, extents{}, used{payload, heads ? 8u : 0u,
                                                      heads == 16 ? 8u : 0u};
    for (unsigned plane = 0; plane < 3; ++plane) {
      offsets[plane] = little64(part.interpretation, 40 + 16 * plane);
      extents[plane] = little64(part.interpretation, 48 + 16 * plane);
      check(offsets[plane] <= part.body.size() &&
                extents[plane] <= part.body.size() - offsets[plane],
            "Series plane closure exceeds Part body");
      if (!used[plane])
        check(!extents[plane] && !format->strides[plane], "absent Series plane has an extent");
    }
    if (expected.empty()) {
      check(part.body.empty(), "empty Series Part unexpectedly allocated payload");
      continue;
    }
    check(part.alignment >= 64 && reinterpret_cast<std::uintptr_t>(part.body.data()) % 64 == 0,
          "Series backing alignment is not qualified");
    check(offsets[0] == 64, "Series payload prefix changed");
    std::vector<bool> occupied(part.body.size());
    const auto tiles = (expected.size() + 7) / 8;
    for (unsigned plane = 0; plane < 3; ++plane) {
      if (!used[plane]) continue;
      check(format->strides[plane] >= used[plane], "Series plane stride is short");
      if (heads)
        check(format->strides[plane] > used[plane], "wide Series fixture lost stride gaps");
      check((tiles - 1) * format->strides[plane] + used[plane] <= extents[plane],
            "Series final occupied tile is inaccessible");
      for (std::size_t tile = 0; tile < tiles; ++tile)
        for (std::size_t byte = 0; byte < used[plane]; ++byte) {
          const auto location = offsets[plane] + tile * format->strides[plane] + byte;
          check(!occupied[location], "Series plane occupied bytes overlap");
          occupied[location] = true;
        }
    }
    // Independent Local wire: whole payload bytes are row ordered; low residual
    // bits are octet bitplanes. Highest and next-highest bytes occupy head planes.
    for (std::size_t row = 0; row < tiles * 8; ++row) {
      const auto value = row < expected.size() ? expected[row][field] : 0;
      const auto tile = row / 8, lane = row % 8;
      for (unsigned bit = 0; bit < width; ++bit) {
        unsigned plane = 0, physical_bit;
        std::size_t within;
        if (bit < residual) {
          within = 8 * whole + bit;
          physical_bit = lane;
        } else if (bit < payload) {
          within = lane * whole + (bit - residual) / 8;
          physical_bit = (bit - residual) % 8;
        } else {
          plane = heads == 16 && bit < width - 8 ? 2 : 1;
          within = lane;
          physical_bit = bit - (plane == 2 ? width - 16 : width - 8);
        }
        const auto location = offsets[plane] + tile * format->strides[plane] + within;
        check(((octet(part.body, location) >> physical_bit) & 1) == ((value >> bit) & 1),
              "Series wire or final-tile slack differs from independent record oracle");
        ++evidence.bit_checks;
      }
    }
    for (std::size_t byte = 0; byte < occupied.size(); ++byte)
      if (!occupied[byte])
        check(part.body[byte] == std::byte{0xa5}, "Series constructor changed a prefix or gap");
    padding(part);
  }
}

void verify(const f::Representation& representation, const f::Schema& schema,
            std::span<const f::Record> expected, f::LogicalIdentity identity, f::Kind kind) {
  const auto description = representation.describe();
  check(description.logical == identity && description.kind == kind,
        "conversion changed logical identity or retained the wrong format");
  check(description.records == expected.size() &&
            std::equal(description.field_widths.begin(), description.field_widths.end(),
                       schema.widths.begin(), schema.widths.end()),
        "description count or schema closure differs");
  check(description.parts.size() == (kind == f::Kind::columns ? schema.widths.size() : 1),
        "conversion did not change Part count as required");
  for (std::size_t index = 0; index < description.parts.size(); ++index) {
    check(description.parts[index].records == expected.size(), "Part logical count differs");
    for (std::size_t prior = 0; prior < index; ++prior)
      check(description.parts[prior].id != description.parts[index].id,
            "Part ids collide within one descriptor");
  }
  equal_records(take(f::read_records(representation), "completed representation read failed"),
                expected, schema);
  equal_records(take(f::read_parts(description), "selected Parts read failed"), expected, schema);
  if (kind == f::Kind::rows) row_wire(description, schema, expected);
  else column_wire(description, schema, expected);
}

f::Completed construct(a::Pool& pool, f::LogicalIdentity identity, const f::Schema& schema,
                       std::span<const f::Record> input, f::Kind kind) {
  f::Costs costs;
  auto result = take(f::construct(pool, identity, schema, input, kind, costs),
                     "valid fixture construction failed");
  add(evidence.construction, costs);
  ++evidence.constructions;
  return result;
}

f::Completed convert(a::Pool& pool, const f::Representation& source, f::Kind target) {
  f::Costs costs;
  auto result = take(f::convert(pool, source, target, costs), "valid conversion failed");
  add(target == f::Kind::rows ? evidence.columns_to_rows : evidence.rows_to_columns, costs);
  ++evidence.conversions;
  return result;
}

void conversion_matrix(a::Pool& pool) {
  const std::array<unsigned, 10> widths{1, 7, 8, 9, 12, 20, 31, 32, 63, 64};
  std::vector<f::Schema> schemas;
  for (const auto width : widths) schemas.push_back({{width}});
  const std::array<unsigned, 8> low{1, 7, 8, 9, 12, 20, 31, 32};
  const std::array<unsigned, 8> high{64, 63, 32, 31, 20, 12, 9, 8};
  for (std::size_t fields = 1; fields <= f::max_fields; ++fields) {
    schemas.push_back({{low.begin(), low.begin() + fields}});
    schemas.push_back({{high.begin(), high.begin() + fields}});
  }
  schemas.push_back({std::vector<unsigned>(8, 64)});
  schemas.push_back({std::vector<unsigned>(8, 63)});
  const std::array<std::size_t, 20> counts{0, 1, 7, 8, 9, 15, 16, 17, 31, 32,
                                        33, 63, 64, 65, 127, 128, 129, 255, 256, 257};
  for (const auto& schema : schemas) {
    for (const auto count : counts) {
      ++evidence.cases;
      const f::LogicalIdentity identity{{0xc011, evidence.cases}, 17};
      const auto input = records(schema, count, evidence.cases);
      for (const auto initial : {f::Kind::columns, f::Kind::rows}) {
        const auto alternative = initial == f::Kind::columns ? f::Kind::rows : f::Kind::columns;
        auto source = construct(pool, identity, schema, input, initial);
        verify(*source, schema, input, identity, initial);
        auto target = convert(pool, *source, alternative);
        verify(*target, schema, input, identity, alternative);
        verify(*source, schema, input, identity, initial);
        auto roundtrip = convert(pool, *target, initial);
        verify(*roundtrip, schema, input, identity, initial);
      }
    }
  }
}

struct Snapshot {
  std::vector<std::vector<std::byte>> bodies, interpretations;
  explicit Snapshot(const f::Representation& representation) {
    for (const auto& part : representation.describe().parts) {
      bodies.emplace_back(part.body.begin(), part.body.end());
      interpretations.emplace_back(part.interpretation.begin(), part.interpretation.end());
    }
  }
  void unchanged(const f::Representation& representation) const {
    const auto parts = representation.describe().parts;
    check(parts.size() == bodies.size(), "failed target construction changed source Parts");
    for (std::size_t index = 0; index < parts.size(); ++index)
      check(std::equal(parts[index].body.begin(), parts[index].body.end(),
                       bodies[index].begin(), bodies[index].end()) &&
                std::equal(parts[index].interpretation.begin(), parts[index].interpretation.end(),
                           interpretations[index].begin(), interpretations[index].end()),
            "failed target construction changed source bytes or interpretation");
  }
};

void expect_failure(f::Result result, f::Error error) {
  check(!result && result.error() == error, "failed construction returned a target or wrong error");
  ++evidence.failures;
}

void failure_boundaries(a::Pool& pool) {
  const f::Schema schema{{1, 7, 9, 20}};
  const auto input = records(schema, 17, 808);
  const f::LogicalIdentity identity{{0xfa11, 808}, 23};
  for (const auto initial : {f::Kind::columns, f::Kind::rows}) {
    auto source = construct(pool, identity, schema, input, initial);
    const Snapshot snapshot(*source);
    const auto target = initial == f::Kind::columns ? f::Kind::rows : f::Kind::columns;
    for (const auto options : {f::BuildOptions{.fail_after_units = 1},
                               f::BuildOptions{.fail_after_allocations = 0},
                               f::BuildOptions{.reject_completion = true}}) {
      f::Costs costs;
      expect_failure(f::convert(pool, *source, target, costs, options),
                     options.reject_completion ? f::Error::incomplete : f::Error::injected);
      if (options.fail_after_units == 1)
        check(costs.completed_units == 1 && costs.construction_calls != 0,
              "prefix failure occurred before constructing a target unit");
      if (options.reject_completion)
        check(costs.completed_units == (target == f::Kind::columns ? schema.widths.size() : input.size()),
              "completion rejection did not follow complete target construction");
      add(evidence.rejected, costs);
      snapshot.unchanged(*source);
      verify(*source, schema, input, identity, initial);
    }
    if (target == f::Kind::columns) {
      f::Costs costs;
      expect_failure(f::convert(pool, *source, target, costs,
                               {.fail_after_allocations = 1}), f::Error::injected);
      check(costs.allocations == 1 && costs.completed_units == 1,
            "allocation failure did not follow a constructed target column");
      add(evidence.rejected, costs);
      snapshot.unchanged(*source);
    }
    auto retry = convert(pool, *source, target);
    verify(*retry, schema, input, identity, target);
    auto invalid = input;
    invalid.back()[schema.widths.size() - 1] = std::uint64_t{1} << schema.widths.back();
    f::Costs costs;
    expect_failure(f::construct(pool, identity, schema, invalid, target, costs), f::Error::value);
    add(evidence.rejected, costs);
    snapshot.unchanged(*source);
  }
}

void malformed_parts(a::Pool& pool) {
  const f::Schema schema{{7, 12, 31, 64}};
  const auto input = records(schema, 17, 999);
  const f::LogicalIdentity identity{{0xbad, 999}, 9};
  for (const auto kind : {f::Kind::columns, f::Kind::rows}) {
    auto owner = construct(pool, identity, schema, input, kind);
    const Snapshot snapshot(*owner);
    const auto original = owner->describe();
    auto reject = [&](std::vector<f::Part> parts, std::size_t count = 17) {
      auto description = original;
      description.records = count;
      description.parts = parts;
      const auto result = f::read_parts(description);
      check(!result && result.error() == f::Error::format,
            "malformed selected Parts were accepted or reported the wrong error");
      ++evidence.malformed_descriptions;
    };
    std::vector<f::Part> parts(original.parts.begin(), original.parts.end());
    ++parts.back().records;
    reject(parts); // A ragged selected column or row count cannot invent records.
    parts.assign(original.parts.begin(), original.parts.end());
    reject(parts, 16);
    parts.back().body = parts.back().body.first(parts.back().body.size() - 1);
    reject(parts);
    parts.assign(original.parts.begin(), original.parts.end());
    parts.pop_back();
    reject(parts);
    parts.assign(original.parts.begin(), original.parts.end());
    std::vector<std::byte> interpretation(parts.back().interpretation.begin(),
                                         parts.back().interpretation.end());
    interpretation[0] ^= std::byte{0xff};
    parts.back().interpretation = interpretation;
    reject(parts);
    parts.assign(original.parts.begin(), original.parts.end());
    parts.back().interpretation = parts.back().interpretation.first(
        parts.back().interpretation.size() - 1);
    reject(parts);
    if (kind == f::Kind::columns) {
      parts.assign(original.parts.begin(), original.parts.end());
      parts[0].interpretation = parts[1].interpretation; // Valid description, wrong column width.
      reject(parts);
    } else {
      const f::Schema different{{8, 9, 32, 63}};
      const auto different_input = records(different, 17, 999);
      auto other = construct(pool, identity, different, different_input, kind);
      parts.assign(original.parts.begin(), original.parts.end());
      parts[0].interpretation = other->describe().parts[0].interpretation;
      reject(parts); // Valid Tuple layout for a different interpretation.
    }
    snapshot.unchanged(*owner);
    verify(*owner, schema, input, identity, kind);
  }
}

void retained_interpretation(a::Pool& pool) {
  const f::LogicalIdentity identity{{0xc105, 42}, 42};
  for (const auto initial : {f::Kind::columns, f::Kind::rows}) {
    f::Completed retained;
    f::Description selected;
    {
      f::Schema temporary_schema{{63, 20, 9, 7, 1}};
      auto temporary_input = records(temporary_schema, 65, 42);
      auto ordinary = construct(pool, identity, temporary_schema, temporary_input, initial);
      retained = ordinary;
      selected = ordinary->describe();
      ordinary.reset();
    } // All external input/schema storage has ended before either read below.
    const f::Schema independent_schema{{63, 20, 9, 7, 1}};
    const auto independent_input = records(independent_schema, 65, 42);
    equal_records(take(f::read_parts(selected), "retained borrowed description lost its closure"),
                  independent_input, independent_schema);
    verify(*retained, independent_schema, independent_input, identity, initial);
    const auto target = initial == f::Kind::columns ? f::Kind::rows : f::Kind::columns;
    auto alternative = convert(pool, *retained, target);
    retained.reset(); // The new description must not borrow the converter's old input or source.
    verify(*alternative, independent_schema, independent_input, identity, target);
    ++evidence.retained_closures;
  }
}

void budget_refusal(a::Pool& source_pool) {
  const f::Schema schema{{7, 20, 64}};
  const auto input = records(schema, 65, 111);
  const f::LogicalIdentity identity{{0xb0d, 111}, 11};
  Runtime constrained(1); // Real allocator budget; no substituted syscalls or fake failure.
  for (const auto initial : {f::Kind::columns, f::Kind::rows}) {
    auto source = construct(source_pool, identity, schema, input, initial);
    const Snapshot snapshot(*source);
    const auto target = initial == f::Kind::columns ? f::Kind::rows : f::Kind::columns;
    f::Costs costs;
    expect_failure(f::convert(*constrained.pool, *source, target, costs), f::Error::allocation);
    add(evidence.rejected, costs);
    snapshot.unchanged(*source);
    verify(*source, schema, input, identity, initial);
    auto retry = convert(source_pool, *source, target);
    verify(*retry, schema, input, identity, target);
  }
  constrained.finish();
}

void print_costs(const f::Costs& costs) {
  std::cout << "{\"source_values\":" << costs.source_values
            << ",\"target_values\":" << costs.target_values
            << ",\"decode_bindings\":" << costs.decode_bindings
            << ",\"construction_bindings\":" << costs.construction_bindings
            << ",\"allocations\":" << costs.allocations
            << ",\"allocated_body_bytes\":" << costs.allocated_body_bytes
            << ",\"padding_bytes\":" << costs.padding_bytes
            << ",\"issued_bytes\":" << costs.issued_bytes
            << ",\"issued_spans\":" << costs.issued_spans
            << ",\"completed_units\":" << costs.completed_units
            << ",\"scratch_bytes\":" << costs.scratch_bytes
            << ",\"descriptor_bytes\":" << costs.descriptor_bytes
            << ",\"construction_calls\":" << costs.construction_calls
            << ",\"guard_initialization_bytes\":" << costs.guard_initialization_bytes << '}';
}
} // namespace

int main() {
  Runtime runtime(128 * 1024 * 1024);
  conversion_matrix(*runtime.pool);
  failure_boundaries(*runtime.pool);
  malformed_parts(*runtime.pool);
  retained_interpretation(*runtime.pool);
  budget_refusal(*runtime.pool);
  const auto allocations = a::statistics(*runtime.pool).allocations;
  runtime.finish();
  std::cout << "{\"status\":\"pass\",\"schema_count_cases\":" << evidence.cases
            << ",\"constructions\":" << evidence.constructions
            << ",\"conversions\":" << evidence.conversions
            << ",\"logical_field_checks\":" << evidence.record_checks
            << ",\"independent_wire_bit_checks\":" << evidence.bit_checks
            << ",\"rejected_constructions\":" << evidence.failures
            << ",\"malformed_descriptions\":" << evidence.malformed_descriptions
            << ",\"retained_closures\":" << evidence.retained_closures
            << ",\"allocator_allocations\":" << allocations
            << ",\"final_charged_bytes\":0,\"construction_costs\":";
  print_costs(evidence.construction);
  std::cout << ",\"columns_to_rows_costs\":";
  print_costs(evidence.columns_to_rows);
  std::cout << ",\"rows_to_columns_costs\":";
  print_costs(evidence.rows_to_columns);
  std::cout << ",\"rejected_costs\":";
  print_costs(evidence.rejected);
  std::cout << "}\n";
}
