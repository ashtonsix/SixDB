#pragma once
#include "native_abi.h"
#include <orbital/object.h>
#include <orbital/representation.h>
#include <array>

namespace index_spike {
// First fixed-capacity native experiment. Format objects outlive all derived
// material/Scopes. They are not a proposed production schema or private MVCC.
struct Format {
  std::array<std::uint32_t,17> bytes{};
  std::uint16_t count{};
  std::uint64_t object_namespace{};
};
struct Control {
  std::pmr::memory_resource* memory{};
  const Format* format{};
  std::uint64_t name{};
  std::uint32_t first{};
  std::uint16_t count{};
  orbital::PartOwners owners{orbital::PartOwners::independent_allocation};
  std::uint8_t reserved{};
  bool valid(std::pmr::memory_resource& r) const noexcept {
    return memory==&r && format && count && format->count<=format->bytes.size()
        && first<=format->count && count<=format->count-first;
  }
  auto* resource() const noexcept { return memory; }
  std::size_t part_count() const noexcept { return count; }
  orbital::PartId id(std::size_t i) const noexcept { return {name,first+i}; }
  orbital::Result<std::size_t> extent(std::size_t i) const noexcept {
    if (i>=count) return std::unexpected(orbital::Error::invalid_argument);
    return format->bytes[first+i];
  }
  std::uint32_t padding(std::size_t) const noexcept { return 0; }
  std::size_t alignment(std::size_t) const noexcept { return 64; }
  void retain() const noexcept {}
  void release() const noexcept {}
  std::size_t selected_bytes(std::size_t) const noexcept { return 0; }
  Control copy_selected(void*,std::size_t i) const noexcept {
    auto c=*this; c.first+=i; c.count=1; return c;
  }
  std::size_t metadata_bytes() const noexcept { return 0; }
  void destroy_selected() const noexcept {}
};
static_assert(sizeof(Control)==32);
using Selection=orbital::RepresentationSelection<Control>;
struct Physical {
  using Value=orbital::Representation<Control>;
  using Selection=index_spike::Selection;
  using Specification=Control;
  using PartSource=orbital::PartSource;
  using ExportPolicy=orbital::PartRetention;
  struct Footprint { std::size_t requested_bytes{},copied_metadata{}; };
  static void retain(Value*) noexcept;
  static void release(Value*) noexcept;
  static Value* get(Selection s) noexcept { return s.realization; }
  static Selection selection(Value* v) noexcept { return {v}; }
  static orbital::Result<Value*> capture(std::pmr::memory_resource&,const Control&,
      std::span<const PartSource>) noexcept;
  // Opaque fixture admission only; actual segment/locator association is a
  // separate producer check in the material/publication vertical treatment.
  static bool accepts(Value*,orbital::ObjectId,std::uint64_t) noexcept;
  static bool accepts_resource(Value*,std::pmr::memory_resource&) noexcept;
  static bool compatible(Value*,Value*) noexcept;
  static Footprint footprint(Value*) noexcept;
  static orbital::Result<orbital::Part<Control>*> export_part(Selection,std::size_t,
      orbital::ObjectBinding,ExportPolicy) noexcept;
};
using Store=orbital::ObjectStore<Physical>;
struct ReadContext {
  std::uint64_t directory_namespace{};
  const Store::Scope* scope{};
  Store::Borrowed borrowed{};
};
}
extern template class orbital::ObjectStore<index_spike::Physical>;
extern template struct orbital::RepresentationSelection<index_spike::Control>;
extern template orbital::Result<orbital::PartSource> orbital::source(
    index_spike::Selection,std::size_t) noexcept;
