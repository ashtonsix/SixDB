// Identical native caller for the archived base/candidate projections.
// Allocation, source admission, wire/readback oracles and release are untimed.
// Existing construct_into/convert_into calls include any local constructor factory.
#include "workbench/spikes/orbital-mvcc/consumer/data.h"
#include "workbench/spikes/orbital-mvcc/consumer/native.h"
#include <orbital/allocation.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>
#include <time.h>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
namespace v = orbital_bench::versioned;
namespace a = orbital::allocation;
using orbital_bench::Row;
using Image = std::array<std::vector<std::byte>, 4>;
constexpr unsigned repetitions = 3;
constexpr std::array<unsigned, 4> widths{64, 64, 20, 32};
std::uint64_t checked_rows{}, checked_wire_bytes{}, old_source_checks{};
bool closed_zero_charge{};
bool clocks_enabled{};

void check(bool condition, const char* message) {
  if (!condition) { std::fprintf(stderr, "schema phase probe: %s\n", message); std::abort(); }
}
template<class T, class E> T take(std::expected<T, E> result, const char* message) {
  if (!result) {
    std::fprintf(stderr, "schema phase probe: %s (error %u)\n", message, unsigned(result.error()));
    std::abort();
  }
  return std::move(*result);
}
void take(orbital::Result<void> result, const char* message) {
  if (!result) {
    std::fprintf(stderr, "schema phase probe: %s (error %u)\n", message, unsigned(result.error()));
    std::abort();
  }
}
std::uint64_t tick(clockid_t clock) {
  timespec value{};
  check(clock_gettime(clock, &value) == 0, "clock_gettime failed");
  return std::uint64_t(value.tv_sec) * 1000000000ull + std::uint64_t(value.tv_nsec);
}
struct Sample { std::uint64_t cpu{}, wall{}; };
template<class Call> Sample timed(Call&& call) {
  if (!clocks_enabled) { call(); return {}; }
  const auto cpu = tick(CLOCK_THREAD_CPUTIME_ID), wall = tick(CLOCK_MONOTONIC);
  call();
  const auto wall_end = tick(CLOCK_MONOTONIC), cpu_end = tick(CLOCK_THREAD_CPUTIME_ID);
  return {cpu_end - cpu, wall_end - wall};
}
struct Observation {
  const char* name;
  std::size_t rows{}, calls{};
  bool tuple_target{};
  std::array<Sample, repetitions> samples;
  v::KernelCosts costs{};
};
template<class Schema> constexpr bool retained_constructor = requires(const Schema& value) { value.constructor; };

struct Runtime {
  orbital::worker::Lane lane;
  orbital::Ref<a::Arena> arena;
  a::Pool* pool{};
  Runtime() {
    take(orbital::worker::adopt(lane), "Lane adopt");
    arena = take(a::open_arena(64 * 1024 * 1024), "Arena open");
    pool = take(a::add_pool(*arena, lane), "Pool add");
  }
  ~Runtime() {
    bool done{};
    for (unsigned attempt = 0; attempt < 65536; ++attempt) {
      const auto result = a::close(*pool);
      check(result.kind() != orbital::Progress::Kind::failed, "physical close failed");
      if (result.kind() == orbital::Progress::Kind::done) { done = true; break; }
      orbital::worker::poll(lane, 1024); std::this_thread::yield();
    }
    check(done, "physical close did not complete");
    closed_zero_charge = !arena->charged_bytes.load() && !a::statistics(*pool).metadata_bytes;
    check(closed_zero_charge, "physical close left charge");
    take(a::remove_pool(*pool), "Pool remove");
    arena.reset(); orbital::worker::relinquish(lane);
  }
};
std::uint64_t mix(std::uint64_t x) {
  x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull;
  x ^= x >> 27; x *= 0x94d049bb133111ebull;
  return x ^ (x >> 31);
}
std::vector<Row> input(std::size_t count, unsigned object = 0) {
  std::vector<Row> rows(count);
  for (std::size_t row = 0; row < count; ++row) {
    const auto seed = mix(row + object * 101 + 91);
    rows[row] = {0x8000000000000000ull + row * 37 + object,
      {0x4000000000000101ull + row * 53 + object,
       std::uint32_t(seed) & orbital_bench::value_mask, std::uint32_t(seed >> 32)}};
    if (row % 7 == 0) rows[row].record.value = 0;
    if (row % 7 == 1) rows[row].record.value = orbital_bench::value_mask;
  }
  return rows;
}
std::uint64_t field(const Row& row, unsigned index) {
  switch (index) {
  case 0: return row.key; case 1: return row.record.next;
  case 2: return row.record.value; case 3: return row.record.tag;
  }
  check(false, "oracle field index"); return 0;
}
std::size_t part_bytes(v::Format format, unsigned index, std::size_t rows) {
  return format == v::Format::rows ? rows * 23 : ((rows + 7) / 8) * widths[index];
}
// Independent bit law: Tuple uses byte ranks; Series Local puts residual bits
// in one lane-bit byte per residual plane, before each lane's whole-byte value.
std::pair<std::size_t, unsigned> location(v::Format format, unsigned index,
    std::size_t row, unsigned bit) {
  if (format == v::Format::rows) {
    constexpr std::array<unsigned, 4> first{0, 64, 128, 152};
    const auto physical = first[index] + bit;
    return {row * 23 + physical / 8, physical % 8};
  }
  const auto whole = widths[index] / 8, residual = widths[index] % 8;
  return {row / 8 * widths[index] + (bit < residual ? 8 * whole + bit :
      row % 8 * whole + (bit - residual) / 8),
      bit < residual ? unsigned(row % 8) : (bit - residual) % 8};
}
void wire_value(Image& image, v::Format format, unsigned index, std::size_t row, std::uint64_t value) {
  auto& bytes = image[format == v::Format::rows ? 0 : index];
  for (unsigned bit = 0; bit < widths[index]; ++bit) {
    const auto [offset, shift] = location(format, index, row, bit);
    check(offset < bytes.size(), "oracle write extent");
    const auto old = std::to_integer<unsigned>(bytes[offset]);
    bytes[offset] = std::byte((old & ~(1u << shift)) | unsigned((value >> bit) & 1) << shift);
  }
}
Image expected_image(v::Format format, std::span<const Row> rows) {
  Image result;
  for (unsigned part = 0; part < (format == v::Format::rows ? 1u : 4u); ++part)
    result[part].resize(part_bytes(format, part, rows.size()));
  for (std::size_t row = 0; row < rows.size(); ++row)
    for (unsigned index = 0; index < 4; ++index) wire_value(result, format, index, row, field(rows[row], index));
  return result;
}
void salt_spare(Image& image, v::Format format, std::size_t rows) {
  if (format == v::Format::rows) {
    for (std::size_t row = 0; row < rows; ++row)
      image[0][row * 23 + 18] |= std::byte(unsigned(row % 15 + 1) << 4);
  } else {
    for (std::size_t row = rows; row < (rows + 7) / 8 * 8; ++row)
      for (unsigned index = 0; index < 4; ++index) wire_value(image, format, index, row, mix(row + index * 173));
  }
}
struct Parts {
  v::Shape shape;
  std::array<a::Allocation, 4> owners;
  std::array<a::Hold, 4> holds;
  std::array<v::WritePart, 4> writes{};
  Parts(a::Pool& pool, const v::Schema& schema, v::Format format, std::size_t rows)
      : shape(take(v::shape(schema, format, rows), "fixture Shape")) {
    for (unsigned part = 0; part < shape.part_count; ++part) {
      const auto& required = shape.parts[part];
      check(required.body_bytes == part_bytes(format, part, rows), "independent physical extent");
      owners[part] = take(a::allocate(pool, {.bytes = required.body_bytes, .alignment = required.alignment,
          .readable_padding = required.readable_padding, .initialization = a::Initialization::uninitialized}), "Allocation");
      writes[part] = {required.id, a::bytes(owners[part])};
    }
  }
  auto targets() const { return std::span(writes).first(shape.part_count); }
  orbital::Bytes bytes(unsigned part) const {
    if (holds[part]) return {holds[part].address, std::size_t(holds[part].footprint.size)};
    return a::bytes(owners[part]);
  }
  Image image() const {
    Image result;
    for (unsigned part = 0; part < shape.part_count; ++part) {
      const auto body = bytes(part); result[part].assign(body.begin(), body.end());
    }
    return result;
  }
  void protect(const Image& image) {
    for (unsigned part = 0; part < shape.part_count; ++part) {
      std::memcpy(owners[part].address, image[part].data(), image[part].size());
      holds[part] = take(a::hold(owners[part], {0, owners[part].size}), "source Hold");
      a::release(owners[part]); writes[part] = {};
    }
    // Hold supplies physical retention only. This caller now authorizes reads
    // and issues no further writes; no snapshot/publication authority is claimed.
  }
  v::ReadData bind(const v::Schema& schema) const {
    std::array<v::ReadPart, 4> read{};
    for (unsigned part = 0; part < shape.part_count; ++part)
      read[part] = {shape.parts[part].id, bytes(part), shape.parts[part].interpretation};
    return take(v::ReadData::bind(schema, shape.interpretation, std::span(read).first(shape.part_count)), "generic bind");
  }
};
void logical(const v::ReadData& data, std::span<const Row> rows) {
  check(data.size() == rows.size(), "generic count");
  for (std::size_t row = 0; row < rows.size(); ++row) {
    check(take(data.row(row), "generic row") == rows[row], "independent logical row");
    ++checked_rows;
  }
  const auto first = rows.size() > 128 ? 127 : 0;
  std::vector<std::uint64_t> values(std::min<std::size_t>(129, rows.size() - first));
  for (unsigned index = 0; index < 4; ++index) {
    take(data.field(static_cast<v::Field>(index), first, values), "generic field range");
    for (std::size_t i = 0; i < values.size(); ++i)
      check(values[i] == field(rows[first + i], index), "independent field range");
  }
}
void validate(const Parts& parts, const v::Schema& schema, std::span<const Row> rows, const Image& expected) {
  const auto actual = parts.image();
  check(actual == expected, "full independent wire image, including spare/slack");
  for (const auto& part : actual) checked_wire_bytes += part.size();
  logical(parts.bind(schema), rows);
}
Observation construct(a::Pool& pool, const v::Schema& schema, v::Format format,
    std::size_t count, std::size_t calls, const char* name) {
  std::vector<std::vector<Row>> rows;
  std::vector<Image> expected;
  std::vector<Parts> target;
  rows.reserve(calls); expected.reserve(calls); target.reserve(calls);
  for (unsigned object = 0; object < calls; ++object) {
    rows.push_back(input(count, object)); expected.push_back(expected_image(format, rows.back()));
    target.emplace_back(pool, schema, format, count);
  }
  v::KernelCosts warm{};
  for (std::size_t object = 0; object < calls; ++object) {
    take(v::construct_into(schema, target[object].shape, target[object].targets(), rows[object], warm), "construct warmup");
    validate(target[object], schema, rows[object], expected[object]);
  }
  Observation result{name, count, calls, format == v::Format::rows, {}, {}};
  for (unsigned rep = 0; rep < repetitions; ++rep) {
    v::KernelCosts costs{};
    result.samples[rep] = timed([&] {
      for (std::size_t object = 0; object < calls; ++object)
        take(v::construct_into(schema, target[object].shape, target[object].targets(), rows[object], costs), "timed construct");
    });
    for (std::size_t object = 0; object < calls; ++object)
      validate(target[object], schema, rows[object], expected[object]);
    result.costs = costs;
  }
  return result;
}
Observation convert(a::Pool& pool, const v::Schema& schema, v::Format source_format,
    std::size_t count, const char* name) {
  const auto target_format = source_format == v::Format::rows ? v::Format::columns : v::Format::rows;
  const auto rows = input(count);
  const auto expected = expected_image(target_format, rows);
  Parts source(pool, schema, source_format, count), target(pool, schema, target_format, count);
  v::KernelCosts warm{};
  take(v::construct_into(schema, source.shape, source.targets(), rows, warm), "source construction");
  validate(source, schema, rows, expected_image(source_format, rows));
  auto original = source.image(); salt_spare(original, source_format, count); source.protect(original);
  const auto bound = source.bind(schema); logical(bound, rows);
  take(v::convert_into(schema, bound, target.shape, target.targets(), warm), "conversion warmup");
  validate(target, schema, rows, expected);
  Observation result{name, count, 1, target_format == v::Format::rows, {}, {}};
  for (unsigned rep = 0; rep < repetitions; ++rep) {
    v::KernelCosts costs{};
    result.samples[rep] = timed([&] {
      take(v::convert_into(schema, bound, target.shape, target.targets(), costs), "timed conversion");
    });
    validate(target, schema, rows, expected);
    check(source.image() == original, "held old source changed"); ++old_source_checks;
    logical(bound, rows);
    result.costs = costs;
  }
  return result;
}
template<class T> void packet(const char* name, bool& first) {
  if (!std::exchange(first, false)) std::cout << ',';
  std::cout << "{\"type\":\"" << name << "\",\"bytes\":" << sizeof(T)
            << ",\"alignment\":" << alignof(T) << '}';
}
void sizes() {
  bool first = true;
  std::cout << "\"schema_retains_constructor\":" << (retained_constructor<v::Schema> ? "true" : "false") << ",\"packets\":[";
  packet<v::Schema>("Schema", first); packet<v::tp::constructor>("TupleConstructor", first);
  packet<std::expected<v::tp::constructor, v::tp::error>>("ExpectedTupleConstructor", first);
  packet<v::tp::layout>("TupleLayout", first); packet<v::tp::reader<8>>("TupleReader8", first);
  packet<v::tp::writer<8>>("TupleWriter8", first); packet<v::tp::writer<64>>("TupleWriter64", first);
  packet<decltype(v::Schema::readers)>("TupleReaders4", first);
  packet<decltype(v::Schema::writers)>("TupleWriters4", first);
  packet<v::ReadData>("ReadData", first); packet<v::KernelCosts>("KernelCosts", first);
  packet<v::tp::construction_input>("ConstructionInput", first);
  packet<v::tp::construction_operation>("ConstructionOperation", first);
  std::cout << ']';
}
void observation(const Observation& value) {
  std::array<std::uint64_t, repetitions> cpu{}, wall{};
  for (unsigned i = 0; i < repetitions; ++i) { cpu[i] = value.samples[i].cpu; wall[i] = value.samples[i].wall; }
  std::sort(cpu.begin(), cpu.end()); std::sort(wall.begin(), wall.end());
  const auto& k = value.costs;
  std::cout << "{\"operation\":\"" << value.name << "\",\"rows_per_body\":" << value.rows
      << ",\"complete_calls_per_sample\":" << value.calls
      << ",\"median_thread_cpu_ns_per_complete_call\":";
  if (clocks_enabled) std::cout << double(cpu[1]) / value.calls; else std::cout << "null";
  std::cout << ",\"median_wall_ns_per_complete_call\":";
  if (clocks_enabled) std::cout << double(wall[1]) / value.calls; else std::cout << "null";
  std::cout << ",\"constructor_factories_in_sample_source_inferred_not_instrumented\":"
      << (std::string_view(value.name) == "schema_setup"
          ? (retained_constructor<v::Schema> ? value.calls : 0)
          : (!retained_constructor<v::Schema> && value.tuple_target ? value.calls : 0))
      << ",\"samples_total_ns\":[";
  for (unsigned i = 0; clocks_enabled && i < repetitions; ++i) {
    if (i) std::cout << ',';
    std::cout << "{\"thread_cpu\":" << value.samples[i].cpu << ",\"wall\":" << value.samples[i].wall << '}';
  }
  std::cout << "],\"last_sample_logical_kernel_costs\":{\"decoded_values\":" << k.decoded_values
      << ",\"encoded_values\":" << k.encoded_values << ",\"bindings\":" << k.bindings
      << ",\"constructor_calls\":" << k.constructor_calls << ",\"issued_bytes\":" << k.issued_bytes
      << ",\"issued_spans\":" << k.issued_spans << ",\"completed_parts\":" << k.completed_parts
      << ",\"completed_rows\":" << k.completed_rows << ",\"scratch_bound_bytes\":" << k.scratch_bound_bytes << "}}";
}
} // namespace
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--sizes-only") {
    std::cout << '{'; sizes(); std::cout << "}\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--benchmark") clocks_enabled = true;
  else check(argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--check-only"),
      "usage: schema_phase_probe [--sizes-only|--check-only|--benchmark]");
  Observation setup{"schema_setup", 0, 1, false, {}, {}};
  for (unsigned rep = 0; rep < repetitions; ++rep) {
    std::optional<v::Schema> schema;
    setup.samples[rep] = timed([&] { schema.emplace(); });
    check(schema->tuple.codes().size() == 23, "prepared canonical layout");
  }
  std::vector<Observation> observations;
  {
    const v::Schema schema;
    Runtime runtime;
    for (const auto count : {std::size_t{129}, std::size_t{4097}}) {
      observations.push_back(construct(*runtime.pool, schema, v::Format::rows, count, 1, "construct_tuple"));
      observations.push_back(construct(*runtime.pool, schema, v::Format::columns, count, 1, "construct_series"));
      observations.push_back(convert(*runtime.pool, schema, v::Format::rows, count, "tuple_to_series"));
      observations.push_back(convert(*runtime.pool, schema, v::Format::columns, count, "series_to_tuple"));
    }
    for (const auto count : {std::size_t{1}, std::size_t{64}}) {
      observations.push_back(construct(*runtime.pool, schema, v::Format::rows, count, 64, "setup64_tuple_calls"));
      observations.push_back(construct(*runtime.pool, schema, v::Format::columns, count, 64, "setup64_series_calls"));
    }
  }
  std::cout << "{\"scope\":\"three repetitions; complete native data calls; allocation, source admission, lookup, Store publication, target readback and release excluded\",\"timing_enabled\":"
      << (clocks_enabled ? "true" : "false") << ",\"repetitions\":" << repetitions << ',';
  sizes(); std::cout << ",\"schema_setup\":"; observation(setup);
  std::cout << ",\"schema_setup_constructor_factories_source_inferred_not_instrumented\":" << (retained_constructor<v::Schema> ? 1 : 0)
      << ",\"observations\":[";
  for (std::size_t i = 0; i < observations.size(); ++i) { if (i) std::cout << ','; observation(observations[i]); }
  std::cout << "],\"independent_checked_rows\":" << checked_rows << ",\"independent_checked_wire_bytes\":" << checked_wire_bytes
      << ",\"held_old_source_checks\":" << old_source_checks << ",\"physical_close_zero_charge\":" << (closed_zero_charge ? "true" : "false") << "}\n";
}
