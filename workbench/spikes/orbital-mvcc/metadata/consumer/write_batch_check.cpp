#include "write_batch.h"
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
#include <utility>
#include <vector>

namespace {
namespace wb = orbital_mvcc::metadata_consumer::write_batch;
namespace v = orbital_bench::versioned;
namespace a = orbital::allocation;
using orbital_bench::Key;
using orbital_bench::Record;
using orbital_bench::Row;
constexpr std::array<unsigned, 4> widths{64, 64, 20, 32};
using Image = std::array<std::vector<std::byte>, v::field_count>;
using Operation = orbital::Result<void> (*)(const v::Schema&, const v::Shape&,
    std::span<const v::WritePart>, std::span<const v::Update>, v::KernelCosts&, wb::ProbeCosts&);
constexpr std::array<Operation, 2> operations{wb::apply, wb::generic_control};

struct Evidence {
  std::uint64_t fixtures{}, successful_batches{}, rejected_calls{}, malformed_calls{};
  std::uint64_t duplicate_batches{}, same_record_batches{}, updates{}, logical_fields{}, wire_bits{};
  std::uint64_t retained_old_checks{}, allocation_calls{}, allocation_reuses{};
  std::uint64_t metadata_overlap_fixtures{};
  std::uint64_t candidate_bindings{}, control_bindings{}, issued_bytes{}, issued_spans{};
  std::uint64_t candidate_key_reads{}, control_full_rows_read{};
} evidence;

void check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "private write batch check: %s\n", message);
  std::abort();
}
template<class T, class E> T take(std::expected<T, E> result, const char* message) {
  if (!result) {
    std::fprintf(stderr, "private write batch check: %s (error %u)\n", message, unsigned(result.error()));
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
    arena = take(a::open_arena(32 * 1024 * 1024), "Arena open");
    pool = take(a::add_pool(*arena, lane), "Pool open");
  }
  ~Runtime() {
    const auto stats = a::statistics(*pool);
    evidence.allocation_calls = stats.allocations;
    evidence.allocation_reuses = stats.reuses;
    bool closed = false;
    for (unsigned attempt = 0; attempt < 65536; ++attempt) {
      const auto progress = a::close(*pool);
      check(progress.kind() != orbital::Progress::Kind::failed, "Pool physical close failed");
      if (progress.kind() == orbital::Progress::Kind::done) { closed = true; break; }
      orbital::worker::poll(lane, 1024);
      std::this_thread::yield();
    }
    check(closed, "physical close did not drain after backing lifetimes ended");
    check(arena->charged_bytes.load() == 0 && a::statistics(*pool).metadata_bytes == 0,
          "physical close retained payload charge or metadata");
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
std::vector<Row> input(std::size_t count) {
  std::vector<Row> result(count);
  for (std::size_t row = 0; row < count; ++row) {
    const Key key = row == 0 ? 0 : row + 1 == count ? UINT64_MAX :
        0x8000000000000100ULL + row * 19;
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
  check(false, "oracle field index escaped schema");
  return 0;
}

// Independent bit placement, not an Ikea reader/writer or codec roundtrip.
std::pair<std::size_t, unsigned> location(v::Format format, unsigned index,
                                        std::size_t row, unsigned bit) {
  if (format == v::Format::rows) {
    constexpr std::array<unsigned, 4> first{0, 64, 128, 152};
    const unsigned physical = first[index] + bit;
    return {row * 23 + physical / 8, physical % 8};
  }
  const unsigned width = widths[index], whole = width / 8, residual = width % 8;
  return {row / 8 * width + (bit < residual ? 8 * whole + bit :
          row % 8 * whole + (bit - residual) / 8),
          bit < residual ? unsigned(row % 8) : (bit - residual) % 8};
}
void write_bits(Image& image, v::Format format, unsigned index,
                std::size_t row, std::uint64_t value) {
  auto& bytes = image[format == v::Format::rows ? 0 : index];
  for (unsigned bit = 0; bit < widths[index]; ++bit) {
    const auto [offset, shift] = location(format, index, row, bit);
    check(offset < bytes.size(), "independent write oracle exceeded Part");
    const unsigned old = std::to_integer<unsigned>(bytes[offset]);
    bytes[offset] = std::byte((old & ~(1u << shift)) | unsigned((value >> bit) & 1) << shift);
  }
}
void wire(const Image& image, v::Format format, std::span<const Row> rows) {
  for (std::size_t row = 0; row < rows.size(); ++row)
    for (unsigned index = 0; index < v::field_count; ++index)
      for (unsigned bit = 0; bit < widths[index]; ++bit) {
        const auto [offset, shift] = location(format, index, row, bit);
        const auto& bytes = image[format == v::Format::rows ? 0 : index];
        check(offset < bytes.size(), "wire read oracle exceeded Part");
        check(((std::to_integer<unsigned>(bytes[offset]) >> shift) & 1) ==
                  ((field(rows[row], index) >> bit) & 1), "wire bit differs from independent logical Row");
        ++evidence.wire_bits;
      }
}

bool same(const v::Shape& left, const v::Shape& right) {
  if (left.format != right.format || left.records != right.records ||
      left.part_count != right.part_count || left.interpretation != right.interpretation) return false;
  for (unsigned i = 0; i < v::field_count; ++i) {
    const auto& a = left.parts[i]; const auto& b = right.parts[i];
    if (a.id != b.id || a.body_bytes != b.body_bytes || a.alignment != b.alignment ||
        a.readable_padding != b.readable_padding || a.interpretation != b.interpretation) return false;
  }
  return true;
}
bool same(const v::KernelCosts& a, const v::KernelCosts& b) {
  return a.decoded_values == b.decoded_values && a.encoded_values == b.encoded_values &&
      a.bindings == b.bindings && a.constructor_calls == b.constructor_calls &&
      a.issued_bytes == b.issued_bytes && a.issued_spans == b.issued_spans &&
      a.completed_parts == b.completed_parts && a.completed_rows == b.completed_rows &&
      a.scratch_bound_bytes == b.scratch_bound_bytes;
}
bool same(const wb::ProbeCosts& a, const wb::ProbeCosts& b) {
  return a.private_admissions == b.private_admissions && a.key_reads == b.key_reads &&
      a.full_rows_read == b.full_rows_read && a.applied_updates == b.applied_updates;
}
bool same(std::span<const v::Update> a, std::span<const v::Update> b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a[i].key != b[i].key || a[i].record != b[i].record) return false;
  return true;
}
v::KernelCosts seeded_kernel() { return {11, 13, 17, 19, 23, 29, 31, 37, 41}; }
wb::ProbeCosts seeded_probe() { return {43, 47, 53, 59}; }

struct Clone {
  const v::Shape& shape;
  std::array<a::Allocation, v::field_count> backing;
  Clone(a::Pool& pool, const v::Shape& shape, const Image& image) : shape(shape) {
    for (unsigned part = 0; part < shape.part_count; ++part) {
      const auto& required = shape.parts[part];
      check(image[part].size() == required.body_bytes, "clone image does not have exact extent");
      if (!required.body_bytes) continue;
      backing[part] = take(a::allocate(pool, {.bytes = required.body_bytes,
          .alignment = required.alignment, .readable_padding = required.readable_padding,
          .initialization = a::Initialization::uninitialized}), "private clone allocation");
      std::copy(image[part].begin(), image[part].end(), backing[part].address);
    }
  }
  std::array<v::WritePart, v::field_count> writes() const {
    std::array<v::WritePart, v::field_count> result{};
    for (unsigned part = 0; part < shape.part_count; ++part)
      result[part] = {shape.parts[part].id, a::bytes(backing[part])};
    return result;
  }
  Image image() const {
    Image result;
    for (unsigned part = 0; part < shape.part_count; ++part) {
      const auto bytes = a::bytes(backing[part]);
      result[part].assign(bytes.begin(), bytes.end());
    }
    return result;
  }
};

struct Source {
  v::Shape shape;
  std::vector<Row> rows;
  Image original;
  std::array<a::Hold, v::field_count> holds;
  std::array<orbital::Bytes, v::field_count> bodies{};
  Source(a::Pool& pool, const v::Schema& schema, v::Format format, std::size_t count)
      : shape(take(v::shape(schema, format, count), "canonical source shape")), rows(input(count)) {
    Image empty;
    for (unsigned part = 0; part < shape.part_count; ++part)
      empty[part].resize(shape.parts[part].body_bytes);
    Clone source(pool, shape, empty);
    const auto writes = source.writes();
    v::KernelCosts construction;
    check(v::construct_into(schema, shape, std::span(writes).first(shape.part_count), rows,
                            construction).has_value(), "existing source constructor");
    original = source.image();
    // Nonzero preserved storage makes accidental wider stores observable.
    if (format == v::Format::rows) {
      for (std::size_t row = 0; row < count; ++row)
        original[0][row * 23 + 18] |= std::byte(unsigned(row % 15 + 1) << 4);
    } else {
      for (std::size_t row = count; row < (count + 7) / 8 * 8; ++row)
        for (unsigned index = 0; index < v::field_count; ++index)
          write_bits(original, format, index, row, mix(row + index * 117));
    }
    for (unsigned part = 0; part < shape.part_count; ++part) {
      const auto bytes = a::bytes(source.backing[part]);
      std::copy(original[part].begin(), original[part].end(), bytes.begin());
      if (bytes.empty()) continue;
      holds[part] = take(a::hold(source.backing[part], {0, bytes.size()}), "old backing Hold");
      bodies[part] = a::bytes(holds[part]);
      source.backing[part] = {}; // Only the actual Hold protects old physical storage now.
    }
    wire(original, format, rows);
    ++evidence.fixtures;
  }
  void unchanged() const {
    for (unsigned part = 0; part < shape.part_count; ++part)
      check(std::equal(bodies[part].begin(), bodies[part].end(),
                       original[part].begin(), original[part].end()), "retained old backing changed");
    ++evidence.retained_old_checks;
  }
};

void logical(const v::Schema& schema, const v::Shape& shape, const Clone& clone,
             std::span<const Row> expected) {
  std::array<v::ReadPart, v::field_count> reads{};
  for (unsigned part = 0; part < shape.part_count; ++part)
    reads[part] = {shape.parts[part].id, a::bytes(clone.backing[part]), shape.parts[part].interpretation};
  const auto data = take(v::ReadData::bind(schema, shape, std::span(reads).first(shape.part_count)),
                         "result read qualification");
  check(data.size() == expected.size(), "result count changed");
  for (std::size_t row = 0; row < expected.size(); ++row) {
    const auto actual = take(data.row(row), "result checked row");
    for (unsigned index = 0; index < v::field_count; ++index) {
      check(field(actual, index) == field(expected[row], index), "logical result differs from independent Row");
      ++evidence.logical_fields;
    }
  }
}

// No encoded lookup body participates in the expected preflight. Every offered
// key is independently searched linearly, including duplicate keys that will
// eventually be overwritten by a later update. Rejection has no oracle effects.
bool preflight(std::span<const Row> rows, std::span<const v::Update> updates,
               std::array<std::size_t, v::write_batch_limit>& indices) {
  if (updates.empty() || updates.size() > v::write_batch_limit) return false;
  for (std::size_t update = 0; update < updates.size(); ++update) {
    if (updates[update].record.value > orbital_bench::value_mask) return false;
    std::size_t row = 0;
    while (row < rows.size() && rows[row].key != updates[update].key) ++row;
    if (row == rows.size()) return false;
    indices[update] = row;
  }
  return true;
}

// The existing consumer validates one command's width, then looks up that
// command's key, before considering the next command. The expected error must
// preserve that offered-order boundary even though neither rejection writes.
std::optional<orbital::Error> expected_error(std::span<const Row> rows,
                                           std::span<const v::Update> updates) {
  if (updates.empty() || updates.size() > v::write_batch_limit)
    return orbital::Error::invalid_argument;
  for (const auto& update : updates) {
    if (update.record.value > orbital_bench::value_mask)
      return orbital::Error::invalid_argument;
    std::size_t row = 0;
    while (row < rows.size() && rows[row].key != update.key) ++row;
    if (row == rows.size()) return orbital::Error::absent;
  }
  return std::nullopt;
}

void refused_with(orbital::Result<void> result, orbital::Error expected, const char* message) {
  check(!result, message);
  if (result.error() == expected) return;
  std::fprintf(stderr, "private write batch check: %s (expected error %u, actual %u)\n",
               message, unsigned(expected), unsigned(result.error()));
  std::abort();
}

std::vector<v::Update> batch(const Source& source, std::size_t count, bool unchanged = false) {
  std::vector<v::Update> result(count);
  for (std::size_t i = 0; i < count; ++i) {
    if (source.rows.empty()) { result[i] = {1, {0, 0, 0}}; continue; }
    const std::size_t index = i % 4 == 0 ? 0 : i % 4 == 1 ? source.rows.size() - 1 :
        i % 4 == 2 ? source.rows.size() / 2 : i % source.rows.size();
    Record replacement{mix(i + 500), std::uint32_t(mix(i + 600)) & orbital_bench::value_mask,
                       std::uint32_t(mix(i + 700))};
    if (i % 7 == 0) replacement = {UINT64_MAX, orbital_bench::value_mask, UINT32_MAX};
    if (unchanged) replacement = source.rows[index].record;
    result[i] = {source.rows[index].key, replacement};
  }
  return result;
}

void reject(a::Pool& pool, const v::Schema& schema, const Source& source,
            const v::Shape& shape, std::span<const v::Update> updates, bool malformed = false) {
  const auto saved_shape = shape;
  const std::vector<v::Update> saved_updates(updates.begin(), updates.end());
  const auto error = malformed ? std::optional{orbital::Error::invalid_argument} :
      expected_error(source.rows, updates);
  check(error.has_value(), "refusal fixture passed independent offered-order preflight");
  for (const auto operation : operations) {
    Clone target(pool, source.shape, source.original);
    const auto writes = target.writes();
    auto costs = seeded_kernel(); auto probe = seeded_probe();
    const auto old_costs = costs; const auto old_probe = probe;
    refused_with(operation(schema, shape, std::span(writes).first(source.shape.part_count), updates, costs, probe),
                 *error, "invalid complete batch was admitted or reported the wrong first failure");
    check(target.image() == source.original, "rejected batch changed private clone bytes");
    check(same(costs, old_costs) && same(probe, old_probe), "rejected batch changed seeded counters");
    check(same(shape, saved_shape) && same(updates, saved_updates), "rejected batch changed immutable input");
    source.unchanged();
    ++evidence.rejected_calls;
    if (malformed) ++evidence.malformed_calls;
  }
}

void success(a::Pool& pool, const v::Schema& schema, const Source& source,
             std::span<const v::Update> updates, bool duplicate = false, bool unchanged = false) {
  std::array<std::size_t, v::write_batch_limit> indices{};
  check(preflight(source.rows, updates, indices), "test success batch failed independent preflight");
  auto expected_image = source.original;
  auto expected_rows = source.rows;
  for (std::size_t i = 0; i < updates.size(); ++i) {
    expected_rows[indices[i]].record = updates[i].record;
    for (unsigned field_index = 1; field_index < v::field_count; ++field_index)
      write_bits(expected_image, source.shape.format, field_index, indices[i],
                 field(expected_rows[indices[i]], field_index));
  }
  Clone candidate(pool, source.shape, source.original);
  Clone control(pool, source.shape, source.original);
  Clone per_row(pool, source.shape, source.original);
  const auto candidate_writes = candidate.writes(), control_writes = control.writes(), row_writes = per_row.writes();
  const auto shape_before = source.shape;
  const std::vector<v::Update> updates_before(updates.begin(), updates.end());
  v::KernelCosts candidate_costs, control_costs, row_costs;
  wb::ProbeCosts candidate_probe, control_probe;
  check(wb::apply(schema, source.shape, std::span(candidate_writes).first(source.shape.part_count),
                  updates, candidate_costs, candidate_probe).has_value(), "candidate valid complete batch");
  check(wb::generic_control(schema, source.shape, std::span(control_writes).first(source.shape.part_count),
                           updates, control_costs, control_probe).has_value(), "generic valid complete batch");
  for (std::size_t i = 0; i < updates.size(); ++i)
    check(v::update_into(schema, source.shape, std::span(row_writes).first(source.shape.part_count),
                         indices[i], updates[i].record, row_costs).has_value(), "unchanged per-row update control");
  check(candidate.image() == expected_image && control.image() == expected_image &&
            per_row.image() == expected_image, "complete bytes differ from independent per-bit last-wins oracle");
  if (unchanged) check(expected_image == source.original, "same-record oracle changed bytes");
  check(same(source.shape, shape_before) && same(updates, updates_before), "successful batch changed immutable metadata/input");
  wire(candidate.image(), source.shape.format, expected_rows);
  logical(schema, source.shape, candidate, expected_rows);
  logical(schema, source.shape, control, expected_rows);
  // An identical record still issues preserving stores. Effects are issued
  // bytes/spans, not changed-byte counts or a union of final modified bytes.
  for (const auto* costs : {&candidate_costs, &control_costs}) {
    check(costs->encoded_values == row_costs.encoded_values &&
              costs->issued_bytes == row_costs.issued_bytes && costs->issued_spans == row_costs.issued_spans,
          "batch issued effects differ from unchanged per-row controls");
    check(costs->constructor_calls == 0 && costs->completed_parts == 0 && costs->completed_rows == 0,
          "update counters claim construction completion");
  }
  check(row_costs.encoded_values == updates.size() * 3 && row_costs.bindings == updates.size() * 3,
        "per-row reference write accounting differs");
  check(candidate_costs.bindings == 3 && control_costs.bindings == row_costs.bindings,
        "candidate did not bind three field writers once per complete batch");
  check(candidate_probe.private_admissions == 1 && control_probe.private_admissions == 1 &&
            candidate_probe.applied_updates == updates.size() && control_probe.applied_updates == updates.size(),
        "complete-call admission/update accounting differs");
  check(candidate_probe.full_rows_read == 0 && control_probe.full_rows_read == updates.size(),
        "key-only/final-Row control accounting differs");
  check(candidate_probe.key_reads > 0 && control_probe.key_reads > 0,
        "nonempty sorted-key preflight reported no key reads");
  evidence.candidate_bindings += candidate_costs.bindings;
  evidence.control_bindings += control_costs.bindings;
  evidence.issued_bytes += candidate_costs.issued_bytes;
  evidence.issued_spans += candidate_costs.issued_spans;
  evidence.candidate_key_reads += candidate_probe.key_reads;
  evidence.control_full_rows_read += control_probe.full_rows_read;
  evidence.updates += updates.size();
  evidence.duplicate_batches += duplicate;
  evidence.same_record_batches += unchanged;
  ++evidence.successful_batches;
  source.unchanged();
}

void batches(a::Pool& pool, const v::Schema& schema, const Source& source) {
  for (const auto count : {std::size_t{0}, std::size_t{1}, std::size_t{64}, std::size_t{65}}) {
    const auto updates = batch(source, count);
    std::array<std::size_t, v::write_batch_limit> indices{};
    if (preflight(source.rows, updates, indices)) success(pool, schema, source, updates, count > source.rows.size());
    else reject(pool, schema, source, source.shape, updates);
  }
  if (!source.rows.empty()) {
    const auto no_op = batch(source, 64, true);
    success(pool, schema, source, no_op, source.rows.size() < 64, true);
    const auto key = source.rows[source.rows.size() / 2].key;
    const std::array<v::Update, 3> duplicates{{
        {key, {0xaaaaaaaaaaaaaaaaULL, 0xaaaaa, 0xaaaaaaaa}},
        {key, {0x5555555555555555ULL, 0x55555, 0x55555555}},
        {key, {UINT64_MAX, orbital_bench::value_mask, UINT32_MAX}}}};
    success(pool, schema, source, duplicates, true);
  }
  // First/middle/last invalid entries forbid all earlier and later stores,
  // including an invalid duplicate that a later valid entry would overwrite.
  for (const auto position : {std::size_t{0}, std::size_t{32}, std::size_t{63}}) {
    auto updates = batch(source, 64);
    updates[position].key = 1; // Sparse independent keys never contain 1.
    std::array<std::size_t, v::write_batch_limit> indices{};
    check(!preflight(source.rows, updates, indices), "absent input unexpectedly passed independent preflight");
    reject(pool, schema, source, source.shape, updates);
    updates = batch(source, 64);
    updates[position].record.value = orbital_bench::value_mask + 1;
    check(!preflight(source.rows, updates, indices), "over-width input unexpectedly passed independent preflight");
    reject(pool, schema, source, source.shape, updates);
  }
  auto mixed = batch(source, 64);
  mixed.front().key = 1;
  mixed.back().record.value = orbital_bench::value_mask + 1;
  check(expected_error(source.rows, mixed) == orbital::Error::absent,
        "absent-first mixed oracle error differs");
  reject(pool, schema, source, source.shape, mixed);
  mixed = batch(source, 64);
  mixed.front().record.value = orbital_bench::value_mask + 1;
  mixed.back().key = 1;
  check(expected_error(source.rows, mixed) == orbital::Error::invalid_argument,
        "over-width-first mixed oracle error differs");
  reject(pool, schema, source, source.shape, mixed);
}

template<class T> T* aligned_object_storage(a::Allocation& backing) {
  const auto first = reinterpret_cast<std::uintptr_t>(backing.address);
  const auto offset = (alignof(T) - first % alignof(T)) % alignof(T);
  check(offset <= backing.size && sizeof(T) <= backing.size - offset,
        "alias fixture object does not fit actual backing");
  return reinterpret_cast<T*>(backing.address + offset);
}

void aliases(a::Pool& pool, const v::Schema& schema, const Source& source,
             std::span<const v::Update> updates) {
  // Construct real, aligned trivial objects in protected backing before each
  // malformed call. No reference to an object without an active lifetime is
  // used. This deliberately violates target/command/metadata disjointness;
  // the baseline for byte preservation is the image after fixture placement.
  const auto verify = [&](const Clone& target, const Image& before,
                          const v::KernelCosts& costs, const wb::ProbeCosts& probe) {
    check(target.image() == before, "known-object overlap refusal changed target bytes");
    check(same(costs, seeded_kernel()) && same(probe, seeded_probe()),
          "known-object overlap refusal changed seeded counters");
    source.unchanged();
    ++evidence.rejected_calls; ++evidence.malformed_calls;
  };
  for (const auto operation : operations) {
    {
      Clone target(pool, source.shape, source.original);
      auto* input = std::construct_at(aligned_object_storage<v::Update>(target.backing[0]), updates[0]);
      const auto before = target.image();
      const auto writes = target.writes();
      auto costs = seeded_kernel(); auto probe = seeded_probe();
      refused_with(operation(schema, source.shape, std::span(writes).first(source.shape.part_count),
                             std::span(input, 1), costs, probe), orbital::Error::invalid_argument,
                   "input/target overlap admitted or reported wrong error");
      check(input->key == updates[0].key && input->record == updates[0].record,
            "overlap refusal changed input object");
      verify(target, before, costs, probe);
      std::destroy_at(input);
    }
    {
      Clone target(pool, source.shape, source.original);
      auto* costs = std::construct_at(aligned_object_storage<v::KernelCosts>(target.backing[0]), seeded_kernel());
      const auto before = target.image();
      const auto writes = target.writes();
      auto probe = seeded_probe();
      refused_with(operation(schema, source.shape, std::span(writes).first(source.shape.part_count),
                             updates, *costs, probe), orbital::Error::invalid_argument,
                   "KernelCosts/target overlap admitted or reported wrong error");
      verify(target, before, *costs, probe);
      std::destroy_at(costs);
    }
    {
      Clone target(pool, source.shape, source.original);
      auto* probe = std::construct_at(aligned_object_storage<wb::ProbeCosts>(target.backing[0]), seeded_probe());
      const auto before = target.image();
      const auto writes = target.writes();
      auto costs = seeded_kernel();
      refused_with(operation(schema, source.shape, std::span(writes).first(source.shape.part_count),
                             updates, costs, *probe), orbital::Error::invalid_argument,
                   "ProbeCosts/target overlap admitted or reported wrong error");
      verify(target, before, costs, *probe);
      std::destroy_at(probe);
    }
    {
      Clone target(pool, source.shape, source.original);
      using Packet = std::array<v::WritePart, v::field_count>;
      auto* packet = std::construct_at(aligned_object_storage<Packet>(target.backing[0]), target.writes());
      const auto before = target.image();
      const auto packet_before = *packet;
      auto costs = seeded_kernel(); auto probe = seeded_probe();
      refused_with(operation(schema, source.shape, std::span(*packet).first(source.shape.part_count),
                             updates, costs, probe), orbital::Error::invalid_argument,
                   "WritePart packet/target overlap admitted or reported wrong error");
      for (unsigned i = 0; i < packet->size(); ++i)
        check((*packet)[i].id == packet_before[i].id &&
                  (*packet)[i].bytes.data() == packet_before[i].bytes.data() &&
                  (*packet)[i].bytes.size() == packet_before[i].bytes.size(),
              "overlap refusal changed WritePart object");
      verify(target, before, costs, probe);
      std::destroy_at(packet);
    }
  }
}

void malformed(a::Pool& pool, const v::Schema& schema, const Source& source) {
  check(source.rows.size() == 9, "malformed fixture count");
  const auto updates = batch(source, 1);
  const auto shape_refusal = [&](auto mutate) {
    auto shape = source.shape;
    mutate(shape);
    reject(pool, schema, source, shape, updates, true);
  };
  shape_refusal([](auto& s) { s.format = v::Format(0); });
  shape_refusal([](auto& s) { ++s.records; });
  shape_refusal([](auto& s) { s.records = SIZE_MAX; });
  shape_refusal([](auto& s) { --s.part_count; });
  shape_refusal([](auto& s) { ++s.part_count; });
  shape_refusal([](auto& s) { s.interpretation.pop_back(); });
  shape_refusal([](auto& s) { s.interpretation.push_back(std::byte{}); });
  shape_refusal([](auto& s) { s.interpretation[20] = std::byte{1}; });
  shape_refusal([](auto& s) { s.interpretation[8] ^= std::byte{1}; });
  if (source.shape.format == v::Format::rows)
    shape_refusal([](auto& s) { s.parts[1].body_bytes = 1; });
  for (unsigned part = 0; part < source.shape.part_count; ++part) {
    shape_refusal([&](auto& s) { s.parts[part].id = {}; });
    shape_refusal([&](auto& s) { ++s.parts[part].body_bytes; });
    shape_refusal([&](auto& s) { s.parts[part].alignment = s.format == v::Format::rows ? 2 : 32; });
    shape_refusal([&](auto& s) { s.parts[part].readable_padding = 1; });
    shape_refusal([&](auto& s) { s.parts[part].interpretation.pop_back(); });
    shape_refusal([&](auto& s) { s.parts[part].interpretation.push_back(std::byte{}); });
    shape_refusal([&](auto& s) { s.parts[part].interpretation[0] ^= std::byte{1}; });
    shape_refusal([&](auto& s) { s.parts[part].interpretation.back() ^= std::byte{1}; });
  }

  // Every provided malformed span still lies within actual protected backing,
  // except the explicitly null span, which must be rejected before access.
  for (const auto operation : operations) {
    Clone target(pool, source.shape, source.original);
    const auto base = target.writes();
    const auto shape_before = source.shape;
    auto guard = take(a::allocate(pool, {.bytes = source.shape.parts[0].body_bytes + 128,
        .alignment = 64, .initialization = a::Initialization::uninitialized}), "placement guard allocation");
    std::fill(a::bytes(guard).begin(), a::bytes(guard).end(), std::byte{0x5a});
    const std::vector<std::byte> guard_before(a::bytes(guard).begin(), a::bytes(guard).end());
    const auto refuse = [&](std::span<const v::WritePart> parts) {
      auto costs = seeded_kernel(); auto probe = seeded_probe();
      const auto old_costs = costs; const auto old_probe = probe;
      const std::vector<v::WritePart> parts_before(parts.begin(), parts.end());
      refused_with(operation(schema, source.shape, parts, updates, costs, probe),
                   orbital::Error::invalid_argument, "malformed output placement admitted or reported wrong error");
      check(target.image() == source.original, "placement refusal changed private body");
      check(std::equal(a::bytes(guard).begin(), a::bytes(guard).end(), guard_before.begin(), guard_before.end()),
            "placement refusal changed guard buffer");
      check(same(costs, old_costs) && same(probe, old_probe), "placement refusal changed seeded counters");
      check(same(source.shape, shape_before), "placement refusal changed Shape");
      for (std::size_t i = 0; i < parts.size(); ++i)
        check(parts[i].id == parts_before[i].id && parts[i].bytes.data() == parts_before[i].bytes.data() &&
                  parts[i].bytes.size() == parts_before[i].bytes.size(), "placement refusal changed WritePart packet");
      source.unchanged();
      ++evidence.rejected_calls; ++evidence.malformed_calls;
    };
    refuse(std::span(base).first(source.shape.part_count - 1));
    std::array<v::WritePart, v::field_count + 1> extra{};
    std::copy(base.begin(), base.end(), extra.begin());
    extra[source.shape.part_count] = base[0];
    refuse(std::span(extra).first(source.shape.part_count + 1));
    for (unsigned part = 0; part < source.shape.part_count; ++part) {
      auto parts = base;
      parts[part].id = {};
      refuse(std::span(parts).first(source.shape.part_count));
      parts = base;
      parts[part].bytes = parts[part].bytes.first(parts[part].bytes.size() - 1);
      refuse(std::span(parts).first(source.shape.part_count));
      parts = base;
      parts[part].bytes = a::bytes(guard).first(source.shape.parts[part].body_bytes + 1);
      refuse(std::span(parts).first(source.shape.part_count));
      parts = base;
      parts[part].bytes = {static_cast<std::byte*>(nullptr), source.shape.parts[part].body_bytes};
      refuse(std::span(parts).first(source.shape.part_count));
      if (source.shape.format == v::Format::columns) {
        parts = base;
        parts[part].bytes = a::bytes(guard).subspan(1, source.shape.parts[part].body_bytes);
        refuse(std::span(parts).first(source.shape.part_count));
      }
    }
    if (source.shape.format == v::Format::columns) {
      auto parts = base;
      parts[1].bytes = parts[0].bytes; // Equal 64-bit extents; overlapping distinct Parts.
      refuse(std::span(parts).first(source.shape.part_count));
      parts = base;
      parts[2].bytes = parts[0].bytes.first(parts[2].bytes.size());
      refuse(std::span(parts).first(source.shape.part_count));
    }
  }
  aliases(pool, schema, source, updates);
}

void metadata_object_overlaps(a::Pool& pool, const v::Schema& schema, const Source& source) {
  // 2048 rows are storage for complete real metadata objects, not another
  // operation-size or giant-count coverage claim. In particular the 16KiB
  // key column fits the full prepared Schema on both qualified architectures.
  check(source.rows.size() == 2048, "metadata-object overlap fixture count");
  const auto updates = batch(source, 1);
  const auto source_shape_before = source.shape;
  for (const auto operation : operations) {
    {
      Clone target(pool, source.shape, source.original);
      auto* placed = std::construct_at(aligned_object_storage<v::Schema>(target.backing[0]));
      const auto before = target.image();
      const auto metadata = orbital::Bytes{reinterpret_cast<const std::byte*>(placed), sizeof(*placed)};
      const std::vector<std::byte> metadata_before(metadata.begin(), metadata.end());
      const auto writes = target.writes();
      auto costs = seeded_kernel(); auto probe = seeded_probe();
      refused_with(operation(*placed, source.shape, std::span(writes).first(source.shape.part_count),
                             updates, costs, probe), orbital::Error::invalid_argument,
                   "actual Schema/target overlap admitted or reported wrong error");
      check(target.image() == before &&
                std::memcmp(placed, metadata_before.data(), sizeof(*placed)) == 0,
            "Schema overlap refusal changed body or prepared object metadata");
      check(same(costs, seeded_kernel()) && same(probe, seeded_probe()),
            "Schema overlap refusal changed seeded counters");
      check(same(source.shape, source_shape_before), "Schema overlap refusal changed supplied Shape");
      source.unchanged();
      ++evidence.rejected_calls; ++evidence.malformed_calls;
      std::destroy_at(placed); // End the actual Schema before Allocation release.
    }
    {
      Clone target(pool, source.shape, source.original);
      auto* placed = std::construct_at(aligned_object_storage<v::Shape>(target.backing[0]), source.shape);
      const auto before = target.image();
      const auto metadata_before = *placed; // Independently owned descriptor snapshots.
      const auto writes = target.writes();
      auto costs = seeded_kernel(); auto probe = seeded_probe();
      refused_with(operation(schema, *placed, std::span(writes).first(source.shape.part_count),
                             updates, costs, probe), orbital::Error::invalid_argument,
                   "actual Shape/target overlap admitted or reported wrong error");
      check(target.image() == before && same(*placed, metadata_before),
            "Shape overlap refusal changed body or owned object metadata");
      check(same(costs, seeded_kernel()) && same(probe, seeded_probe()),
            "Shape overlap refusal changed seeded counters");
      check(same(source.shape, source_shape_before), "Shape overlap refusal changed source Shape");
      source.unchanged();
      ++evidence.rejected_calls; ++evidence.malformed_calls;
      std::destroy_at(placed); // Owned vectors end while real backing is live.
    }
  }
  ++evidence.metadata_overlap_fixtures;
}

void descriptor_storage_overlaps(a::Pool& pool, const v::Schema& schema, const Source& source) {
  check(source.shape.format == v::Format::rows && source.rows.size() == 1,
        "descriptor-storage overlap fixture must be one Tuple row");
  const auto updates = batch(source, 1);
  for (const auto operation : operations)
    for (const bool parent : {true, false}) {
      Clone target(pool, source.shape, source.original);
      auto shape = source.shape;
      const auto metadata_before = shape;
      auto writes = target.writes();
      // These are actual owned metadata buffers (24B/75B). Their first 23B
      // deliberately replace the claimed writable body; alignment1 admits the
      // address but the known storage overlap must reject before encoded use.
      auto descriptor = parent ? std::span(shape.interpretation) :
                                 std::span(shape.parts[0].interpretation);
      check(descriptor.size() >= v::tuple_bytes, "descriptor alias body does not fit");
      writes[0].bytes = descriptor.first(v::tuple_bytes);
      auto costs = seeded_kernel(); auto probe = seeded_probe();
      refused_with(operation(schema, shape, std::span(writes).first(1), updates, costs, probe),
                   orbital::Error::invalid_argument,
                   "actual descriptor-storage/target overlap admitted or reported wrong error");
      check(same(shape, metadata_before), "descriptor overlap refusal changed descriptor/object metadata");
      check(target.image() == source.original, "descriptor overlap refusal changed private clone");
      check(same(costs, seeded_kernel()) && same(probe, seeded_probe()),
            "descriptor overlap refusal changed seeded counters");
      source.unchanged();
      ++evidence.rejected_calls; ++evidence.malformed_calls;
    }
  ++evidence.metadata_overlap_fixtures;
}

void run(a::Pool& pool, const v::Schema& schema, v::Format format, std::size_t count) {
  Source source(pool, schema, format, count);
  batches(pool, schema, source);
  if (count == 9) malformed(pool, schema, source);
  source.unchanged();
}
} // namespace

int main() {
  {
    Runtime runtime;
    // Writes require the actual prepared write plans. Schema remains alive
    // through every complete call, reference update and secondary read oracle;
    // its retained bytes are reported rather than hidden behind a pointer.
    const v::Schema schema;
    for (const auto format : {v::Format::rows, v::Format::columns})
      for (const std::size_t count : {0u, 1u, 7u, 8u, 9u, 63u, 64u, 65u, 129u})
        run(*runtime.pool, schema, format, count);
    for (const auto format : {v::Format::rows, v::Format::columns}) {
      Source metadata_fixture(*runtime.pool, schema, format, 2048);
      metadata_object_overlaps(*runtime.pool, schema, metadata_fixture);
    }
    Source descriptor_fixture(*runtime.pool, schema, v::Format::rows, 1);
    descriptor_storage_overlaps(*runtime.pool, schema, descriptor_fixture);
  }
  std::cout << "{\"check\":\"private_complete_write_batch\",\"fixtures\":" << evidence.fixtures
      << ",\"successful_batches\":" << evidence.successful_batches
      << ",\"rejected_calls\":" << evidence.rejected_calls
      << ",\"malformed_calls\":" << evidence.malformed_calls
      << ",\"duplicate_batches\":" << evidence.duplicate_batches
      << ",\"same_record_batches\":" << evidence.same_record_batches
      << ",\"updates\":" << evidence.updates
      << ",\"logical_fields\":" << evidence.logical_fields
      << ",\"wire_bits\":" << evidence.wire_bits
      << ",\"retained_old_checks\":" << evidence.retained_old_checks
      << ",\"metadata_overlap_fixtures\":" << evidence.metadata_overlap_fixtures
      << ",\"allocation_calls\":" << evidence.allocation_calls
      << ",\"allocation_reuses\":" << evidence.allocation_reuses
      << ",\"candidate_bindings\":" << evidence.candidate_bindings
      << ",\"control_bindings\":" << evidence.control_bindings
      << ",\"issued_bytes\":" << evidence.issued_bytes
      << ",\"issued_spans\":" << evidence.issued_spans
      << ",\"candidate_key_reads\":" << evidence.candidate_key_reads
      << ",\"control_full_rows_read\":" << evidence.control_full_rows_read
      << ",\"retained_schema_bytes\":" << sizeof(v::Schema)
      << ",\"retained_writer_plans\":4,\"physical_close_zero_charge\":true"
         ",\"hardware_or_timing_claim\":false}\n";
}
