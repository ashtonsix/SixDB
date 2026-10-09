#include "material.h"
#include "../../../../../orbital/src/allocation/internal.h"
#include <cstring>

namespace orbital_mvcc::composed {
namespace {
std::size_t partition(std::size_t total, std::size_t offered) {
  if (total < 1024 * 1024 || (offered && offered >= total))
    throw std::invalid_argument("composed budget must leave body capacity");
  return offered ? offered : std::min<std::size_t>(total / 4, 1024 * 1024);
}
std::size_t shape_heap(const v::Shape& shape) {
  auto result = shape.interpretation.capacity();
  for (const auto& part : shape.parts) result += part.interpretation.capacity();
  return result;
}
template<class T> constexpr bool tracks_extent = requires(const T& value) { value.held_extent; };
template<class Storage> std::optional<std::size_t> held_extent(const Storage& storage) {
  if constexpr (tracks_extent<Storage>) return storage.held_extent;
  else return std::nullopt;
}
// Cold accounting observes the actual baseline or shared-route layout.
// These adapters do not create owners or substitute a guessed metadata charge.
template<class Group> const auto& storages(const Group& group) {
  if constexpr (requires { group.uses.slots; }) return group.uses.slots;
  else return group.uses;
}
template<class Storage> auto identity(const Storage& storage) {
  if constexpr (requires { storage.identity(); }) return storage.identity();
  else return storage.identity;
}
template<class Window> const auto& window_storage(const Window& window) {
  if constexpr (requires { window.uses.slots; }) return window.uses.slots[0];
  else return window.use;
}
template<class Window> std::optional<std::size_t> window_extent(const Window& window) {
  // Storage16 keeps a wide Window extent outside its finite Storage record.
  if constexpr (tracks_extent<Window>) return window.held_extent;
  else return held_extent(window_storage(window));
}
}

Material::Material(const p::Corpus& corpus, std::size_t total, std::size_t metadata_bytes)
    : corpus_(corpus), columns_(p::need(v::shape(corpus.schema, v::Format::columns,
          corpus.images.at(0).rows.size()))), runtime(total - partition(total, metadata_bytes)) {
  memory.limit = partition(total, metadata_bytes);
}
Material::~Material() {
  tuple_read_.reset(); tuple_write_.reset(); columns_write_.reset();
  tuple_.reset(); columns_proof_.reset();
  // The owning Backend destroys its catalog before this resource/runtime.
  // Scopes and pending conversions may not outlive that enclosing Backend.
  if (memory.live) std::terminate();
}
const v::Shape& Material::shape(v::Format format) const {
  if (format == v::Format::rows) return corpus_.images.at(0).shape;
  if (format == v::Format::columns) return columns_;
  throw std::invalid_argument("unknown composed format");
}
const v::Shape& Material::shape(cs::Selection selected) const {
  if (!selected || selected.records() != corpus_.images[0].rows.size())
    throw std::invalid_argument("composed selection/count differs");
  if (tuple_ && selected.shape() == tuple_.get()) return shape(v::Format::rows);
  if (columns_proof_ && selected.shape() == columns_proof_.get()) return columns_;
  throw std::invalid_argument("composed Shape is not this fixture's proven law");
}
Result<PrivateParts> Material::allocate(const v::Shape& shape, v::Costs& costs) {
  PrivateParts result; result.shape = &shape;
  for (unsigned i = 0; i < shape.part_count; ++i) {
    const auto& part = shape.parts[i];
    auto allocation = oa::allocate(*runtime.pool, {.bytes = part.body_bytes,
        .alignment = part.alignment, .readable_padding = part.readable_padding,
        .initialization = oa::Initialization::uninitialized});
    if (!allocation) return std::unexpected(allocation.error());
    result.allocations[i] = std::move(*allocation);
    result.writable[i] = {part.id, oa::bytes(result.allocations[i])};
    costs.requested_body_bytes += part.body_bytes; ++costs.requested_parts;
  }
  return result;
}
Result<PrivateParts> Material::construct(std::span<const orbital_bench::Row> rows,
    v::Format format, v::Costs& costs) {
  auto result = allocate(shape(format), costs);
  if (!result) return std::unexpected(result.error());
  const auto built = v::construct_into(corpus_.schema, *result->shape, result->parts(), rows, costs.kernel);
  if (!built) return std::unexpected(built.error());
  return result;
}
Result<PrivateParts> Material::clone(cs::Selection selected, v::Costs& costs) {
  auto result = allocate(shape(selected), costs);
  if (!result) return std::unexpected(result.error());
  for (unsigned i = 0; i < result->shape->part_count; ++i) {
    auto source = selected.part(result->shape->parts[i].id);
    if (!source) return std::unexpected(source.error());
    auto destination = result->writable[i].bytes;
    std::memcpy(destination.data(), source->address(), destination.size());
    costs.copied_bytes += destination.size(); ++costs.copied_spans;
  }
  return result;
}
Result<PrivateParts> Material::convert(cs::Selection selected, v::Format format,
    v::Costs& costs, v::ConstructionOptions options) {
  const auto source = generic(selected);
  auto result = allocate(shape(format), costs);
  if (!result) return std::unexpected(result.error());
  const auto converted = v::convert_into(corpus_.schema, source, *result->shape,
      result->parts(), costs.kernel, options);
  if (!converted) return std::unexpected(converted.error());
  return result;
}
Result<cs::Shape*> Material::proof(const PrivateParts& parts) {
  auto& owner = parts.shape->format == v::Format::rows ? tuple_ : columns_proof_;
  if (!owner) {
    std::array<compact_descriptor::PartSource, 4> sources;
    for (unsigned i = 0; i < parts.shape->part_count; ++i) {
      const auto& part = parts.shape->parts[i];
      sources[i] = {part.id, parts.writable[i].bytes, part.interpretation,
          part.readable_padding, part.alignment, {}};
    }
    auto proven = cs::prove_fixture(memory, corpus_.schema, v::data_program,
        parts.shape->interpretation, std::span(sources).first(parts.shape->part_count));
    if (!proven) return std::unexpected(proven.error());
    owner.reset(*proven);
    if (parts.shape->format == v::Format::rows) {
      auto admitted = sr::admit_tuple_shape(owner.get(), v::data_program, parts.shape->parts[0].id);
      if (!admitted) { owner.reset(); return std::unexpected(admitted.error()); }
      tuple_read_ = *admitted;
      const auto write_admitted = pw::TupleLaw::prepare(tuple_write_, owner.get(),
          corpus_.schema.tuple, std::span(corpus_.schema.writers).subspan<2, 2>(),
          v::data_program, parts.shape->parts[0].id);
      if (!write_admitted) {
        tuple_read_.reset(); tuple_write_.reset(); owner.reset();
        return std::unexpected(write_admitted.error());
      }
    } else {
      std::array<orbital::PartId, 4> ids;
      for (unsigned i = 0; i < ids.size(); ++i) ids[i] = parts.shape->parts[i].id;
      const auto write_admitted = sw::admit_series_shape(owner.get(), v::data_program, ids);
      if (!write_admitted) { owner.reset(); return std::unexpected(write_admitted.error()); }
      columns_write_ = *write_admitted;
    }
  }
  return owner.get();
}
v::ReadData Material::generic(cs::Selection selected) const {
  const auto& physical = shape(selected);
  std::array<v::ReadPart, 4> parts;
  for (unsigned i = 0; i < physical.part_count; ++i) {
    const auto& declared = physical.parts[i];
    const auto view = p::need(selected.part(declared.id));
    const auto extent = p::need(cs::geometry(selected.shape(), i).extent(selected.records()));
    if (extent != declared.body_bytes) throw std::invalid_argument("composed Part extent differs");
    parts[i] = {declared.id, {view.address(), declared.body_bytes}, declared.interpretation};
  }
  return p::need(v::ReadData::bind(corpus_.schema, physical.interpretation,
      std::span(parts).first(physical.part_count)));
}
Result<void> Material::apply(cs::Selection basis, const PrivateParts& target,
    std::span<const pw::Command> input, v::KernelCosts& costs, wb::ProbeCosts& probes) const {
  if (!basis) return std::unexpected(Error::absent);
  if (!target.shape || target.shape->records != basis.records())
    return std::unexpected(Error::invalid_argument);
  // The private clone is constructed from this protected exact basis. Fresh
  // supplied IDs/extents/aliases and all keys are still admitted by pw::apply.
  if (tuple_write_ && basis.shape() == tuple_write_->shape() &&
      target.shape == &shape(v::Format::rows))
    return pw::apply(tuple_write_->proof(), basis.records(), target.parts(), input, costs, probes);
  if (columns_write_ && basis.shape() == columns_write_->shape() &&
      target.shape == &shape(v::Format::columns))
    return pw::apply(*columns_write_, basis.records(), target.parts(), input, costs, probes);
  return std::unexpected(Error::invalid_argument);
}
orbital_bench::Outcome Material::consume(cs::Selection selected,
    const orbital_bench::Operation& op) const {
  using orbital_bench::Kind;
  if (selected && selected.shape() == tuple_.get() && (op.kind == Kind::read || op.kind == Kind::chain)) {
    if (op.kind == Kind::chain && op.count > 16) throw std::invalid_argument("composed chain exceeds fixture bound");
    orbital_bench::Outcome result;
    p::need(sr::read_proven(*tuple_read_, selected, op.key, op.kind == Kind::chain ? op.count : 1, result));
    return result;
  }
  return p::read_operation(generic(selected), op);
}
Accounting Material::accounting() const {
  Accounting result;
  result.metadata_live = memory.live; result.metadata_peak = memory.peak;
  result.metadata_calls = memory.calls; result.metadata_limit = memory.limit;
  result.shared_shape_bytes = (tuple_ ? cs::shape_bytes(tuple_.get()) : 0) +
      (columns_proof_ ? cs::shape_bytes(columns_proof_.get()) : 0);
  result.write_shape_heap_bytes = shape_heap(shape(v::Format::rows)) + shape_heap(columns_);
  result.schema_bytes = sizeof(v::Schema);
  result.borrowed_write_shape_record_bytes = sizeof(v::Shape);
  result.write_control_live_bytes = (tuple_write_ ? sizeof(pw::TupleLaw) : 0) +
      (columns_write_ ? sizeof(sw::SeriesProof) : 0);
  result.write_control_slots_bytes = sizeof(tuple_write_) + sizeof(columns_write_);
  result.pool_setup_and_inventory_bytes = sizeof(oa::Arena) + sizeof(oa::detail::ArenaState) +
      sizeof(oa::Pool) + sizeof(oa::detail::PoolState) +
      runtime.arena->state->pools.capacity() * sizeof(std::unique_ptr<oa::Pool>) +
      runtime.pool->internal->groups.capacity() * sizeof(std::unique_ptr<oa::detail::Group>) +
      runtime.pool->internal->windows.capacity() * sizeof(std::unique_ptr<oa::detail::Window>);
  result.pool = oa::statistics(*runtime.pool);
  result.arena_charge = runtime.arena->charged_bytes.load();
  if constexpr (tracks_extent<oa::Storage>) result.held_extent_bytes = 0;
  const auto observe = [&](const oa::Storage& storage, std::size_t capacity,
      std::optional<std::size_t> extent) {
    if (storage.users.load(std::memory_order_relaxed)) {
      ++result.active_allocations; result.live_capacity_bytes += capacity;
      if (result.held_extent_bytes) {
        if (extent) *result.held_extent_bytes += *extent;
        else result.held_extent_bytes.reset();
      }
    }
  };
  for (const auto& group : runtime.pool->internal->groups)
    for (const auto& storage : storages(*group)) {
      // An empty retiring Group can already have released its geometry.
      if (!storage.users.load(std::memory_order_relaxed)) continue;
      observe(storage, group->geometry->bound_capacity({identity(storage)}), held_extent(storage));
    }
  for (const auto& window : runtime.pool->internal->windows)
    observe(window_storage(*window), window->prepared, window_extent(*window));
  return result;
}
} // namespace orbital_mvcc::composed
