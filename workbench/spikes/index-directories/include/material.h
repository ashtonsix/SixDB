#pragma once
#include "native_abi.h"
#include <orbital/object.h>
#include <orbital/representation.h>
#include <array>

namespace index_spike {
struct DirectoryDependencies;
// First fixed-capacity native experiment. Format objects outlive all derived
// material/Scopes. They are not a proposed production schema or private MVCC.
struct Format {
  std::array<std::uint32_t,17> bytes{};
  std::uint16_t count{};
  std::uint64_t object_namespace{};
  DirectoryDependencies* dependencies{};
  std::pmr::memory_resource* memory{};
};
// First bounded pool hypothesis; actual backing/RSS and complete callers decide.
inline constexpr std::pmr::pool_options metadata_pool_options{1024,128};
enum class ExtentMode : std::uint8_t { parts, prefix8 };
struct Control {
  const Format* format{};
  std::uint64_t name{};
  std::uint32_t first{};
  std::uint16_t count{};
  orbital::PartOwners owners{orbital::PartOwners::independent_allocation};
  ExtentMode extent_mode{};
  bool valid(std::pmr::memory_resource& r) const noexcept {
    if (!format || format->memory!=&r || !count || format->count>format->bytes.size()) return false;
    if (extent_mode==ExtentMode::prefix8)
      return count==1 && format->count==1 && std::size_t(first)*8>=format->bytes[1]
          && std::size_t(first)*8<=format->bytes[0];
    return extent_mode==ExtentMode::parts && first<=format->count && count<=format->count-first;
  }
  auto* resource() const noexcept { return format?format->memory:nullptr; }
  std::size_t part_count() const noexcept { return count; }
  orbital::PartId id(std::size_t i) const noexcept {
    return {name,extent_mode==ExtentMode::prefix8?i:first+i};
  }
  orbital::Result<std::size_t> extent(std::size_t i) const noexcept {
    if (i>=count) return std::unexpected(orbital::Error::invalid_argument);
    return extent_mode==ExtentMode::prefix8?std::size_t(first)*8:format->bytes[first+i];
  }
  std::uint32_t padding(std::size_t) const noexcept { return 0; }
  std::size_t alignment(std::size_t) const noexcept { return 64; }
  void retain() const noexcept;
  void release() const noexcept;
  std::size_t selected_bytes(std::size_t) const noexcept { return 0; }
  Control copy_selected(void*,std::size_t i) const noexcept {
    auto c=*this;
    if (extent_mode==ExtentMode::parts) c.first+=i;
    c.count=1; c.retain(); return c;
  }
  std::size_t metadata_bytes() const noexcept { return 0; }
  void destroy_selected() const noexcept { release(); }
};
static_assert(sizeof(Control)==24 && sizeof(Format)==96);
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
// Physical interpretation proof for the CURRENT directory topology. It owns
// actual children directly, never a prior root or logical version ledger.
struct DirectoryDependencies {
  std::pmr::memory_resource* memory;
  std::size_t references{1};
  std::uint64_t name;
  Format format{{576},1,0,this};
  std::array<Store::Owner,32> children;
  DirectoryDependencies(std::pmr::memory_resource& r,std::uint64_t ns,std::uint64_t id)
      : memory(&r),name(id) { format.object_namespace=ns; format.memory=&r; }
  static DirectoryDependencies* make(std::pmr::memory_resource&,std::uint64_t,std::uint64_t);
  void release() noexcept;
};
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
