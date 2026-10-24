#pragma once

#include "description.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace frame3_owner {

// Runtime image metadata, retained with the material's interpretation. Node
// identities and coordinate laws come from the canonical program; cardinality
// and byte placement come from this image. This is not another wire format.
struct section_location {
    std::uint32_t owner_part{}; // Original material ordinal, not a projected Part's local index.
    std::size_t offset{}, extent{};
    bool operator==(const section_location &) const = default;
};
struct stream_manifest {
    std::uint16_t root_node{};
    std::size_t rows{};
    std::vector<section_location> groups; // Stream-local group index is implicit.
    bool operator==(const stream_manifest &) const = default;
};
struct row_range {
    std::size_t first{}, count{};
};
struct section_key {
    std::uint16_t root_node{};
    std::size_t group{};
    bool operator==(const section_key &) const = default;
};
enum class manifest_error {
    description, unsupported, range, streams, rows, groups, placement,
    overlap, unavailable, resources,
};
std::string_view describe(manifest_error) noexcept;
// Check this experiment's executable shape without binding any image or
// choosing runtime cardinality/owner placement.
std::expected<void, manifest_error> validate_program_shape(const description &) noexcept;

// The finite admitted shapes are pack, bound->pack, and intervals_sizes at
// the root with distinct basic delta/checkpoint subtrees. Root stream identity
// is the program root; the separate checkpoint stream identity is its second
// child's node. A wrapper around intervals, relative ends, and other valid
// placeholders remain unsupported. Each basic subtree uses existing
// binding_recipe admission; no leaf format or algorithm is implemented here.
// Geometry is the description's experimental block256/group16; root_rows is
// at most65536, and zero rows means zero groups in every expected stream.
// The manifest contains every expected stream exactly once, in any order.
// Independent sections may co-pack in one Part but cannot overlap. Unclaimed
// Part bytes are permitted; extents are owner declarations, not read grants.
std::expected<void, manifest_error> validate_manifest(const description &, std::size_t root_rows,
    std::span<const std::size_t> part_extents, std::span<const stream_manifest>) noexcept;

// Metadata-only closure. Each nonempty request conservatively requires its
// root groups and every checkpoint group covering [first/G,(last/G)+1).
// Empty ranges require no sections; all ranges are checked before returning.
// Results are deduplicated and sorted by (root_node, group). This validates
// program/cardinality/group counts; placement remains validate_manifest's job.
std::expected<std::vector<section_key>, manifest_error> required_sections(const description &,
    std::size_t root_rows, std::span<const stream_manifest>, std::span<const row_range>);

// Call before resolving keys into addresses. Required keys come from a
// validated closure; available keys are an owner's explicitly granted set,
// not query demand and not a claim inferred from extent/physical co-location.
// Extra or duplicate available keys do not grant a missing required key.
std::expected<void, manifest_error> admit_sections(std::span<const section_key> required,
    std::span<const section_key> available) noexcept;

} // namespace frame3_owner
