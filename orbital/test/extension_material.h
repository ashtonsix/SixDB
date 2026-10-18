#pragma once
#include <orbital/object.h>
#include <orbital/representation.h>
#include <type_traits>

// A caller-owned format: one or two independently retained integer Parts.
namespace extension_test {
struct Control {
  std::pmr::memory_resource* memory{};
  std::uint32_t count{2}, first{};
  bool valid(std::pmr::memory_resource& resource) const noexcept {
    return memory == &resource && count && first < 2 && count <= 2 - first;
  }
  auto* resource() const noexcept { return memory; }
  std::size_t part_count() const noexcept { return count; }
  orbital::PartId id(std::size_t index) const noexcept { return {0x455854, first + index}; }
  orbital::Result<std::size_t> extent(std::size_t) const noexcept { return sizeof(std::uint64_t); }
  std::uint32_t padding(std::size_t) const noexcept { return 0; }
  std::size_t alignment(std::size_t) const noexcept { return alignof(std::uint64_t); }
  void retain() const noexcept {}
  void release() const noexcept {}
  std::size_t selected_bytes(std::size_t) const noexcept { return 0; }
  Control copy_selected(void*, std::size_t index) const noexcept {
    return {memory, 1, first + static_cast<std::uint32_t>(index)};
  }
  std::size_t metadata_bytes() const noexcept { return 0; }
  void destroy_selected() const noexcept {}
};
static_assert(std::is_trivially_copyable_v<Control>);

using Selection = orbital::RepresentationSelection<Control>;
struct Physical {
  using Value = orbital::Representation<Control>;
  using Selection = extension_test::Selection;
  using Specification = Control;
  using PartSource = orbital::PartSource;
  using ExportPolicy = orbital::PartRetention;
  struct Footprint { std::size_t requested_bytes{}, copied_metadata{}; };
  static void retain(Value*) noexcept;
  static void release(Value*) noexcept;
  static Value* get(Selection selected) noexcept { return selected.realization; }
  static Selection selection(Value* value) noexcept { return {value}; }
  static orbital::Result<Value*> capture(std::pmr::memory_resource&, const Control&,
      std::span<const PartSource>) noexcept;
  static bool accepts(Value*, orbital::ObjectId, std::uint64_t) noexcept { return true; }
  static bool accepts_resource(Value*, std::pmr::memory_resource&) noexcept;
  static bool compatible(Value*, Value*) noexcept;
  static Footprint footprint(Value*) noexcept;
  static orbital::Result<orbital::Part<Control>*> export_part(Selection, std::size_t,
      orbital::ObjectBinding, ExportPolicy) noexcept;
};
using Store = orbital::ObjectStore<Physical>;
} // namespace extension_test

extern template class orbital::ObjectStore<extension_test::Physical>;
extern template struct orbital::RepresentationSelection<extension_test::Control>;
extern template void orbital::release(orbital::Part<extension_test::Control>*) noexcept;
extern template orbital::View orbital::view(const orbital::Part<extension_test::Control>*) noexcept;
extern template orbital::ObjectBinding orbital::binding(const orbital::Part<extension_test::Control>*) noexcept;
