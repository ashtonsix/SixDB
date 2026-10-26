#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace ikea::frame {

// A Frame composition description, framed by Ikea's five-byte
// description envelope (tag Fr, version 1). It describes no data block and
// replaces no leaf format. All integer fields on wire are little-endian.
//
// Body: domain:u8, block_rows:u16, group_blocks:u16, nodes:u16, root:u16;
// then each node: kind:u8, parameter:u16, heads:u16, edges:u8;
// then its edges: child:u16, coordinate:u8, in semantic operand order.
// A node's vector index is its declared identity. Child identities are lower
// than their parent. Canonical encoding preserves those identities, including
// sharing; it neither duplicates shared children nor merges distinct ones.
// Every node is reachable from root. This is not graph-isomorphism numbering.
//
// The limits below bound one description/binding unit, not a whole column.
// The initial image geometry is 256 rows/block and 16 blocks/group. Placeholder nodes describe shape, sharing and
// coordinate relationships only; they are NOT an executable leaf byte law.
inline constexpr std::size_t maximum_nodes = 256;
inline constexpr std::size_t maximum_edges = 1024;
inline constexpr std::size_t maximum_children = 2;

enum class logical_domain : std::uint8_t { uint64 = 1 };
enum class pack_preset : std::uint8_t { compact = 0, bulk_x86 = 1, bulk_arm = 2 };
enum class pack_geometry : std::uint8_t { local, striped };
// Fr/version1 freezes these resolved byte-law families. The CPU-oriented names
// are construction conveniences, not a promise to consult a future version of
// SeriesPack's tuning presets when decoding existing material.
// Apply this rule to remaining payload K-H, after the leaf's actual image has
// established H<=K. K is image data; H is an independent pack-node parameter.
constexpr pack_geometry packing_geometry(pack_preset p, unsigned payload) noexcept {
    const bool narrow = payload > 0 && payload < 8;
    const bool x86 = narrow || payload == 12 || payload == 20;
    const bool arm = x86 || payload == 10 || payload == 14 || payload == 15;
    return (p == pack_preset::bulk_x86 && x86) || (p == pack_preset::bulk_arm && arm)
        ? pack_geometry::striped : pack_geometry::local;
}
enum class node_kind : std::uint8_t {
    pack = 1,
    bound = 2,
    intervals_sizes = 3,
    intervals_relative_ends = 4,
    bec = 5,
    presence = 6,
    tuple_record = 7,
    tuple_field = 8,
    split = 9,
    runs = 10,
};
constexpr bool is_intervals(node_kind kind) noexcept {
    return kind == node_kind::intervals_sizes || kind == node_kind::intervals_relative_ends;
}
enum class coordinate : std::uint8_t {
    identity = 0,
    checkpoints = 1,
    present_rank = 2,
    record_field = 3,
    run_ordinal = 4,
};
struct edge {
    std::uint16_t child{};
    coordinate mapping{coordinate::identity};
    bool operator==(const edge &) const = default;
};
struct node {
    node_kind kind{node_kind::pack};
    // pack: preset; intervals: checkpoint spacing; Bec: 256 or 4096;
    // tuple_record: field count; tuple_field: field index;
    // split: low-bit count; bound/presence: zero; runs: zero means runs
    // terminate at every description.block_rows boundary. Its two ordered
    // children store sizes-minus-one and values at the same data-dependent run
    // ordinal. Their common cardinality and block-to-run map belong to image
    // metadata; no run placement or executable image law is admitted here.
    std::uint16_t parameter{};
    // Pack only: 0,8,16 leading bits, orthogonal to its preset. Syntax does not
    // establish per-block K or H<=K. H8/H16 are interpretation placeholders;
    // executable binding refuses them rather than treating them as H0.
    // Every other node requires zero. H0 descriptions retain their old bytes.
    // Fit policy, addresses and bound operations are not decoding parameters.
    std::uint16_t heads{};
    std::vector<edge> children;
    bool operator==(const node &) const = default;
};
struct description {
    logical_domain domain{logical_domain::uint64};
    std::uint16_t block_rows{256};
    std::uint16_t group_blocks{16};
    std::uint16_t root{};
    std::vector<node> nodes;
    bool operator==(const description &) const = default;
};
enum class description_error {
    envelope, version, domain, geometry, limit, kind, parameters,
    coordinates, topology, root, unreachable, truncated, trailing,
    capacity, unsupported,
};
std::string_view describe(description_error) noexcept;

// Syntax/shape validation is distinct from executable binding admission.
// It checks local coordinate laws, not data cardinalities or group contents.
[[nodiscard]] std::expected<void, description_error> validate(const description &) noexcept;
[[nodiscard]] std::expected<std::size_t, description_error> encoded_size(const description &) noexcept;
// On refusal, output is untouched. On success exactly the returned prefix is
// written; callers pass that exact prefix to decode.
[[nodiscard]] std::expected<std::size_t, description_error> encode(const description &, std::span<std::byte> output) noexcept;
[[nodiscard]] std::expected<std::vector<std::byte>, description_error> encode(const description &);
[[nodiscard]] std::expected<description, description_error> decode(std::span<const std::byte>);

} // namespace ikea::frame
