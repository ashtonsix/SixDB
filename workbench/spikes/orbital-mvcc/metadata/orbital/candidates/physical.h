#pragma once

#include "../descriptor/descriptor.h"
#include <utility>

namespace compact_history {

struct DescriptorPhysical {
  using Value = compact_descriptor::Descriptor;
  using Selection = compact_descriptor::Selection;
  using Specification = compact_descriptor::RepresentationSpec;
  using PartSource = compact_descriptor::PartSource;
  struct Binding {};
  static void retain(Value* v) noexcept { compact_descriptor::retain(v); }
  static void release(Value* v) noexcept { compact_descriptor::release(v); }
  static Value* get(Selection s) noexcept { return s.descriptor; }
  static Selection selection(Value* v) noexcept { return {v}; }
  static auto capture(std::pmr::memory_resource& memory, const Specification& spec,
      std::span<const PartSource> parts) noexcept { return compact_descriptor::capture(memory, spec, parts); }
  static bool accepts(Value* v, orbital::ObjectId object, std::uint64_t cut) noexcept {
    return compact_descriptor::object(v) == object && compact_descriptor::version(v) == cut;
  }
  static bool equivalent(Value* a, Value* b) noexcept { return compact_descriptor::same_logical_state(a, b); }
  static auto footprint(Value* v) noexcept { return compact_descriptor::footprint(v); }
  static Binding binding(orbital::ObjectId, std::uint64_t) noexcept { return {}; }
  static orbital::ObjectId bound_object(Value* source, Binding) noexcept { return compact_descriptor::object(source); }
  static std::uint64_t bound_cut(Value* source, Binding) noexcept { return compact_descriptor::version(source); }
};

template<class Physical> class PhysicalOwner {
  using Value = typename Physical::Value;
  Value* value_{};
public:
  PhysicalOwner() = default;
  static PhysicalOwner take(Value* value) noexcept { PhysicalOwner r; r.value_ = value; return r; }
  static PhysicalOwner retain(Value* value) noexcept { if (value) Physical::retain(value); return take(value); }
  PhysicalOwner(const PhysicalOwner& other) : PhysicalOwner(retain(other.value_)) {}
  PhysicalOwner(PhysicalOwner&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
  PhysicalOwner& operator=(PhysicalOwner other) noexcept { std::swap(value_, other.value_); return *this; }
  ~PhysicalOwner() { if (value_) Physical::release(value_); }
  Value* get() const noexcept { return value_; }
  typename Physical::Selection selection() const noexcept { return Physical::selection(value_); }
  explicit operator bool() const noexcept { return value_; }
};

using DescriptorOwner = PhysicalOwner<DescriptorPhysical>;
template<class Owner> struct ChangeOf { orbital::ObjectId object; Owner representation; };
using Change = ChangeOf<DescriptorOwner>;

} // namespace compact_history
