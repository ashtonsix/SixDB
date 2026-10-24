#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace frame3_owner {

// An experimental Frame composition description, framed by Ikea's five-byte
// description envelope (tag Fr, version 1). It describes no data block and
// replaces no leaf format. All integer fields on wire are little-endian.
//
// Body: domain:u8, block_rows:u16, group_blocks:u16, nodes:u16, root:u16;
// then each node: kind:u8, parameter:u16, reserved:u16, edges:u8;
// then its edges: child:u16, coordinate:u8, in semantic operand order.
// A node's vector index is its declared identity. Child identities are lower
// than their parent. Canonical encoding preserves those identities, including
// sharing; it neither duplicates shared children nor merges distinct ones.
// Every node is reachable from root. This is not graph-isomorphism numbering.
//
// The limits and geometry below are bounds for this experiment, not promises
// for a future public Frame API. Placeholder nodes describe shape, sharing and
// coordinate relationships only; they are NOT an executable leaf byte law.
inline constexpr std::size_t maximum_nodes = 256;
inline constexpr std::size_t maximum_edges = 1024;
inline constexpr std::size_t maximum_children = 2;

enum class logical_domain : std::uint8_t { uint64 = 1 };
enum class pack_preset : std::uint8_t { compact = 0, bulk_x86 = 1, bulk_arm = 2 };
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
};
enum class coordinate : std::uint8_t {
    identity = 0,
    checkpoints = 1,
    present_rank = 2,
    record_field = 3,
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
    // split: low-bit count; bound/presence: zero.
    std::uint16_t parameter{};
    // Reserved, required zero. Fit penalties, exception caps/slack, costs,
    // addresses and bound operations do not belong in this description.
    std::uint16_t reserved{};
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
enum class error {
    envelope, version, domain, geometry, limit, kind, parameters,
    coordinates, topology, root, unreachable, truncated, trailing,
    capacity, unsupported,
};
std::string_view describe(error) noexcept;

// Syntax/shape validation is distinct from executable binding admission.
// It checks local coordinate laws, not data cardinalities or group contents.
std::expected<void, error> validate(const description &) noexcept;
std::expected<std::size_t, error> encoded_size(const description &) noexcept;
// On refusal, output is untouched. On success exactly the returned prefix is
// written; callers pass that exact prefix to decode.
std::expected<std::size_t, error> encode(const description &, std::span<std::byte> output) noexcept;
std::expected<std::vector<std::byte>, error> encode(const description &);
std::expected<description, error> decode(std::span<const std::byte>);

struct bound_pack_recipe {
    pack_preset preset{pack_preset::compact};
    bool bounded{};
    bool operator==(const bound_pack_recipe &) const = default;
};
// Refuses valid placeholders before any payload attachment. The owner builds
// the existing proto pack (placement=16), optionally wrapping it in bound;
// its separately admitted fit policy supplies penalty/cap/slack.
std::expected<bound_pack_recipe, error> binding_recipe(const description &) noexcept;

} // namespace frame3_owner
