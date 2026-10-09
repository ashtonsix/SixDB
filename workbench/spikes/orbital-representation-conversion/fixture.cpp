#include "fixture.h"
#include <ikea/seriespack.h>
#include <ikea/tuplepack.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <numeric>

namespace orbital_conversion_fixture {
namespace {
namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
namespace alloc = orbital::allocation;
constexpr std::size_t padding = 64;
struct Refusal { Error error; };
void require(bool condition, Error error = Error::format) {
  if (!condition) throw Refusal{error};
}
template<class T, class E> T take(std::expected<T, E> value, Error error = Error::format) {
  require(bool(value), error);
  return std::move(*value);
}
template<class E> void take(std::expected<void, E> value, Error error = Error::format) {
  require(bool(value), error);
}
bool supported(unsigned width) {
  switch (width) {
    case 1: case 7: case 8: case 9: case 12: case 20: case 31: case 32: case 63: case 64:
      return true;
    default: return false;
  }
}
void qualify_schema(std::span<const unsigned> widths) {
  require(!widths.empty() && widths.size() <= max_fields, Error::schema);
  for (auto width : widths) require(supported(width), Error::schema);
}
template<class Function> void dispatch(unsigned width, Function&& function) {
  switch (width) {
#define WIDTH(K) case K: function.template operator()<K>(); break
    WIDTH(1); WIDTH(7); WIDTH(8); WIDTH(9); WIDTH(12);
    WIDTH(20); WIDTH(31); WIDTH(32); WIDTH(63); WIDTH(64);
#undef WIDTH
    default: throw Refusal{Error::schema};
  }
}
template<unsigned K>
using ColumnFormat = sp::format<K, sp::geometry::local, (K >= 20 ? 16 : K >= 9 ? 8 : 0)>;
std::size_t multiply(std::size_t a, std::size_t b) {
  require(!b || a <= SIZE_MAX / b, Error::schema);
  return a * b;
}
std::size_t add(std::size_t a, std::size_t b) {
  require(a <= SIZE_MAX - b, Error::schema);
  return a + b;
}
std::size_t align64(std::size_t n) { return add(n, 63) & ~std::size_t{63}; }
void append_u64(std::vector<std::byte>& out, std::uint64_t n) {
  for (unsigned i = 0; i < 8; ++i) out.push_back(std::byte((n >> (i * 8)) & 255));
}
std::uint64_t u64(orbital::Bytes input, std::size_t at) {
  require(at <= input.size() && input.size() - at >= 8);
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value |= std::uint64_t(std::to_integer<unsigned>(input[at + i])) << (i * 8);
  return value;
}
struct Planes {
  std::array<std::size_t, 3> offsets{}, extents{}, strides{}, used{};
  std::size_t body{};
};
template<class F> Planes placement(std::size_t records) {
  Planes result;
  result.used = {F::tile_bytes, F::heads >= 8 ? F::tile_rows : 0,
                 F::heads == 16 ? F::tile_rows : 0};
  const auto tiles = records / F::tile_rows + (records % F::tile_rows != 0);
  std::size_t cursor = 64;
  for (unsigned p = 0; p < 3; ++p) {
    if (!result.used[p]) continue;
    result.strides[p] = (result.used[p] + 8 + 15) & ~std::size_t{15};
    if (!tiles) continue;
    result.offsets[p] = align64(cursor);
    result.extents[p] = add(multiply(tiles - 1, result.strides[p]), result.used[p]);
    cursor = add(add(result.offsets[p], result.extents[p]), 13);
  }
  result.body = tiles ? add(align64(cursor), 32) : 0;
  return result;
}
struct Fragment { unsigned field, shift, rank; };
struct RowRecipe {
  tp::layout layout;
  std::vector<Fragment> fragments;
};
RowRecipe row_recipe(std::span<const unsigned> widths) {
  std::vector<tp::code> codes;
  std::vector<Fragment> fragments;
  unsigned position = 0;
  for (unsigned field = 0; field < widths.size(); ++field) {
    unsigned shift = 0;
    while (shift < widths[field]) {
      const auto n = std::min(widths[field] - shift, 8 - position % 8);
      fragments.push_back({field, shift, unsigned(codes.size())});
      codes.push_back({tp::byte(position / 8), tp::byte(position % 8), tp::byte(n)});
      position += n;
      shift += n;
    }
  }
  return {take(tp::layout::make((position + 7) / 8, codes)), std::move(fragments)};
}
void account(const ikea::source_write_journal& effects, const void* source,
             std::span<const std::size_t> extents, Costs& costs) {
  costs.issued_spans += effects.used;
  for (const auto& effect : effects.entries()) {
    require(effect.source == source && effect.bytes.plane < extents.size());
    const auto extent = extents[effect.bytes.plane];
    require(effect.bytes.offset <= extent && effect.bytes.size <= extent - effect.bytes.offset);
    costs.issued_bytes += effect.bytes.size;
  }
}
} // namespace

struct Representation::Impl {
  LogicalIdentity logical;
  Kind kind;
  std::size_t records;
  Schema schema;
  std::vector<Part> parts;
  std::vector<std::vector<std::byte>> interpretation;
  std::vector<alloc::Allocation> private_backing;
  std::vector<alloc::Hold> immutable_backing;
};

Representation::Representation(std::unique_ptr<Impl> value) : impl_(std::move(value)) {}
Representation::~Representation() = default;
Description Representation::describe() const noexcept {
  return {impl_->logical, impl_->kind, impl_->records, impl_->schema.widths, impl_->parts};
}

namespace {
void allocate_part(Representation::Impl& target, alloc::Pool& pool, std::size_t body,
                   Costs& costs, BuildOptions options) {
  if (!body) { target.private_backing.emplace_back(); return; }
  require(costs.allocations < options.fail_after_allocations, Error::injected);
  auto allocation = take(alloc::allocate(pool,
      {.bytes = body, .alignment = 64, .readable_padding = padding,
       .initialization = alloc::Initialization::uninitialized}), Error::allocation);
  ++costs.allocations;
  costs.allocated_body_bytes += body;
  costs.padding_bytes += allocation.readable_padding;
  // Diagnostic sentinel for untouched prefix/stride gaps, not encoded initialization.
  std::memset(allocation.address, 0xa5, body);
  costs.guard_initialization_bytes += body;
  target.private_backing.push_back(std::move(allocation));
}
template<unsigned K>
void construct_column(Representation::Impl& target, alloc::Pool& pool, unsigned field,
                      std::span<const Record> records, Costs& costs, BuildOptions options) {
  using F = ColumnFormat<K>;
  const auto geometry = placement<F>(records.size());
  allocate_part(target, pool, geometry.body, costs, options);
  const auto& allocation = target.private_backing.back();
  std::array<sp::plane<std::uint8_t>, 3> planes{};
  for (unsigned p = 0; p < 3; ++p) {
    planes[p].stride = geometry.strides[p];
    if (geometry.extents[p]) planes[p].bytes = {
        reinterpret_cast<std::uint8_t*>(allocation.address) + geometry.offsets[p], geometry.extents[p]};
  }
  // Views and their bound operation stay named on this uninterrupted stack.
  const auto view = take(sp::view<F, std::uint8_t>::attach(records.size(), planes));
  const auto prepared = take(sp::bind_mutation(view));
  ++costs.construction_bindings;
  std::vector<std::uint64_t> input(records.size());
  for (std::size_t row = 0; row < records.size(); ++row) input[row] = records[row][field];
  const auto needed = take(prepared.construction_effect_capacity());
  std::vector<ikea::owner_write> journal(needed);
  costs.scratch_bytes = std::max(costs.scratch_bytes,
      std::uint64_t(multiply(input.size(), sizeof(input[0])) + multiply(journal.size(), sizeof(journal[0]))));
  ikea::source_write_journal effects{journal};
  take(prepared.initialize(std::span<const std::uint64_t>{input}, effects));
  ++costs.construction_calls;
  account(effects, &view, geometry.extents, costs);
  auto descriptor = take(sp::encode_descriptor(sp::describe_storage(view)));
  std::vector<std::byte> interpretation;
  interpretation.reserve(88);
  for (auto byte : descriptor) interpretation.push_back(std::byte{byte});
  for (unsigned p = 0; p < 3; ++p) {
    append_u64(interpretation, geometry.offsets[p]);
    append_u64(interpretation, geometry.extents[p]);
  }
  target.interpretation.push_back(std::move(interpretation));
  target.parts.push_back({{0x434f4c, field}, Codec::seriespack, records.size(), K,
      alloc::bytes(allocation), allocation.readable_padding, 64, target.interpretation.back()});
  costs.target_values += records.size();
  ++costs.completed_units;
}
void construct_rows(Representation::Impl& target, alloc::Pool& pool,
                    std::span<const Record> records, Costs& costs, BuildOptions options) {
  const auto recipe = row_recipe(target.schema.widths);
  const auto body = multiply(records.size(), recipe.layout.bytes());
  allocate_part(target, pool, body, costs, options);
  const auto& allocation = target.private_backing.back();
  std::span<tp::byte> bytes{reinterpret_cast<tp::byte*>(allocation.address), body};
  const auto view = take(tp::view::bind(recipe.layout, bytes, records.size(), recipe.layout.bytes()));
  const auto constructor = take(tp::constructor::make(recipe.layout));
  const auto operation = take(tp::bind_constructor(constructor, view));
  ++costs.construction_bindings;
  std::array<ikea::owner_write, 1> journal;
  const std::array<std::size_t, 1> extents{body};
  for (std::size_t row = 0; row < records.size(); ++row) {
    require(costs.completed_units < options.fail_after_units, Error::injected);
    tp::construction_input input{};
    for (const auto& fragment : recipe.fragments) {
      const auto bits = recipe.layout.codes()[fragment.rank].width;
      input[fragment.rank] = tp::byte((records[row][fragment.field] >> fragment.shift) & ((1u << bits) - 1));
    }
    ikea::source_write_journal effects{journal};
    take(operation.initialize(row, std::span<const tp::construction_input>{&input, 1}, effects));
    ++costs.construction_calls;
    account(effects, &view, extents, costs);
    costs.target_values += target.schema.widths.size();
    ++costs.completed_units;
  }
  costs.scratch_bytes = std::max(costs.scratch_bytes,
      std::uint64_t(sizeof(tp::construction_input) + sizeof(journal) + recipe.fragments.capacity() * sizeof(Fragment)));
  std::vector<tp::byte> encoded(recipe.layout.encoded_size());
  take(recipe.layout.encode(encoded));
  std::vector<std::byte> metadata;
  for (auto byte : encoded) metadata.push_back(std::byte{byte});
  target.interpretation.push_back(std::move(metadata));
  target.parts.push_back({{0x524f57, 0}, Codec::tuplepack, records.size(), 0,
      alloc::bytes(allocation), allocation.readable_padding, 64, target.interpretation.back()});
}
} // namespace

Result construct(alloc::Pool& pool, LogicalIdentity logical, const Schema& schema,
                 std::span<const Record> records, Kind kind, Costs& costs, BuildOptions options) {
  try {
    qualify_schema(schema.widths);
    if (options.fail_after_units != UINT64_MAX)
      options.fail_after_units = add(costs.completed_units, options.fail_after_units);
    if (options.fail_after_allocations != UINT64_MAX)
      options.fail_after_allocations = add(costs.allocations, options.fail_after_allocations);
    for (const auto& record : records)
      for (unsigned field = 0; field < schema.widths.size(); ++field)
        require(schema.widths[field] == 64 || !(record[field] >> schema.widths[field]), Error::value);
    auto target = std::make_unique<Representation::Impl>();
    target->logical = logical; target->schema = schema; target->records = records.size(); target->kind = kind;
    const auto parts = kind == Kind::columns ? schema.widths.size() : 1;
    target->parts.reserve(parts); target->interpretation.reserve(parts);
    target->private_backing.reserve(parts); target->immutable_backing.reserve(parts);
    if (kind == Kind::columns) {
      for (unsigned field = 0; field < schema.widths.size(); ++field) {
        require(costs.completed_units < options.fail_after_units, Error::injected);
        dispatch(schema.widths[field], [&]<unsigned K> {
          construct_column<K>(*target, pool, field, records, costs, options);
        });
      }
    } else construct_rows(*target, pool, records, costs, options);
    require(!options.reject_completion, Error::incomplete);
    // Freeze by retaining the fully initialized backing and removing every mutable owner.
    for (auto& allocation : target->private_backing) {
      if (allocation) target->immutable_backing.push_back(take(alloc::hold(allocation,
          {0, allocation.size + allocation.readable_padding}), Error::allocation));
      else target->immutable_backing.emplace_back();
      alloc::release(allocation);
    }
    target->private_backing.clear();
    costs.descriptor_bytes = sizeof(Representation) + sizeof(Representation::Impl) +
        target->schema.widths.capacity() * sizeof(unsigned) + target->parts.capacity() * sizeof(Part) +
        target->interpretation.capacity() * sizeof(std::vector<std::byte>) +
        target->private_backing.capacity() * sizeof(alloc::Allocation) +
        target->immutable_backing.capacity() * sizeof(alloc::Hold);
    for (const auto& metadata : target->interpretation) costs.descriptor_bytes += metadata.capacity();
    return Completed(new Representation(std::move(target)));
  } catch (Refusal failure) { return std::unexpected(failure.error); }
    catch (const std::bad_alloc&) { return std::unexpected(Error::allocation); }
}

namespace {
template<unsigned K>
void read_column(const Part& part, unsigned field, std::vector<Record>& output, Costs* costs) {
  using F = ColumnFormat<K>;
  require(part.interpretation.size() == 88 && part.width == K && part.codec == Codec::seriespack);
  const auto descriptor = take(sp::decode_descriptor({
      reinterpret_cast<const std::uint8_t*>(part.interpretation.data()), 40}));
  const auto geometry = placement<F>(output.size());
  require(descriptor.width == K && descriptor.heads == F::heads &&
      descriptor.storage == F::storage && descriptor.count == output.size() && part.body.size() == geometry.body);
  std::array<std::span<const std::uint8_t>, 3> planes{};
  for (unsigned p = 0; p < 3; ++p) {
    const auto offset = u64(part.interpretation, 40 + p * 16);
    const auto extent = u64(part.interpretation, 48 + p * 16);
    require(offset == geometry.offsets[p] && extent == geometry.extents[p] && descriptor.strides[p] == geometry.strides[p]);
    require(offset <= part.body.size() && extent <= part.body.size() - offset);
    if (extent) planes[p] = {reinterpret_cast<const std::uint8_t*>(part.body.data()) + offset,
                            static_cast<std::size_t>(extent)};
  }
  const auto view = take(sp::attach_representation<F>(descriptor, planes));
  const auto decoder = sp::bind_decoder<std::uint64_t>(view);
  if (costs) ++costs->decode_bindings;
  std::vector<std::uint64_t> values(output.size());
  take(decoder.read(0, values));
  for (std::size_t row = 0; row < output.size(); ++row) output[row][field] = values[row];
  if (costs) {
    costs->source_values += output.size();
    costs->scratch_bytes = std::max(costs->scratch_bytes, std::uint64_t(values.capacity() * sizeof(values[0])));
  }
}
void read_rows(const Description& source, std::vector<Record>& output, Costs* costs) {
  const auto& part = source.parts[0];
  require(part.codec == Codec::tuplepack && part.width == 0);
  const auto recipe = row_recipe(source.field_widths);
  const auto layout = take(tp::layout::decode({reinterpret_cast<const tp::byte*>(part.interpretation.data()),
                                              part.interpretation.size()}));
  require(layout == recipe.layout && part.body.size() == multiply(source.records, layout.bytes()));
  const auto view = take(tp::const_view::bind(layout,
      {reinterpret_cast<const tp::byte*>(part.body.data()), part.body.size()}, source.records, layout.bytes()));
  std::array<tp::byte, 64> low_map{}, high_map{};
  const auto count = layout.codes().size();
  const auto low_count = std::min(std::size_t{64}, count), high_count = count - low_count;
  std::iota(low_map.begin(), low_map.begin() + low_count, tp::byte{0});
  std::iota(high_map.begin(), high_map.begin() + high_count, tp::byte{64});
  const auto low_plan = take(tp::reader<64>::make(layout, std::span{low_map}.first(low_count)));
  const auto low = take(tp::bind_reader(low_plan, view));
  const auto high_plan = take(tp::reader<64>::make(layout, std::span{high_map}.first(high_count)));
  const auto high = take(tp::bind_reader(high_plan, view));
  if (costs) costs->decode_bindings += 1 + (high_count != 0);
  for (std::size_t row = 0; row < output.size(); ++row) {
    const auto first = take(low.get(row));
    tp::packet<64> second{};
    if (high_count) second = take(high.get(row));
    for (const auto& fragment : recipe.fragments) {
      const auto value = fragment.rank < 64 ? first[fragment.rank] : second[fragment.rank - 64];
      output[row][fragment.field] |= std::uint64_t(value) << fragment.shift;
    }
  }
  if (costs) costs->source_values += multiply(output.size(), source.field_widths.size());
}
} // namespace

std::expected<std::vector<Record>, Error> read_parts(const Description& source, Costs* costs) {
  try {
    qualify_schema(source.field_widths);
    require(source.parts.size() == (source.kind == Kind::columns ? source.field_widths.size() : 1));
    for (unsigned i = 0; i < source.parts.size(); ++i) {
      const auto& part = source.parts[i];
      require(part.records == source.records);
      const auto expected = source.kind == Kind::columns ? orbital::PartId{0x434f4c, i} : orbital::PartId{0x524f57, 0};
      require(part.id == expected);
    }
    std::vector<Record> result(source.records);
    if (source.kind == Kind::columns) {
      for (unsigned field = 0; field < source.field_widths.size(); ++field)
        dispatch(source.field_widths[field], [&]<unsigned K> { read_column<K>(source.parts[field], field, result, costs); });
    } else read_rows(source, result, costs);
    return result;
  } catch (Refusal failure) { return std::unexpected(failure.error); }
    catch (const std::bad_alloc&) { return std::unexpected(Error::allocation); }
}
std::expected<std::vector<Record>, Error> read_records(const Representation& source, Costs* costs) {
  return read_parts(source.describe(), costs);
}
Result convert(alloc::Pool& pool, const Representation& source, Kind kind, Costs& costs,
               BuildOptions options) {
  try {
  const auto description = source.describe();
  auto records = read_records(source, &costs);
  if (!records) return std::unexpected(records.error());
  const auto read_scratch = costs.scratch_bytes;
  costs.scratch_bytes = 0;
  const Schema schema{{description.field_widths.begin(), description.field_widths.end()}};
  auto target = construct(pool, description.logical, schema, *records, kind, costs, options);
  costs.scratch_bytes = records->capacity() * sizeof(Record) + std::max(read_scratch, costs.scratch_bytes);
  return target;
  } catch (const std::bad_alloc&) { return std::unexpected(Error::allocation); }
}
} // namespace orbital_conversion_fixture
