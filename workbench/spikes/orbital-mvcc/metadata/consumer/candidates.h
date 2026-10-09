#pragma once

// CONSUMER_METADATA_FROZEN selects the root-owned 7f73c81 capture. Otherwise
// use the relocated experimental spike baseline. No production source changes.
#if defined(CONSUMER_METADATA_FROZEN) && CONSUMER_METADATA_FROZEN
#include "workbench/benchmarks/orbital/versioned/native.h"
#include "workbench/benchmarks/orbital/versioned/consumer.h"
#else
#include "consumer/native.h"
#include "consumer/consumer.h"
#endif
#include <array>
#include <optional>
#include <span>
#include <utility>

namespace orbital_mvcc::metadata_consumer {
namespace v = orbital_bench::versioned;
namespace tp = ikea::tuplepack;
using orbital_bench::Key;
using orbital_bench::Record;
using orbital_bench::Row;

// Real shared storage, not a pointer to an unreported heap record. This full
// immutable Schema (layout, constructor, four readers, four writers) is resident
// once per program when the DefaultTuple16 comparator is linked. Its separate
// TU keeps that comparator's eager Schema initialization out of direct8/16
// consumers. Count its initialization/storage when linked; no lazy guard per get.
extern const v::Schema canonical_tuple_schema;

struct ChainResult { std::size_t completed{}; Key next{}; };
template<class Body>
[[gnu::always_inline]] inline ChainResult body_chain(const Body& body, Key key,
                                                    std::span<Record> output) {
  std::size_t completed = 0;
  for (; completed < output.size(); ++completed) {
    const auto record = body.get(key);
    if (!record) break;
    output[completed] = *record;
    key = record->next;
  }
  return {completed, key};
}

// Candidate A: a real two-word read state, with canonical program-wide Schema
// placement. It borrows encoded bytes only; it owns neither backing nor a
// descriptor/owner box. Native calls rederive frozen TupleBody from the shared
// schema and copied count. The ephemeral body is still 24B before optimization.
class DefaultTuple16 {
  v::TupleCursor cursor_;
  std::size_t records_;
  DefaultTuple16(v::TupleCursor cursor, std::size_t records) : cursor_(cursor), records_(records) {}
public:
  static orbital::Result<DefaultTuple16> qualify(std::size_t records, orbital::Bytes parent,
                                               std::span<const v::ReadPart> parts) {
    const v::TupleControl control{canonical_tuple_schema, records};
    const auto cursor = v::qualify_tuple(control, parent, parts);
    if (!cursor) return std::unexpected(cursor.error());
    return DefaultTuple16{*cursor, records};
  }
  std::size_t size() const noexcept { return records_; }
  [[gnu::always_inline]] v::TupleBody body() const {
    return cursor_.open({canonical_tuple_schema, records_});
  }
  [[gnu::always_inline]] std::optional<Record> get(Key key) const { return body().get(key); }
  std::size_t lower_bound(Key key) const { return body().lower_bound(key); }
  orbital::Result<Row> row(std::size_t row) const { return body().row(row); }
  orbital::Result<void> field(v::Field field, std::size_t first, std::span<std::uint64_t> output) const {
    return body().field(field, first, output);
  }
  std::size_t scan(Key key, std::span<Row> output) const { return body().scan(key, output); }
  [[gnu::always_inline]] ChainResult chain(Key key, std::span<Record> output) const {
    const auto native = body();
    return body_chain(native, key, output);
  }
};
static_assert(sizeof(DefaultTuple16) == 16);

// Candidate B: pointer+count, with Schema an explicit whole-call argument.
// Direct cold qualification proves the same frozen fixed-law metadata and
// placement conditions before deriving its pointer. Every native operation
// requires the same/equivalent immutable Schema used here, backing protection,
// sorted keys for seek/get/scan/chain, and disjoint outputs. These are trusted
// lifetime/association conditions, not new ownership or commit authority.
class ExplicitTuple16 {
  const tp::byte* data_;
  std::size_t records_;
  ExplicitTuple16(const tp::byte* data, std::size_t records) : data_(data), records_(records) {}
  friend orbital::Result<ExplicitTuple16> qualify_tuple16(const v::TupleControl&,
      orbital::Bytes, std::span<const v::ReadPart>);
  friend orbital::Result<void> qualify_tuple_part16(const v::TupleControl&,
      orbital::Bytes, orbital::Bytes, ExplicitTuple16&);
  friend class QualifiedTuple8;
public:
  // Output storage only until a qualification call succeeds. Native use still
  // requires the cold proof and caller-held immutable backing/Schema protection.
  ExplicitTuple16() : data_(nullptr), records_(0) {}
  static orbital::Result<ExplicitTuple16> qualify(const v::TupleControl& control,
      orbital::Bytes parent, std::span<const v::ReadPart> parts);
  std::size_t size() const noexcept { return records_; }
  [[gnu::always_inline]] tp::const_view view(const v::Schema& schema) const {
    const auto placed = tp::const_view::bind(schema.tuple,
        {data_, records_ * v::tuple_bytes}, records_, v::tuple_bytes);
    if (!placed) std::unreachable();
    return *placed;
  }
};
static_assert(sizeof(ExplicitTuple16) == 16);

namespace detail {
inline bool valid_span(orbital::Bytes bytes) {
  const auto address = reinterpret_cast<std::uintptr_t>(bytes.data());
  return (!bytes.size() || address) && bytes.size() <= UINTPTR_MAX - address;
}
inline std::array<std::byte, 8> count_bytes(std::size_t count) {
  std::array<std::byte, 8> result;
  for (unsigned i = 0; i < 8; ++i) result[i] = std::byte((std::uint64_t(count) >> (i * 8)) & 255);
  return result;
}
inline bool logical_count(const orbital::object::Description& description, std::size_t count) {
  if (description.format != v::data_program || description.logical_metadata.size() != 8 ||
      !valid_span(description.logical_metadata)) return false;
  const auto expected = count_bytes(count);
  return std::equal(expected.begin(), expected.end(), description.logical_metadata.begin());
}
} // namespace detail

// Independent complete Tuple Part admission, with no sibling discovery. The
// enclosing selected fixture separately proves Object/version/ProgramId,
// logical count and expected requested PartId association. This helper proves
// only exact body/Part descriptor/unit/transfer facts for the supplied control.
// Status+out writes output exactly once, after every cold check succeeds.
namespace detail {
inline orbital::Result<void> admit_tuple_part(const v::TupleControl& control,
    orbital::Bytes body, orbital::Bytes interpretation, const tp::byte*& output) {
  const auto refused = [] { return std::unexpected(orbital::Error::invalid_argument); };
  if (control.records > SIZE_MAX / v::tuple_bytes ||
      body.size() != control.records * v::tuple_bytes || !detail::valid_span(body) ||
      interpretation.size() != 75 || !detail::valid_span(interpretation) ||
      control.schema.tuple.encoded_size() != 75) return refused();
  std::array<tp::byte, 75> encoded;
  const auto serialized = control.schema.tuple.encode(encoded);
  if (!serialized || *serialized != encoded.size() ||
      !std::equal(encoded.begin(), encoded.end(),
          reinterpret_cast<const tp::byte*>(interpretation.data()))) return refused();
  const auto placed = tp::const_view::bind(control.schema.tuple,
      {reinterpret_cast<const tp::byte*>(body.data()), body.size()}, control.records, v::tuple_bytes);
  if (!placed) return refused();
  const auto admit = [&]<unsigned Bytes>(unsigned field) {
    const auto operation = tp::bind_reader(control.schema.readers[field], *placed);
    return operation && tp::native_reader(*operation).template admit_transfer<Bytes>().has_value();
  };
  if (!admit.template operator()<8>(0) || !admit.template operator()<8>(1) ||
      !admit.template operator()<3>(2) || !admit.template operator()<4>(3)) return refused();
  output = reinterpret_cast<const tp::byte*>(body.data());
  return {};
}
} // namespace detail
inline orbital::Result<void> qualify_tuple_part16(const v::TupleControl& control,
    orbital::Bytes body, orbital::Bytes interpretation, ExplicitTuple16& output) {
  const tp::byte* data{};
  const auto status = detail::admit_tuple_part(control, body, interpretation, data);
  if (!status) return std::unexpected(status.error());
  output = ExplicitTuple16{data, control.records};
  return {};
}
inline orbital::Result<ExplicitTuple16> qualify_tuple_part16(const v::TupleControl& control,
    orbital::Bytes body, orbital::Bytes interpretation) {
  ExplicitTuple16 output;
  const auto status = qualify_tuple_part16(control, body, interpretation, output);
  if (!status) return std::unexpected(status.error());
  return output;
}
inline orbital::Result<void> qualify_tuple_part16(const v::TupleControl& control,
    orbital::object::View view, ExplicitTuple16& output) {
  if (!view) return std::unexpected(orbital::Error::invalid_argument);
  return qualify_tuple_part16(control, view.body(), view.interpretation(), output);
}
inline orbital::Result<ExplicitTuple16> qualify_tuple_part16(const v::TupleControl& control,
                                                         orbital::object::View view) {
  ExplicitTuple16 output;
  const auto status = qualify_tuple_part16(control, view, output);
  if (!status) return std::unexpected(status.error());
  return output;
}

// Checked legacy serialized parent admission is shared with actual descriptor
// adapters. The caller also admits ProgramId/logical metadata/Object/version
// and proves this requested PartId names the actual supplied View. This helper
// covers canonical24B parent/count/field law/reserved plus fixed row ID/one Part.
inline orbital::Result<void> qualify_fixed_tuple_parent(std::size_t records,
    orbital::Bytes parent, orbital::PartId row_part_id, std::size_t actual_part_count) {
  const auto refused = [] { return std::unexpected(orbital::Error::invalid_argument); };
  if (parent.size() != 24 || !detail::valid_span(parent) || actual_part_count != 1 ||
      row_part_id != orbital::PartId{0x56455253494f4e44ULL, 0} ||
      records > SIZE_MAX / v::tuple_bytes)
    return refused();
  std::array<std::byte, 24> expected{};
  expected[0] = std::byte{'O'}; expected[1] = std::byte{'B'};
  expected[2] = std::byte{'V'}; expected[3] = std::byte{'D'};
  expected[4] = std::byte{1}; expected[5] = std::byte{1};
  expected[6] = std::byte{4}; expected[7] = std::byte{1};
  const auto count = detail::count_bytes(records);
  std::copy(count.begin(), count.end(), expected.begin() + 8);
  constexpr std::array<unsigned, 4> widths{64, 64, 20, 32};
  for (unsigned i = 0; i < 4; ++i) expected[16 + i] = std::byte(widths[i]);
  if (!std::equal(expected.begin(), expected.end(), parent.begin())) return refused();
  return {};
}
inline orbital::Result<void> qualify_tuple_parent(const v::TupleControl& control,
    orbital::Bytes parent, orbital::PartId row_part_id, std::size_t actual_part_count) {
  return qualify_fixed_tuple_parent(control.records, parent, row_part_id, actual_part_count);
}
// Full legacy parent admission, still direct: bounded24B parent and75B Part
// scratch, no generic ReadData56/Result64 or decoded392B layout. Rejection must
// leave caller output unchanged, including when parent admission fails.
inline orbital::Result<void> qualify_tuple16(const v::TupleControl& control,
    orbital::Bytes parent, std::span<const v::ReadPart> parts, ExplicitTuple16& output) {
  const auto status = qualify_tuple_parent(control, parent,
      parts.size() == 1 ? parts[0].id : orbital::PartId{}, parts.size());
  if (!status) return std::unexpected(status.error());
  const auto& part = parts[0];
  return qualify_tuple_part16(control, part.bytes, part.interpretation, output);
}
inline orbital::Result<ExplicitTuple16> qualify_tuple16(const v::TupleControl& control,
    orbital::Bytes parent, std::span<const v::ReadPart> parts) {
  ExplicitTuple16 output;
  const auto status = qualify_tuple16(control, parent, parts, output);
  if (!status) return std::unexpected(status.error());
  return output;
}
inline orbital::Result<ExplicitTuple16> ExplicitTuple16::qualify(const v::TupleControl& control,
    orbital::Bytes parent, std::span<const v::ReadPart> parts) {
  return qualify_tuple16(control, parent, parts);
}
static_assert(sizeof(orbital::Result<ExplicitTuple16>) == 24);

// Direct pointer-only success is a real additional candidate, not a pointer to
// a hidden count/Schema/owner record. The stable explicit control supplies the
// very count admitted by qualify_tuple_part8; replacing it arbitrarily is not a
// checked operation. Successful result is16B, successful state is8B.
class QualifiedTuple8 {
  const tp::byte* data_;
  explicit QualifiedTuple8(const tp::byte* data) : data_(data) {}
  friend orbital::Result<void> qualify_tuple_part8(const v::TupleControl&,
      orbital::Bytes, orbital::Bytes, QualifiedTuple8&);
public:
  QualifiedTuple8() : data_(nullptr) {} // Output storage until qualification succeeds.
  [[gnu::always_inline]] ExplicitTuple16 with(const v::TupleControl& control) const {
    return ExplicitTuple16{data_, control.records};
  }
};
static_assert(sizeof(QualifiedTuple8) == 8);
static_assert(sizeof(orbital::Result<QualifiedTuple8>) == 16);
inline orbital::Result<void> qualify_tuple_part8(const v::TupleControl& control,
    orbital::Bytes body, orbital::Bytes interpretation, QualifiedTuple8& output) {
  const tp::byte* data{};
  const auto status = detail::admit_tuple_part(control, body, interpretation, data);
  if (!status) return std::unexpected(status.error());
  output = QualifiedTuple8{data};
  return {};
}
inline orbital::Result<QualifiedTuple8> qualify_tuple_part8(const v::TupleControl& control,
    orbital::Bytes body, orbital::Bytes interpretation) {
  QualifiedTuple8 output;
  const auto status = qualify_tuple_part8(control, body, interpretation, output);
  if (!status) return std::unexpected(status.error());
  return output;
}
inline orbital::Result<void> qualify_tuple8(const v::TupleControl& control,
    orbital::Bytes parent, std::span<const v::ReadPart> parts, QualifiedTuple8& output) {
  const auto status = qualify_tuple_parent(control, parent,
      parts.size() == 1 ? parts[0].id : orbital::PartId{}, parts.size());
  if (!status) return std::unexpected(status.error());
  return qualify_tuple_part8(control, parts[0].bytes, parts[0].interpretation, output);
}
inline orbital::Result<QualifiedTuple8> qualify_tuple8(const v::TupleControl& control,
    orbital::Bytes parent, std::span<const v::ReadPart> parts) {
  QualifiedTuple8 output;
  const auto status = qualify_tuple8(control, parent, parts, output);
  if (!status) return std::unexpected(status.error());
  return output;
}
inline orbital::Result<void> qualify_tuple_part8(const v::TupleControl& control,
    orbital::object::View view, QualifiedTuple8& output) {
  if (!view) return std::unexpected(orbital::Error::invalid_argument);
  return qualify_tuple_part8(control, view.body(), view.interpretation(), output);
}
inline orbital::Result<QualifiedTuple8> qualify_tuple_part8(const v::TupleControl& control,
                                                       orbital::object::View view) {
  QualifiedTuple8 output;
  const auto status = qualify_tuple_part8(control, view, output);
  if (!status) return std::unexpected(status.error());
  return output;
}

// These overloads derive qualified data directly from the actual Orbital
// descriptor closure. Selection/context/Representation/retained View protection
// remains caller-owned and live through native use; no identity or owner is
// hidden in ExplicitTuple16. Selection/Representation prove actual directory
// completeness here. A direct View exposes no directory count: its caller must
// already have admitted the whole parent's one-Part requirement. That overload
// still checks available ProgramId/logical/serialized parent/PartId facts; it
// cannot turn a resolved Part into proof of its parent's directory completeness.
template<class Access>
inline orbital::Result<void> qualify_selected_tuple16(const v::TupleControl& control,
    const Access& selected, ExplicitTuple16& output) {
  if (!selected || selected.part_count() != 1 ||
      !detail::logical_count(selected.description(), control.records))
    return std::unexpected(orbital::Error::invalid_argument);
  const auto view = selected.part(std::size_t{0});
  if (!view) return std::unexpected(view.error());
  const v::ReadPart part{view->id(), view->body(), view->interpretation()};
  return qualify_tuple16(control, selected.description().interpretation,
                        std::span<const v::ReadPart>{&part, 1}, output);
}
template<class Access>
inline orbital::Result<ExplicitTuple16> qualify_selected_tuple16(const v::TupleControl& control,
                                                               const Access& selected) {
  ExplicitTuple16 output;
  const auto status = qualify_selected_tuple16(control, selected, output);
  if (!status) return std::unexpected(status.error());
  return output;
}
inline orbital::Result<ExplicitTuple16> qualify_tuple16(const v::TupleControl& control,
                                                     orbital::object::Selection selected) {
  return qualify_selected_tuple16(control, selected);
}
inline orbital::Result<void> qualify_tuple16(const v::TupleControl& control,
    orbital::object::Selection selected, ExplicitTuple16& output) {
  return qualify_selected_tuple16(control, selected, output);
}
inline orbital::Result<ExplicitTuple16> qualify_tuple16(const v::TupleControl& control,
                                                     const orbital::object::Representation& representation) {
  return qualify_selected_tuple16(control, representation);
}
inline orbital::Result<void> qualify_tuple16(const v::TupleControl& control,
    const orbital::object::Representation& representation, ExplicitTuple16& output) {
  return qualify_selected_tuple16(control, representation, output);
}
inline orbital::Result<void> qualify_tuple16(const v::TupleControl& control,
    orbital::object::View view, ExplicitTuple16& output) {
  // Prior whole-parent admission, including actual Part count, is required.
  // The one-element span describes this View only, not its parent directory.
  if (!view || !detail::logical_count(view.description(), control.records))
    return std::unexpected(orbital::Error::invalid_argument);
  const v::ReadPart part{view.id(), view.body(), view.interpretation()};
  return qualify_tuple16(control, view.description().interpretation,
                        std::span<const v::ReadPart>{&part, 1}, output);
}
inline orbital::Result<ExplicitTuple16> qualify_tuple16(const v::TupleControl& control,
                                                     orbital::object::View view) {
  ExplicitTuple16 output;
  const auto status = qualify_tuple16(control, view, output);
  if (!status) return std::unexpected(status.error());
  return output;
}
orbital::Result<ExplicitTuple16> qualify_tuple16(const v::TupleControl&,
                                              orbital::object::Representation&&) = delete;
orbital::Result<void> qualify_tuple16(const v::TupleControl&,
                                    orbital::object::Representation&&, ExplicitTuple16&) = delete;

// All payload reads below use the frozen fixed-transfer Tuple kernel via its
// existing native helpers. Only operation placement and whole-call fusion
// differ. Endpoints borrow one named view inside the uninterrupted call and
// never escape into a movable state, shared record or returned handle.
struct LookupOutcome { Record record{}; bool found{}; };
[[gnu::always_inline]] inline void tuple_get_into(const v::Schema& schema,
    ExplicitTuple16 state, Key key, LookupOutcome& output) {
  const auto source = state.view(schema);
  const auto read = v::native_detail::tuple_read(schema, source, 0);
  const auto read_key = [&](std::size_t row) { return v::native_detail::tuple_value<8>(read, row); };
  const auto row = v::native_detail::seek(state.size(), key, read_key);
  if (row == state.size() || read_key(row) != key) { output.found = false; return; }
  const auto next = v::native_detail::tuple_read(schema, source, 1);
  const auto value = v::native_detail::tuple_read(schema, source, 2);
  const auto tag = v::native_detail::tuple_read(schema, source, 3);
  output.record = {v::native_detail::tuple_value<8>(next, row),
      std::uint32_t(v::native_detail::tuple_value<3>(value, row)),
      std::uint32_t(v::native_detail::tuple_value<4>(tag, row))};
  output.found = true;
}
[[gnu::always_inline]] inline std::optional<Record> tuple_get(const v::Schema& schema,
                                                            ExplicitTuple16 state, Key key) {
  LookupOutcome output;
  tuple_get_into(schema, state, key, output);
  if (!output.found) return {};
  return output.record;
}
inline std::size_t tuple_lower_bound(const v::Schema& schema, ExplicitTuple16 state, Key key) {
  const auto source = state.view(schema);
  const auto read = v::native_detail::tuple_read(schema, source, 0);
  return v::native_detail::seek(state.size(), key,
      [&](std::size_t row) { return v::native_detail::tuple_value<8>(read, row); });
}
inline orbital::Result<Row> tuple_row(const v::Schema& schema, ExplicitTuple16 state, std::size_t row) {
  if (row >= state.size()) return std::unexpected(orbital::Error::invalid_argument);
  const auto source = state.view(schema);
  const auto reads = v::native_detail::tuple_reads(schema, source);
  return Row{v::native_detail::tuple_value<8>(reads[0], row), v::native_detail::tuple_record(reads, row)};
}
inline orbital::Result<void> tuple_field(const v::Schema& schema, ExplicitTuple16 state,
    v::Field field, std::size_t first, std::span<std::uint64_t> output) {
  const auto index = static_cast<unsigned>(field);
  if (index >= v::field_count || first > state.size() || output.size() > state.size() - first)
    return std::unexpected(orbital::Error::invalid_argument);
  const auto source = state.view(schema);
  const auto read = v::native_detail::tuple_read(schema, source, index);
  const auto range = [&]<unsigned Bytes>() {
    for (std::size_t i = 0; i < output.size(); ++i)
      output[i] = v::native_detail::tuple_value<Bytes>(read, first + i);
  };
  switch (field) {
  case v::Field::key: case v::Field::next: range.template operator()<8>(); break;
  case v::Field::value: range.template operator()<3>(); break;
  case v::Field::tag: range.template operator()<4>(); break;
  }
  return {};
}
inline std::size_t tuple_scan(const v::Schema& schema, ExplicitTuple16 state, Key key,
                             std::span<Row> output) {
  const auto source = state.view(schema);
  const auto reads = v::native_detail::tuple_reads(schema, source);
  const auto read_key = [&](std::size_t row) { return v::native_detail::tuple_value<8>(reads[0], row); };
  const auto first = v::native_detail::seek(state.size(), key, read_key);
  const auto count = std::min(output.size(), state.size() - first);
  for (std::size_t i = 0; i < count; ++i)
    output[i] = {read_key(first + i), v::native_detail::tuple_record(reads, first + i)};
  return count;
}
[[gnu::always_inline]] inline void tuple_chain_into(const v::Schema& schema,
    ExplicitTuple16 state, Key key, std::span<Record> output, ChainResult& outcome) {
  const auto source = state.view(schema);
  const auto reads = v::native_detail::tuple_reads(schema, source);
  const auto read_key = [&](std::size_t row) { return v::native_detail::tuple_value<8>(reads[0], row); };
  std::size_t completed = 0;
  for (; completed < output.size(); ++completed) {
    const auto row = v::native_detail::seek(state.size(), key, read_key);
    if (row == state.size() || read_key(row) != key) break;
    const auto record = v::native_detail::tuple_record(reads, row);
    output[completed] = record;
    key = record.next;
  }
  outcome = {completed, key};
}
[[gnu::always_inline]] inline ChainResult tuple_chain(const v::Schema& schema,
    ExplicitTuple16 state, Key key, std::span<Record> output) {
  ChainResult outcome;
  tuple_chain_into(schema, state, key, output, outcome);
  return outcome;
}
[[gnu::always_inline]] inline std::optional<Record> tuple_get8(const v::TupleControl& control,
                                                            QualifiedTuple8 state, Key key) {
  return tuple_get(control.schema, state.with(control), key);
}
[[gnu::always_inline]] inline void tuple_get8_into(const v::TupleControl& control,
    QualifiedTuple8 state, Key key, LookupOutcome& output) {
  tuple_get_into(control.schema, state.with(control), key, output);
}
[[gnu::always_inline]] inline ChainResult tuple_chain8(const v::TupleControl& control,
    QualifiedTuple8 state, Key key, std::span<Record> output) {
  return tuple_chain(control.schema, state.with(control), key, output);
}
[[gnu::always_inline]] inline void tuple_chain8_into(const v::TupleControl& control,
    QualifiedTuple8 state, Key key, std::span<Record> output, ChainResult& outcome) {
  tuple_chain_into(control.schema, state.with(control), key, output, outcome);
}

// Column comparator: four independent admitted pointers remain visible and
// count stays explicit. No row/column group backing record or container policy.
[[gnu::always_inline]] inline ChainResult columns_chain(const v::ColumnControl& control,
    v::ColumnCursors cursors, Key key, std::span<Record> output) {
  const auto body = cursors.open(control);
  return body_chain(body, key, output);
}

struct StaticFootprint {
  std::size_t default_tuple_state, explicit_tuple_state, schema, tuple_control,
      column_control, tuple_cursor, column_cursors, tuple_body, column_body,
      tuple_view, tuple_endpoint, four_tuple_endpoints, schema_layout,
      schema_constructor, schema_readers, schema_writers;
};
inline constexpr StaticFootprint footprint{
    sizeof(DefaultTuple16), sizeof(ExplicitTuple16), sizeof(v::Schema), sizeof(v::TupleControl),
    sizeof(v::ColumnControl), sizeof(v::TupleCursor), sizeof(v::ColumnCursors), sizeof(v::TupleBody),
    sizeof(v::ColumnBody), sizeof(tp::const_view), sizeof(v::native_detail::TupleRead),
    sizeof(std::array<v::native_detail::TupleRead, v::field_count>), sizeof(tp::layout),
    sizeof(tp::constructor), sizeof(std::array<tp::reader<8>, v::field_count>),
    sizeof(std::array<tp::writer<8>, v::field_count>)};
// Footprint entries are actual C++ records, not a claim that their sum is a
// machine stack frame. Optimizer/register behavior belongs to Tooling's matched
// whole-body inspection. Parent/Part serialized interpretations and body bytes,
// Allocation/Store protection and any caller Schema are additional live state.
} // namespace orbital_mvcc::metadata_consumer
