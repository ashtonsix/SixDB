#pragma once
#include <orbital/detail/object/representation_ops.h>
#include <algorithm>
#include <cassert>
#include <vector>
// Instantiate once beside the concrete interpretation. Control supplies admitted
// geometry, a resource and nonthrowing proof lifetime/copy operations. Its
// selected copy fits selected_bytes() bytes aligned as Part<Control> and preserves
// the selected Part at index zero. No control operation adds a read-side call.
// Control is a trivially copyable, non-owning handle; copying it alone retains
// no proof. Its operations are nonthrowing, and admitted geometry stays fixed.
//
// valid(memory) admits only a Control whose resource() is &memory. That resource
// outlives every Representation, Part and proof operation, including cleanup.
// part_count(), id(), extent(), padding() and alignment() describe the admitted
// Parts; extent is body bytes, padding is readable bytes beyond that body.
// retain()/release() preserve the exact proof and its interpretation dependencies.
// Orbital releases physical owners before releasing or destroying that proof.
//
// selected_bytes(index) is the exact metadata byte count for a selected copy.
// copy_selected(destination, index) constructs that copy wholly in the supplied
// tail, which has selected_bytes(index) bytes and alignof(Part<Control>) alignment.
// It retains every needed dependency independently of the source proof, preserves
// the selected Part's interpretation at index zero, and returns a Control with
// the same resource() and metadata_bytes() equal to that selected_bytes(index).
// Those resource and size values must remain stable: Orbital uses them to return
// the entire Part allocation. destroy_selected() destroys the in-place proof and
// its retained dependencies without deallocating the tail; Orbital deallocates it.
// A selected copy does not also receive retain()/release().
namespace orbital {
namespace object_detail {
inline std::size_t body_stride(PartOwners owners) {
  return shared(owners) ? sizeof(SharedBody) : mixed(owners) ? sizeof(MixedBody) : sizeof(AllocatedBody);
}
template<class Control> std::size_t representation_bytes(Control control, PartOwners owners) {
  if (grouped(owners)) return sizeof(Representation<Control>) + control.part_count() * sizeof(const std::byte*) +
      (control.part_count() ? (shared(owners) ? sizeof(std::shared_ptr<const void>) : sizeof(void*)) : 0);
  return sizeof(Representation<Control>) + control.part_count() * body_stride(owners);
}
template<class Control> bool part_shared(Representation<Control>* value, std::size_t index) {
  return mixed(value->owners) ? mixed_bodies(value)[index].shared : shared(value->owners);
}
template<class Control> std::shared_ptr<const void>& shared_owner(Representation<Control>* value, std::size_t index) {
  if (grouped(value->owners)) return *static_cast<std::shared_ptr<const void>*>(group_owner(value));
  if (mixed(value->owners)) return mixed_bodies(value)[index].owner.shared;
  return shared_bodies(value)[index].owner;
}
template<class Control> allocation::Storage* allocation_owner(Representation<Control>* value, std::size_t index) {
  if (grouped(value->owners)) return *static_cast<allocation::Storage**>(group_owner(value));
  if (mixed(value->owners)) return mixed_bodies(value)[index].owner.allocation;
  return allocated_bodies(value)[index].owner;
}
template<class Control> Result<PartSource> source_of(Control control, std::size_t index,
    const std::byte* address, allocation::Storage* native, const std::shared_ptr<const void>* backing) {
  auto extent = control.extent(index);
  if (!extent) return std::unexpected(extent.error());
  PartSource result{control.id(index), {address, *extent}};
  if (!valid(result.body, control.padding(index))) return std::unexpected(Error::invalid_argument);
  result.retained = backing ? SourceAccess::certificate(result.body, control.padding(index), *backing) :
      SourceAccess::certificate(result.body, control.padding(index), native);
  return result;
}
} // namespace object_detail

template<class Control> Result<PartSource> source(const Part<Control>* part) noexcept {
  if (!part) return std::unexpected(Error::absent);
  const auto shared = object_detail::shared(part->owners);
  return object_detail::source_of(part->control, part->index, part->address,
      shared ? nullptr : part->owner.allocation, shared ? &part->owner.shared : nullptr);
}

template<class Control> Result<PartSource> source(RepresentationSelection<Control> selection, std::size_t index) noexcept {
  using namespace object_detail;
  if (!selection || index >= selection.part_count()) return std::unexpected(Error::absent);
  auto* value = selection.realization;
  const auto shared = part_shared(value, index);
  return source_of(value->control, index, *address_slot(value, index),
      shared ? nullptr : allocation_owner(value, index), shared ? &shared_owner(value, index) : nullptr);
}

template<class Control> Result<Representation<Control>*> capture_representation(
    std::pmr::memory_resource& memory, Control control, PartOwners owners, std::span<const PartSource> bodies) noexcept {
  using namespace object_detail;
  using Realization = Representation<Control>;
  if (!control.valid(memory) || bodies.size() != control.part_count() || static_cast<unsigned>(owners) > 4)
    return std::unexpected(Error::invalid_argument);
  const auto is_shared = [&](const PartSource& body) { return SourceAccess::shared(body, owners); };
  std::size_t supplier = bodies.size();
  bool retained_native = false;
  const auto validate = [&]() -> std::optional<Error> {
    supplier = bodies.size();
    retained_native = false;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
      const auto& body = bodies[i];
      const auto extent = control.extent(i);
      const auto padding = control.padding(i);
      const auto alignment = control.alignment(i);
      if (body.id != control.id(i) || !extent || *extent != body.body.size() ||
          !valid(body.body, padding) || ((!body.body.empty() || padding) &&
           (!alignment || std::uintptr_t(body.body.data()) % alignment)))
        return Error::invalid_argument;
      if (SourceAccess::retained(body)) {
        if (body.allocation || body.backing.use_count() || !SourceAccess::within(body, padding))
          return Error::invalid_argument;
        if (!mixed(owners) && is_shared(body) != shared(owners)) return Error::invalid_argument;
      } else if (mixed(owners) && body.allocation && body.backing.use_count()) return Error::invalid_argument;
      if (is_shared(body)) {
        const auto& backing = SourceAccess::backing(body);
        if ((body.body.size() || padding) && !backing.use_count()) return Error::invalid_argument;
        if (grouped(owners) && i) {
          const auto& first = SourceAccess::backing(bodies[0]);
          if (backing.owner_before(first) || first.owner_before(backing)) return Error::invalid_argument;
        }
      } else {
        retained_native |= SourceAccess::retained(body);
        if (!SourceAccess::retained(body)) {
          if ((body.body.size() || padding) &&
              (!body.allocation || !within(*body.allocation, body.body, padding)))
            return Error::invalid_argument;
          if (body.allocation && *body.allocation) {
            if (grouped(owners) && supplier != bodies.size() && body.allocation != bodies[supplier].allocation)
              return Error::invalid_argument;
            if (supplier == bodies.size()) supplier = i;
          }
        }
        if (grouped(owners) && i && SourceAccess::storage(body) != SourceAccess::storage(bodies[0]))
          return Error::invalid_argument;
      }
    }
    return {};
  };
  const auto stride = grouped(owners) ? sizeof(const std::byte*) : body_stride(owners);
  const auto group_bytes = grouped(owners) && !bodies.empty() ?
      (shared(owners) ? sizeof(std::shared_ptr<const void>) : sizeof(void*)) : 0;
  if (bodies.size() > (SIZE_MAX - sizeof(Realization) - group_bytes) / stride)
    return std::unexpected(Error::capacity);
  const auto size = sizeof(Realization) + group_bytes + bodies.size() * stride;
  try {
    std::pmr::vector<allocation::Storage*> scratch(&memory);
    if (!shared(owners) && !grouped(owners) && bodies.size() > 8) {
      const auto fresh = std::count_if(bodies.begin(), bodies.end(), [&](const PartSource& body) {
        return !SourceAccess::retained(body) && !is_shared(body) && body.allocation && body.allocation->storage;
      });
      // Advisory capacity only: allocation/proof callbacks can still change sources.
      if (fresh > 8) scratch.resize(fresh);
    }
    auto* value = std::construct_at(static_cast<Realization*>(memory.allocate(size, alignof(Realization))),
        Realization{control, 1, owners, static_cast<std::uint8_t>(stride)});
    control.retain();
    // Initialize every owner slot before callbacks can refuse. Fresh Allocation
    // authority is transferred only after all validation and retained pins succeed.
    if (grouped(owners)) {
      for (std::size_t i = 0; i < bodies.size(); ++i) std::construct_at(addresses(value) + i, nullptr);
      if (!bodies.empty()) {
        if (shared(owners)) std::construct_at(static_cast<std::shared_ptr<const void>*>(group_owner(value)));
        else std::construct_at(static_cast<allocation::Storage**>(group_owner(value)), nullptr);
      }
    } else {
      for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (shared(owners)) std::construct_at(shared_bodies(value) + i, SharedBody{nullptr, {}});
        else if (mixed(owners)) std::construct_at(mixed_bodies(value) + i);
        else std::construct_at(allocated_bodies(value) + i, AllocatedBody{nullptr, nullptr});
      }
    }
    if (auto error = validate()) { release(value); return std::unexpected(*error); }
    if (!shared(owners)) {
      if (!grouped(owners) && !unique_allocations(bodies, scratch)) {
        release(value); return std::unexpected(Error::invalid_argument);
      }
      if (grouped(owners)) {
        if (supplier != bodies.size()) {
          auto ready = capture_ready(bodies[supplier].allocation);
          if (!ready) { release(value); return std::unexpected(ready.error()); }
        }
      } else {
        for (const auto& body : bodies) {
          if (is_shared(body) || SourceAccess::retained(body)) continue;
          auto ready = capture_ready(body.allocation);
          if (!ready) { release(value); return std::unexpected(ready.error()); }
        }
      }
    }
    if (grouped(owners)) {
      if (!bodies.empty()) {
        if (shared(owners)) *static_cast<std::shared_ptr<const void>*>(group_owner(value)) = SourceAccess::backing(bodies[0]);
        else if (supplier == bodies.size()) {
          auto held = pin(SourceAccess::storage(bodies[0]));
          if (!held) { release(value); return std::unexpected(held.error()); }
          *static_cast<allocation::Storage**>(group_owner(value)) = *held;
        }
      }
    } else if (retained_native) {
      for (std::size_t i = 0; i < bodies.size(); ++i) {
        const auto& body = bodies[i];
        if (!is_shared(body) && SourceAccess::retained(body)) {
          auto held = pin(SourceAccess::storage(body));
          if (!held) { release(value); return std::unexpected(held.error()); }
          if (mixed(owners)) mixed_bodies(value)[i].owner.allocation = *held;
          else allocated_bodies(value)[i].owner = *held;
        }
      }
    }
    // No fallible work or callback remains. Commit fresh uses and addresses.
    if (grouped(owners)) {
      for (std::size_t i = 0; i < bodies.size(); ++i) addresses(value)[i] = bodies[i].body.data();
      if (!shared(owners) && supplier != bodies.size())
        *static_cast<allocation::Storage**>(group_owner(value)) = take_source(bodies[supplier].allocation);
    } else {
      for (std::size_t i = 0; i < bodies.size(); ++i) {
        const auto& body = bodies[i];
        if (mixed(owners)) mixed_bodies(value)[i].address = body.body.data();
        else if (shared(owners)) shared_bodies(value)[i].address = body.body.data();
        else allocated_bodies(value)[i].address = body.body.data();
        if (is_shared(body)) {
          if (mixed(owners)) {
            auto& slot = mixed_bodies(value)[i];
            slot.shared = true;
            std::construct_at(&slot.owner.shared, SourceAccess::backing(body));
          } else shared_bodies(value)[i].owner = SourceAccess::backing(body);
        } else if (!SourceAccess::retained(body)) {
          if (mixed(owners)) mixed_bodies(value)[i].owner.allocation = take_source(body.allocation);
          else allocated_bodies(value)[i].owner = take_source(body.allocation);
        }
      }
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
      else if (mixed(value->owners)) {
        auto& slot = mixed_bodies(value)[i];
        if (slot.shared) std::destroy_at(&slot.owner.shared);
        else drop(slot.owner.allocation);
        std::destroy_at(&slot);
      }
      else drop(allocated_bodies(value)[i].owner);
    }
  }
  std::destroy_at(value);
  memory->deallocate(value, size, alignof(Representation<Control>));
  control.release();
}
template<class Control> Control RepresentationSelection<Control>::control() const noexcept { return realization->control; }
template<class Control> std::size_t RepresentationSelection<Control>::part_count() const noexcept { return realization ? realization->control.part_count() : 0; }
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
    value->index = index;
    value->owners = part_shared(realization, index) ? PartOwners::independent_shared : PartOwners::independent_allocation;
    value->retention = interpretation;
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
