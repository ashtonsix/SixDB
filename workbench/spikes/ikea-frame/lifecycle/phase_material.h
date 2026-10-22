// Private native phase experiment; these fixed limits are not a Frame format.
#pragma once
#include "../execution/material-witness/material.h"
#include <orbital/object.h>
#include <orbital/representation.h>
#include <orbital/instantiate/representation.h>
#include <array>
#include <bit>
#include <span>
#include <type_traits>

namespace frame_lifecycle::phase {
namespace fe = frame_encoding;
namespace fm = frame_material_witness;
using u64 = std::uint64_t;
using u128 = unsigned __int128;
constexpr unsigned blocks_per_group = 16, rows_per_group = blocks_per_group * 256;
constexpr unsigned groups = 2, rows = groups * rows_per_group;
constexpr unsigned flag_words = rows_per_group / 64, flag_bytes = flag_words * 8;
constexpr u64 object_tag = 0x4652415048415345;
constexpr orbital::ObjectId object{object_tag, 1};

struct BlockSelection {
    std::uint32_t bits = UINT32_MAX;
    std::uint16_t group(unsigned g) const noexcept { return std::uint16_t(bits >> (g * 16)); }
    bool row(unsigned p) const noexcept { return (bits >> (p / 256)) & 1; }
};
struct LogicalRow {
    u64 value{};
    bool present{true}, valid{true};
    bool contributes() const noexcept { return present && valid; }
};
struct Range { u64 minimum = UINT64_MAX, maximum{}; };
// Presence and non-NULL validity are distinct. Value payload for an invalid
// position need not change; it never contributes until supplied a valid value.
struct GroupHeader {
    std::uint32_t bytes{};
    u64 identity{}; // Names the coupled logical base values + validity, not its placement.
    u64 base{};
    std::uint8_t native_bytes{}; // 0=encoded; otherwise native FOR lane1/2/4/8.
    std::array<std::uint16_t, blocks_per_group> present_count{}, valid_count{};
    std::array<Range, blocks_per_group> bounds{};
    bool valid() const noexcept;
};
struct EncodedBase {
    GroupHeader header{};
    std::array<fe::descriptor, blocks_per_group> blocks{};
    std::array<std::uint32_t, blocks_per_group> grants{};
    fm::Dictionary dictionary{};
    std::uint16_t coded{};
    fe::checkpoints checkpoints = fe::checkpoints::native;
    static constexpr unsigned parts = 2;
    bool valid() const noexcept;
    std::size_t extent(unsigned i) const noexcept { return i ? flag_bytes * 2 : header.bytes; }
};
struct NativeBase {
    GroupHeader header{};
    static constexpr unsigned parts = 2;
    bool valid() const noexcept { return header.native_bytes && header.valid(); }
    std::size_t extent(unsigned i) const noexcept { return i ? flag_bytes * 2 : header.bytes; }
};
// Overlay is a separate Part. Entries are sorted original positions; its
// columns are positions(u16), old/new flags(u8), old/new logical values(u64).
// The 8-byte occupied header keeps an empty overlay a real bounded source.
struct Overlay {
    std::uint16_t count{};
    u64 basis{};
    static constexpr std::size_t bytes(unsigned n) { return 8 + std::size_t(n) * 20; }
};
struct OverlayBase {
    EncodedBase frozen{};
    Overlay changes{};
    static constexpr unsigned parts = 3;
    bool valid() const noexcept {
        return changes.count <= rows_per_group && changes.basis == frozen.header.identity && frozen.valid();
    }
    std::size_t extent(unsigned i) const noexcept {
        return i == 2 ? Overlay::bytes(changes.count) : frozen.extent(i);
    }
};

template <class Group> struct Layout {
    std::pmr::memory_resource* memory{};
    std::array<Group, groups> group{};
    unsigned first{}, count = groups * Group::parts;
    bool valid(std::pmr::memory_resource& r) const noexcept {
        if (memory != &r || !count || first >= groups * Group::parts ||
            count > groups * Group::parts - first) return false;
        for (unsigned i = 0; i < count; ++i)
            if (!group[(first + i) / Group::parts].valid()) return false;
        return true;
    }
    auto* resource() const noexcept { return memory; }
    std::size_t part_count() const noexcept { return count; }
    orbital::PartId id(std::size_t i) const noexcept { return {object_tag, std::uint32_t(first + i)}; }
    orbital::Result<std::size_t> extent(std::size_t i) const noexcept {
        const auto p = first + unsigned(i);
        return group[p / Group::parts].extent(p % Group::parts);
    }
    std::size_t alignment(std::size_t) const noexcept { return 64; }
    std::uint32_t padding(std::size_t) const noexcept { return 0; }
    void retain() const noexcept {}
    void release() const noexcept {}
    std::size_t selected_bytes(std::size_t) const noexcept { return 0; }
    Layout copy_selected(void*, std::size_t i) const noexcept {
        Layout out; out.memory = memory; out.first = first + unsigned(i); out.count = 1;
        out.group[out.first / Group::parts] = group[out.first / Group::parts];
        return out;
    }
    std::size_t metadata_bytes() const noexcept { return 0; }
    void destroy_selected() const noexcept {}
};

// Reuses Orbital's public representation/object protocol. The two/four-Part
// layouts are experimental ownership controls, not a general owner framework.
template <class C> struct Physical {
    using Value = orbital::Representation<C>;
    using Selection = orbital::RepresentationSelection<C>;
    using Specification = C;
    using PartSource = orbital::PartSource;
    using ExportPolicy = orbital::PartRetention;
    struct Footprint { std::size_t requested_bytes{}, copied_metadata{}; };
    static void retain(Value* p) noexcept { orbital::retain(p); }
    static void release(Value* p) noexcept { orbital::release(p); }
    static Value* get(Selection s) noexcept { return s.realization; }
    static Selection selection(Value* p) noexcept { return {p}; }
    static orbital::Result<Value*> capture(std::pmr::memory_resource& r, const C& c,
            std::span<const PartSource> parts) noexcept {
        return orbital::capture_representation(r, c, orbital::PartOwners::independent_allocation, parts);
    }
    static bool accepts(Value*, orbital::ObjectId, std::uint64_t) noexcept { return true; }
    static bool accepts_resource(Value* p, std::pmr::memory_resource& r) noexcept {
        return p && Selection{p}.control().resource() == &r;
    }
    static bool compatible(Value* a, Value* b) noexcept {
        return Selection{a}.part_count() == Selection{b}.part_count();
    } // Logical equivalence is the converter's separate obligation.
    static Footprint footprint(Value* p) noexcept {
        const auto c = Selection{p}.control();
        return {orbital::object_detail::representation_bytes(c, orbital::PartOwners::independent_allocation), 0};
    }
    static orbital::Result<orbital::Part<C>*> export_part(Selection s, std::size_t i,
            orbital::ObjectBinding b, ExportPolicy p) noexcept {
        return orbital::object_detail::retain_part(s, i, b, p);
    }
};
using EncodedLayout = Layout<EncodedBase>;
using OverlayLayout = Layout<OverlayBase>;
using NativeLayout = Layout<NativeBase>;
using EncodedStore = orbital::ObjectStore<Physical<EncodedLayout>>;
using OverlayStore = orbital::ObjectStore<Physical<OverlayLayout>>;
using NativeStore = orbital::ObjectStore<Physical<NativeLayout>>;
static_assert(std::is_trivially_copyable_v<EncodedLayout> &&
              std::is_trivially_copyable_v<OverlayLayout> &&
              std::is_trivially_copyable_v<NativeLayout>);

struct Bytes { const std::byte* data{}; std::size_t size{}; };
struct QueryResult { u128 sum{}; u64 count{}, present{}; friend bool operator==(QueryResult, QueryResult) = default; };
struct QueryCache {
    fe::code_sum sum;
    fe::code_count count;
    explicit QueryCache(u64 threshold) : count(threshold) {}
};
inline const GroupHeader& header(const EncodedBase& b) { return b.header; }
inline const GroupHeader& header(const NativeBase& b) { return b.header; }
fe::borrowed_block borrow(const EncodedBase&, Bytes, unsigned block);
LogicalRow point(const EncodedBase&, Bytes values, Bytes flags, unsigned row);
LogicalRow point(const NativeBase&, Bytes values, Bytes flags, unsigned row);
QueryResult query(const EncodedBase&, Bytes values, Bytes flags, std::uint16_t selected, u64 threshold, QueryCache&);
QueryResult query(const NativeBase&, Bytes values, Bytes flags, std::uint16_t selected, u64 threshold, QueryCache&);
QueryResult query_overlay(const EncodedBase&, Bytes values, Bytes flags, const Overlay&, Bytes,
                          std::uint16_t selected, u64 threshold, QueryCache&);
bool admit(const EncodedBase&, Bytes values, Bytes flags);
bool admit(const NativeBase&, Bytes values, Bytes flags);
bool admit(const Overlay&, Bytes);
} // namespace frame_lifecycle::phase
