#pragma once
#include <ikea/boundlist/types.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

namespace ikea::boundlist {
using byte = std::byte;
using bytes = std::span<const byte>;
inline constexpr unsigned interval_capacity = 256;
inline constexpr unsigned maximum_interval_size = 65535;
enum class geometry : std::uint8_t {
    absolute_ends,
    sizes,
    relative_ends,
    direct = absolute_ends,
    width = sizes // Source-compatible names for the same two encodings.
};

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
    bool fixed, legacy;
    [[nodiscard]] static std::expected<geometry_sizes, error> prepare(description,
                                                                      bool legacy = false);
};
} // namespace detail

/// Immutable schema preparation, shared by blocks. Persist encode()'s portable
/// description, never this object's native representation or borrowed pointers.
class layout {
    description description_;
    detail::geometry_sizes geometry_;
    layout() = default;
    [[nodiscard]] static std::expected<layout, error> prepare_impl(description, bool legacy);

  public:
    [[nodiscard]] static std::expected<layout, error> prepare(description);
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
    std::array<byte, 8> encode() const noexcept;
    [[nodiscard]] static std::expected<layout, error> decode(bytes);
};
} // namespace ikea::boundlist
