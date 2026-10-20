#pragma once
#include "material.h"
#include <loom/worker.h>
#include <array>

namespace index_spike {
struct Record {
  std::uint32_t prefix{};
  std::array<std::byte,64> descriptor{},material{};
};

// An owning experimental producer, not a second versioning/index authority.
// Input records are setup-only; every later lookup/edit selects actual Orbital
// material. Field0 of each native descriptor is its actual segment Part address;
// canonical peer metadata uses the physical prefix instead of process addresses.
class GroupFixture {
public:
  static constexpr std::uint64_t directory_space=0x494458475250;
  static constexpr std::uint64_t material_space=0x494458444154;
  static constexpr std::uint64_t root_base=1;
  static constexpr std::uint64_t graph_space=0x494458475241;
private:
  struct GraphGroup { std::uint32_t index; std::uint16_t mask; Store::Owner owner; };
  struct GraphPath {
    std::array<Selection,6> nodes{};
    std::array<unsigned,6> slots{};
    unsigned count{};
  };
  loom::Worker& worker_;
  std::pmr::unsynchronized_pool_resource memory_{metadata_pool_options};
  std::pmr::memory_resource* metadata_;
  std::array<Format,17> groups_{};
  Format data_{{64},1,material_space};
  Store store_;
  bool graph_{};
  std::uint64_t next_graph_name_{2};
  std::size_t initial_graph_nodes_{};
  unsigned directory_grain_;
  std::size_t initial_directory_allocations_{},initial_directory_bytes_{};
  orbital::Result<Store::Owner> finish(std::uint64_t,const Format&,
      orbital::allocation::Allocation&);
  Store::Owner build_graph(std::span<GraphGroup>,unsigned shift,std::uint64_t name);
  bool graph_lookup(const Store::Scope&,std::uint32_t,Lookup&,GraphPath&) const;
  orbital::Result<Store::Owner> copy_graph(const GraphPath&,Store::Owner);
public:
  explicit GroupFixture(loom::Worker&,std::span<const Record>,
      std::pmr::memory_resource* metadata=nullptr,bool graph=false,unsigned directory_grain=16);
  GroupFixture(const GroupFixture&)=delete;
  GroupFixture& operator=(const GroupFixture&)=delete;
  Store& store() noexcept { return store_; }
  std::pmr::memory_resource& metadata() noexcept { return *metadata_; }
  std::uint64_t directory_namespace() const noexcept { return graph_?graph_space:directory_space; }
  std::size_t initial_graph_nodes() const noexcept { return initial_graph_nodes_; }
  unsigned directory_grain() const noexcept { return directory_grain_; }
  std::size_t initial_directory_allocations() const noexcept { return initial_directory_allocations_; }
  std::size_t initial_directory_bytes() const noexcept { return initial_directory_bytes_; }
  const std::byte* read(const NativeRead& view,std::uint32_t prefix) const noexcept {
    return graph_?idx_asm_graph32(&view,prefix):idx_asm_group16_read(&view,prefix);
  }
  NativeRead view(ReadContext& context) const noexcept {
    return {&context,root_base,0,0,0,0,0,0,0,0};
  }
  static orbital::ObjectId material_id(std::uint32_t prefix) noexcept {
    return {material_space,std::uint64_t(prefix)+1};
  }
  // Complete actual producer work: validate old association, allocate/construct
  // new material, derive its Part pointer, clone directory group, capture both,
  // prepare/install one packet, and settle local owners before returning.
  orbital::Result<void> replace(std::uint32_t,unsigned word,std::uint64_t);
};
}
