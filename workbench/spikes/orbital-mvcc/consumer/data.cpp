#include "data.h"
#include "native.h"
#include <ikea/tuplepack/author/execution.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace orbital_bench::versioned {
namespace {
struct Refusal { orbital::Error error; };
void require(bool condition, orbital::Error error = orbital::Error::invalid_argument) {
  if (!condition) throw Refusal{error};
}
template<class T, class E> T take(std::expected<T, E> result) {
  require(result.has_value());
  return std::move(*result);
}
template<class E> void take(std::expected<void, E> result) { require(result.has_value()); }

constexpr std::array<unsigned, field_count> widths{64, 64, 20, 32};
constexpr std::array<unsigned, field_count> map_counts{8, 8, 3, 4};
constexpr std::array<std::array<tp::byte, 8>, field_count> maps{{
    {0,1,2,3,4,5,6,7}, {8,9,10,11,12,13,14,15}, {16,17,18}, {19,20,21,22}}};
constexpr std::size_t parent_bytes = 24;
constexpr std::uint64_t part_namespace = 0x56455253494f4e44ULL;

tp::layout make_tuple() {
  std::array<tp::code, tuple_bytes> codes;
  for (unsigned i = 0; i < codes.size(); ++i)
    codes[i] = {tp::byte(i), 0, tp::byte(i == 18 ? 4 : 8)};
  return take(tp::layout::make(tuple_bytes, codes));
}
std::span<const tp::byte> field_map(unsigned field) {
  return std::span<const tp::byte>{maps[field]}.first(map_counts[field]);
}
std::array<tp::reader<8>, field_count> make_readers(const tp::layout& layout) {
  return {take(tp::reader<8>::make(layout, field_map(0))),
          take(tp::reader<8>::make(layout, field_map(1))),
          take(tp::reader<8>::make(layout, field_map(2))),
          take(tp::reader<8>::make(layout, field_map(3)))};
}
std::array<tp::writer<8>, field_count> make_writers(const tp::layout& layout) {
  return {take(tp::writer<8>::make(layout, field_map(0))),
          take(tp::writer<8>::make(layout, field_map(1))),
          take(tp::writer<8>::make(layout, field_map(2))),
          take(tp::writer<8>::make(layout, field_map(3)))};
}

std::size_t multiply(std::size_t a, std::size_t b) {
  require(!b || a <= SIZE_MAX / b);
  return a * b;
}
std::size_t column_bytes(unsigned field, std::size_t records) {
  return multiply(records / 8 + (records % 8 != 0), widths[field]);
}
unsigned parts(Format format) {
  require(format == Format::rows || format == Format::columns);
  return format == Format::rows ? 1 : field_count;
}
orbital::PartId part_id(Format format, unsigned field) {
  return {part_namespace, format == Format::rows ? 0 : std::uint64_t(field + 1)};
}
std::array<std::byte, parent_bytes> parent(Format format, std::size_t records) {
  // Disposable fixture v1: magic, version, format, field/Part counts, LE64 row
  // count, four unsigned widths and four reserved zero bytes. No native plans.
  std::array<std::byte, parent_bytes> result{};
  result[0] = std::byte{'O'}; result[1] = std::byte{'B'};
  result[2] = std::byte{'V'}; result[3] = std::byte{'D'};
  result[4] = std::byte{1}; result[5] = std::byte(static_cast<unsigned>(format));
  result[6] = std::byte{field_count}; result[7] = std::byte(parts(format));
  for (unsigned i = 0; i < 8; ++i) result[8 + i] = std::byte((std::uint64_t(records) >> (8 * i)) & 255);
  for (unsigned i = 0; i < field_count; ++i) result[16 + i] = std::byte(widths[i]);
  return result;
}
bool equal(orbital::Bytes left, orbital::Bytes right) {
  return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
}
struct Parent { Format format; std::size_t records; unsigned part_count; };
Parent parse_parent(orbital::Bytes interpretation) {
  require(interpretation.size() == parent_bytes);
  const auto format = static_cast<Format>(std::to_integer<unsigned>(interpretation[5]));
  std::uint64_t records = 0;
  for (unsigned i = 0; i < 8; ++i)
    records |= std::uint64_t(std::to_integer<unsigned>(interpretation[8 + i])) << (8 * i);
  require(records <= SIZE_MAX);
  const auto canonical = parent(format, static_cast<std::size_t>(records));
  require(equal(interpretation, canonical));
  return {format, static_cast<std::size_t>(records), parts(format)};
}
sp::representation column_description(unsigned field, std::size_t records) {
  return {widths[field], 0, sp::geometry::local, records, {widths[field], 0, 0}};
}
void qualify_shape(const Schema& schema, const Shape& shape) {
  require(shape.part_count == parts(shape.format));
  const auto expected_parent = parent(shape.format, shape.records);
  require(equal(shape.interpretation, expected_parent));
  for (unsigned field = 0; field < shape.part_count; ++field) {
    const auto& part = shape.parts[field];
    require(part.id == part_id(shape.format, field) && part.readable_padding == 0);
    if (shape.format == Format::rows) {
      require(part.body_bytes == multiply(shape.records, tuple_bytes) && part.alignment == 1);
      const auto layout = take(tp::layout::decode({
          reinterpret_cast<const tp::byte*>(part.interpretation.data()), part.interpretation.size()}));
      require(layout == schema.tuple);
    } else {
      require(part.body_bytes == column_bytes(field, shape.records) && part.alignment == 64);
      const auto descriptor = take(sp::decode_descriptor({
          reinterpret_cast<const std::uint8_t*>(part.interpretation.data()), part.interpretation.size()}));
      require(descriptor == column_description(field, shape.records));
    }
  }
  for (unsigned field = shape.part_count; field < field_count; ++field) {
    const auto& unused = shape.parts[field];
    require(unused.id == orbital::PartId{} && unused.body_bytes == 0 &&
            unused.alignment == 1 && unused.readable_padding == 0 && unused.interpretation.empty());
  }
}

template<unsigned K, class Byte, class Storage>
sp::view<Column<K>, Byte> column_view(std::size_t records, Storage storage) {
  std::array<sp::plane<Byte>, 3> planes{};
  planes[0] = {{reinterpret_cast<Byte*>(storage.data()), storage.size()}, Column<K>::tile_bytes};
  return take(sp::view<Column<K>, Byte>::attach(records, planes));
}
struct WriteColumns {
  sp::view<Column<64>, std::uint8_t> key, next;
  sp::view<Column<20>, std::uint8_t> value;
  sp::view<Column<32>, std::uint8_t> tag;
};
using WriteViews = std::variant<tp::view, WriteColumns>;

std::pair<std::uintptr_t, std::uintptr_t> interval(const void* address, std::size_t size) {
  const auto begin = reinterpret_cast<std::uintptr_t>(address);
  require(begin <= UINTPTR_MAX - size);
  require(!size || begin != 0);
  return {begin, begin + size};
}
bool overlap(std::pair<std::uintptr_t, std::uintptr_t> a,
             std::pair<std::uintptr_t, std::uintptr_t> b) {
  return a.first < b.second && b.first < a.second;
}
WriteViews write_views(const Schema& schema, const Shape& shape, std::span<const WritePart> target) {
  qualify_shape(schema, shape);
  require(target.size() == shape.part_count);
  std::array<std::pair<std::uintptr_t, std::uintptr_t>, field_count> ranges{};
  for (unsigned field = 0; field < target.size(); ++field) {
    require(target[field].id == shape.parts[field].id &&
            target[field].bytes.size() == shape.parts[field].body_bytes);
    ranges[field] = interval(target[field].bytes.data(), target[field].bytes.size());
    require(target[field].bytes.empty() || ranges[field].first % shape.parts[field].alignment == 0);
    for (unsigned earlier = 0; earlier < field; ++earlier)
      require(!overlap(ranges[earlier], ranges[field]));
  }
  if (shape.format == Format::rows)
    return take(tp::view::bind(schema.tuple,
        {reinterpret_cast<tp::byte*>(target[0].bytes.data()), target[0].bytes.size()},
        shape.records, tuple_bytes));
  return WriteColumns{column_view<64, std::uint8_t>(shape.records, target[0].bytes),
                      column_view<64, std::uint8_t>(shape.records, target[1].bytes),
                      column_view<20, std::uint8_t>(shape.records, target[2].bytes),
                      column_view<32, std::uint8_t>(shape.records, target[3].bytes)};
}

template<class View> void account(const ikea::source_write_journal& effects, const View& view,
                                  std::size_t extent, KernelCosts& costs) {
  costs.issued_spans += effects.used;
  for (const auto& effect : effects.entries()) {
    // Resolve every borrowed source identity before the named view expires.
    require(effect.source == &view && effect.bytes.plane == 0 &&
            effect.bytes.offset <= extent && effect.bytes.size <= extent - effect.bytes.offset);
    costs.issued_bytes += effect.bytes.size;
  }
}
std::uint64_t field_value(const Row& row, unsigned field) {
  switch (field) {
  case 0: return row.key;
  case 1: return row.record.next;
  case 2: return row.record.value;
  case 3: return row.record.tag;
  default: throw Refusal{orbital::Error::invalid_argument};
  }
}
void construction_input(tp::construction_input& input, const Row& row) {
  for (unsigned field = 0; field < field_count; ++field) {
    auto value = field_value(row, field);
    for (unsigned slot = 0; slot < map_counts[field]; ++slot) {
      input[maps[field][slot]] = tp::byte(value);
      value >>= 8;
    }
  }
}
template<class Source>
void construct_rows(const Schema& schema, const tp::view& view, Source&& source,
                    KernelCosts& costs, ConstructionOptions options) {
  require(options.fail_after_parts != 0, orbital::Error::rejected);
  const auto operation = take(tp::bind_constructor(schema.constructor, view));
  ++costs.bindings;
  tp::construction_input input{}; // Unused slots cleared once, never per row.
  std::array<ikea::owner_write, 1> journal;
  costs.scratch_bound_bytes = std::max(costs.scratch_bound_bytes, sizeof(input) + sizeof(journal));
  for (std::size_t row = 0; row < view.size(); ++row) {
    require(row < options.fail_after_rows, orbital::Error::rejected);
    construction_input(input, source(row));
    ikea::source_write_journal effects{journal};
    take(operation.initialize(row, std::span<const tp::construction_input>{&input, 1}, effects));
    ++costs.constructor_calls;
    account(effects, view, view.storage().size(), costs);
    costs.encoded_values += field_count;
    ++costs.completed_rows;
  }
  ++costs.completed_parts;
}
template<unsigned K, class Source>
void construct_column(const sp::view<Column<K>, std::uint8_t>& full, unsigned field,
                      Source&& source, KernelCosts& costs, ConstructionOptions options) {
  std::array<std::uint64_t, conversion_chunk> values;
  // Local20 admission counts whole-byte/residual fields for every tile even
  // when the physical constructor later emits one contiguous body span.
  std::array<ikea::owner_write, conversion_chunk / 8 * 2> journal;
  costs.scratch_bound_bytes = std::max(costs.scratch_bound_bytes, sizeof(values) + sizeof(journal));
  const auto permitted = std::min(full.size(), options.fail_after_rows);
  for (std::size_t first = 0; first < permitted;) {
    const auto count = std::min(conversion_chunk, permitted - first);
    source(field, first, std::span{values}.first(count));
    // Every nonfinal chunk has 128 rows. Its tile offset is a multiple of 64B
    // for each selected Local width. Exact prefix injections may initialize
    // slack in their last private tile; those targets must be discarded.
    const auto offset = multiply(first / Column<K>::tile_rows, Column<K>::tile_bytes);
    const auto extent = multiply(count / Column<K>::tile_rows + (count % Column<K>::tile_rows != 0),
                                 Column<K>::tile_bytes);
    const auto bytes = full.stream(0).bytes.subspan(offset, extent);
    const auto view = take(sp::view<Column<K>, std::uint8_t>::attach(count, {{{bytes, Column<K>::tile_bytes}, {}, {}}}));
    const auto prepared = take(sp::bind_mutation(view));
    require(take(prepared.construction_effect_capacity()) <= journal.size());
    ++costs.bindings;
    ikea::source_write_journal effects{journal};
    take(prepared.initialize(std::span<const std::uint64_t>{values}.first(count), effects));
    ++costs.constructor_calls;
    account(effects, view, extent, costs);
    costs.encoded_values += count;
    if (field == field_count - 1) costs.completed_rows += count;
    first += count;
  }
  require(permitted == full.size(), orbital::Error::rejected);
  ++costs.completed_parts;
}
template<class Source>
void construct_columns(const WriteColumns& views, Source&& source, KernelCosts& costs,
                       ConstructionOptions options) {
  // Injection limits are local to this call: completed physical Parts, and an
  // exact logical-row prefix per Part. Counters may otherwise accumulate.
  require(options.fail_after_parts > 0, orbital::Error::rejected);
  construct_column(views.key, 0, source, costs, options);
  require(options.fail_after_parts > 1, orbital::Error::rejected);
  construct_column(views.next, 1, source, costs, options);
  require(options.fail_after_parts > 2, orbital::Error::rejected);
  construct_column(views.value, 2, source, costs, options);
  require(options.fail_after_parts > 3, orbital::Error::rejected);
  construct_column(views.tag, 3, source, costs, options);
}

using TupleRead = native_detail::TupleRead;
std::array<TupleRead, field_count> checked_tuple_reads(const Schema& schema, const tp::const_view& view) {
  return {take(tp::bind_reader(schema.readers[0], view)),
          take(tp::bind_reader(schema.readers[1], view)),
          take(tp::bind_reader(schema.readers[2], view)),
          take(tp::bind_reader(schema.readers[3], view))};
}
} // namespace

Schema::Schema() : tuple(make_tuple()), constructor(take(tp::constructor::make(tuple))),
                   readers(make_readers(tuple)), writers(make_writers(tuple)) {}

orbital::Result<Shape> shape(const Schema& schema, Format format, std::size_t records) {
  try {
    Shape result;
    result.format = format; result.records = records; result.part_count = parts(format);
    const auto encoded_parent = parent(format, records);
    result.interpretation.assign(encoded_parent.begin(), encoded_parent.end());
    for (unsigned field = 0; field < result.part_count; ++field) {
      auto& part = result.parts[field];
      part.id = part_id(format, field);
      if (format == Format::rows) {
        part.body_bytes = multiply(records, tuple_bytes);
        part.interpretation.resize(schema.tuple.encoded_size());
        take(schema.tuple.encode({reinterpret_cast<tp::byte*>(part.interpretation.data()),
                                 part.interpretation.size()}));
      } else {
        part.body_bytes = column_bytes(field, records); part.alignment = 64;
        const auto descriptor = take(sp::encode_descriptor(column_description(field, records)));
        part.interpretation.resize(descriptor.size());
        std::memcpy(part.interpretation.data(), descriptor.data(), descriptor.size());
      }
    }
    return result;
  } catch (Refusal error) { return std::unexpected(error.error); }
    catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
orbital::Result<Shape> decode_shape(const Schema& schema, orbital::Bytes interpretation) {
  try {
    const auto parsed = parse_parent(interpretation);
    return shape(schema, parsed.format, parsed.records);
  } catch (Refusal error) { return std::unexpected(error.error); }
}

ReadData::ReadData(const Schema& schema, std::size_t records, TupleCursor cursor)
    : schema_(&schema), records_(records), cursors_(cursor) {}
ReadData::ReadData(const Schema& schema, std::size_t records, ColumnCursors cursors)
    : schema_(&schema), records_(records), cursors_(cursors) {}
orbital::Result<ReadData> ReadData::bind(const Schema& schema, const Shape& shape,
                                      std::span<const ReadPart> source) {
  try {
    qualify_shape(schema, shape);
    require(source.size() == shape.part_count);
    for (unsigned field = 0; field < source.size(); ++field) {
      require(source[field].id == shape.parts[field].id &&
              source[field].bytes.size() == shape.parts[field].body_bytes &&
              equal(source[field].interpretation, shape.parts[field].interpretation));
    }
    return bind(schema, orbital::Bytes{shape.interpretation}, source);
  } catch (Refusal error) { return std::unexpected(error.error); }
}
orbital::Result<ReadData> ReadData::bind(const Schema& schema, orbital::Bytes interpretation,
                                      std::span<const ReadPart> source) {
  try {
    const auto parsed = parse_parent(interpretation);
    require(source.size() == parsed.part_count);
    for (unsigned field = 0; field < source.size(); ++field) {
      const auto& part = source[field];
      require(part.id == part_id(parsed.format, field));
      (void)interval(part.bytes.data(), part.bytes.size());
      if (parsed.format == Format::rows) {
        require(part.bytes.size() == multiply(parsed.records, tuple_bytes));
        const auto layout = take(tp::layout::decode({
            reinterpret_cast<const tp::byte*>(part.interpretation.data()), part.interpretation.size()}));
        require(layout == schema.tuple);
      } else {
        require(part.bytes.size() == column_bytes(field, parsed.records));
        const auto descriptor = take(sp::decode_descriptor({
            reinterpret_cast<const std::uint8_t*>(part.interpretation.data()), part.interpretation.size()}));
        require(descriptor == column_description(field, parsed.records));
      }
    }
    if (parsed.format == Format::rows) {
      const auto view = take(tp::const_view::bind(schema.tuple,
          {reinterpret_cast<const tp::byte*>(source[0].bytes.data()), source[0].bytes.size()},
          parsed.records, tuple_bytes));
      const auto operations = checked_tuple_reads(schema, view);
      take(tp::native_reader(operations[0]).admit_transfer<8>());
      take(tp::native_reader(operations[1]).admit_transfer<8>());
      take(tp::native_reader(operations[2]).admit_transfer<3>());
      take(tp::native_reader(operations[3]).admit_transfer<4>());
      return ReadData{schema, parsed.records, TupleCursor{view.storage().data()}};
    }
    // Preserve full placement admission, then keep only typed body pointers.
    // The shared count determines every exact tight headless Local extent.
    (void)column_view<64, const std::uint8_t>(parsed.records, source[0].bytes);
    (void)column_view<64, const std::uint8_t>(parsed.records, source[1].bytes);
    (void)column_view<20, const std::uint8_t>(parsed.records, source[2].bytes);
    (void)column_view<32, const std::uint8_t>(parsed.records, source[3].bytes);
    return ReadData{schema, parsed.records, ColumnCursors{
        ColumnCursor<64>{reinterpret_cast<const std::uint8_t*>(source[0].bytes.data())},
        ColumnCursor<64>{reinterpret_cast<const std::uint8_t*>(source[1].bytes.data())},
        ColumnCursor<20>{reinterpret_cast<const std::uint8_t*>(source[2].bytes.data())},
        ColumnCursor<32>{reinterpret_cast<const std::uint8_t*>(source[3].bytes.data())}}};
  } catch (Refusal error) { return std::unexpected(error.error); }
}

orbital::Result<TupleCursor> ReadData::tuple_cursor() const {
  if (const auto* cursor = std::get_if<TupleCursor>(&cursors_)) return *cursor;
  return std::unexpected(orbital::Error::invalid_argument);
}
orbital::Result<ColumnCursors> ReadData::column_cursors() const {
  if (const auto* cursors = std::get_if<ColumnCursors>(&cursors_)) return *cursors;
  return std::unexpected(orbital::Error::invalid_argument);
}
orbital::Result<TupleCursor> qualify_tuple(const TupleControl& control,
    orbital::Bytes interpretation, std::span<const ReadPart> parts) {
  const auto data = ReadData::bind(control.schema, interpretation, parts);
  if (!data) return std::unexpected(data.error());
  if (data->size() != control.records) return std::unexpected(orbital::Error::invalid_argument);
  return data->tuple_cursor();
}
orbital::Result<ColumnCursors> qualify_columns(const Schema& schema, const ColumnControl& control,
    orbital::Bytes interpretation, std::span<const ReadPart> parts) {
  const auto data = ReadData::bind(schema, interpretation, parts);
  if (!data) return std::unexpected(data.error());
  if (data->size() != control.records) return std::unexpected(orbital::Error::invalid_argument);
  return data->column_cursors();
}
std::uint64_t ReadData::field_unchecked(Field field, std::size_t row) const {
  if (const auto* cursor = std::get_if<TupleCursor>(&cursors_))
    return cursor->open({*schema_, records_}).field_unchecked(field, row);
  return std::get<ColumnCursors>(cursors_).open({records_}).field_unchecked(field, row);
}
Row ReadData::row_unchecked(std::size_t row) const {
  if (const auto* cursor = std::get_if<TupleCursor>(&cursors_))
    return cursor->open({*schema_, records_}).row_unchecked(row);
  return std::get<ColumnCursors>(cursors_).open({records_}).row_unchecked(row);
}
orbital::Result<Row> ReadData::row(std::size_t row) const {
  if (row >= records_) return std::unexpected(orbital::Error::invalid_argument);
  return row_unchecked(row);
}
orbital::Result<void> ReadData::field(Field field, std::size_t first,
                                    std::span<std::uint64_t> output) const {
  if (const auto* cursor = std::get_if<TupleCursor>(&cursors_))
    return cursor->open({*schema_, records_}).field(field, first, output);
  return std::get<ColumnCursors>(cursors_).open({records_}).field(field, first, output);
}
std::size_t ReadData::lower_bound(Key key) const {
  if (const auto* cursor = std::get_if<TupleCursor>(&cursors_))
    return cursor->open({*schema_, records_}).lower_bound(key);
  return std::get<ColumnCursors>(cursors_).open({records_}).lower_bound(key);
}
std::optional<Record> ReadData::get(Key key) const {
  if (const auto* cursor = std::get_if<TupleCursor>(&cursors_))
    return cursor->open({*schema_, records_}).get(key);
  return std::get<ColumnCursors>(cursors_).open({records_}).get(key);
}
std::size_t ReadData::scan(Key key, std::span<Row> output) const {
  if (const auto* cursor = std::get_if<TupleCursor>(&cursors_))
    return cursor->open({*schema_, records_}).scan(key, output);
  return std::get<ColumnCursors>(cursors_).open({records_}).scan(key, output);
}

orbital::Result<void> construct_into(const Schema& schema, const Shape& shape,
    std::span<const WritePart> target, std::span<const Row> input, KernelCosts& costs,
    ConstructionOptions options) {
  try {
    require(input.size() == shape.records);
    for (const auto& row : input) require(row.record.value <= value_mask);
    auto views = write_views(schema, shape, target); // Qualify every Part before the first store.
    const auto input_range = interval(input.data(), multiply(input.size(), sizeof(Row)));
    for (const auto& part : target) require(!overlap(input_range, interval(part.bytes.data(), part.bytes.size())));
    if (const auto* rows = std::get_if<tp::view>(&views))
      construct_rows(schema, *rows, [&](std::size_t row) { return input[row]; }, costs, options);
    else {
      auto source = [&](unsigned field, std::size_t first, std::span<std::uint64_t> output) {
        for (std::size_t i = 0; i < output.size(); ++i) output[i] = field_value(input[first + i], field);
      };
      construct_columns(std::get<WriteColumns>(views), source, costs, options);
    }
    return {};
  } catch (Refusal error) { return std::unexpected(error.error); }
    catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
orbital::Result<void> convert_into(const Schema& schema, const ReadData& source, const Shape& shape,
    std::span<const WritePart> target, KernelCosts& costs, ConstructionOptions options) {
  try {
    require(source.size() == shape.records);
    // The bound source's fixed physical widths prove every decoded value fits
    // the destination law. Builder authority supplies immutable/disjoint input.
    auto views = write_views(schema, shape, target);
    if (const auto* rows = std::get_if<tp::view>(&views)) {
      require(options.fail_after_parts != 0, orbital::Error::rejected);
      const auto operation = take(tp::bind_constructor(schema.constructor, *rows));
      ++costs.bindings;
      std::array<std::array<std::uint64_t, conversion_chunk>, field_count> fields;
      tp::construction_input input{};
      std::array<ikea::owner_write, 1> journal;
      costs.scratch_bound_bytes = std::max(costs.scratch_bound_bytes,
          sizeof(fields) + sizeof(input) + sizeof(journal));
      const auto permitted = std::min(source.size(), options.fail_after_rows);
      for (std::size_t first = 0; first < permitted;) {
        const auto count = std::min(conversion_chunk, permitted - first);
        for (unsigned field = 0; field < field_count; ++field) {
          take(source.field(static_cast<Field>(field), first, std::span{fields[field]}.first(count)));
          ++costs.bindings; costs.decoded_values += count;
        }
        for (std::size_t i = 0; i < count; ++i) {
          const Row row{fields[0][i], {fields[1][i], std::uint32_t(fields[2][i]), std::uint32_t(fields[3][i])}};
          construction_input(input, row);
          ikea::source_write_journal effects{journal};
          take(operation.initialize(first + i, std::span<const tp::construction_input>{&input, 1}, effects));
          ++costs.constructor_calls;
          account(effects, *rows, rows->storage().size(), costs);
          costs.encoded_values += field_count; ++costs.completed_rows;
        }
        first += count;
      }
      require(permitted == source.size(), orbital::Error::rejected);
      ++costs.completed_parts;
    } else {
      auto decode = [&](unsigned field, std::size_t first, std::span<std::uint64_t> output) {
        take(source.field(static_cast<Field>(field), first, output));
        ++costs.bindings; costs.decoded_values += output.size();
      };
      construct_columns(std::get<WriteColumns>(views), decode, costs, options);
    }
    return {};
  } catch (Refusal error) { return std::unexpected(error.error); }
    catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
orbital::Result<void> update_into(const Schema& schema, const Shape& shape,
    std::span<const WritePart> target, std::size_t row, Record value, KernelCosts& costs) {
  try {
    require(row < shape.records && value.value <= value_mask);
    auto views = write_views(schema, shape, target);
    std::array<ikea::owner_write, 4> journal;
    costs.scratch_bound_bytes = std::max(costs.scratch_bound_bytes, sizeof(journal));
    ikea::source_write_journal effects{journal};
    if (const auto* tuples = std::get_if<tp::view>(&views)) {
      const auto next = take(tp::bind_writer(schema.writers[1], *tuples));
      const auto encoded_value = take(tp::bind_writer(schema.writers[2], *tuples));
      const auto tag = take(tp::bind_writer(schema.writers[3], *tuples));
      const tp::native_writer next_writer{next}, value_writer{encoded_value}, tag_writer{tag};
      // All value/range/capacity admission precedes the first preserving store.
      require(next_writer.accepts(value.next) && value_writer.accepts(value.value) && tag_writer.accepts(value.tag));
      require(next.effect_capacity() + encoded_value.effect_capacity() + tag.effect_capacity() <= journal.size());
      costs.bindings += 3;
      take(next_writer.set(row, value.next, effects));
      take(value_writer.set(row, std::uint64_t(value.value), effects));
      take(tag_writer.set(row, std::uint64_t(value.tag), effects));
      account(effects, *tuples, tuples->storage().size(), costs);
    } else {
      const auto& columns = std::get<WriteColumns>(views);
      const auto next = take(sp::bind_mutation(columns.next));
      const auto encoded_value = take(sp::bind_mutation(columns.value));
      const auto tag = take(sp::bind_mutation(columns.tag));
      const auto needed = take(next.effect_capacity(row, 1, sp::row_selection::all())) +
          take(encoded_value.effect_capacity(row, 1, sp::row_selection::all())) +
          take(tag.effect_capacity(row, 1, sp::row_selection::all()));
      require(needed <= journal.size());
      costs.bindings += 3;
      sp::no_summary summary;
      // Resolve each view's identities separately; mutation keeps its neighbors.
      take(next.set(row, value.next, summary, effects));
      account(effects, columns.next, columns.next.stream(0).bytes.size(), costs); effects.used = 0;
      take(encoded_value.set(row, std::uint64_t(value.value), summary, effects));
      account(effects, columns.value, columns.value.stream(0).bytes.size(), costs); effects.used = 0;
      take(tag.set(row, std::uint64_t(value.tag), summary, effects));
      account(effects, columns.tag, columns.tag.stream(0).bytes.size(), costs);
    }
    costs.encoded_values += 3;
    return {};
  } catch (Refusal error) { return std::unexpected(error.error); }
    catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
} // namespace orbital_bench::versioned
