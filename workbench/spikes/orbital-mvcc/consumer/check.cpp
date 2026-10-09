#include "consumer.h"
#include "native.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace {
namespace v = orbital_bench::versioned;
namespace obj = orbital::object;
namespace a = orbital::allocation;
using orbital_bench::Row;
constexpr orbital::ObjectId object_a{0x56455253494f4e44ULL, 1};
constexpr orbital::ObjectId object_b{0x56455253494f4e44ULL, 2};
constexpr std::array<unsigned, 4> widths{64, 64, 20, 32};

struct Evidence {
  std::uint64_t histories{}, publications{}, adoptions{}, conversions{}, updates{};
  std::uint64_t logical_fields{}, wire_bits{}, refusals{}, metadata_refusals{};
  std::uint64_t private_injections{}, independently_released_parts{};
  v::Costs costs;
} evidence;

void check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "versioned baseline history check: %s\n", message);
  std::abort();
}
template<class T, class E> T take(std::expected<T, E> result, const char* message) {
  if (!result) {
    std::fprintf(stderr, "versioned baseline history check: %s (error %u)\n",
                 message, unsigned(result.error()));
    std::abort();
  }
  return std::move(*result);
}
void require(orbital::Result<void> result, const char* message) {
  check(result.has_value(), message);
}
template<class T> void refused(const orbital::Result<T>& result, orbital::Error error,
                                const char* message) {
  check(!result && result.error() == error, message);
  ++evidence.refusals;
}
template<class T> void refused(const orbital::Result<T>& result, const char* message) {
  check(!result, message);
  ++evidence.refusals;
}
void account(const v::Costs& costs) {
  auto& result = evidence.costs;
  result.requested_body_bytes += costs.requested_body_bytes;
  result.requested_parts += costs.requested_parts;
  result.copied_bytes += costs.copied_bytes;
  result.copied_spans += costs.copied_spans;
  auto& kernel = result.kernel;
  kernel.decoded_values += costs.kernel.decoded_values;
  kernel.encoded_values += costs.kernel.encoded_values;
  kernel.bindings += costs.kernel.bindings;
  kernel.constructor_calls += costs.kernel.constructor_calls;
  kernel.issued_bytes += costs.kernel.issued_bytes;
  kernel.issued_spans += costs.kernel.issued_spans;
  kernel.completed_parts += costs.kernel.completed_parts;
  kernel.completed_rows += costs.kernel.completed_rows;
  kernel.scratch_bound_bytes = std::max(kernel.scratch_bound_bytes,
                                        costs.kernel.scratch_bound_bytes);
}

struct Runtime {
  orbital::worker::Lane lane;
  orbital::Ref<a::Arena> arena;
  a::Pool* pool{};
  explicit Runtime(std::uint64_t limit = 64 * 1024 * 1024) {
    require(orbital::worker::adopt(lane), "Lane adoption");
    arena = take(a::open_arena(limit), "Arena open");
    pool = take(a::add_pool(*arena, lane), "Pool creation");
  }
  ~Runtime() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
      const auto progress = a::close(*pool);
      check(progress.kind() != orbital::Progress::Kind::failed, "Pool close failed");
      if (progress.kind() == orbital::Progress::Kind::done) break;
      check(std::chrono::steady_clock::now() < deadline, "live physical use at close deadline");
      orbital::worker::poll(lane, 1024);
      std::this_thread::yield();
    }
    check(arena->charged_bytes.load() == 0, "final Arena charge is nonzero");
    check(a::statistics(*pool).metadata_bytes == 0, "final Pool metadata is nonzero");
    require(a::remove_pool(*pool), "closed Pool removal");
    arena.reset();
    orbital::worker::relinquish(lane);
  }
};
void drain(obj::Store& store) {
  bool finished{};
  for (unsigned pass = 0; pass < 128; ++pass)
    if (!store.drain(4096)) { finished = true; break; }
  check(finished && store.statistics().retirement_records == 0, "metadata retirement did not drain");
}
obj::Store open(Runtime& runtime, std::size_t limit = 8 * 1024 * 1024) {
  return take(obj::Store::open(runtime.lane, limit), "Store open");
}

std::uint64_t mix(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}
std::vector<Row> records(std::size_t count, unsigned seed = 0) {
  std::vector<Row> result(count);
  for (std::size_t row = 0; row < count; ++row) {
    result[row] = {0x8000000000000100ULL + row * 19,
        {mix(row + 11 + seed), std::uint32_t(mix(row + 59 + seed)) & orbital_bench::value_mask,
         std::uint32_t(mix(row + 89 + seed))}};
    if (row % 7 == 0) result[row].record = {0, 0, 0};
    if (row % 7 == 1) result[row].record = {UINT64_MAX, orbital_bench::value_mask, UINT32_MAX};
  }
  return result;
}
std::uint64_t field(const Row& row, unsigned index) {
  switch (index) {
  case 0: return row.key;
  case 1: return row.record.next;
  case 2: return row.record.value;
  case 3: return row.record.tag;
  default: check(false, "oracle field escaped schema"); return 0;
  }
}
unsigned byte(orbital::Bytes bytes, std::size_t offset) {
  check(offset < bytes.size(), "physical oracle exceeded Part extent");
  return std::to_integer<unsigned>(bytes[offset]);
}
std::array<std::byte, 8> logical(std::size_t count) {
  std::array<std::byte, 8> result;
  for (unsigned i = 0; i < result.size(); ++i) result[i] = std::byte((count >> (8 * i)) & 255);
  return result;
}
template<class Data> void equal(const Data& data, std::span<const Row> expected) {
  check(data.size() == expected.size(), "logical count differs");
  for (std::size_t row = 0; row < expected.size(); ++row) {
    const auto actual = take(data.row(row), "in-range read");
    for (unsigned index = 0; index < widths.size(); ++index) {
      check(field(actual, index) == field(expected[row], index), "logical field differs from input oracle");
      ++evidence.logical_fields;
    }
  }
  refused(data.row(expected.size()), orbital::Error::invalid_argument, "out-of-range row admitted");
}
template<class Body> void thin_reads(const Body& body, std::span<const Row> expected) {
  equal(body, expected);
  for (unsigned index = 0; index < widths.size(); ++index) {
    std::vector<std::uint64_t> values(expected.size());
    require(body.field(static_cast<v::Field>(index), 0, values), "thin field range");
    for (std::size_t row = 0; row < expected.size(); ++row) {
      check(values[row] == field(expected[row], index), "thin field differs from independent oracle");
      check(body.field_unchecked(static_cast<v::Field>(index), row) == field(expected[row], index),
            "thin point field differs from independent oracle");
      check(body.row_unchecked(row) == expected[row], "thin unchecked row differs from input oracle");
      ++evidence.logical_fields;
    }
    const auto first = std::min(std::size_t{3}, expected.size());
    values.resize(expected.size() - first);
    require(body.field(static_cast<v::Field>(index), first, values), "thin unaligned field range");
    for (std::size_t i = 0; i < values.size(); ++i)
      check(values[i] == field(expected[first + i], index), "thin sliced field differs from input oracle");
  }
  std::array<std::uint64_t, 1> untouched{0xf00dcafe12345678ULL};
  refused(body.field(v::Field::value, expected.size(), untouched), orbital::Error::invalid_argument,
          "thin out-of-range field admitted");
  check(untouched[0] == 0xf00dcafe12345678ULL, "thin refused field changed output");
  refused(body.field(static_cast<v::Field>(widths.size()), 0, untouched),
          orbital::Error::invalid_argument, "thin unknown field admitted");
  check(untouched[0] == 0xf00dcafe12345678ULL, "thin refused unknown field changed output");
  require(body.field(v::Field::tag, expected.size(), {}), "thin empty final field range");

  const auto probe = [&](std::uint64_t key) {
    const auto found = std::lower_bound(expected.begin(), expected.end(), key,
        [](const Row& row, std::uint64_t sought) { return row.key < sought; });
    const auto first = expected.empty() ? 0 : std::size_t(found - expected.begin());
    check(body.lower_bound(key) == first, "thin lower_bound differs from sorted input oracle");
    const auto record = body.get(key);
    const bool present = found != expected.end() && found->key == key;
    check(bool(record) == present, "thin lookup presence differs from input oracle");
    if (present) check(*record == found->record, "thin lookup record differs from input oracle");
    std::array<Row, 19> output;
    const Row sentinel{0xdeadbeefdeadbeefULL, {0xdeadbeefdeadbeefULL, 0xdeadbeef, 0xdeadbeef}};
    output.fill(sentinel);
    const auto count = std::min(output.size(), expected.size() - first);
    check(body.scan(key, output) == count, "thin scan count differs from input oracle");
    for (std::size_t i = 0; i < count; ++i)
      check(output[i] == expected[first + i], "thin scan row differs from input oracle");
    for (std::size_t i = count; i < output.size(); ++i)
      check(output[i] == sentinel, "thin scan changed output past returned rows");
    check(body.scan(key, {}) == 0, "thin empty scan returned rows");
  };
  probe(0);
  probe(UINT64_MAX);
  for (const auto& row : expected) {
    probe(row.key);
    probe(row.key + 1);
  }
}
template<class Cursor, class Control>
auto thin_body_after_scope(const Cursor& cursor, const Control& control) {
  const auto local_control = control;
  const auto local_cursor = cursor;
  auto original = local_cursor.open(local_control);
  const auto moved = std::move(original);
  return moved;
}
void verify_thin(const v::Schema& schema, obj::Selection access,
                 std::span<const Row> expected, v::Format format) {
  if (format == v::Format::rows) {
    const v::TupleControl control{schema, expected.size()};
    const auto cursor = take(v::qualify_tuple(control, access), "thin selected Tuple qualification");
    thin_reads(thin_body_after_scope(cursor, control), expected);
    const auto part = take(access.part(std::size_t{0}), "thin complete Tuple Part");
    const auto direct = take(v::qualify_tuple(control, part), "thin direct Tuple Part qualification");
    thin_reads(direct.open(control), expected);
  } else {
    const v::ColumnControl control{expected.size()};
    const auto cursors = take(v::qualify_columns(schema, control, access), "thin columns qualification");
    thin_reads(thin_body_after_scope(cursors, control), expected);
  }
}
// This mapping is independent of Ikea binding and the fixture decoder. Checking
// both directions against it prevents mutually compensating codec map errors.
void wire(obj::View view, unsigned column, std::span<const Row> expected, bool rows,
          std::span<const unsigned> spare = {}) {
  const auto bytes = view.body();
  if (rows) {
    check(bytes.size() == expected.size() * 23, "Tuple occupied extent differs");
    constexpr std::array<unsigned, 4> first{0, 64, 128, 152};
    for (std::size_t row = 0; row < expected.size(); ++row) {
      for (unsigned index = 0; index < widths.size(); ++index)
        for (unsigned bit = 0; bit < widths[index]; ++bit) {
          const auto physical = first[index] + bit;
          check(((byte(bytes, row * 23 + physical / 8) >> (physical % 8)) & 1) ==
                    ((field(expected[row], index) >> bit) & 1), "Tuple bit differs from independent oracle");
          ++evidence.wire_bits;
        }
      check((byte(bytes, row * 23 + 18) >> 4) == (spare.empty() ? 0 : spare[row]),
            "Tuple spare nibble differs from initialized oracle");
    }
  } else {
    const auto width = widths[column], whole = width / 8, residual = width % 8;
    check(bytes.size() == (expected.size() + 7) / 8 * width, "Series tight tile extent differs");
    for (std::size_t row = 0; row < (expected.size() + 7) / 8 * 8; ++row) {
      const auto value = row < expected.size() ? field(expected[row], column) : 0;
      for (unsigned bit = 0; bit < width; ++bit) {
        const auto offset = row / 8 * width + (bit < residual ? 8 * whole + bit :
                            row % 8 * whole + (bit - residual) / 8);
        const auto physical = bit < residual ? row % 8 : (bit - residual) % 8;
        check(((byte(bytes, offset) >> physical) & 1) == ((value >> bit) & 1),
              "Series bit or initialized tail slack differs from independent oracle");
        ++evidence.wire_bits;
      }
    }
  }
}
template<class Access>
void verify(const v::Schema& schema, const Access& access, std::span<const Row> expected,
            v::Format format) {
  const auto data = take(v::bind(schema, access), "qualified native bind");
  check(data.format() == format, "physical representation changed unexpectedly");
  equal(data, expected);
  const auto description = access.description();
  const auto metadata = logical(expected.size());
  check(description.format == v::data_program && description.logical_metadata.size() == 8 &&
            std::equal(metadata.begin(), metadata.end(), description.logical_metadata.begin()),
        "immutable logical metadata differs");
  check(access.part_count() == (format == v::Format::rows ? 1 : 4), "selected physical Part count differs");
  for (std::size_t index = 0; index < access.part_count(); ++index) {
    const auto part = take(access.part(index), "selected Part lookup");
    check(part.description().object == description.object &&
              part.description().version == description.version &&
              part.description().interpretation.data() == description.interpretation.data(),
          "Part escaped parent immutable description closure");
    wire(part, unsigned(index), expected, format == v::Format::rows);
  }
}
obj::Representation construct(const v::Schema& schema, obj::Store& store, Runtime& runtime,
    orbital::ObjectId id, std::uint64_t version, std::span<const Row> input, v::Format format) {
  v::Costs costs;
  auto result = take(v::construct(schema, store, *runtime.pool, id, version, input, format, costs),
                     "complete representation construction");
  account(costs);
  verify(schema, result, input, format);
  return result;
}
obj::Representation convert(const v::Schema& schema, obj::Store& store, Runtime& runtime,
                            obj::Selection source, v::Format target) {
  v::Costs costs;
  auto result = take(v::convert(schema, store, *runtime.pool, source, target, costs),
                     "complete representation conversion");
  account(costs);
  ++evidence.conversions;
  check(result.description().object == source.description().object &&
            result.description().version == source.description().version,
        "conversion changed selected logical identity");
  return result;
}
void publish(obj::Store& store, obj::Draft& draft) {
  auto prepared = take(store.prepare(draft), "finished Draft preparation");
  check(prepared.disposition() == obj::Disposition::accepted && !draft.version(),
        "prepare did not consume Draft/issue accepted authority");
  require(store.install(prepared), "accepted publication");
  check(prepared.disposition() == obj::Disposition::installed, "publication disposition differs");
  prepared.retire();
  check(prepared.disposition() == obj::Disposition::empty, "installed authority did not retire");
  ++evidence.publications;
}
obj::Representation updated(const v::Schema& schema, obj::Store& store, Runtime& runtime,
                           obj::Selection source, std::uint64_t version,
                           std::span<const v::Update> changes) {
  v::Costs costs;
  auto result = take(v::update(schema, store, *runtime.pool, source, version, changes, costs),
                     "private updated representation");
  account(costs);
  ++evidence.updates;
  check(result.description().version == version && costs.copied_spans == source.part_count(),
        "update did not copy each initialized Part into requested private version");
  return result;
}

void conversion_boundaries(const v::Schema& schema, Runtime& runtime) {
  for (auto format : {v::Format::rows, v::Format::columns})
    for (auto count : {std::size_t{0}, std::size_t{1}, std::size_t{129}}) {
      auto store = open(runtime);
      const auto expected = records(count);
      auto draft = take(store.edit(), "initial Draft");
      require(draft.put(construct(schema, store, runtime, object_a, draft.version(), expected, format)),
              "insert immutable result");
      publish(store, draft);
      {
        auto context = take(store.read(), "conversion context");
        const auto source = take(obj::select(context, object_a), "conversion basis");
        verify(schema, source, expected, format);
        verify_thin(schema, source, expected, format);
        const auto target = format == v::Format::rows ? v::Format::columns : v::Format::rows;
        auto converted = convert(schema, store, runtime, source, target);
        verify(schema, converted, expected, target);
        const auto cut = store.statistics().completed_cut;
        require(store.adopt(source, std::move(converted)), "same-version alternative adoption");
        ++evidence.adoptions;
        check(store.statistics().completed_cut == cut, "physical adoption advanced logical cut");
        const auto alternative = take(obj::select(context, object_a, true), "alternative selection");
        verify(schema, alternative, expected, target);
        verify_thin(schema, alternative, expected, target);
        // The earlier descriptor remains the original physical access.
        verify(schema, source, expected, format);
        auto roundtrip = convert(schema, store, runtime, alternative, format);
        verify(schema, roundtrip, expected, format);
        if (target == v::Format::rows) {
          const auto view = take(alternative.part(std::size_t{0}), "complete Tuple View");
          equal(take(v::bind(schema, view), "direct complete Tuple View bind"), expected);
        } else {
          refused(v::bind(schema, take(alternative.part(std::size_t{0}), "single column")),
                  "single column falsely binds complete four-field consumer");
        }
      }
      drain(store);
      ++evidence.histories;
    }
}

void coherent_history(const v::Schema& schema, Runtime& runtime, v::Format format) {
  auto store = open(runtime);
  auto old_a = records(129, 0), old_b = records(129, 1000);
  auto new_a = old_a, new_b = old_b;
  auto initial = take(store.edit(), "initial coherent Draft");
  require(initial.put(construct(schema, store, runtime, object_a, initial.version(), old_a, format)),
          "initial A insertion");
  require(initial.put(construct(schema, store, runtime, object_b, initial.version(), old_b, format)),
          "initial B insertion");
  publish(store, initial);
  obj::RetainedSelection escaped_selection;
  obj::RetainedView escaped_tuple;
  {
    auto old_context = take(store.read(), "old coherent context");
    const auto basis_a = take(obj::select(old_context, object_a), "old A basis");
    const auto first_part = take(basis_a.part(std::size_t{0}), "first resolved old Part");
    wire(first_part, 0, old_a, format == v::Format::rows);
    const auto old_version = basis_a.description().version;
    // B remains unresolved until after the publication below.
    auto draft = take(store.edit(), "multi-object update Draft");
    const std::array first_changes{v::Update{old_a[0].key, {0x123456789abcdef0ULL, 0x54321, 0xa5a5a5a5}}};
    new_a[0].record = first_changes[0].record;
    require(draft.put(updated(schema, store, runtime, basis_a, draft.version(), first_changes)),
            "first private A update");
    const auto own_a = take(draft.select(object_a), "Draft read-own-write selection");
    verify(schema, own_a, new_a, format);
    const std::array second_changes{v::Update{old_a[128].key, {0xfedcba9876543210ULL, 0x12345, 0x5a5a5a5a}}};
    new_a[128].record = second_changes[0].record;
    // Same Draft version is legitimate private read-own-write derivation.
    require(draft.put(updated(schema, store, runtime, own_a, draft.version(), second_changes)),
            "same-version second private A update");
    const auto own_b = take(draft.select(object_b), "B selected from Draft basis");
    const std::array b_changes{v::Update{old_b[64].key, {old_a[0].key, 0xabcde, 0x77777777}}};
    new_b[64].record = b_changes[0].record;
    require(draft.put(updated(schema, store, runtime, own_b, draft.version(), b_changes)),
            "private B update");
    verify(schema, take(draft.select(object_a), "completed private A"), new_a, format);
    publish(store, draft);
    // Discovery of the remaining Parts and previously unresolved Object B
    // occurs after publication, while the old context still owns its cut.
    verify(schema, basis_a, old_a, format);
    verify(schema, take(obj::select(old_context, object_b), "late old B discovery"), old_b, format);
    auto fresh = take(store.read(), "fresh coherent context");
    verify(schema, take(obj::select(fresh, object_a), "fresh A"), new_a, format);
    verify(schema, take(obj::select(fresh, object_b), "fresh B"), new_b, format);
    const auto target = format == v::Format::rows ? v::Format::columns : v::Format::rows;
    auto old_conversion = convert(schema, store, runtime, basis_a, target);
    check(old_conversion.description().version == old_version &&
              old_version != take(obj::select(fresh, object_a), "fresh identity").description().version,
          "conversion fell back to latest version");
    const auto cut = store.statistics().completed_cut;
    require(store.adopt(basis_a, std::move(old_conversion)), "old exact-basis adoption after publication");
    ++evidence.adoptions;
    const auto alternative = take(obj::select(old_context, object_a, true), "old alternative discovery");
    verify(schema, alternative, old_a, target);
    check(store.statistics().completed_cut == cut, "old adoption changed completed cut");
    const auto new_alternative = obj::select(fresh, object_a, true);
    check(!new_alternative && new_alternative.error() == orbital::Error::absent,
          "old physical adoption leaked into new logical version");
    ++evidence.refusals;
    verify(schema, take(obj::select(fresh, object_a), "new version has its own default"), new_a, format);
    escaped_selection = obj::retain(alternative);
    const auto tuple = target == v::Format::rows ? alternative : basis_a;
    escaped_tuple = obj::retain(take(tuple.part(std::size_t{0}), "escaped Tuple Part"));
  }
  drain(store);
  verify(schema, escaped_selection.selection(), old_a,
         format == v::Format::rows ? v::Format::columns : v::Format::rows);
  store = {};
  // Contexts, ordinary Representations and Store are all gone. Schema remains.
  verify(schema, escaped_selection.selection(), old_a,
         format == v::Format::rows ? v::Format::columns : v::Format::rows);
  equal(take(v::bind(schema, escaped_tuple.view()), "Tuple View survives Store/context"), old_a);
  check(escaped_tuple.view().description().logical_metadata.size() == 8 &&
            escaped_tuple.view().interpretation().size() == 75, "retained Tuple metadata expired");
  escaped_selection = {};
  equal(take(v::bind(schema, escaped_tuple.view()), "resolved Tuple alone survives"), old_a);
  escaped_tuple = {};
  ++evidence.histories;
}

template<v::Format Format>
void thin_cursor_history(const v::Schema& schema, Runtime& runtime) {
  const auto old = records(129, 747);
  auto next = old;
  const v::TupleControl tuple_control{schema, old.size()};
  const v::ColumnControl column_control{old.size()};
  const v::TupleControl wrong_tuple_count{schema, old.size() + 1};
  const v::ColumnControl wrong_column_count{old.size() + 1};
  obj::RetainedView escaped;
  std::optional<v::TupleCursor> escaped_cursor;
  {
    auto store = open(runtime);
    auto initial = take(store.edit(), "thin initial Draft");
    require(initial.put(construct(schema, store, runtime, object_a, initial.version(), old, Format)),
            "thin initial insertion");
    publish(store, initial);
    {
      auto context = take(store.read(), "thin old context");
      const auto source = take(obj::select(context, object_a), "thin old selection");
      const auto control = [&] {
        if constexpr (Format == v::Format::rows) return tuple_control;
        else return column_control;
      }();
      const auto qualify = [&](obj::Selection selected) {
        if constexpr (Format == v::Format::rows) return v::qualify_tuple(control, selected);
        else return v::qualify_columns(schema, control, selected);
      };
      // Qualification is cold. Copies and moves carry only immutable payload
      // pointers, and no endpoint borrows the original cursor's stack address.
      const auto cursor = [&] {
        const auto original = take(qualify(source), "thin old cursor qualification");
        auto copied = original;
        const auto moved = std::move(copied);
        return moved;
      }();
      thin_reads(cursor.open(control), old);
      const auto first_part = take(source.part(std::size_t{0}), "thin first old Part");
      if constexpr (Format == v::Format::rows) {
        refused(v::qualify_tuple(wrong_tuple_count, source), "thin Tuple accepted wrong control count");
        refused(v::qualify_tuple(wrong_tuple_count, first_part), "thin Tuple Part accepted wrong count");
        refused(v::qualify_columns(schema, column_control, source), "thin columns accepted Tuple format");
      } else {
        refused(v::qualify_columns(schema, wrong_column_count, source), "thin columns accepted wrong count");
        refused(v::qualify_tuple(tuple_control, source), "thin Tuple accepted columns format");
        refused(v::qualify_tuple(tuple_control, first_part), "thin Tuple accepted one column as full row");
      }
      refused(v::qualify_tuple(tuple_control, obj::Selection{}), "thin Tuple accepted empty selection");
      refused(v::qualify_tuple(tuple_control, obj::View{}), "thin Tuple accepted empty Part");
      refused(v::qualify_columns(schema, column_control, obj::Selection{}),
              "thin columns accepted empty selection");

      auto draft = take(store.edit(), "thin update Draft");
      const std::array changes{
          v::Update{old[0].key, {0x123456789abcdef0ULL, 0x13579, 0x97531}},
          v::Update{old[64].key, {0xfedcba9876543210ULL, 0x2468a, 0x8642}},
          v::Update{old[128].key, {UINT64_MAX, orbital_bench::value_mask, UINT32_MAX}}};
      next[0].record = changes[0].record;
      next[64].record = changes[1].record;
      next[128].record = changes[2].record;
      require(draft.put(updated(schema, store, runtime, source, draft.version(), changes)),
              "thin private update insertion");
      const auto staged = take(draft.select(object_a), "thin private updated selection");
      thin_reads(take(qualify(staged), "thin private updated cursor").open(control), next);
      publish(store, draft);
      thin_reads(cursor.open(control), old);
      auto fresh = take(store.read(), "thin new context");
      const auto current = take(obj::select(fresh, object_a), "thin new selection");
      const auto current_cursor = take(qualify(current), "thin new cursor qualification");
      thin_reads(current_cursor.open(control), next);

      constexpr auto target = Format == v::Format::rows ? v::Format::columns : v::Format::rows;
      auto converted = convert(schema, store, runtime, source, target);
      const auto cut = store.statistics().completed_cut;
      require(store.adopt(source, std::move(converted)), "thin old conversion adoption");
      ++evidence.adoptions;
      const auto alternative = take(obj::select(context, object_a, true), "thin old alternative selection");
      verify_thin(schema, alternative, old, target);
      thin_reads(cursor.open(control), old);
      thin_reads(current_cursor.open(control), next);
      check(store.statistics().completed_cut == cut, "thin old conversion changed logical cut");
      refused(obj::select(fresh, object_a, true), orbital::Error::absent,
              "thin old conversion appeared in new version");

      const auto tuple = Format == v::Format::rows ? source : alternative;
      escaped = obj::retain(take(tuple.part(std::size_t{0}), "thin escaping complete Tuple Part"));
      escaped_cursor = take(v::qualify_tuple(tuple_control, escaped.view()), "thin retained Tuple cursor");
    }
    drain(store);
  }
  // The saved 8 B cursor survives closure of all contexts, Selections and the
  // Store under one actual RetainedView. It neither owns nor discovers siblings.
  thin_reads(escaped_cursor->open(tuple_control), old);
  const auto direct = take(v::qualify_tuple(tuple_control, escaped.view()),
                           "thin escaped Tuple qualification after Store destruction");
  thin_reads(direct.open(tuple_control), old);
  escaped_cursor.reset();
  escaped = {};
  ++evidence.histories;
}

void private_refusal_history(const v::Schema& schema, Runtime& runtime, v::Format format) {
  auto store = open(runtime);
  const auto expected = records(129);
  auto initial = take(store.edit(), "private-refusal initial Draft");
  require(initial.put(construct(schema, store, runtime, object_a, initial.version(), expected, format)),
          "private-refusal initial insertion");
  publish(store, initial);
  {
    auto context = take(store.read(), "private-refusal context");
    const auto source = take(obj::select(context, object_a), "private-refusal basis");
    const auto cut = store.statistics().completed_cut;
    for (auto target : {v::Format::rows, v::Format::columns}) {
      v::Costs failed;
      const auto options = target == v::Format::rows ?
          v::ConstructionOptions{.fail_after_rows = 128} :
          v::ConstructionOptions{.fail_after_parts = 1};
      const auto denied = v::convert(schema, store, *runtime.pool, source, target, failed, options);
      refused(denied, "injected incomplete conversion escaped as Representation");
      check(failed.kernel.encoded_values > 0 && failed.kernel.issued_bytes > 0 &&
                (target == v::Format::rows ? failed.kernel.completed_rows == 128 :
                  failed.kernel.completed_parts == 1 && !failed.kernel.completed_rows),
            "conversion injection did not cross requested real private work frontier");
      account(failed);
      ++evidence.private_injections;
      verify(schema, source, expected, format);
      check(store.statistics().completed_cut == cut, "private conversion failure published");
      auto retry = convert(schema, store, runtime, source, target);
      verify(schema, retry, expected, target);
      v::Costs construction_failure;
      const auto denied_construct = v::construct(schema, store, *runtime.pool, object_b,
          cut + 1, expected, target, construction_failure, options);
      refused(denied_construct, "injected incomplete construction escaped as Representation");
      check(construction_failure.kernel.encoded_values > 0 && construction_failure.kernel.issued_bytes > 0,
            "construction injection did no physical private work");
      account(construction_failure);
      ++evidence.private_injections;
      auto retry_construct = construct(schema, store, runtime, object_b, cut + 1, expected, target);
      verify(schema, retry_construct, expected, target);
      verify(schema, source, expected, format);
    }
    auto draft = take(store.edit(), "update refusal Draft");
    auto no_private_writes = [&](std::span<const v::Update> changes, orbital::Error error) {
      v::Costs costs;
      refused(v::update(schema, store, *runtime.pool, source, draft.version(), changes, costs),
              error, "malformed update admitted");
      check(!costs.requested_parts && !costs.requested_body_bytes && !costs.copied_bytes &&
                !costs.kernel.encoded_values && !costs.kernel.issued_bytes,
            "refused update allocated/copied/wrote target");
      verify(schema, source, expected, format);
      check(store.statistics().completed_cut == cut, "refused update changed publication");
    };
    no_private_writes({}, orbital::Error::invalid_argument);
    const std::array absent{v::Update{expected[0].key + 1, {1, 1, 1}}};
    no_private_writes(absent, orbital::Error::absent);
    const std::array malformed{v::Update{expected.back().key, {1, orbital_bench::value_mask + 1, 1}}};
    no_private_writes(malformed, orbital::Error::invalid_argument);
    std::array<v::Update, 65> over_limit;
    over_limit.fill({expected[0].key, {1, 1, 1}});
    no_private_writes(over_limit, orbital::Error::invalid_argument);
    auto malformed_rows = expected;
    malformed_rows.back().record.value = orbital_bench::value_mask + 1;
    v::Costs costs;
    refused(v::construct(schema, store, *runtime.pool, object_b, draft.version(), malformed_rows,
                         format, costs), "late invalid construction value admitted");
    check(!costs.requested_parts && !costs.kernel.encoded_values, "late bad value caused private writes");
    const std::array good{v::Update{expected[128].key, {UINT64_MAX, 42, 43}}};
    auto changed = expected;
    changed[128].record = good[0].record;
    require(draft.put(updated(schema, store, runtime, source, draft.version(), good)), "retry update insertion");
    publish(store, draft);
    auto current = store.borrow();
    verify(schema, take(obj::select(current, object_a), "retry published selection"), changed, format);
    verify(schema, source, expected, format);
  }
  drain(store);
  ++evidence.histories;
}

// Test-only capture owner: description corruption is made with real immutable
// backing, never fabricated Selection/Prepared provenance.
obj::Representation capture_copy(obj::Store& store, obj::RepresentationSpec specification,
                                std::span<const obj::PartSource> parts) {
  return take(store.capture(specification, parts), "capture independently malformed description");
}
void malformed_descriptions(const v::Schema& schema, Runtime& runtime) {
  auto store = open(runtime);
  const auto expected = records(129);
  for (auto format : {v::Format::rows, v::Format::columns}) {
    auto source = construct(schema, store, runtime, object_a, 1, expected, format);
    auto owner = std::make_shared<obj::Representation>(source);
    const auto& description = source.description();
    auto spec = obj::RepresentationSpec{description.object, description.version, description.replica,
        description.format, description.logical_metadata, description.interpretation, {}};
    std::vector<obj::PartSource> parts;
    for (std::size_t index = 0; index < source.part_count(); ++index) {
      const auto part = take(source.part(index), "malformed source Part");
      parts.push_back({part.id(), part.body(), part.interpretation(), part.readable_padding(),
                       part.alignment(), owner});
    }
    auto reject = [&](obj::RepresentationSpec metadata, std::span<const obj::PartSource> input) {
      const auto captured = capture_copy(store, metadata, input);
      refused(v::bind(schema, captured), "malformed immutable closure admitted");
      // Real private selection exposes the same immutable captured closure to
      // the thin factory; no fabricated cursor/Selection provenance is used.
      auto draft = take(store.edit(), "malformed thin qualification Draft");
      require(draft.put(captured), "malformed immutable private insertion");
      const auto selected = take(draft.select(object_a), "malformed thin private selection");
      if (format == v::Format::rows) {
        const v::TupleControl control{schema, expected.size()};
        refused(v::qualify_tuple(control, selected), "thin Tuple admitted malformed immutable closure");
        if (selected.part_count())
          refused(v::qualify_tuple(control, take(selected.part(std::size_t{0}), "malformed thin Tuple Part")),
                  "thin direct Tuple admitted malformed immutable closure");
      } else {
        const v::ColumnControl control{expected.size()};
        refused(v::qualify_columns(schema, control, selected),
                "thin columns admitted malformed immutable closure");
      }
      ++evidence.metadata_refusals;
    };
    auto wrong_format = spec;
    wrong_format.format = {0, 99};
    reject(wrong_format, parts);
    auto wrong_logical = logical(expected.size() + 1);
    auto wrong_count = spec;
    wrong_count.logical_metadata = wrong_logical;
    reject(wrong_count, parts);
    auto parent = std::vector<std::byte>(spec.interpretation.begin(), spec.interpretation.end());
    parent[20] = std::byte{1};
    auto wrong_parent = spec;
    wrong_parent.interpretation = parent;
    reject(wrong_parent, parts);
    auto wrong_part = parts;
    wrong_part[0].id.low += 100;
    reject(spec, wrong_part);
    auto truncated = parts;
    truncated.back().body = truncated.back().body.first(truncated.back().body.size() - 1);
    reject(spec, truncated);
    auto interpretation = std::vector<std::byte>(parts.back().interpretation.begin(), parts.back().interpretation.end());
    interpretation[0] ^= std::byte{1};
    auto corrupt_part = parts;
    corrupt_part.back().interpretation = interpretation;
    reject(spec, corrupt_part);
    reject(spec, std::span(parts).first(parts.size() - 1));
    verify(schema, source, expected, format);
  }
  drain(store);
  ++evidence.histories;
}

void builder_history(const v::Schema& schema, Runtime& runtime) {
  auto store = open(runtime);
  const auto expected = records(1);
  const auto shape = take(v::shape(schema, v::Format::rows, expected.size()), "Builder row shape");
  const auto metadata = logical(expected.size());
  const obj::RepresentationSpec specification{object_a, 1, {}, v::data_program, metadata,
                                              shape.interpretation, {}};
  const std::array plans{obj::PartPlan{shape.parts[0].id,
      {.bytes = shape.parts[0].body_bytes, .alignment = shape.parts[0].alignment,
       .initialization = a::Initialization::uninitialized}, shape.parts[0].interpretation}};
  {
    auto builder = take(store.build(specification, *runtime.pool, plans), "real Builder");
    auto writer = take(builder.write(0), "real Writer");
    const auto bytes = take(writer.bytes(), "active Writer bytes");
    const std::array targets{v::WritePart{shape.parts[0].id, bytes}};
    v::KernelCosts costs;
    require(v::construct_into(schema, shape, targets, expected, costs), "real Writer initialization");
    refused(builder.freeze(), orbital::Error::conflict, "Builder froze while Writer open");
    refused(builder.write(0), orbital::Error::conflict, "duplicate exclusive Writer admitted");
    refused(builder.write(1), orbital::Error::absent, "out-of-range Writer admitted");
    auto moved = std::move(writer);
    refused(writer.bytes(), orbital::Error::stale, "moved Writer supplied bytes");
    moved = {};
    auto result = take(builder.freeze(), "freeze after busy refusal and Writer closure");
    verify(schema, result, expected, v::Format::rows);
    refused(builder.write(0), orbital::Error::stale, "terminal Builder supplied Writer");
    refused(builder.freeze(), orbital::Error::stale, "terminal Builder froze twice");
  }
  {
    auto builder = take(store.build(specification, *runtime.pool, plans), "abandoned Builder");
    auto writer = take(builder.write(0), "abandoned Writer");
    check(take(writer.bytes(), "pre-abandon Writer bytes").size() == 23, "Writer extent differs");
    builder = {};
    refused(writer.bytes(), orbital::Error::stale, "revoked Writer supplied bytes");
    // The prior span is deliberately never dereferenced after revocation.
    writer = {};
  }
  drain(store);
  ++evidence.histories;
}

void initialized_spare_history(const v::Schema& schema, Runtime& runtime) {
  auto store = open(runtime);
  const auto old = records(17);
  auto expected = old;
  std::array<unsigned, 17> spare;
  auto draft = take(store.edit(), "initialized spare Draft");
  const auto shape = take(v::shape(schema, v::Format::rows, old.size()), "initialized spare shape");
  const auto metadata = logical(old.size());
  const obj::RepresentationSpec specification{object_a, draft.version(), {}, v::data_program,
                                              metadata, shape.interpretation, {}};
  const std::array plans{obj::PartPlan{shape.parts[0].id,
      {.bytes = shape.parts[0].body_bytes, .alignment = 1,
       .initialization = a::Initialization::uninitialized}, shape.parts[0].interpretation}};
  auto builder = take(store.build(specification, *runtime.pool, plans), "initialized spare Builder");
  {
    auto writer = take(builder.write(0), "initialized spare Writer");
    const auto bytes = take(writer.bytes(), "initialized spare exclusive bytes");
    const std::array targets{v::WritePart{shape.parts[0].id, bytes}};
    v::KernelCosts costs;
    require(v::construct_into(schema, shape, targets, old, costs), "initialized spare construction");
    for (std::size_t row = 0; row < old.size(); ++row) {
      spare[row] = unsigned(row % 15 + 1);
      bytes[row * 23 + 18] |= std::byte(spare[row] << 4);
    }
  }
  require(draft.put(take(builder.freeze(), "initialized spare immutable freeze")),
          "initialized spare insertion");
  publish(store, draft);
  {
    auto context = take(store.read(), "initialized spare old context");
    const auto source = take(obj::select(context, object_a), "initialized spare old selection");
    equal(take(v::bind(schema, source), "initialized spare old binding"), old);
    wire(take(source.part(std::size_t{0}), "initialized spare old Part"), 0, old, true, spare);
    const std::array changes{v::Update{old[0].key, {7, 9, 11}},
                            v::Update{old[16].key, {UINT64_MAX, orbital_bench::value_mask, UINT32_MAX}}};
    expected[0].record = changes[0].record;
    expected[16].record = changes[1].record;
    auto update_draft = take(store.edit(), "initialized spare update Draft");
    auto result = updated(schema, store, runtime, source, update_draft.version(), changes);
    equal(take(v::bind(schema, result), "initialized spare private update binding"), expected);
    wire(take(result.part(std::size_t{0}), "initialized spare private updated Part"), 0,
         expected, true, spare);
    require(update_draft.put(std::move(result)), "initialized spare updated insertion");
    publish(store, update_draft);
    auto current = store.borrow();
    const auto published = take(obj::select(current, object_a), "initialized spare new selection");
    equal(take(v::bind(schema, published), "initialized spare new binding"), expected);
    wire(take(published.part(std::size_t{0}), "initialized spare new Part"), 0, expected, true, spare);
    equal(take(v::bind(schema, source), "initialized spare old unchanged binding"), old);
    wire(take(source.part(std::size_t{0}), "initialized spare old unchanged Part"), 0, old, true, spare);
  }
  drain(store);
  ++evidence.histories;
}

void authority_history(const v::Schema& schema, Runtime& runtime) {
  auto store = open(runtime), foreign = open(runtime);
  const auto original = records(17), changed = records(17, 313), later = records(17, 515);
  auto initial = take(store.edit(), "authority initial Draft");
  require(initial.put(construct(schema, store, runtime, object_a, initial.version(), original, v::Format::rows)),
          "authority initial insertion");
  publish(store, initial);
  {
    auto draft = take(store.edit(), "empty authority Draft");
    refused(store.prepare(draft), orbital::Error::invalid_argument, "empty Draft accepted");
    auto foreign_rep = construct(schema, foreign, runtime, object_a, draft.version(), changed, v::Format::rows);
    refused(draft.put(std::move(foreign_rep)), orbital::Error::conflict, "foreign Store representation inserted");
    auto wrong_version = construct(schema, store, runtime, object_a, draft.version() + 1, changed, v::Format::rows);
    refused(draft.put(std::move(wrong_version)), orbital::Error::conflict, "wrong Draft version inserted");
    require(draft.put(construct(schema, store, runtime, object_a, draft.version(), changed, v::Format::rows)),
            "authority private insertion");
    refused(foreign.prepare(draft), orbital::Error::conflict, "foreign Store prepared Draft");
    auto contender = take(store.edit(), "contending Draft");
    require(contender.put(construct(schema, store, runtime, object_a, contender.version(), later, v::Format::rows)),
            "contending immutable insertion");
    auto prepared = take(store.prepare(draft), "accepted pending authority");
    refused(store.prepare(contender), orbital::Error::unavailable, "second pending authority accepted");
    verify(schema, take(contender.select(object_a), "contender after refusal"), later, v::Format::rows);
    const auto cut = store.statistics().completed_cut;
    prepared.abort();
    check(prepared.disposition() == obj::Disposition::aborted && store.statistics().completed_cut == cut,
          "abort advanced publication");
    refused(store.install(prepared), orbital::Error::rejected, "aborted authority installed");
    prepared.retire();
    auto accepted = take(store.prepare(contender), "retry contender preparation");
    auto moved = std::move(accepted);
    check(accepted.disposition() == obj::Disposition::empty, "moved Prepared retained authority");
    refused(foreign.install(moved), orbital::Error::conflict, "foreign Store installed authority");
    refused(store.install(accepted), orbital::Error::invalid_argument, "empty authority installed");
    require(store.install(moved), "Store-bound authority installation");
    ++evidence.publications;
    refused(store.install(moved), orbital::Error::rejected, "single-use authority replay installed");
    moved.retire();
  }
  {
    auto stale = take(store.edit(), "stale Draft");
    require(stale.put(construct(schema, store, runtime, object_a, stale.version(), changed, v::Format::rows)),
            "stale private result");
    auto replacement = take(store.edit(), "basis replacement Draft");
    require(replacement.put(construct(schema, store, runtime, object_a, replacement.version(), original, v::Format::rows)),
            "basis replacement result");
    publish(store, replacement);
    refused(store.prepare(stale), orbital::Error::stale, "stale Draft accepted");
    verify(schema, take(stale.select(object_a), "stale private survives refusal"), changed, v::Format::rows);
    const auto aborted = store.statistics().aborted;
    {
      auto draft = take(store.edit(), "destructor-abort Draft");
      require(draft.put(construct(schema, store, runtime, object_a, draft.version(), changed, v::Format::rows)),
              "destructor-abort immutable result");
      auto pending = take(store.prepare(draft), "destructor-abort acceptance");
      check(pending.disposition() == obj::Disposition::accepted, "destructor-abort was not accepted");
    }
    check(store.statistics().aborted == aborted + 1, "accepted Prepared destructor did not abort");
    auto current = store.borrow();
    verify(schema, take(obj::select(current, object_a), "post-abort current"), original, v::Format::rows);
    auto context = take(store.read(), "adoption provenance context");
    const auto selected = take(obj::select(context, object_a), "adoption provenance basis");
    auto wrong_basis = construct(schema, store, runtime, object_a, selected.description().version + 1,
                                original, v::Format::columns);
    refused(store.adopt(selected, std::move(wrong_basis)), orbital::Error::conflict,
            "wrong exact logical version adopted");
    auto other_store = construct(schema, foreign, runtime, object_a, selected.description().version,
                                original, v::Format::columns);
    refused(store.adopt(selected, std::move(other_store)), orbital::Error::conflict,
            "foreign representation adopted");
    auto immutable = construct(schema, store, runtime, object_a, selected.description().version,
                               original, v::Format::rows);
    auto owner = std::make_shared<obj::Representation>(immutable);
    const auto view = take(immutable.part(std::size_t{0}), "wrong logical metadata backing");
    const std::array parts{obj::PartSource{view.id(), view.body(), view.interpretation(),
                                         view.readable_padding(), view.alignment(), owner}};
    const auto wrong_logical = logical(original.size() + 1);
    const auto& description = immutable.description();
    auto wrong_metadata = take(store.capture({object_a, selected.description().version, {},
        v::data_program, wrong_logical, description.interpretation, {}}, parts),
        "wrong logical metadata capture");
    refused(store.adopt(selected, std::move(wrong_metadata)), orbital::Error::conflict,
            "different logical metadata adopted under exact basis");
  }
  drain(store);
  drain(foreign);
  ++evidence.histories;
}

struct IndependentBacking {
  a::Allocation allocation;
  unsigned* destroyed{};
  ~IndependentBacking() { ++*destroyed; }
};
void narrow_retention(const v::Schema& schema, Runtime& runtime) {
  std::array<unsigned, 4> destroyed{};
  obj::RetainedView retained;
  const auto expected = records(129);
  {
    auto store = open(runtime);
    auto draft = take(store.edit(), "narrow retention Draft");
    const auto shape = take(v::shape(schema, v::Format::columns, expected.size()), "narrow retention shape");
    std::array<std::shared_ptr<IndependentBacking>, 4> owners;
    std::array<v::WritePart, 4> writes;
    for (unsigned index = 0; index < 4; ++index) {
      owners[index] = std::make_shared<IndependentBacking>();
      owners[index]->destroyed = &destroyed[index];
      owners[index]->allocation = take(a::allocate(*runtime.pool,
          {.bytes = shape.parts[index].body_bytes, .alignment = shape.parts[index].alignment,
           .initialization = a::Initialization::uninitialized}), "independent Part allocation");
      writes[index] = {shape.parts[index].id, a::bytes(owners[index]->allocation)};
    }
    v::KernelCosts costs;
    require(v::construct_into(schema, shape, writes, expected, costs), "independent Part construction");
    std::array<obj::PartSource, 4> parts;
    for (unsigned index = 0; index < 4; ++index)
      parts[index] = {shape.parts[index].id, a::bytes(owners[index]->allocation),
                     shape.parts[index].interpretation, 0, shape.parts[index].alignment, owners[index]};
    const auto metadata = logical(expected.size());
    auto rep = take(store.capture({object_a, draft.version(), {}, v::data_program, metadata,
                                  shape.interpretation, {}}, parts), "independent Part capture");
    require(draft.put(rep), "independent capture insertion");
    publish(store, draft);
    {
      auto context = take(store.read(), "narrow retention context");
      const auto selection = take(obj::select(context, object_a), "narrow retention selection");
      retained = obj::retain(take(selection.part(std::size_t{0}), "narrow resolved key Part"));
    }
    parts = {};
    owners = {};
    rep = {};
    auto erased = take(store.edit(), "narrow retention erasure");
    require(erased.erase(object_a), "narrow retention erase");
    publish(store, erased);
    drain(store);
    check(destroyed == std::array<unsigned, 4>{0, 1, 1, 1},
          "one retained Part kept sibling independent Allocation owners");
    evidence.independently_released_parts += 3;
  }
  check(destroyed == std::array<unsigned, 4>{0, 1, 1, 1}, "Store destruction lost narrow retention");
  const auto view = retained.view();
  check(view.description().logical_metadata.size() == 8 && view.description().interpretation.size() == 24,
        "single Part lost parent metadata");
  const auto descriptor = take(v::sp::decode_descriptor(
      {reinterpret_cast<const std::uint8_t*>(view.interpretation().data()), view.interpretation().size()}),
      "retained column descriptor");
  const std::array<std::span<const std::uint8_t>, 3> planes{
      std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(view.body().data()),
                                  view.body().size()}, {}, {}};
  const auto series = take(v::sp::attach_representation<v::Column<64>, const std::uint8_t>(descriptor, planes),
      "retained column native Ikea attachment");
  for (std::size_t row = 0; row < expected.size(); ++row) {
    check(v::sp::get_unchecked(series, row) == expected[row].key, "retained key column native read differs");
    ++evidence.logical_fields;
  }
  wire(view, 0, expected, false);
  retained = {};
  check(destroyed == std::array<unsigned, 4>{1, 1, 1, 1}, "last retained Part did not release Allocation owner");
  ++evidence.independently_released_parts;
  ++evidence.histories;
}

void metadata_capacity_history(const v::Schema& schema, Runtime& runtime) {
  auto store = open(runtime, 32 * 1024);
  const auto old = records(17), next = records(17, 333);
  auto initial = take(store.edit(), "metadata initial Draft");
  require(initial.put(construct(schema, store, runtime, object_a, initial.version(), old, v::Format::columns)),
          "metadata initial insertion");
  publish(store, initial);
  {
    auto draft = take(store.edit(), "metadata private Draft");
    require(draft.put(construct(schema, store, runtime, object_a, draft.version(), next, v::Format::columns)),
            "metadata private insertion");
    std::vector<obj::ReadContext> occupied;
    bool exhausted{};
    for (unsigned count = 0; count < 4096; ++count) {
      auto context = store.read();
      if (!context) {
        refused(context, orbital::Error::capacity, "metadata context refused unexpectedly");
        exhausted = true;
        break;
      }
      occupied.push_back(std::move(*context));
    }
    check(exhausted, "finite metadata budget did not refuse context enrollment");
    refused(store.prepare(draft), orbital::Error::capacity, "exhausted metadata accepted private Draft");
    ++evidence.metadata_refusals;
    verify(schema, take(draft.select(object_a), "metadata refusal private result"), next, v::Format::columns);
    auto current = store.borrow();
    verify(schema, take(obj::select(current, object_a), "metadata refusal published source"), old, v::Format::columns);
    occupied.clear();
    drain(store);
    publish(store, draft);
    check(store.statistics().metadata_bytes <= store.statistics().metadata_limit,
          "metadata budget exceeded recorded limit");
  }
  const auto current = store.borrow();
  verify(schema, take(obj::select(current, object_a), "metadata retry published result"), next, v::Format::columns);
  drain(store);
  ++evidence.histories;
}

void allocation_capacity_history(const v::Schema& schema) {
  Runtime runtime(1024 * 1024);
  auto store = open(runtime);
  const auto old = records(17);
  auto initial = take(store.edit(), "allocation initial Draft");
  require(initial.put(construct(schema, store, runtime, object_a, initial.version(), old, v::Format::rows)),
          "allocation initial insertion");
  publish(store, initial);
  {
    auto context = take(store.read(), "allocation refusal context");
    const auto source = take(obj::select(context, object_a), "allocation refusal source");
    const auto cut = store.statistics().completed_cut;
    const auto metadata = logical(old.size());
    const obj::RepresentationSpec spec{object_b, cut + 1, {}, v::data_program, metadata, {}, {}};
    const std::array plans{
      obj::PartPlan{{0, 1}, {.bytes = 8, .alignment = 8, .initialization = a::Initialization::zero}, {}},
      obj::PartPlan{{0, 2}, {.bytes = 2 * 1024 * 1024, .alignment = 64,
                            .initialization = a::Initialization::uninitialized}, {}}};
    refused(store.build(spec, *runtime.pool, plans), orbital::Error::capacity,
            "partial real Builder allocation exceeded Arena budget");
    verify(schema, source, old, v::Format::rows);
    check(store.statistics().completed_cut == cut, "physical allocation refusal published");
    auto constrained_arena = take(a::open_arena(1), "constrained Arena open");
    auto* constrained_pool = take(a::add_pool(*constrained_arena, runtime.lane), "constrained Pool open");
    v::Costs denied_costs;
    refused(v::convert(schema, store, *constrained_pool, source, v::Format::columns, denied_costs),
            orbital::Error::capacity, "consumer conversion ignored constrained physical budget");
    check(!denied_costs.kernel.encoded_values && !denied_costs.kernel.issued_bytes,
          "allocation refusal performed target codec writes");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
      const auto progress = a::close(*constrained_pool);
      check(progress.kind() != orbital::Progress::Kind::failed, "constrained Pool close failed");
      if (progress.kind() == orbital::Progress::Kind::done) break;
      check(std::chrono::steady_clock::now() < deadline, "constrained Pool failed to drain");
      orbital::worker::poll(runtime.lane, 1024);
      std::this_thread::yield();
    }
    check(!constrained_arena->charged_bytes.load(), "constrained Arena retained charge");
    require(a::remove_pool(*constrained_pool), "constrained Pool removal");
    constrained_arena.reset();
    auto retry = convert(schema, store, runtime, source, v::Format::columns);
    verify(schema, retry, old, v::Format::columns);
    require(store.adopt(source, std::move(retry)), "physical budget retry adoption");
    ++evidence.adoptions;
    verify(schema, take(obj::select(context, object_a, true), "physical budget retry selection"), old, v::Format::columns);
  }
  drain(store);
  ++evidence.histories;
}
} // namespace

int main() {
  static_assert(sizeof(v::TupleCursor) == 8 && sizeof(v::ColumnCursors) == 32);
  static_assert(sizeof(v::TupleBody) == 24 && sizeof(v::ColumnBody) == 40);
  // Outer stable Schema owns every bound Tuple reader/writer/layout throughout
  // native reads, including resolved retention after Store destruction.
  const v::Schema schema;
  {
    Runtime runtime;
    conversion_boundaries(schema, runtime);
    for (auto format : {v::Format::rows, v::Format::columns}) {
      coherent_history(schema, runtime, format);
      private_refusal_history(schema, runtime, format);
    }
    thin_cursor_history<v::Format::rows>(schema, runtime);
    thin_cursor_history<v::Format::columns>(schema, runtime);
    malformed_descriptions(schema, runtime);
    builder_history(schema, runtime);
    initialized_spare_history(schema, runtime);
    authority_history(schema, runtime);
    narrow_retention(schema, runtime);
    metadata_capacity_history(schema, runtime);
  }
  allocation_capacity_history(schema);
  std::cout << "{\"scope\":\"experimental-objects-and-views-history\",\"histories\":" << evidence.histories
      << ",\"publications\":" << evidence.publications << ",\"adoptions\":" << evidence.adoptions
      << ",\"conversions\":" << evidence.conversions << ",\"updates\":" << evidence.updates
      << ",\"logical_fields\":" << evidence.logical_fields << ",\"independent_wire_bits\":" << evidence.wire_bits
      << ",\"refusals\":" << evidence.refusals << ",\"metadata_refusals\":" << evidence.metadata_refusals
      << ",\"private_injections\":" << evidence.private_injections
      << ",\"independently_released_parts\":" << evidence.independently_released_parts
      << ",\"requested_body_bytes\":" << evidence.costs.requested_body_bytes
      << ",\"requested_parts\":" << evidence.costs.requested_parts
      << ",\"copied_bytes\":" << evidence.costs.copied_bytes
      << ",\"copied_spans\":" << evidence.costs.copied_spans
      << ",\"decoded_values\":" << evidence.costs.kernel.decoded_values
      << ",\"encoded_values\":" << evidence.costs.kernel.encoded_values
      << ",\"issued_bytes\":" << evidence.costs.kernel.issued_bytes
      << ",\"issued_spans\":" << evidence.costs.kernel.issued_spans
      << ",\"scratch_bound_bytes\":" << evidence.costs.kernel.scratch_bound_bytes
      << ",\"final_arena_charge\":0,\"pending_physical_access_tested\":false}\n";
}
