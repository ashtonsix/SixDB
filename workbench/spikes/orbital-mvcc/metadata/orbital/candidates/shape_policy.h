#pragma once

#include "store.h"
#include "../shape/shape.h"

namespace compact_history {

struct ShapePhysical {
  using Value = compact_shape::Realization;
  using Selection = compact_shape::Selection;
  using Specification = compact_shape::Specification;
  using PartSource = compact_shape::BodySource;
  using Export = compact_shape::Export;
  using ExportPolicy = compact_shape::Interpretation;
  static void retain(Value* value) noexcept { compact_shape::retain(value); }
  static void release(Value* value) noexcept { compact_shape::release(value); }
  static Value* get(Selection selected) noexcept { return selected.realization; }
  static Selection selection(Value* value) noexcept { return {value}; }
  static auto capture(std::pmr::memory_resource& memory, const Specification& spec,
      std::span<const PartSource> parts) noexcept { return compact_shape::capture(memory, spec, parts); }
  // Logical identity/cut belongs to the catalog-issued accepted result. Shape
  // capture establishes physical geometry/ownership, not caller-labelled identity.
  static bool accepts(Value*, ObjectId, std::uint64_t) noexcept { return true; }
  static bool accepts_resource(Value* value, std::pmr::memory_resource& memory) noexcept {
    return compact_shape::resource(value) == &memory;
  }
  // Program/count compatibility does not prove equal logical content.
  static bool compatible(Value* a, Value* b) noexcept { return compact_shape::equivalent(a, b); }
  static auto footprint(Value* value) noexcept { return compact_shape::footprint(value); }
  static auto export_part(Selection selected, std::size_t index, auto binding, ExportPolicy policy) noexcept {
    return compact_shape::detail::retain_part(selected, index, {binding.object, binding.version}, policy);
  }
};

using ShapeStore16 = Store<Slab<Version16>, compact_retention::DistinctRetention, ShapePhysical>;
using ShapeStore32 = Store<Slab<Version32>, compact_retention::DistinctRetention, ShapePhysical>;
using ShapeStore8 = Store<Chunks8, compact_retention::DistinctRetention, ShapePhysical>;
using CompactShapeStore16 = Store<Slab<Version16>, compact_retention::DistinctRetention, ShapePhysical, CompactObjects>;
using CompactShapeStore32 = Store<Slab<Version32>, compact_retention::DistinctRetention, ShapePhysical, CompactObjects>;
using CompactShapeStore8 = Store<Chunks8, compact_retention::DistinctRetention, ShapePhysical, CompactObjects>;
using InlineShapeStore16 = Store<Slab<Version16>, compact_retention::DistinctRetention, ShapePhysical, TipObjects>;

} // namespace compact_history
