#pragma once

#include "trace.h"
#include <ikea/seriespack.h>
#include <ikea/tuplepack.h>
#include <orbital/types.h>
#include <array>
#include <variant>

// Disposable fixed-record codec. The experimental access adapter supplies actual
// qualified bytes and retains their selected version, interpretation and rights.
namespace orbital_bench::versioned {
namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
enum class Format : std::uint8_t { rows = 1, columns = 2 };
enum class Field : unsigned { key, next, value, tag };
constexpr std::size_t field_count = 4;
constexpr std::size_t conversion_chunk = 128;
constexpr unsigned tuple_bytes = 23;
template<unsigned K> using Column = sp::layout<K, sp::geometry::local, 0>;

struct Schema {
  const tp::layout tuple;
  const tp::write_plan<128> constructor;
  const std::array<tp::read_plan<8>, field_count> readers;
  const std::array<tp::write_plan<8>, field_count> writers;
  Schema();
};

// Shared caller control, separate from each admitted Part cursor. Schema and
// backing protection remain live through native use. open() requires the same
// or equivalent immutable control used at qualification; it does not validate
// an arbitrary replacement count. Only qualify_* is a checked entry point.
struct TupleControl { const Schema& schema; const std::size_t records; };
struct ColumnControl { const std::size_t records; };
class TupleBody;
class ColumnBody;
class TupleCursor {
  const std::byte* data_;
  explicit TupleCursor(const std::byte* data) : data_(data) {}
  friend class ReadData;
  friend class TupleBody;
public:
  TupleBody open(const TupleControl&) const;
};
template<unsigned K> class ColumnCursor {
  const std::byte* data_;
  explicit ColumnCursor(const std::byte* data) : data_(data) {}
  friend class ReadData;
  friend class ColumnBody;
};
class ColumnCursors {
  ColumnCursor<64> key, next;
  ColumnCursor<20> value;
  ColumnCursor<32> tag;
  ColumnCursors(ColumnCursor<64> key, ColumnCursor<64> next,
                ColumnCursor<20> value, ColumnCursor<32> tag)
      : key(key), next(next), value(value), tag(tag) {}
  friend class ReadData;
  friend class ColumnBody;
public:
  ColumnBody open(const ColumnControl&) const;
};

struct PartShape {
  orbital::PartId id{};
  std::size_t body_bytes{}, alignment{1}, readable_padding{};
  std::vector<std::byte> interpretation;
};
struct Shape {
  Format format{};
  std::size_t records{};
  unsigned part_count{};
  std::array<PartShape, field_count> parts;
  // Versioned fixture interpretation; owns format, count and fixed field law.
  std::vector<std::byte> interpretation;
};
orbital::Result<Shape> shape(const Schema&, Format, std::size_t records);
orbital::Result<Shape> decode_shape(const Schema&, orbital::Bytes interpretation);

struct ReadPart { orbital::PartId id{}; orbital::Bytes bytes, interpretation; };
struct WritePart { orbital::PartId id{}; orbital::MutableBytes bytes; };
struct KernelCosts {
  std::uint64_t decoded_values{}, encoded_values{}, bindings{}, constructor_calls{};
  std::uint64_t issued_bytes{}, issued_spans{}, completed_parts{}, completed_rows{};
  std::size_t scratch_bound_bytes{};
};
struct ConstructionOptions {
  std::size_t fail_after_parts{SIZE_MAX}, fail_after_rows{SIZE_MAX};
};

// Qualified placement contains no endpoints to this object's former address,
// so moving this record is safe. Schema and bytes remain borrowed; this is
// neither a lifetime handle nor selected-version or write authority.
class ReadData {
  const Schema* schema_;
  std::size_t records_;
  std::variant<TupleCursor, ColumnCursors> cursors_;
  ReadData(const Schema&, std::size_t, TupleCursor);
  ReadData(const Schema&, std::size_t, ColumnCursors);
public:
  static orbital::Result<ReadData> bind(const Schema&, const Shape&, std::span<const ReadPart>);
  // Parse/qualify retained parent and Part interpretation on stack; no owning
  // Shape reconstruction or allocation is needed for selected/native access.
  static orbital::Result<ReadData> bind(const Schema&, orbital::Bytes parent_interpretation,
                                      std::span<const ReadPart>);
  std::size_t size() const noexcept { return records_; }
  Format format() const noexcept { return cursors_.index() == 0 ? Format::rows : Format::columns; }
  orbital::Result<TupleCursor> tuple_cursor() const;
  orbital::Result<ColumnCursors> column_cursors() const;
  orbital::Result<Row> row(std::size_t) const;
  orbital::Result<void> field(Field, std::size_t first, std::span<std::uint64_t>) const;
  // Requires sorted keys. Reads the encoded key field; no decoded-key directory.
  std::size_t lower_bound(Key) const;
  std::optional<Record> get(Key) const;
  std::size_t scan(Key, std::span<Row>) const;
  Row row_unchecked(std::size_t) const;
  std::uint64_t field_unchecked(Field, std::size_t) const;
};

// Cold qualification consumes descriptors; returned cursors borrow only bytes,
// not the ReadPart array or a temporary/movable ReadData. No allocation occurs.
orbital::Result<TupleCursor> qualify_tuple(const TupleControl&, orbital::Bytes parent_interpretation,
                                         std::span<const ReadPart>);
orbital::Result<ColumnCursors> qualify_columns(const Schema&, const ColumnControl&,
    orbital::Bytes parent_interpretation, std::span<const ReadPart>);

// Bodies must have exact shapes and disjoint exclusive storage. Construct and
// convert may leave a private target partially initialized after injected
// failure; their enclosing baseline Builder must discard it. No published
// bytes are writable here. No whole-input Row vector or sentinel initialization.
orbital::Result<void> construct_into(const Schema&, const Shape&, std::span<const WritePart>,
    std::span<const Row>, KernelCosts&, ConstructionOptions = {});
orbital::Result<void> convert_into(const Schema&, const ReadData&, const Shape&,
    std::span<const WritePart>, KernelCosts&, ConstructionOptions = {});
// Target is already completely initialized, private and exclusive. This changes
// next/value/tag at one fixed row; key and membership stay unchanged.
orbital::Result<void> update_into(const Schema&, const Shape&, std::span<const WritePart>,
    std::size_t row, Record, KernelCosts&);
} // namespace orbital_bench::versioned
