#pragma once
#include "data.h"
#include <ikea/tuplepack/author/execution.h>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace orbital_bench::versioned {
namespace native_detail {
using TupleRead = tp::read_operation<8, const tp::byte>;
[[gnu::always_inline]] inline TupleRead tuple_read(const Schema& schema,
    const tp::const_view& view, unsigned field) {
  // Qualification proved immutable plan/view unit and transfer shapes. Every
  // endpoint borrows this call's stationary view and ends with the native body.
  const auto operation = tp::bind_reader(schema.readers[field], view);
  if (!operation) std::unreachable();
  return *operation;
}
template<unsigned Bytes>
[[gnu::always_inline]] inline std::uint64_t tuple_value(const TupleRead& operation,
                                                      std::size_t row) {
  return tp::native_reader(operation).template get_transfer_unchecked<Bytes>(row);
}
[[gnu::always_inline]] inline auto tuple_reads(const Schema& schema, const tp::const_view& view) {
  return std::array{tuple_read(schema, view, 0), tuple_read(schema, view, 1),
                    tuple_read(schema, view, 2), tuple_read(schema, view, 3)};
}
[[gnu::always_inline]] inline Record tuple_record(const std::array<TupleRead, field_count>& reads,
                                                 std::size_t row) {
  return {tuple_value<8>(reads[1], row), std::uint32_t(tuple_value<3>(reads[2], row)),
          std::uint32_t(tuple_value<4>(reads[3], row))};
}
template<class ReadKey>
[[gnu::always_inline]] inline std::size_t seek(std::size_t records, Key key, ReadKey&& read_key) {
  std::size_t first = 0, last = records;
  while (first < last) {
    const auto middle = first + (last - first) / 2;
    if (read_key(middle) < key) first = middle + 1;
    else last = middle;
  }
  return first;
}
} // namespace native_detail

// Ephemeral complete native body: one pointer plus explicit shared schema/count.
// Copies/moves borrow no former cursor, control or frame address. The caller
// admits matching control at qualify_tuple and retains Schema/backing protection.
// Lookup/scan require sorted keys. Field/scan outputs are disjoint from source
// bytes and live metadata; checked methods admit ranges, not these conditions.
class TupleBody {
  const Schema* schema_;
  std::size_t records_;
  TupleCursor cursor_;
  TupleBody(TupleCursor cursor, const TupleControl& control)
      : schema_(&control.schema), records_(control.records), cursor_(cursor) {}
  friend class TupleCursor;
  [[gnu::always_inline]] tp::const_view view() const {
    // Exact extent/overflow, zero offset and tight stride were already proved.
    const auto placed = tp::const_view::bind(schema_->tuple,
        {cursor_.data_, records_ * tuple_bytes}, records_, tuple_bytes);
    if (!placed) std::unreachable();
    return *placed;
  }
public:
  std::size_t size() const noexcept { return records_; }
  std::uint64_t field_unchecked(Field field, std::size_t row) const {
    const auto source = view();
    const auto index = static_cast<unsigned>(field);
    if (index >= field_count) throw std::logic_error("invalid fixture Field in unchecked read");
    const auto read = native_detail::tuple_read(*schema_, source, index);
    switch (field) {
    case Field::key: case Field::next: return native_detail::tuple_value<8>(read, row);
    case Field::value: return native_detail::tuple_value<3>(read, row);
    case Field::tag: return native_detail::tuple_value<4>(read, row);
    }
    std::unreachable();
  }
  Row row_unchecked(std::size_t row) const {
    const auto source = view();
    const auto reads = native_detail::tuple_reads(*schema_, source);
    return {native_detail::tuple_value<8>(reads[0], row), native_detail::tuple_record(reads, row)};
  }
  orbital::Result<Row> row(std::size_t row) const {
    if (row >= records_) return std::unexpected(orbital::Error::invalid_argument);
    return row_unchecked(row);
  }
  orbital::Result<void> field(Field field, std::size_t first, std::span<std::uint64_t> output) const {
    const auto index = static_cast<unsigned>(field);
    if (index >= field_count || first > records_ || output.size() > records_ - first)
      return std::unexpected(orbital::Error::invalid_argument);
    const auto source = view();
    const auto read = native_detail::tuple_read(*schema_, source, index);
    const auto range = [&]<unsigned Bytes>() {
      for (std::size_t i = 0; i < output.size(); ++i)
        output[i] = native_detail::tuple_value<Bytes>(read, first + i);
    };
    switch (field) {
    case Field::key: case Field::next: range.template operator()<8>(); break;
    case Field::value: range.template operator()<3>(); break;
    case Field::tag: range.template operator()<4>(); break;
    }
    return {};
  }
  std::size_t lower_bound(Key key) const {
    const auto source = view();
    const auto read = native_detail::tuple_read(*schema_, source, 0);
    return native_detail::seek(records_, key,
        [&](std::size_t row) { return native_detail::tuple_value<8>(read, row); });
  }
  [[gnu::always_inline]] std::optional<Record> get(Key key) const {
    const auto source = view();
    const auto read = native_detail::tuple_read(*schema_, source, 0);
    const auto read_key = [&](std::size_t row) { return native_detail::tuple_value<8>(read, row); };
    const auto row = native_detail::seek(records_, key, read_key);
    if (row == records_ || read_key(row) != key) return {};
    const auto next = native_detail::tuple_read(*schema_, source, 1);
    const auto value = native_detail::tuple_read(*schema_, source, 2);
    const auto tag = native_detail::tuple_read(*schema_, source, 3);
    return Record{native_detail::tuple_value<8>(next, row),
        std::uint32_t(native_detail::tuple_value<3>(value, row)),
        std::uint32_t(native_detail::tuple_value<4>(tag, row))};
  }
  std::size_t scan(Key key, std::span<Row> output) const {
    const auto source = view();
    const auto reads = native_detail::tuple_reads(*schema_, source);
    const auto read_key = [&](std::size_t row) { return native_detail::tuple_value<8>(reads[0], row); };
    const auto first = native_detail::seek(records_, key, read_key);
    const auto count = std::min(output.size(), records_ - first);
    for (std::size_t i = 0; i < count; ++i)
      output[i] = {read_key(first + i), native_detail::tuple_record(reads, first + i)};
    return count;
  }
};
inline TupleBody TupleCursor::open(const TupleControl& control) const { return {*this, control}; }

// Four already-admitted tight Local column pointers plus one shared row count.
// The typed cursors need no Schema or runtime format tag in their native body.
// Sorted-key and disjoint-output contracts are the same as TupleBody's.
class ColumnBody {
  std::size_t records_;
  ColumnCursors cursors_;
  ColumnBody(ColumnCursors cursors, const ColumnControl& control)
      : records_(control.records), cursors_(cursors) {}
  friend class ColumnCursors;
  template<unsigned K> auto decoder(ColumnCursor<K> cursor) const {
    return sp::bind_decoder<std::uint64_t>(sp::dense_source<Column<K>>{cursor.data_, records_});
  }
  template<unsigned K> std::uint64_t value(ColumnCursor<K> cursor, std::size_t row) const {
    return decoder(cursor).get_unchecked(row);
  }
  Record record(std::size_t row) const {
    return {value(cursors_.next, row), std::uint32_t(value(cursors_.value, row)),
            std::uint32_t(value(cursors_.tag, row))};
  }
public:
  std::size_t size() const noexcept { return records_; }
  std::uint64_t field_unchecked(Field field, std::size_t row) const {
    switch (field) {
    case Field::key: return value(cursors_.key, row);
    case Field::next: return value(cursors_.next, row);
    case Field::value: return value(cursors_.value, row);
    case Field::tag: return value(cursors_.tag, row);
    }
    throw std::logic_error("invalid fixture Field in unchecked read");
  }
  Row row_unchecked(std::size_t row) const { return {value(cursors_.key, row), record(row)}; }
  orbital::Result<Row> row(std::size_t row) const {
    if (row >= records_) return std::unexpected(orbital::Error::invalid_argument);
    return row_unchecked(row);
  }
  orbital::Result<void> field(Field field, std::size_t first, std::span<std::uint64_t> output) const {
    if (static_cast<unsigned>(field) >= field_count || first > records_ || output.size() > records_ - first)
      return std::unexpected(orbital::Error::invalid_argument);
    switch (field) {
    case Field::key: decoder(cursors_.key).read_unchecked(first, output.size(), output.data()); break;
    case Field::next: decoder(cursors_.next).read_unchecked(first, output.size(), output.data()); break;
    case Field::value: decoder(cursors_.value).read_unchecked(first, output.size(), output.data()); break;
    case Field::tag: decoder(cursors_.tag).read_unchecked(first, output.size(), output.data()); break;
    }
    return {};
  }
  std::size_t lower_bound(Key key) const {
    return native_detail::seek(records_, key, [&](std::size_t row) { return value(cursors_.key, row); });
  }
  std::optional<Record> get(Key key) const {
    const auto read_key = [&](std::size_t row) { return value(cursors_.key, row); };
    const auto row = native_detail::seek(records_, key, read_key);
    if (row == records_ || read_key(row) != key) return {};
    return record(row);
  }
  std::size_t scan(Key key, std::span<Row> output) const {
    const auto read_key = [&](std::size_t row) { return value(cursors_.key, row); };
    const auto first = native_detail::seek(records_, key, read_key);
    const auto count = std::min(output.size(), records_ - first);
    for (std::size_t i = 0; i < count; ++i) output[i] = {read_key(first + i), record(first + i)};
    return count;
  }
};
inline ColumnBody ColumnCursors::open(const ColumnControl& control) const { return {*this, control}; }
} // namespace orbital_bench::versioned
