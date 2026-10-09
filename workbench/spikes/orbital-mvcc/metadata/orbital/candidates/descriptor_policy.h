#pragma once

#include "store.h"
#include "../descriptor/descriptor.h"

// Generic byte-descriptor comparison control. The catalog itself depends only
// on its Physical policy; this header supplies the descriptor-specific aliases.
namespace compact_history {

using compact_descriptor::Descriptor;
using compact_descriptor::Selection;
using compact_descriptor::RepresentationSpec;
using compact_descriptor::PartSource;

struct DescriptorPhysical {
  using Value = compact_descriptor::Descriptor;
  using Selection = compact_descriptor::Selection;
  using Specification = compact_descriptor::RepresentationSpec;
  using PartSource = compact_descriptor::PartSource;
  using Export = compact_descriptor::Export;
  enum class ExportPolicy { selected };
  static void retain(Value* v) noexcept { compact_descriptor::retain(v); }
  static void release(Value* v) noexcept { compact_descriptor::release(v); }
  static Value* get(Selection s) noexcept { return s.descriptor; }
  static Selection selection(Value* v) noexcept { return {v}; }
  static auto capture(std::pmr::memory_resource& memory, const Specification& spec,
      std::span<const PartSource> parts) noexcept { return compact_descriptor::capture(memory, spec, parts); }
  static bool accepts(Value* v, orbital::ObjectId object, std::uint64_t cut) noexcept {
    return compact_descriptor::object(v) == object && compact_descriptor::version(v) == cut;
  }
  // Identity/logical metadata compatibility; the converter proves equal values.
  static bool compatible(Value* a, Value* b) noexcept { return compact_descriptor::same_logical_state(a, b); }
  static auto footprint(Value* v) noexcept { return compact_descriptor::footprint(v); }
  static auto export_part(Selection selected, std::size_t index, Binding, ExportPolicy) noexcept {
    return compact_descriptor::retain_part(selected, index);
  }
};

using DescriptorOwner = PhysicalOwner<DescriptorPhysical>;
using Change = ChangeOf<DescriptorOwner>;
using Store32 = Store<Slab<Version32>, compact_retention::FloorRetention, DescriptorPhysical>;
using Store16 = Store<Slab<Version16>, compact_retention::FloorRetention, DescriptorPhysical>;
using Store8 = Store<Chunks8, compact_retention::FloorRetention, DescriptorPhysical>;
static_assert(sizeof(Store32::Scope) == 32 && sizeof(Store16::Scope) == 32 && sizeof(Store8::Scope) == 32);
static_assert(sizeof(Store32) == 8 && sizeof(Store16) == 8 && sizeof(Store8) == 8);
static_assert(sizeof(Store32::Borrowed) == 16 && sizeof(Store16::Borrowed) == 16);

} // namespace compact_history
