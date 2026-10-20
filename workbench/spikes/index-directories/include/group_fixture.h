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
private:
  loom::Worker& worker_;
  std::pmr::unsynchronized_pool_resource memory_;
  std::pmr::memory_resource* metadata_;
  std::array<Format,17> groups_{};
  Format data_{{64},1,material_space};
  Store store_;
  orbital::Result<Store::Owner> finish(std::uint64_t,const Format&,
      orbital::allocation::Allocation&);
public:
  explicit GroupFixture(loom::Worker&,std::span<const Record>,
      std::pmr::memory_resource* metadata=nullptr);
  GroupFixture(const GroupFixture&)=delete;
  GroupFixture& operator=(const GroupFixture&)=delete;
  Store& store() noexcept { return store_; }
  std::pmr::memory_resource& metadata() noexcept { return *metadata_; }
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
