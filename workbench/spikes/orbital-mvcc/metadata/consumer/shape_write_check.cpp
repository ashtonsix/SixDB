#include "shape_write.h"
#include "../orbital/candidates/common.h"
#include <orbital/allocation.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
namespace sw = orbital_mvcc::metadata_consumer::shape_write;
namespace wb = orbital_mvcc::metadata_consumer::write_batch;
namespace cs = compact_shape;
namespace cd = compact_descriptor;
namespace v = orbital_bench::versioned;
namespace a = orbital::allocation;
using Memory = compact_version_probe::Memory;
using orbital_bench::Key;
using orbital_bench::Record;
using orbital_bench::Row;
constexpr orbital::ProgramId program{19, 3};
constexpr std::array<unsigned, 4> widths{64, 64, 20, 32};
using Image = std::array<std::vector<std::byte>, 4>;

struct Evidence {
  std::uint64_t captures{}, shape_factories{}, schema_instances{}, successful_batches{}, rejected_calls{};
  std::uint64_t malformed_calls{}, refused_laws{}, lookup_calls{}, mapped_calls{}, updates{};
  std::uint64_t duplicate_batches{}, same_record_batches{}, wire_bits{}, logical_fields{}, old_backing_checks{};
  std::uint64_t allocation_calls{}, allocation_reuses{}, issued_bytes{}, issued_spans{};
  std::size_t metadata_peak{}, metadata_calls{};
  unsigned live_schemas{};
} evidence;

void check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "shared Shape write check: %s\n", message);
  std::abort();
}
template<class T, class E> T take(std::expected<T, E> result, const char* message) {
  if (!result) {
    std::fprintf(stderr, "shared Shape write check: %s (error %u)\n", message, unsigned(result.error()));
    std::abort();
  }
  return std::move(*result);
}
void refused(orbital::Result<void> result, orbital::Error expected, const char* message) {
  if (!result && result.error() == expected) return;
  std::fprintf(stderr, "shared Shape write check: %s (expected %u, actual %u)\n", message,
      unsigned(expected), result ? 0u : unsigned(result.error()));
  std::abort();
}
struct Preparation {
  const v::Schema schema;
  Preparation() { ++evidence.live_schemas; ++evidence.schema_instances; }
  ~Preparation() { --evidence.live_schemas; }
};
struct ShapeOwner {
  cs::Shape* pointer{};
  ShapeOwner() = default;
  explicit ShapeOwner(cs::Shape* p) : pointer(p) {}
  ShapeOwner(const ShapeOwner&) = delete;
  ShapeOwner(ShapeOwner&& other) noexcept : pointer(std::exchange(other.pointer, nullptr)) {}
  ShapeOwner& operator=(ShapeOwner other) noexcept { std::swap(pointer, other.pointer); return *this; }
  ~ShapeOwner() { if (pointer) cs::release(pointer); }
};
struct RealizationOwner {
  cs::Realization* pointer{};
  RealizationOwner() = default;
  explicit RealizationOwner(cs::Realization* p) : pointer(p) {}
  RealizationOwner(const RealizationOwner&) = delete;
  RealizationOwner(RealizationOwner&& other) noexcept : pointer(std::exchange(other.pointer, nullptr)) {}
  RealizationOwner& operator=(RealizationOwner other) noexcept { std::swap(pointer, other.pointer); return *this; }
  ~RealizationOwner() { if (pointer) cs::release(pointer); }
  cs::Selection selection() const { return {pointer}; }
};
struct Runtime {
  orbital::worker::Lane lane;
  orbital::Ref<a::Arena> arena;
  a::Pool* pool{};
  Runtime() {
    check(orbital::worker::adopt(lane).has_value(), "Lane adoption");
    arena = take(a::open_arena(32 * 1024 * 1024), "Arena open");
    pool = take(a::add_pool(*arena, lane), "Pool open");
  }
  ~Runtime() {
    const auto stats = a::statistics(*pool);
    evidence.allocation_calls = stats.allocations; evidence.allocation_reuses = stats.reuses;
    bool closed = false;
    for (unsigned attempt = 0; attempt < 65536; ++attempt) {
      const auto progress = a::close(*pool);
      check(progress.kind() != orbital::Progress::Kind::failed, "physical Pool close failed");
      if (progress.kind() == orbital::Progress::Kind::done) { closed = true; break; }
      orbital::worker::poll(lane, 1024); std::this_thread::yield();
    }
    check(closed, "physical Pool close did not drain");
    check(!arena->charged_bytes.load() && !a::statistics(*pool).metadata_bytes,
          "physical Pool close retained charge");
    check(a::remove_pool(*pool).has_value(), "closed Pool removal");
    arena.reset(); orbital::worker::relinquish(lane);
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
    const Key key = row == 0 ? 0 : row + 1 == count ? UINT64_MAX : 0x8000000000000100ULL + row * 19;
    Record record{mix(row + 0x100001), std::uint32_t(mix(row + 81)) & orbital_bench::value_mask,
                  std::uint32_t(mix(row + 123))};
    if (row % 7 == 0) record = {0, 0, 0};
    if (row % 7 == 1) record = {UINT64_MAX, orbital_bench::value_mask, UINT32_MAX};
    result[row] = {key, record};
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
  check(false, "independent field index escaped schema"); return 0;
}
// This oracle knows the fixed wire law, not Ikea's reader/writer implementation.
std::pair<std::size_t, unsigned> location(v::Format format, unsigned index, std::size_t row, unsigned bit) {
  if (format == v::Format::rows) {
    constexpr std::array<unsigned, 4> first{0, 64, 128, 152};
    const unsigned physical = first[index] + bit;
    return {row * 23 + physical / 8, physical % 8};
  }
  const auto whole = widths[index] / 8, residual = widths[index] % 8;
  return {row / 8 * widths[index] + (bit < residual ? 8 * whole + bit :
          row % 8 * whole + (bit - residual) / 8),
          bit < residual ? unsigned(row % 8) : (bit - residual) % 8};
}
void write_bits(Image& image, v::Format format, unsigned index, std::size_t row, std::uint64_t value) {
  auto& bytes = image[format == v::Format::rows ? 0 : index];
  for (unsigned bit = 0; bit < widths[index]; ++bit) {
    const auto [offset, shift] = location(format, index, row, bit);
    check(offset < bytes.size(), "independent write exceeded Part");
    const auto old = std::to_integer<unsigned>(bytes[offset]);
    bytes[offset] = std::byte((old & ~(1u << shift)) | unsigned((value >> bit) & 1) << shift);
  }
}
void wire(const Image& image, v::Format format, std::span<const Row> rows) {
  for (std::size_t row = 0; row < rows.size(); ++row)
    for (unsigned index = 0; index < 4; ++index) {
      std::uint64_t decoded = 0;
      for (unsigned bit = 0; bit < widths[index]; ++bit) {
        const auto [offset, shift] = location(format, index, row, bit);
        const auto& bytes = image[format == v::Format::rows ? 0 : index];
        check(offset < bytes.size(), "independent read exceeded Part");
        decoded |= std::uint64_t((std::to_integer<unsigned>(bytes[offset]) >> shift) & 1) << bit;
        ++evidence.wire_bits;
      }
      check(decoded == field(rows[row], index), "wire differs from independently expected Row");
      ++evidence.logical_fields;
    }
}
void salt_spare(Image& image, v::Format format, std::size_t count) {
  if (format == v::Format::rows) {
    for (std::size_t row = 0; row < count; ++row)
      image[0][row * 23 + 18] |= std::byte(unsigned(row % 15 + 1) << 4);
  } else {
    for (std::size_t row = count; row < (count + 7) / 8 * 8; ++row)
      for (unsigned index = 0; index < 4; ++index) write_bits(image, format, index, row, mix(row + index * 117));
  }
}

struct PreparedParts {
  v::Shape physical;
  std::array<a::Allocation, 4> backing;
  std::array<v::WritePart, 4> writes{};
  std::array<cd::PartSource, 4> descriptions{};
  PreparedParts(a::Pool& pool, const v::Schema& schema, v::Format format, std::size_t count)
      : physical(take(v::shape(schema, format, count), "existing fixture shape")) {
    for (unsigned part = 0; part < physical.part_count; ++part) {
      const auto& required = physical.parts[part];
      if (required.body_bytes)
        backing[part] = take(a::allocate(pool, {.bytes = required.body_bytes, .alignment = required.alignment,
            .readable_padding = required.readable_padding, .initialization = a::Initialization::zero}),
            "construction Allocation");
      writes[part] = {required.id, a::bytes(backing[part])};
      descriptions[part] = {required.id, a::bytes(backing[part]), required.interpretation,
          required.readable_padding, required.alignment, {}};
    }
  }
  Image image() const {
    Image result;
    for (unsigned part = 0; part < physical.part_count; ++part) {
      const auto bytes = a::bytes(backing[part]); result[part].assign(bytes.begin(), bytes.end());
    }
    return result;
  }
  void assign(const Image& image) {
    for (unsigned part = 0; part < physical.part_count; ++part)
      std::copy(image[part].begin(), image[part].end(), backing[part].address);
  }
};
ShapeOwner seed_shape(Memory& memory, a::Pool& pool, const v::Schema& schema, v::Format format) {
  PreparedParts parts(pool, schema, format, 1);
  const auto rows = input(1); v::KernelCosts costs;
  check(v::construct_into(schema, parts.physical, std::span(parts.writes).first(parts.physical.part_count),
      rows, costs).has_value(), "real seed construction");
  auto* shape = take(cs::prove_fixture(memory, schema, program, parts.physical.interpretation,
      std::span(parts.descriptions).first(parts.physical.part_count)), "actual Shape proof");
  ++evidence.shape_factories;
  return ShapeOwner{shape};
}

orbital::Bytes selected_body(cs::Selection selected, unsigned part) {
  const auto view = take(selected.part(std::size_t{part}), "actual protected Part");
  const auto extent = take(cs::geometry(selected.shape(), part).extent(selected.records()), "captured geometry extent");
  check(extent <= SIZE_MAX, "captured extent exceeds native space");
  return {view.address(), static_cast<std::size_t>(extent)};
}
Image selected_image(cs::Selection selected) {
  Image result;
  for (unsigned part = 0; part < selected.part_count(); ++part) {
    const auto bytes = selected_body(selected, part); result[part].assign(bytes.begin(), bytes.end());
  }
  return result;
}
struct Source {
  const v::Format format;
  std::vector<Row> rows;
  Image original;
  RealizationOwner owner;
  Source(Memory& memory, a::Pool& pool, cs::Shape* shape, v::Format format, std::size_t count)
      : format(format), rows(input(count)) {
    Preparation preparation;
    PreparedParts parts(pool, preparation.schema, format, count); v::KernelCosts costs;
    check(v::construct_into(preparation.schema, parts.physical,
        std::span(parts.writes).first(parts.physical.part_count), rows, costs).has_value(), "real source construction");
    original = parts.image(); salt_spare(original, format, count); parts.assign(original); wire(original, format, rows);
    std::array<cs::BodySource, 4> bodies{};
    for (unsigned part = 0; part < parts.physical.part_count; ++part)
      bodies[part] = {parts.writes[part].id, a::bytes(parts.backing[part]), {}, &parts.backing[part]};
    owner = RealizationOwner{take(cs::capture(memory, {shape, count, cs::Owners::independent_allocation},
        std::span(bodies).first(parts.physical.part_count)), "actual Allocation-backed capture")};
    ++evidence.captures;
    // Capture acquires actual Storage holds and releases source Allocations on
    // success. No Schema, descriptor vector or construction view escapes.
  }
  cs::Shape* shape() const { return const_cast<cs::Shape*>(owner.selection().shape()); }
  void unchanged() const {
    check(selected_image(owner.selection()) == original, "immutable captured old backing changed");
    ++evidence.old_backing_checks;
  }
};
struct Clone {
  const cs::Shape* shape;
  const std::size_t count;
  std::array<a::Allocation, 4> backing;
  Clone(a::Pool& pool, const Source& source) : shape(source.shape()), count(source.rows.size()) {
    for (unsigned part = 0; part < cs::part_count(shape); ++part) {
      const auto& geometry = cs::geometry(shape, part);
      const auto size = take(geometry.extent(count), "clone extent");
      check(size == source.original[part].size(), "clone extent differs from original");
      if (!size) continue;
      backing[part] = take(a::allocate(pool, {.bytes = static_cast<std::size_t>(size),
          .alignment = std::size_t{1} << geometry.alignment_log2, .readable_padding = geometry.readable_padding,
          .initialization = a::Initialization::uninitialized}), "private clone Allocation");
      std::copy(source.original[part].begin(), source.original[part].end(), backing[part].address);
    }
  }
  std::array<v::WritePart, 4> writes() const {
    std::array<v::WritePart, 4> result{};
    for (unsigned part = 0; part < cs::part_count(shape); ++part)
      result[part] = {cs::geometry(shape, part).id, a::bytes(backing[part])};
    return result;
  }
  Image image() const {
    Image result;
    for (unsigned part = 0; part < cs::part_count(shape); ++part) {
      const auto bytes = a::bytes(backing[part]); result[part].assign(bytes.begin(), bytes.end());
    }
    return result;
  }
};

bool same(const v::KernelCosts& a, const v::KernelCosts& b) {
  return a.decoded_values == b.decoded_values && a.encoded_values == b.encoded_values && a.bindings == b.bindings &&
      a.constructor_calls == b.constructor_calls && a.issued_bytes == b.issued_bytes && a.issued_spans == b.issued_spans &&
      a.completed_parts == b.completed_parts && a.completed_rows == b.completed_rows && a.scratch_bound_bytes == b.scratch_bound_bytes;
}
bool same(const wb::ProbeCosts& a, const wb::ProbeCosts& b) {
  return a.private_admissions == b.private_admissions && a.key_reads == b.key_reads &&
      a.full_rows_read == b.full_rows_read && a.applied_updates == b.applied_updates;
}
v::KernelCosts seeded_kernel() { return {11, 13, 17, 19, 23, 29, 31, 37, 41}; }
wb::ProbeCosts seeded_probe() { return {43, 47, 53, 59}; }
std::optional<orbital::Error> expected_error(std::span<const Row> rows, std::span<const v::Update> updates) {
  if (updates.empty() || updates.size() > v::write_batch_limit) return orbital::Error::invalid_argument;
  for (const auto& update : updates) {
    if (update.record.value > orbital_bench::value_mask) return orbital::Error::invalid_argument;
    if (std::none_of(rows.begin(), rows.end(), [&](const Row& row) { return row.key == update.key; }))
      return orbital::Error::absent;
  }
  return {};
}
std::size_t linear_row(std::span<const Row> rows, Key key) {
  std::size_t row = 0; while (row < rows.size() && rows[row].key != key) ++row; return row;
}
std::vector<v::Update> batch(const Source& source, std::size_t count, bool unchanged = false) {
  std::vector<v::Update> result(count);
  for (std::size_t i = 0; i < count; ++i) {
    if (source.rows.empty()) { result[i] = {1, {0, 0, 0}}; continue; }
    const auto last = source.rows.size() - 1;
    const std::array<std::size_t, 8> choices{0, last, std::min(last, std::size_t{7}), std::min(last, std::size_t{8}),
        std::min(last, std::size_t{63}), std::min(last, std::size_t{64}), source.rows.size() / 2, i % source.rows.size()};
    const auto row = choices[i % choices.size()];
    Record replacement{mix(i + 500), std::uint32_t(mix(i + 600)) & orbital_bench::value_mask, std::uint32_t(mix(i + 700))};
    if (i % 7 == 0) replacement = {UINT64_MAX, orbital_bench::value_mask, UINT32_MAX};
    if (unchanged) replacement = source.rows[row].record;
    result[i] = {source.rows[row].key, replacement};
  }
  return result;
}

// The reference uses unchanged per-row Ikea updates only inside a transient
// Schema scope; its bytes/effects survive, not the Schema or borrowed endpoints.
v::KernelCosts reference(a::Pool& pool, const Source& source, std::span<const v::Update> updates, const Image& expected) {
  Preparation preparation;
  const auto physical = take(v::shape(preparation.schema, source.format, source.rows.size()), "reference fixture shape");
  Clone target(pool, source); const auto writes = target.writes(); v::KernelCosts costs;
  for (const auto& update : updates)
    check(v::update_into(preparation.schema, physical, std::span(writes).first(physical.part_count),
        linear_row(source.rows, update.key), update.record, costs).has_value(), "unchanged per-row update reference");
  check(target.image() == expected, "reference differs from independent wire oracle");
  return costs;
}
template<class Proof> void success(a::Pool& pool, Proof proof, const Source& source,
    std::span<const v::Update> updates, bool duplicate = false, bool unchanged = false) {
  check(!expected_error(source.rows, updates), "successful fixture failed independent sequential preflight");
  auto expected = source.original; auto rows = source.rows;
  for (const auto& update : updates) {
    const auto row = linear_row(rows, update.key); rows[row].record = update.record;
    for (unsigned index = 1; index < 4; ++index) write_bits(expected, source.format, index, row, field(rows[row], index));
  }
  const auto control = reference(pool, source, updates, expected);
  check(!evidence.live_schemas, "Schema retained for candidate write");
  Clone target(pool, source); const auto writes = target.writes(); v::KernelCosts costs; wb::ProbeCosts probes;
  const std::vector<v::Update> input_before(updates.begin(), updates.end());
  check(sw::apply(proof, source.rows.size(), std::span(writes).first(cs::part_count(source.shape())), updates,
      costs, probes).has_value(), "valid Schema-dead complete batch");
  check(target.image() == expected, "candidate differs from independent complete bit image");
  check(std::equal(updates.begin(), updates.end(), input_before.begin(), input_before.end(),
      [](const auto& a, const auto& b) { return a.key == b.key && a.record == b.record; }), "successful write changed offered commands");
  wire(target.image(), source.format, rows);
  if (unchanged) check(expected == source.original, "same-record oracle changed bytes");
  check(costs.encoded_values == updates.size() * 3 && costs.encoded_values == control.encoded_values &&
      costs.issued_bytes == control.issued_bytes && costs.issued_spans == control.issued_spans,
      "issued effects differ from unchanged per-row reference");
  check(costs.bindings == 3 && probes.private_admissions == 1 && probes.full_rows_read == 0 &&
      probes.key_reads && probes.applied_updates == updates.size(), "complete write accounting differs");
  check(!costs.constructor_calls && !costs.completed_parts && !costs.completed_rows,
      "mutation counters claim construction completion");
  source.unchanged(); evidence.updates += updates.size(); ++evidence.successful_batches;
  evidence.duplicate_batches += duplicate; evidence.same_record_batches += unchanged;
  evidence.issued_bytes += costs.issued_bytes; evidence.issued_spans += costs.issued_spans;
}
template<class Proof> void reject(a::Pool& pool, Proof proof, const Source& source, std::span<const v::Update> updates) {
  const auto error = expected_error(source.rows, updates); check(error.has_value(), "invalid fixture passed independent preflight");
  check(!evidence.live_schemas, "Schema retained for refusal");
  Clone target(pool, source); const auto writes = target.writes();
  auto costs = seeded_kernel(); auto probes = seeded_probe();
  refused(sw::apply(proof, source.rows.size(), std::span(writes).first(cs::part_count(source.shape())), updates,
      costs, probes), *error, "complete batch returned wrong first failure");
  check(target.image() == source.original, "refused batch changed bytes");
  check(same(costs, seeded_kernel()) && same(probes, seeded_probe()), "refused batch changed seeded counters");
  source.unchanged(); ++evidence.rejected_calls;
}
template<class Proof> void batches(a::Pool& pool, Proof proof, const Source& source) {
  for (const std::size_t count : {0u, 1u, 64u, 65u}) {
    const auto updates = batch(source, count);
    if (expected_error(source.rows, updates)) reject(pool, proof, source, updates);
    else success(pool, proof, source, updates, count > source.rows.size());
  }
  if (!source.rows.empty()) {
    success(pool, proof, source, batch(source, 64, true), source.rows.size() < 64, true);
    const auto key = source.rows[source.rows.size() / 2].key;
    const std::array<v::Update, 3> duplicate{{{key, {0xaaaaaaaaaaaaaaaaULL, 0xaaaaa, 0xaaaaaaaa}},
        {key, {0x5555555555555555ULL, 0x55555, 0x55555555}}, {key, {UINT64_MAX, orbital_bench::value_mask, UINT32_MAX}}}};
    success(pool, proof, source, duplicate, true);
  }
  for (const std::size_t position : {0u, 32u, 63u}) {
    auto updates = batch(source, 64); updates[position].key = 1; reject(pool, proof, source, updates);
    updates = batch(source, 64); updates[position].record.value = orbital_bench::value_mask + 1;
    reject(pool, proof, source, updates);
  }
  // Byte3 lies outside the native three-byte packet map. Only the complete
  // uint32 value20 admission can reject this offered value before any stores.
  auto outside_packet = batch(source, 1); outside_packet.front().record.value = 0x01000000u;
  check(expected_error(source.rows, outside_packet) == orbital::Error::invalid_argument,
      "bit24 value oracle did not reject before key resolution");
  reject(pool, proof, source, outside_packet);
  auto mixed = batch(source, 64); mixed.front().key = 1; mixed.back().record.value = orbital_bench::value_mask + 1;
  check(expected_error(source.rows, mixed) == orbital::Error::absent, "absent-first oracle differs");
  reject(pool, proof, source, mixed);
  mixed = batch(source, 64); mixed.front().record.value = orbital_bench::value_mask + 1; mixed.back().key = 1;
  check(expected_error(source.rows, mixed) == orbital::Error::invalid_argument, "value-first oracle differs");
  reject(pool, proof, source, mixed);
}

template<class Proof> void lookups(Proof proof, const Source& source) {
  check(!evidence.live_schemas, "Schema retained for lookup");
  std::vector<Key> keys{0, 1, 0x8000000000000101ULL, UINT64_MAX};
  for (const auto& row : source.rows) keys.push_back(row.key);
  for (const auto key : keys) {
    const auto expected = linear_row(source.rows, key);
    const Record saved{0x123456789abcdef0ULL, 0x54321, 0xfedcba98};
    std::size_t row = 777; Record record = saved;
    const auto result = sw::lookup(proof, source.owner.selection(), key, row, record);
    if (expected == source.rows.size()) {
      refused(result, orbital::Error::absent, "absent lookup error");
      check(row == 777 && record == saved, "refused lookup changed outputs");
    } else check(result && row == expected && record == source.rows[expected].record, "lookup differs from independent linear Row");
    ++evidence.lookup_calls;
    if constexpr (std::is_same_v<Proof, sw::TupleProof>) {
      row = 777; record = saved;
      const auto mapped = sw::lookup_mapped(proof, source.rows.size(), selected_body(source.owner.selection(), 0), key, row, record);
      if (expected == source.rows.size()) {
        refused(mapped, orbital::Error::absent, "absent mapped lookup error");
        check(row == 777 && record == saved, "refused mapped lookup changed outputs");
      } else check(mapped && row == expected && record == source.rows[expected].record, "mapped lookup differs from linear Row");
      ++evidence.mapped_calls;
    }
  }
  const Record saved{UINT64_MAX, 0x13579, UINT32_MAX}; std::size_t row = 313; Record record = saved;
  refused(sw::lookup(proof, {}, 0, row, record), orbital::Error::absent, "null selected lookup");
  check(row == 313 && record == saved, "null lookup changed outputs");
  ++evidence.lookup_calls; source.unchanged();
}

template<class T> T* object_storage(a::Allocation& backing) {
  const auto first = reinterpret_cast<std::uintptr_t>(backing.address);
  const auto offset = (alignof(T) - first % alignof(T)) % alignof(T);
  check(offset <= backing.size && sizeof(T) <= backing.size - offset, "real alias object does not fit clone");
  return reinterpret_cast<T*>(backing.address + offset);
}
template<class Proof> void malformed(a::Pool& pool, Proof proof, const Source& source) {
  check(source.rows.size() == 9 && !evidence.live_schemas, "malformed fixture count/Schema");
  const auto updates = batch(source, 1);
  Clone target(pool, source); const auto base = target.writes(); const auto parts = cs::part_count(source.shape());
  auto guard = take(a::allocate(pool, {.bytes = source.original[0].size() + 128, .alignment = 64,
      .initialization = a::Initialization::zero}), "placement guard Allocation");
  std::fill(a::bytes(guard).begin(), a::bytes(guard).end(), std::byte{0x5a});
  const std::vector<std::byte> guard_before(a::bytes(guard).begin(), a::bytes(guard).end());
  const auto refuse = [&](std::uint64_t count, std::span<const v::WritePart> writes) {
    auto costs = seeded_kernel(); auto probes = seeded_probe();
    refused(sw::apply(proof, count, writes, updates, costs, probes), orbital::Error::invalid_argument, "malformed private clone admitted");
    check(target.image() == source.original && std::equal(a::bytes(guard).begin(), a::bytes(guard).end(),
        guard_before.begin(), guard_before.end()), "placement refusal changed body/guard");
    check(same(costs, seeded_kernel()) && same(probes, seeded_probe()), "placement refusal changed counters");
    source.unchanged(); ++evidence.rejected_calls; ++evidence.malformed_calls;
  };
  refuse(source.rows.size(), std::span(base).first(parts - 1));
  std::array<v::WritePart, 5> extra{}; std::copy(base.begin(), base.end(), extra.begin()); extra[parts] = base[0];
  refuse(source.rows.size(), std::span(extra).first(parts + 1));
  refuse(UINT64_MAX, std::span(base).first(parts));
  // Column counts sharing the same final tile are an enclosing association,
  // not inferable from bytes; this supplied count crosses a tile boundary.
  refuse(source.format == v::Format::rows ? 10 : 17, std::span(base).first(parts));
  for (unsigned part = 0; part < parts; ++part) {
    auto writes = base; writes[part].id = {}; refuse(source.rows.size(), std::span(writes).first(parts));
    writes = base; writes[part].bytes = writes[part].bytes.first(writes[part].bytes.size() - 1);
    refuse(source.rows.size(), std::span(writes).first(parts));
    writes = base; writes[part].bytes = a::bytes(guard).first(writes[part].bytes.size() + 1);
    refuse(source.rows.size(), std::span(writes).first(parts));
    writes = base; writes[part].bytes = {static_cast<std::byte*>(nullptr), writes[part].bytes.size()};
    refuse(source.rows.size(), std::span(writes).first(parts));
    writes = base; writes[part].bytes = {reinterpret_cast<std::byte*>(UINTPTR_MAX - 3), writes[part].bytes.size()};
    refuse(source.rows.size(), std::span(writes).first(parts));
    if (source.format == v::Format::columns) {
      writes = base; writes[part].bytes = a::bytes(guard).subspan(1, writes[part].bytes.size());
      refuse(source.rows.size(), std::span(writes).first(parts));
    }
  }
  if (source.format == v::Format::columns) {
    auto writes = base; writes[1].bytes = writes[0].bytes; refuse(source.rows.size(), std::span(writes).first(parts));
    writes = base; writes[2].bytes = writes[0].bytes.first(writes[2].bytes.size());
    refuse(source.rows.size(), std::span(writes).first(parts));
  }
  const auto alias_verify = [&](const Clone& clone, const Image& before, const v::KernelCosts& costs, const wb::ProbeCosts& probes) {
    check(clone.image() == before && same(costs, seeded_kernel()) && same(probes, seeded_probe()),
        "real-object alias refusal changed bytes/counters");
    source.unchanged(); ++evidence.rejected_calls; ++evidence.malformed_calls;
  };
  {
    Clone clone(pool, source); auto* input = std::construct_at(object_storage<v::Update>(clone.backing[0]), updates[0]);
    const auto before = clone.image(); const auto writes = clone.writes(); auto costs = seeded_kernel(); auto probes = seeded_probe();
    refused(sw::apply(proof, clone.count, std::span(writes).first(parts), std::span(input, 1), costs, probes),
        orbital::Error::invalid_argument, "command/target overlap admitted");
    alias_verify(clone, before, costs, probes); std::destroy_at(input);
  }
  {
    Clone clone(pool, source); auto* costs = std::construct_at(object_storage<v::KernelCosts>(clone.backing[0]), seeded_kernel());
    const auto before = clone.image(); const auto writes = clone.writes(); auto probes = seeded_probe();
    refused(sw::apply(proof, clone.count, std::span(writes).first(parts), updates, *costs, probes),
        orbital::Error::invalid_argument, "costs/target overlap admitted");
    alias_verify(clone, before, *costs, probes); std::destroy_at(costs);
  }
  {
    Clone clone(pool, source); auto* probes = std::construct_at(object_storage<wb::ProbeCosts>(clone.backing[0]), seeded_probe());
    const auto before = clone.image(); const auto writes = clone.writes(); auto costs = seeded_kernel();
    refused(sw::apply(proof, clone.count, std::span(writes).first(parts), updates, costs, *probes),
        orbital::Error::invalid_argument, "probes/target overlap admitted");
    alias_verify(clone, before, costs, *probes); std::destroy_at(probes);
  }
  {
    Clone clone(pool, source); using Packet = std::array<v::WritePart, 4>;
    auto* packet = std::construct_at(object_storage<Packet>(clone.backing[0]), clone.writes());
    const auto before = clone.image(); auto costs = seeded_kernel(); auto probes = seeded_probe();
    refused(sw::apply(proof, clone.count, std::span(*packet).first(parts), updates, costs, probes),
        orbital::Error::invalid_argument, "target packet/target overlap admitted");
    alias_verify(clone, before, costs, probes); std::destroy_at(packet);
  }
}

void cold_laws(Memory& memory, a::Pool& pool, const v::Schema& schema,
    cs::Shape* tuple, cs::Shape* series, std::optional<sw::TupleLaw>& law, const std::array<orbital::PartId, 4>& ids) {
  const auto part = cs::geometry(tuple, 0).id;
  const auto writers = std::span(schema.writers).subspan<1, 3>();
  const auto reject_law = [&](const cs::Shape* shape, const auto& offered, orbital::ProgramId expected_program, orbital::PartId expected_part) {
    std::optional<sw::TupleLaw> destination;
    refused(sw::TupleLaw::prepare(destination, shape, schema.tuple, offered, expected_program, expected_part),
        orbital::Error::invalid_argument, "bad Tuple law admitted");
    check(!destination, "failed law preparation engaged final-place destination"); ++evidence.refused_laws;
  };
  reject_law(nullptr, writers, program, part); reject_law(tuple, writers, {}, part);
  reject_law(tuple, writers, program, {}); reject_law(series, writers, program, part);
  // A key writer has the same unit/width as next; only semantic map admission
  // prevents this legitimate but mislabeled plan from modifying fixed keys.
  const std::array<v::tp::writer<8>, 3> mislabeled{schema.writers[0], schema.writers[2], schema.writers[3]};
  reject_law(tuple, std::span(mislabeled), program, part);
  const std::array<v::tp::writer<8>, 3> swapped{schema.writers[1], schema.writers[3], schema.writers[2]};
  reject_law(tuple, std::span(swapped), program, part);
  check(sw::TupleLaw::prepare(law, tuple, schema.tuple, writers, program, part).has_value(), "final-place law preparation");
  const auto* address = &*law;
  const auto bytes = orbital::Bytes{reinterpret_cast<const std::byte*>(address), sizeof(*address)};
  const std::vector<std::byte> before(bytes.begin(), bytes.end());
  refused(sw::TupleLaw::prepare(law, tuple, schema.tuple, writers, program, part), orbital::Error::invalid_argument,
      "engaged final-place law was replaced");
  check(&*law == address && !std::memcmp(address, before.data(), before.size()), "refused reprepare changed law");
  ++evidence.refused_laws;
  const auto reject_series = [&](const cs::Shape* shape, orbital::ProgramId expected_program, const auto& expected_ids) {
    const auto result = sw::admit_series_shape(shape, expected_program, expected_ids);
    check(!result && result.error() == orbital::Error::invalid_argument, "bad Series law admitted"); ++evidence.refused_laws;
  };
  reject_series(nullptr, program, std::span(ids)); reject_series(series, {}, std::span(ids));
  reject_series(tuple, program, std::span(ids));
  auto swapped_ids = ids; std::swap(swapped_ids[0], swapped_ids[1]);
  reject_series(series, program, std::span(swapped_ids));
  for (const bool wrong_width : {true, false}) {
    PreparedParts parts(pool, schema, v::Format::columns, 1);
    if (wrong_width) {
      const auto encoded = take(v::sp::encode_descriptor({19, 0, v::sp::geometry::local, 1, {19, 0, 0}}),
          "actual Series19 descriptor");
      parts.physical.parts[2].interpretation.assign(reinterpret_cast<const std::byte*>(encoded.data()),
          reinterpret_cast<const std::byte*>(encoded.data() + encoded.size()));
      parts.descriptions[2].body = a::bytes(parts.backing[2]).first(19);
      parts.descriptions[2].interpretation = parts.physical.parts[2].interpretation;
    } else std::swap(parts.descriptions[0].id, parts.descriptions[1].id);
    ShapeOwner other{take(cs::prove_series(memory, program, std::span(parts.descriptions)), "actual noncanonical Series Shape")};
    reject_series(other.pointer, program, std::span(ids));
  }
}

template<class Proof> void wrong_selection(Proof proof, cs::Selection selected) {
  check(!evidence.live_schemas, "Schema retained for wrong-Shape lookup");
  const Record before{UINT64_MAX, 0x12345, UINT32_MAX}; Record record = before; std::size_t row = 991;
  refused(sw::lookup(proof, selected, 0, row, record), orbital::Error::invalid_argument, "wrong actual Shape accepted");
  check(row == 991 && record == before, "wrong-Shape lookup changed outputs"); ++evidence.lookup_calls;
}
void mapped_refusals(sw::TupleProof proof, const Source& source) {
  const auto body = selected_body(source.owner.selection(), 0);
  const auto refuse = [&](std::uint64_t count, orbital::Bytes bytes) {
    const Record before{UINT64_MAX, 0x12345, UINT32_MAX}; Record record = before; std::size_t row = 991;
    refused(sw::lookup_mapped(proof, count, bytes, 0, row, record), orbital::Error::invalid_argument, "bad mapped bounds accepted");
    check(row == 991 && record == before, "bad mapped lookup changed outputs"); ++evidence.mapped_calls;
  };
  refuse(source.rows.size(), body.first(body.size() - 1));
  const std::vector<std::byte> extra(body.size() + 1);
  refuse(source.rows.size(), extra);
  refuse(source.rows.size(), {static_cast<const std::byte*>(nullptr), body.size()});
  refuse(UINT64_MAX, {});
  refuse(source.rows.size(), {reinterpret_cast<const std::byte*>(UINTPTR_MAX - 3), body.size()});
  source.unchanged();
}
void metadata_aliases(a::Pool& pool, sw::TupleProof proof, const Source& source) {
  check(source.rows.size() == 1 && !evidence.live_schemas, "metadata alias fixture");
  Clone clone(pool, source); const auto base = clone.writes(); const auto updates = batch(source, 1);
  const auto refuse = [&](std::byte* address) {
    auto writes = base; writes[0].bytes = {address, 23}; auto costs = seeded_kernel(); auto probes = seeded_probe();
    refused(sw::apply(proof, 1, std::span(writes).first(1), updates, costs, probes), orbital::Error::invalid_argument,
        "actual law/Shape metadata storage accepted as body");
    check(clone.image() == source.original && same(costs, seeded_kernel()) && same(probes, seeded_probe()),
        "metadata alias refusal changed clone/counters");
    source.unchanged(); ++evidence.rejected_calls; ++evidence.malformed_calls;
  };
  const auto* law = &proof.law(); const auto shape = source.shape();
  const auto shape_size = cs::shape_bytes(shape);
  std::vector<std::byte> law_before(sizeof(*law)), shape_before(shape_size);
  std::memcpy(law_before.data(), law, law_before.size()); std::memcpy(shape_before.data(), shape, shape_size);
  // The second spans exercise trailing controls/descriptors, not only headers.
  auto* law_bytes = reinterpret_cast<std::byte*>(const_cast<sw::TupleLaw*>(law));
  auto* shape_bytes = reinterpret_cast<std::byte*>(shape);
  refuse(law_bytes); refuse(law_bytes + sizeof(*law) - 23);
  refuse(shape_bytes); refuse(shape_bytes + shape_size - 23);
  check(!std::memcmp(law, law_before.data(), law_before.size()) && !std::memcmp(shape, shape_before.data(), shape_size),
      "metadata alias refusal changed retained law/Shape bytes");
}
} // namespace

int main() {
  Memory memory;
  {
    Runtime runtime;
    ShapeOwner tuple, series;
    std::optional<sw::TupleLaw> law; // Final address is stable until all proofs end.
    std::optional<sw::SeriesProof> series_proof;
    {
      Preparation preparation;
      tuple = seed_shape(memory, *runtime.pool, preparation.schema, v::Format::rows);
      series = seed_shape(memory, *runtime.pool, preparation.schema, v::Format::columns);
      std::array<orbital::PartId, 4> ids;
      for (unsigned i = 0; i < 4; ++i) ids[i] = cs::geometry(series.pointer, i).id;
      cold_laws(memory, *runtime.pool, preparation.schema, tuple.pointer, series.pointer, law, ids);
      series_proof.emplace(take(sw::admit_series_shape(series.pointer, program, std::span(ids)), "canonical Series proof"));
    }
    check(!evidence.live_schemas, "Schema escaped final-place law preparation");
    const auto tuple_proof = law->proof();
    for (const std::size_t count : {0u, 1u, 2u, 7u, 8u, 9u, 63u, 64u, 65u, 129u}) {
      Source rows(memory, *runtime.pool, tuple.pointer, v::Format::rows, count);
      Source columns(memory, *runtime.pool, series.pointer, v::Format::columns, count);
      check(!evidence.live_schemas, "Schema escaped real captures");
      lookups(tuple_proof, rows); lookups(*series_proof, columns);
      batches(*runtime.pool, tuple_proof, rows); batches(*runtime.pool, *series_proof, columns);
      wrong_selection(tuple_proof, columns.owner.selection()); wrong_selection(*series_proof, rows.owner.selection());
      if (count == 9) {
        malformed(*runtime.pool, tuple_proof, rows); malformed(*runtime.pool, *series_proof, columns);
        mapped_refusals(tuple_proof, rows);
      }
      if (count == 1) metadata_aliases(*runtime.pool, tuple_proof, rows);
    }
    ShapeOwner distinct_tuple, distinct_series;
    {
      Preparation preparation;
      distinct_tuple = seed_shape(memory, *runtime.pool, preparation.schema, v::Format::rows);
      distinct_series = seed_shape(memory, *runtime.pool, preparation.schema, v::Format::columns);
    }
    Source other_rows(memory, *runtime.pool, distinct_tuple.pointer, v::Format::rows, 1);
    Source other_columns(memory, *runtime.pool, distinct_series.pointer, v::Format::columns, 1);
    wrong_selection(tuple_proof, other_rows.owner.selection()); wrong_selection(*series_proof, other_columns.owner.selection());
  }
  evidence.metadata_peak = memory.peak; evidence.metadata_calls = memory.calls;
  check(!memory.live && !evidence.live_schemas, "cleanup retained actual Shape/Realization metadata or Schema");
  std::cout << "{\"check\":\"actual_shared_shape_complete_writes\",\"captures\":" << evidence.captures
      << ",\"shape_factories\":" << evidence.shape_factories << ",\"transient_schemas\":" << evidence.schema_instances
      << ",\"successful_batches\":" << evidence.successful_batches << ",\"rejected_calls\":" << evidence.rejected_calls
      << ",\"malformed_calls\":" << evidence.malformed_calls << ",\"refused_laws\":" << evidence.refused_laws
      << ",\"lookup_calls\":" << evidence.lookup_calls << ",\"mapped_calls\":" << evidence.mapped_calls
      << ",\"updates\":" << evidence.updates << ",\"duplicate_batches\":" << evidence.duplicate_batches
      << ",\"same_record_batches\":" << evidence.same_record_batches << ",\"wire_bits\":" << evidence.wire_bits
      << ",\"logical_fields\":" << evidence.logical_fields << ",\"old_backing_checks\":" << evidence.old_backing_checks
      << ",\"allocation_calls\":" << evidence.allocation_calls << ",\"allocation_reuses\":" << evidence.allocation_reuses
      << ",\"issued_bytes\":" << evidence.issued_bytes << ",\"issued_spans\":" << evidence.issued_spans
      << ",\"metadata_peak_bytes\":" << evidence.metadata_peak << ",\"metadata_allocation_calls\":" << evidence.metadata_calls
      << ",\"tuple_law_bytes\":" << sizeof(sw::TupleLaw) << ",\"tuple_optional_final_place_bytes\":" << sizeof(std::optional<sw::TupleLaw>)
      << ",\"tuple_proof_bytes\":" << sizeof(sw::TupleProof) << ",\"series_proof_bytes\":" << sizeof(sw::SeriesProof)
      << ",\"series_retained_runtime_plan_bytes\":0,\"schema_retained_for_candidate_calls\":false"
         ",\"physical_close_zero_charge\":true,\"metadata_zero_after_cleanup\":true,\"hardware_or_timing_claim\":false}\n";
}
