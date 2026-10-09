#include "consumer.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace orbital_bench::versioned {
namespace {
struct Refusal { orbital::Error error; };
void require(bool valid, orbital::Error error = orbital::Error::invalid_argument) {
  if (!valid) throw Refusal{error};
}
template<class T> T take(orbital::Result<T> value) {
  if (!value) throw Refusal{value.error()};
  return std::move(*value);
}
void take(orbital::Result<void> value) {
  if (!value) throw Refusal{value.error()};
}
std::array<std::byte, 8> logical_metadata(std::size_t records) {
  std::array<std::byte, 8> value;
  for (unsigned i = 0; i < value.size(); ++i)
    value[i] = std::byte((std::uint64_t(records) >> (i * 8)) & 255);
  return value;
}
void qualify(const obj::Description& description, std::size_t records) {
  require(description.format == data_program);
  const auto logical = logical_metadata(records);
  require(description.logical_metadata.size() == logical.size() &&
      std::equal(logical.begin(), logical.end(), description.logical_metadata.begin()));
}
template<class Access>
orbital::Result<ReadData> bind_access(const Schema& schema, const Access& access) {
  try {
    require(bool(access));
    const auto& description = access.description();
    const auto count = access.part_count();
    require(description.format == data_program);
    require(count > 0 && count <= field_count);
    std::array<ReadPart, field_count> parts;
    for (std::size_t i = 0; i < count; ++i) {
      const auto view = take(access.part(i));
      parts[i] = {view.id(), view.body(), view.interpretation()};
    }
    auto data = ReadData::bind(schema, description.interpretation, std::span{parts}.first(count));
    if (!data) return data;
    qualify(description, data->size());
    return data;
  } catch (Refusal error) { return std::unexpected(error.error); }
}
obj::Builder build(obj::Store& store, orbital::allocation::Pool& pool,
    const obj::RepresentationSpec& specification, const Shape& shape, Costs& costs) {
  std::array<obj::PartPlan, field_count> plans;
  for (unsigned i = 0; i < shape.part_count; ++i) {
    const auto& part = shape.parts[i];
    plans[i] = {part.id, {.bytes = part.body_bytes, .alignment = part.alignment,
        .readable_padding = part.readable_padding,
        .initialization = orbital::allocation::Initialization::uninitialized}, part.interpretation};
  }
  auto builder = take(store.build(specification, pool, std::span{plans}.first(shape.part_count)));
  for (unsigned i = 0; i < shape.part_count; ++i) {
    costs.requested_body_bytes += shape.parts[i].body_bytes;
    costs.requested_parts += shape.parts[i].body_bytes != 0;
  }
  return builder;
}
struct Writers {
  std::array<obj::WriteView, field_count> owners;
  std::array<WritePart, field_count> parts;
  Writers(obj::Builder& builder, const Shape& shape) {
    for (unsigned i = 0; i < shape.part_count; ++i) {
      owners[i] = take(builder.write(i));
      parts[i] = {shape.parts[i].id, take(owners[i].bytes())};
    }
  }
};
} // namespace

orbital::Result<ReadData> bind(const Schema& schema, obj::Selection access) {
  return bind_access(schema, access);
}
orbital::Result<ReadData> bind(const Schema& schema, const obj::Representation& access) {
  return bind_access(schema, access);
}
orbital::Result<ReadData> bind(const Schema& schema, obj::View view) {
  try {
    require(bool(view));
    const auto& description = view.description();
    require(description.format == data_program);
    const ReadPart part{view.id(), view.body(), view.interpretation()};
    auto data = ReadData::bind(schema, description.interpretation, std::span<const ReadPart>{&part, 1});
    if (!data) return data;
    require(data->format() == Format::rows);
    qualify(description, data->size());
    return data;
  } catch (Refusal error) { return std::unexpected(error.error); }
}

orbital::Result<TupleCursor> qualify_tuple(const TupleControl& control, obj::Selection selected) {
  const auto data = bind(control.schema, selected);
  if (!data) return std::unexpected(data.error());
  if (data->size() != control.records) return std::unexpected(orbital::Error::invalid_argument);
  return data->tuple_cursor();
}
orbital::Result<TupleCursor> qualify_tuple(const TupleControl& control, obj::View view) {
  const auto data = bind(control.schema, view);
  if (!data) return std::unexpected(data.error());
  if (data->size() != control.records) return std::unexpected(orbital::Error::invalid_argument);
  return data->tuple_cursor();
}
orbital::Result<ColumnCursors> qualify_columns(const Schema& schema, const ColumnControl& control,
                                             obj::Selection selected) {
  const auto data = bind(schema, selected);
  if (!data) return std::unexpected(data.error());
  if (data->size() != control.records) return std::unexpected(orbital::Error::invalid_argument);
  return data->column_cursors();
}

orbital::Result<obj::Representation> construct(const Schema& schema, obj::Store& store,
    orbital::allocation::Pool& pool, orbital::ObjectId id, std::uint64_t version,
    std::span<const Row> input, Format format, Costs& costs, ConstructionOptions options) {
  try {
    for (const auto& row : input) require(row.record.value <= value_mask);
    const auto layout = take(shape(schema, format, input.size()));
    const auto logical = logical_metadata(input.size());
    const obj::RepresentationSpec specification{id, version, {}, data_program,
        logical, layout.interpretation, {}};
    auto builder = build(store, pool, specification, layout, costs);
    {
      Writers writers(builder, layout);
      take(construct_into(schema, layout, std::span{writers.parts}.first(layout.part_count),
          input, costs.kernel, options));
    }
    return builder.freeze();
  } catch (Refusal error) { return std::unexpected(error.error); }
    catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
orbital::Result<obj::Representation> convert(const Schema& schema, obj::Store& store,
    orbital::allocation::Pool& pool, obj::Selection selected, Format format, Costs& costs,
    ConstructionOptions options) {
  try {
    const auto source = take(bind(schema, selected));
    const auto layout = take(shape(schema, format, source.size()));
    const auto& old = selected.description();
    const obj::RepresentationSpec specification{old.object, old.version, old.replica,
        data_program, old.logical_metadata, layout.interpretation, {}};
    auto builder = build(store, pool, specification, layout, costs);
    {
      Writers writers(builder, layout);
      take(convert_into(schema, source, layout, std::span{writers.parts}.first(layout.part_count),
          costs.kernel, options));
    }
    return builder.freeze();
  } catch (Refusal error) { return std::unexpected(error.error); }
    catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
orbital::Result<obj::Representation> update(const Schema& schema, obj::Store& store,
    orbital::allocation::Pool& pool, obj::Selection selected, std::uint64_t version,
    std::span<const Update> input, Costs& costs) {
  try {
    require(!input.empty() && input.size() <= write_batch_limit);
    const auto source = take(bind(schema, selected));
    require(version >= selected.description().version);
    std::array<std::size_t, write_batch_limit> rows;
    for (std::size_t i = 0; i < input.size(); ++i) {
      require(input[i].record.value <= value_mask);
      rows[i] = source.lower_bound(input[i].key);
      require(rows[i] < source.size() && source.row_unchecked(rows[i]).key == input[i].key,
          orbital::Error::absent);
    }
    const auto layout = take(shape(schema, source.format(), source.size()));
    const auto& old = selected.description();
    const obj::RepresentationSpec specification{old.object, version, old.replica,
        data_program, old.logical_metadata, layout.interpretation, {}};
    auto builder = build(store, pool, specification, layout, costs);
    {
      Writers writers(builder, layout);
      for (unsigned i = 0; i < layout.part_count; ++i) {
        const auto original = take(selected.part(std::size_t(i)));
        require(original.body().size() == writers.parts[i].bytes.size());
        if (!original.body().empty()) {
          std::memcpy(writers.parts[i].bytes.data(), original.body().data(), original.body().size());
          costs.copied_bytes += original.body().size();
          ++costs.copied_spans;
        }
      }
      for (std::size_t i = 0; i < input.size(); ++i)
        take(update_into(schema, layout, std::span{writers.parts}.first(layout.part_count),
            rows[i], input[i].record, costs.kernel));
    }
    return builder.freeze();
  } catch (Refusal error) { return std::unexpected(error.error); }
    catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
} // namespace orbital_bench::versioned
