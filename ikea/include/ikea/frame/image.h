#pragma once

#include <ikea/frame/description.h>
#include <ikea/frame/types.h>
#include <memory>
#include <utility>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace ikea::frame {

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
// Check the initial executable shape without binding any image or
// choosing runtime cardinality/owner placement.
[[nodiscard]] std::expected<void, manifest_error> validate_program_shape(const description &) noexcept;

// The finite admitted shapes are pack, bound->pack, and either intervals form
// at the root with distinct basic field/checkpoint subtrees. Root stream identity
// is the program root; the separate checkpoint stream identity is its second
// child's node. A wrapper around intervals and other valid
// placeholders remain unsupported. Each basic subtree uses existing
// binding_recipe admission; no leaf format or algorithm is implemented here.
// Geometry is the description's initial block256/group16; root_rows is
// at most65536, and zero rows means zero groups in every expected stream.
// The manifest contains every expected stream exactly once, in any order.
// Independent sections may co-pack in one Part but cannot overlap. Unclaimed
// Part bytes are permitted; extents are owner declarations, not read grants.
[[nodiscard]] std::expected<void, manifest_error> validate_manifest(const description &, std::size_t root_rows,
    std::span<const std::size_t> part_extents, std::span<const stream_manifest>) noexcept;

// Metadata-only closure. Each nonempty request conservatively requires its
// root groups and every checkpoint group covering [first/G,(last/G)+1).
// Empty ranges require no sections; all ranges are checked before returning.
// Results are deduplicated and sorted by (root_node, group). This validates
// program/cardinality/group counts; placement remains validate_manifest's job.
[[nodiscard]] std::expected<std::vector<section_key>, manifest_error> required_sections(const description &,
    std::size_t root_rows, std::span<const stream_manifest>, std::span<const row_range>);

// Call before resolving keys into addresses. Required keys come from a
// validated closure; available keys are an owner's explicitly granted set,
// not query demand and not a claim inferred from extent/physical co-location.
// Extra or duplicate available keys do not grant a missing required key.
[[nodiscard]] std::expected<void, manifest_error> admit_sections(std::span<const section_key> required,
    std::span<const section_key> available) noexcept;

struct borrowed_part {
    std::uint32_t owner_part{};
    std::span<const std::byte> bytes;
};
/// Borrowed metadata and placed bytes. Part extents bound placement, while
/// available explicitly grants initialized, read-isolated section bytes.
/// Query selections never grant access. Unavailable sections need no address.
struct image_view {
    std::size_t rows{};
    std::span<const std::size_t> part_extents;
    std::span<const stream_manifest> streams;
    std::span<const borrowed_part> parts;
    std::span<const section_key> available;
};
/// Metadata-only admission; reads no payload. All borrowed metadata stays live
/// until validation finishes. Payload backing stays immutable, placed and live
/// through every validated_image and reader that borrows it.
[[nodiscard]] std::expected<image_view, error> attach_image(const description&, image_view);
namespace detail { struct image_state; }
class reader;
class validated_image {
    std::shared_ptr<const detail::image_state> state_;
    explicit validated_image(std::shared_ptr<const detail::image_state> state) : state_(std::move(state)) {}
    friend std::expected<validated_image, error> validate_image(const description&, image_view);
    friend class reader;
  public:
    validated_image(const validated_image&) = default;
    validated_image(validated_image&&) noexcept = default;
    validated_image& operator=(const validated_image&) = default;
    validated_image& operator=(validated_image&&) noexcept = default;
    std::size_t size() const noexcept;
};
/// Copies interpretation/metadata, validates granted contents and cross-child
/// premises once, and borrows payload. Missing dependencies may attach, but
/// operations requiring them refuse before writing output. Conservative false
/// evidence disables laws; unsound true evidence refuses validation.
[[nodiscard]] std::expected<validated_image, error> validate_image(const description&, image_view);
} // namespace ikea::frame
