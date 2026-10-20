#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace index_spike {
// Non-owning caller view: an external live Scope protects every use, or an
// uninterrupted Borrowed call ends before publication/adoption/suspension/reset.
// NativeRead/context existence pins nothing and supplies no shadow index.
// The constructor admits sorted prefixes, count within capacity, slot ranges,
// and matching header/column/child geometry. Generic Part capture alone does
// not prove these directory semantics or real segment-locator association.
struct NativeRead {
  void* context{};
  std::uint64_t root{};
  std::uint32_t internal_prefix{}, internal_children{};
  std::uint32_t leaf_prefix{}, leaf_slots{}, leaf_names{};
  std::uint32_t chunk_log2{}, colocated{}, reserved{};
};
struct NodeBorrow {
  const std::byte* bytes;
  void* selection;
};
struct Lookup {
  const std::byte* descriptor{};
  void* payload_selection{};
  void* leaf_selection{};
  std::uint64_t leaf_name{};
  std::uint32_t rank{}, slot{}, part{}, reserved{};
};
static_assert(std::is_trivially_copyable_v<NodeBorrow> && sizeof(NodeBorrow)==16);
static_assert(sizeof(NativeRead)==48 && offsetof(NativeRead,chunk_log2)==36);
static_assert(sizeof(Lookup)==48 && offsetof(Lookup,rank)==32);
}

// Typed C++ seams invoke the real modules and retain their actual ABI/costs.
// No private Orbital Representation or catalog layout is exposed to assembly.
extern "C" index_spike::NodeBorrow idx_module_bind(const index_spike::NativeRead*,
    std::uint64_t name) noexcept;
extern "C" const std::byte* idx_module_part(void* selection,std::uint32_t ordinal) noexcept;
extern "C" unsigned idx_ikea_lower_u32(const std::uint32_t*,unsigned,std::uint32_t) noexcept;
extern "C" unsigned idx_ikea_upper_u32(const std::uint32_t*,unsigned,std::uint32_t) noexcept;

// Returned bytes require the external binding above through final consumption.
// Exact physical-prefix lookup reads prefix columns, not logical bounds.
extern "C" const std::byte* idx_asm_exact(const index_spike::NativeRead*,
    std::uint32_t prefix,index_spike::Lookup* optional_provenance) noexcept;
// Direct-named16-position group: root+prefix/16 selects the Object; low16-bit
// original suffix mask in its header ranks sorted64B payloads beginning at64.
extern "C" const std::byte* idx_asm_group16(const index_spike::NativeRead*,
    std::uint32_t prefix,index_spike::Lookup* optional_provenance) noexcept;
extern "C" const std::byte* idx_asm_group16_read(const index_spike::NativeRead*,
    std::uint32_t prefix) noexcept;
// One selected root owns immutable F32 radix nodes, then original-mask groups.
// Each576B node has shift:u8@2,min/max groupIndex:u32@8/12 and32 entries
// {child bytes:pointer@0, original group mask:u16@8} starting64,stride16.
// Root shift is a multiple of5 in0..25; every internal child decrements it by5.
extern "C" const std::byte* idx_asm_graph32(const index_spike::NativeRead*,
    std::uint32_t prefix) noexcept;
// Actual root Part bytes selected under the SAME still-live Scope/Borrowed.
// This is qualified byte reuse, not a numeric-cut cache or an owning handle.
extern "C" const std::byte* idx_asm_graph32_bound(const std::byte* selected_root,
    std::uint32_t prefix) noexcept;
extern "C" std::uint64_t idx_asm_digest64(const std::byte*) noexcept;
extern "C" void idx_asm_copy64(std::byte* private_destination,const std::byte*) noexcept;
// Disjoint private destination, blocks>0, exact initialized64-byte blocks;
// aligned eight-byte patch wholly inside the destination. Constructor-proved.
extern "C" void idx_asm_clone_patch64(std::byte* private_destination,
    const std::byte* source,std::uint32_t blocks,std::uint32_t byte_offset,
    std::uint64_t replacement) noexcept;
