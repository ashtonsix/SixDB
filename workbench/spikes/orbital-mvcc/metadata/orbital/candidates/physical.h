#pragma once

#include <orbital/types.h>
#include <utility>

namespace compact_history {

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

template<class Owner> struct ChangeOf { orbital::ObjectId object; Owner representation; };

} // namespace compact_history
