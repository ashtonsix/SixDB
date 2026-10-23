#pragma once
#include <ikea/intervals/search.h>
#include <ikea/streampack/types.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

// StreamPack owns this compact metadata/retirement image. Common borrowed
// search/arithmetic bodies live in Intervals; no bitmap policy is exported there.
namespace ikea::streampack::detail::compact {
using ikea::intervals::delta_source;
using ikea::intervals::direct_source;
using ikea::intervals::edge;
using ikea::intervals::interval;
using ikea::intervals::selection;
using ikea::intervals::relative_end_source;
namespace detail { using namespace ikea::intervals::detail; }
namespace native { using namespace ikea::intervals::native; }
}

namespace ikea::streampack::detail::compact {
using ikea::streampack::byte;
using ikea::streampack::bytes;
using ikea::streampack::error;
using ikea::streampack::geometry;
inline constexpr unsigned interval_capacity = slot_capacity;
inline constexpr unsigned maximum_interval_size = 65535;

/// Schema facts, independent of execution ISA. The extent limit can seal a source
/// before its rank capacity. Omitted facts cannot subsequently be mutated.
struct description {
    geometry family = geometry::sizes;
    std::uint16_t maximum = maximum_interval_size;
    std::uint32_t extent_limit = interval_capacity * maximum;
    std::optional<std::uint16_t> fixed_size = std::nullopt;
    bool immortal = false;
    bool operator==(const description &) const = default;
};

namespace detail {
/// Prepared geometry shared by standalone and enclosing value recipes. No
/// duplicated schema, U placement, source pointers or consumer policy.
struct geometry_sizes {
    std::uint32_t directory;
    std::uint16_t tombstones;
    std::uint8_t width_bytes, offset_bytes, control_bytes, stride;
    bool fixed;
    [[nodiscard]] static std::expected<geometry_sizes, error> prepare(description);
};
} // namespace detail

/// Immutable schema preparation, shared by blocks. Persist encode()'s portable
/// description, never this object's native representation or borrowed pointers.
class layout {
    description description_;
    detail::geometry_sizes geometry_;
    layout() = default;

  public:
    [[nodiscard]] static std::expected<layout, error> make(description);
    const description &schema() const noexcept { return description_; }
    static constexpr unsigned capacity() noexcept { return interval_capacity; }
    unsigned extent_limit() const noexcept { return description_.extent_limit; }
    bool fixed() const noexcept { return geometry_.fixed; }
    unsigned fixed_width() const noexcept { return description_.fixed_size.value_or(0); }
    bool has_tombstones() const noexcept { return geometry_.tombstones != 0; }
    bool has_extent() const noexcept { return geometry_.control_bytes > 2; }
    unsigned width_bytes() const noexcept { return geometry_.width_bytes; }
    unsigned offset_bytes() const noexcept { return geometry_.offset_bytes; }
    unsigned control_bytes() const noexcept { return geometry_.control_bytes; }
    unsigned directory_bytes() const noexcept { return geometry_.directory; }
    unsigned tombstone_bytes() const noexcept { return geometry_.tombstones; }
    unsigned geometry_offset() const noexcept { return geometry_.tombstones; }
    unsigned group_stride() const noexcept { return geometry_.stride; }
    unsigned lengths_alignment() const noexcept { return geometry_.width_bytes; }
    unsigned geometry_lengths_offset() const noexcept { return 0; }
    /// Initialized/readable reservation widths include complete G16 groups.
    unsigned lengths_bytes(unsigned assigned) const noexcept;
    unsigned lengths_bytes() const noexcept { return lengths_bytes(interval_capacity); }
    /// Conservative journal slots; admission can accept a smaller exact bound.
    unsigned append_effect_capacity() const noexcept { return 8; }
    static constexpr std::size_t encoded_size() noexcept { return 13; }
    [[nodiscard]] std::expected<std::size_t, error> encode(ikea::mutable_bytes) const;
    [[nodiscard]] static std::expected<layout, error> decode(bytes);
};
} // namespace ikea::streampack::detail::compact
