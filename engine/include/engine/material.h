#pragma once
#include <orbital/object.h>
#include <orbital/representation.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine::material {
namespace detail {
enum class Kind : std::uint32_t { segment, schema, counter, ordinary, hash, leaf, branch, extras };
struct Access;
}

struct Geometry {
  std::uint64_t bytes{};
  std::uint32_t alignment{64};
  std::uint32_t padding{};
  friend bool operator==(const Geometry&,const Geometry&)=default;
};

/// Immutable geometry and interpretation. Its metadata resource follows
/// Orbital's lifetime contract. All uses, including exported Part destruction,
/// remain on the catalog's owner; this is not a cross-Lane ownership adapter.
class Layout {
  friend struct detail::Access;
  mutable std::size_t references_{1};
  Layout(std::pmr::memory_resource&,std::uint64_t,std::uint32_t,detail::Kind,
      std::span<const Geometry>,std::shared_ptr<const void>);
public:
  Layout(const Layout&)=delete;
  Layout(Layout&&)=delete;
  Layout& operator=(const Layout&)=delete;
  Layout& operator=(Layout&&)=delete;
  std::pmr::memory_resource* const memory;
  const std::uint64_t object_namespace;
  const std::uint32_t representation;
  const detail::Kind kind;
  const std::pmr::vector<Geometry> parts;
  const std::shared_ptr<const void> interpretation;
  void retain() const noexcept { ++references_; }
  void release() const noexcept;
};

struct Control;
class Definition {
  friend struct detail::Access;
  const Layout* layout_{};
  explicit Definition(const Layout* p):layout_(p) {}
public:
  Definition()=default;
  Definition(const Definition& x) noexcept :layout_(x.layout_) { if (layout_) layout_->retain(); }
  Definition(Definition&& x) noexcept :layout_(std::exchange(x.layout_,nullptr)) {}
  Definition& operator=(Definition x) noexcept { std::swap(layout_,x.layout_); return *this; }
  ~Definition() { if (layout_) layout_->release(); }
  const Layout* get() const noexcept { return layout_; }
  explicit operator bool() const noexcept { return layout_; }
  /// The returned Control borrows this definition until capture finishes.
  Control control(std::uint64_t name,orbital::PartOwners owners=
      orbital::PartOwners::independent_allocation) const noexcept;
  [[nodiscard]] static orbital::Result<Definition> native(std::pmr::memory_resource&,
      std::uint64_t object_namespace,std::uint32_t representation,
      std::span<const Geometry>,std::shared_ptr<const void> interpretation={});
};

/// Trivially copied borrow. Orbital explicitly retains the Layout on capture
/// and selected-copy export. The full ObjectId is namespace+name, never a hash.
struct Control {
  const Layout* layout{};
  std::uint64_t name{};
  std::uint32_t first{};
  std::uint8_t projected{};
  orbital::PartOwners owners{orbital::PartOwners::independent_allocation};
  std::uint16_t reserved{};
  bool valid(std::pmr::memory_resource&) const noexcept;
  auto* resource() const noexcept { return layout?layout->memory:nullptr; }
  std::size_t part_count() const noexcept { return !layout?0:projected?1:layout->parts.size(); }
  orbital::PartId id(std::size_t i) const noexcept { return {name,first+i}; }
  [[nodiscard]] orbital::Result<std::size_t> extent(std::size_t i) const noexcept;
  std::size_t alignment(std::size_t i) const noexcept { return layout->parts[first+i].alignment; }
  std::uint32_t padding(std::size_t i) const noexcept { return layout->parts[first+i].padding; }
  void retain() const noexcept { layout->retain(); }
  void release() const noexcept { layout->release(); }
  std::size_t selected_bytes(std::size_t) const noexcept { return 0; }
  Control copy_selected(void*,std::size_t i) const noexcept {
    auto c=*this; c.first+=i; c.projected=1; c.retain(); return c;
  }
  std::size_t metadata_bytes() const noexcept { return 0; }
  void destroy_selected() const noexcept { release(); }
};
static_assert(sizeof(Control)==24 && std::is_trivially_copyable_v<Control>);
using Selection=orbital::RepresentationSelection<Control>;
struct Physical {
  using Value=orbital::Representation<Control>;
  using Selection=material::Selection;
  using Specification=Control;
  using PartSource=orbital::PartSource;
  using ExportPolicy=orbital::PartRetention;
  struct Footprint { std::size_t requested_bytes{},copied_metadata{}; };
  static void retain(Value*) noexcept;
  static void release(Value*) noexcept;
  static Value* get(Selection s) noexcept { return s.realization; }
  static Selection selection(Value* p) noexcept { return {p}; }
  static orbital::Result<Value*> capture(std::pmr::memory_resource&,const Control&,
      std::span<const PartSource>) noexcept;
  static bool accepts(Value*,orbital::ObjectId,std::uint64_t) noexcept;
  static bool accepts_resource(Value*,std::pmr::memory_resource&) noexcept;
  static bool compatible(Value*,Value*) noexcept;
  static Footprint footprint(Value*) noexcept;
  static orbital::Result<orbital::Part<Control>*> export_part(Selection,std::size_t,
      orbital::ObjectBinding,ExportPolicy) noexcept;
};
using Store=orbital::ObjectStore<Physical>;
}

extern template class orbital::ObjectStore<engine::material::Physical>;
extern template struct orbital::RepresentationSelection<engine::material::Control>;
extern template orbital::Result<orbital::PartSource> orbital::source(
    engine::material::Selection,std::size_t) noexcept;
extern template orbital::Result<orbital::PartSource> orbital::source(
    const orbital::Part<engine::material::Control>*) noexcept;
extern template void orbital::retain(orbital::Part<engine::material::Control>*) noexcept;
extern template void orbital::release(orbital::Part<engine::material::Control>*) noexcept;
extern template orbital::View orbital::view(const orbital::Part<engine::material::Control>*) noexcept;
extern template orbital::ObjectBinding orbital::binding(const orbital::Part<engine::material::Control>*) noexcept;
