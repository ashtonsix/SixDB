#pragma once
#include "representation_ops.h"
#include <algorithm>
#include <cassert>
// Instantiate once beside the concrete interpretation. Control supplies admitted
// geometry, a resource and nonthrowing proof lifetime/copy operations. Its
// selected copy fits selected_bytes() bytes aligned as Part<Control> and preserves
// the selected Part at index zero. No control operation adds a read-side call.
namespace orbital {
namespace object_detail {
template<class Control> std::size_t representation_bytes(Control control, PartOwners owners) {
  if (grouped(owners)) return sizeof(Representation<Control>) + control.part_count() * sizeof(const std::byte*) +
      (control.part_count() ? (shared(owners) ? sizeof(std::shared_ptr<const void>) : sizeof(void*)) : 0);
  return sizeof(Representation<Control>) + control.part_count() * (shared(owners) ? sizeof(SharedBody) : sizeof(AllocatedBody));
}
template<class Control> std::shared_ptr<const void>& shared_owner(Representation<Control>* value, std::size_t index) {
  if (grouped(value->owners)) return *static_cast<std::shared_ptr<const void>*>(group_owner(value));
  return shared_bodies(value)[index].owner;
}
template<class Control> orbital::allocation::Storage* allocation_owner(Representation<Control>* value, std::size_t index) {
  if (grouped(value->owners)) return *static_cast<orbital::allocation::Storage**>(group_owner(value));
  return allocated_bodies(value)[index].owner;
}
} // namespace object_detail
template<class Control> Result<Representation<Control>*> capture_representation(
    std::pmr::memory_resource& memory, Control control, PartOwners owners, std::span<const PartSource> bodies) noexcept {
  using namespace object_detail;
  using Realization = Representation<Control>;
  if (!control.valid(memory) || bodies.size() != control.part_count() || static_cast<unsigned>(owners) > 3)
    return std::unexpected(Error::invalid_argument);
  for (std::size_t i = 0; i < bodies.size(); ++i) {
    const auto& body = bodies[i];
    const auto extent = control.extent(i);
    const auto padding = control.padding(i);
    const auto alignment = control.alignment(i);
    if (body.id != control.id(i) || !extent || *extent != body.body.size() ||
        !valid(body.body, padding) || ((!body.body.empty() || padding) &&
         (!alignment || std::uintptr_t(body.body.data()) % alignment)))
      return std::unexpected(Error::invalid_argument);
    if (shared(owners)) {
      if ((body.body.size() || padding) && !body.backing.use_count()) return std::unexpected(Error::invalid_argument);
      if (grouped(owners) && i &&
          (body.backing.owner_before(bodies[0].backing) || bodies[0].backing.owner_before(body.backing)))
        return std::unexpected(Error::invalid_argument);
    } else {
      if ((body.body.size() || padding) &&
          (!body.allocation || !within(*body.allocation, body.body, padding)))
        return std::unexpected(Error::invalid_argument);
      if (grouped(owners) && i && body.allocation != bodies[0].allocation) return std::unexpected(Error::invalid_argument);
    }
  }
  const auto stride = grouped(owners) ? sizeof(const std::byte*) :
      (shared(owners) ? sizeof(SharedBody) : sizeof(AllocatedBody));
  const auto group_bytes = grouped(owners) && !bodies.empty() ?
      (shared(owners) ? sizeof(std::shared_ptr<const void>) : sizeof(void*)) : 0;
  if (bodies.size() > (SIZE_MAX - sizeof(Realization) - group_bytes) / stride)
    return std::unexpected(Error::capacity);
  const auto size = representation_bytes(control, owners);
  try {
    if (!shared(owners) && !grouped(owners) && !unique_allocations(memory, bodies))
      return std::unexpected(Error::invalid_argument);
    auto* value = std::construct_at(static_cast<Realization*>(memory.allocate(size, alignof(Realization))),
        Realization{control, 1, owners});
    control.retain();
    if (grouped(owners)) {
      for (std::size_t i = 0; i < bodies.size(); ++i) std::construct_at(addresses(value) + i, bodies[i].body.data());
      if (!bodies.empty()) {
        if (shared(owners)) std::construct_at(static_cast<std::shared_ptr<const void>*>(group_owner(value)), bodies[0].backing);
        else {
          auto* owner = static_cast<orbital::allocation::Storage**>(group_owner(value));
          *owner = nullptr;
          auto held = hold_source(bodies[0].allocation);
          if (!held) { release(value); return std::unexpected(held.error()); }
          *owner = *held;
        }
      }
    } else {
      for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (shared(owners)) std::construct_at(shared_bodies(value) + i, SharedBody{bodies[i].body.data(), bodies[i].backing});
        else std::construct_at(allocated_bodies(value) + i, AllocatedBody{bodies[i].body.data(), nullptr});
      }
      if (!shared(owners)) {
        for (std::size_t i = 0; i < bodies.size(); ++i) {
          auto held = hold_source(bodies[i].allocation);
          if (!held) { release(value); return std::unexpected(held.error()); }
          allocated_bodies(value)[i].owner = *held;
        }
      }
    }
    if (!shared(owners)) {
      const auto originals = grouped(owners) ? std::min<std::size_t>(1, bodies.size()) : bodies.size();
      for (std::size_t i = 0; i < originals; ++i)
        if (bodies[i].allocation) orbital::allocation::release(*bodies[i].allocation);
    }
    return value;
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}

template<class Control> void retain(Representation<Control>* value) noexcept { if (value) { assert(value->references != UINT32_MAX); ++value->references; } }
template<class Control> void release(Representation<Control>* value) noexcept {
  if (!value || --value->references) return;
  using namespace object_detail;
  const auto control = value->control;
  auto* memory = control.resource();
  const auto size = representation_bytes(control, value->owners);
  if (grouped(value->owners)) {
    if (control.part_count()) {
      if (shared(value->owners)) std::destroy_at(static_cast<std::shared_ptr<const void>*>(group_owner(value)));
      else drop(*static_cast<orbital::allocation::Storage**>(group_owner(value)));
    }
  } else {
    for (std::size_t i = 0; i < control.part_count(); ++i) {
      if (shared(value->owners)) std::destroy_at(shared_bodies(value) + i);
      else drop(allocated_bodies(value)[i].owner);
    }
  }
  std::destroy_at(value);
  memory->deallocate(value, size, alignof(Representation<Control>));
  control.release();
}
template<class Control> Control RepresentationSelection<Control>::control() const noexcept { return realization->control; }
template<class Control> std::size_t RepresentationSelection<Control>::part_count() const noexcept { return realization ? realization->control.part_count() : 0; }
template<class Control> Result<View> RepresentationSelection<Control>::part(std::size_t index) const noexcept {
  if (!realization || index >= realization->control.part_count()) return std::unexpected(Error::absent);
  return View{object_detail::address_slot(realization, index)};
}
template<class Control> Result<View> RepresentationSelection<Control>::part(PartId id) const noexcept {
  if (!realization) return std::unexpected(Error::absent);
  for (std::size_t i = 0; i < realization->control.part_count(); ++i)
    if (realization->control.id(i) == id) return View{object_detail::address_slot(realization, i)};
  return std::unexpected(Error::absent);
}
template<class Control> Result<Part<Control>*> object_detail::retain_part(
    RepresentationSelection<Control> selection, std::size_t index, ObjectBinding binding, PartRetention interpretation) noexcept {
  using Export = Part<Control>;
  if (interpretation != PartRetention::shared_control && interpretation != PartRetention::selected_copy)
    return std::unexpected(Error::invalid_argument);
  if (!selection || index >= selection.part_count()) return std::unexpected(Error::absent);
  auto* realization = selection.realization;
  const auto source = realization->control;
  auto* memory = source.resource();
  const auto copy_bytes = source.selected_bytes(index);
  if (interpretation == PartRetention::selected_copy && copy_bytes > SIZE_MAX - sizeof(Export))
    return std::unexpected(Error::capacity);
  const auto size = sizeof(Export) + (interpretation == PartRetention::selected_copy ? copy_bytes : 0);
  Export* value = nullptr;
  try {
    value = static_cast<Export*>(memory->allocate(size, alignof(Export)));
    std::construct_at(value);
    value->control = source;
    value->address = *address_slot(realization, index); value->binding = binding;
    value->index = index; value->owners = realization->owners; value->retention = interpretation;
    if (shared(value->owners)) std::construct_at(&value->owner.shared, shared_owner(realization, index));
    else {
      auto held = pin(allocation_owner(realization, index));
      if (!held) {
        std::destroy_at(value);
        memory->deallocate(value, size, alignof(Export));
        return std::unexpected(held.error());
      }
      value->owner.allocation = *held;
    }
    if (interpretation == PartRetention::shared_control) source.retain();
    else {
      value->control = source.copy_selected(bytes(value) + sizeof(Export), index);
      value->index = 0;
    }
    return value;
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}
template<class Control> void retain(Part<Control>* value) noexcept { if (value) { assert(value->references != UINT32_MAX); ++value->references; } }
template<class Control> void release(Part<Control>* value) noexcept {
  if (!value || --value->references) return;
  using namespace object_detail;
  using Export = Part<Control>;
  const auto control = value->control;
  auto* memory = control.resource();
  const auto size = sizeof(Export) + (value->retention == PartRetention::selected_copy ? control.metadata_bytes() : 0);
  const auto copied = value->retention == PartRetention::selected_copy;
  if (shared(value->owners)) std::destroy_at(&value->owner.shared);
  else drop(value->owner.allocation);
  std::destroy_at(value);
  if (copied) control.destroy_selected();
  memory->deallocate(value, size, alignof(Export));
  if (!copied) control.release();
}
template<class Control> View view(const Part<Control>* value) noexcept { return {&value->address}; }
template<class Control> ObjectBinding binding(const Part<Control>* value) noexcept { return value->binding; }
} // namespace orbital
