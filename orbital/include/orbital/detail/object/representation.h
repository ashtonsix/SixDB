#pragma once
#include <orbital/representation.h>
#include <type_traits>

namespace orbital {
template<class Control> struct alignas(alignof(Control) > alignof(void*) ? alignof(Control) : alignof(void*)) Representation {
  static_assert(std::is_trivially_copyable_v<Control>);
  Control control;
  std::uint32_t references{1};
  PartOwners owners;
  std::uint8_t stride{}; // Address-slot spacing, fixed at capture; fits header padding.
};
namespace object_detail {
struct SharedBody { const std::byte* address; std::shared_ptr<const void> owner; };
struct AllocatedBody { const std::byte* address; allocation::Storage* owner; };
union PartOwner {
  std::shared_ptr<const void> shared;
  allocation::Storage* allocation;
  PartOwner() {}
  ~PartOwner() {}
};
struct MixedBody {
  const std::byte* address{};
  bool shared{};
  PartOwner owner;
  MixedBody() { owner.allocation = nullptr; }
};
inline bool shared(PartOwners owners) {
  return owners == PartOwners::independent_shared || owners == PartOwners::grouped_shared;
}
inline bool grouped(PartOwners owners) {
  return owners == PartOwners::grouped_shared || owners == PartOwners::grouped_allocation;
}
inline bool mixed(PartOwners owners) { return owners == PartOwners::independent_mixed; }
inline std::byte* bytes(void* p) { return static_cast<std::byte*>(p); }
inline const std::byte* bytes(const void* p) { return static_cast<const std::byte*>(p); }
template<class Control> inline const std::byte** addresses(Representation<Control>* value) {
  return static_cast<const std::byte**>(static_cast<void*>(bytes(value) + sizeof(*value)));
}
template<class Control> inline SharedBody* shared_bodies(Representation<Control>* value) {
  return static_cast<SharedBody*>(static_cast<void*>(bytes(value) + sizeof(*value)));
}
template<class Control> inline AllocatedBody* allocated_bodies(Representation<Control>* value) {
  return static_cast<AllocatedBody*>(static_cast<void*>(bytes(value) + sizeof(*value)));
}
template<class Control> inline MixedBody* mixed_bodies(Representation<Control>* value) {
  return static_cast<MixedBody*>(static_cast<void*>(bytes(value) + sizeof(*value)));
}
template<class Control> inline void* group_owner(Representation<Control>* value) {
  return bytes(value) + sizeof(*value) + value->control.part_count() * sizeof(const std::byte*);
}
template<class Control> inline const std::byte* const* address_slot(Representation<Control>* value, std::size_t index) {
  return static_cast<const std::byte* const*>(static_cast<const void*>(
      bytes(value) + sizeof(*value) + index * value->stride));
}
static_assert(sizeof(SharedBody) == 24 && sizeof(AllocatedBody) == 16);
static_assert(std::is_standard_layout_v<SharedBody> && std::is_standard_layout_v<AllocatedBody> &&
    std::is_standard_layout_v<MixedBody>);
static_assert(offsetof(SharedBody, address) == 0 && offsetof(AllocatedBody, address) == 0 &&
    offsetof(MixedBody, address) == 0);
static_assert(sizeof(MixedBody) <= UINT8_MAX && alignof(MixedBody) <= alignof(void*) &&
    alignof(SharedBody) <= alignof(void*) && alignof(AllocatedBody) <= alignof(void*));
} // namespace object_detail

template<class Control> struct Part {
  Control control;
  const std::byte* address;
  ObjectBinding binding;
  std::uint32_t references{1}, index;
  PartOwners owners;
  PartRetention retention;
  object_detail::PartOwner owner;
};
namespace object_detail {
// The catalog's sealed BoundSelection supplies exact identity and selection.
// This implementation seam is not a caller-labelled public export operation.
template<class Control> [[nodiscard]] Result<Part<Control>*> retain_part(
    RepresentationSelection<Control>, std::size_t, ObjectBinding, PartRetention) noexcept;
} // namespace object_detail
} // namespace orbital
