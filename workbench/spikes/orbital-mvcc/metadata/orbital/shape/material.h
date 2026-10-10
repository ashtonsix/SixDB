#pragma once
#include "shape.h"
#include <orbital/detail/object/representation.h>

// Static material adapter shared by the selected catalog and comparison stores.
// Interpretation stays with Shape; Orbital owns physical retention and escape.
namespace compact_history {
using orbital::ObjectId;

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
      std::span<const PartSource> parts) noexcept {
    return orbital::capture_representation(memory, compact_shape::Control{spec.shape, spec.records}, spec.owners, parts);
  }
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
    return orbital::object_detail::retain_part<compact_shape::Control>(selected, index, {binding.object, binding.version}, policy);
  }
};

} // namespace compact_history
