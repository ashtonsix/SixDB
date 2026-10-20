#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

namespace ikea::boundlist::sidecar {
using byte = std::byte;
using bytes = std::span<const byte>;
inline constexpr unsigned slot_capacity = 256;
enum class error {
    description,
    capacity,
    alignment,
    alias,
    contents,
    rank,
    retired,
    value,
    effects,
    range
};
enum class geometry : std::uint8_t { direct, width };

/// Schema facts, independent of execution ISA. The extent limit can seal a source
/// before its rank capacity. Omitted facts cannot subsequently be mutated.
struct description {
    geometry family = geometry::width;
    std::uint16_t maximum = 65535;
    std::uint32_t extent_limit = slot_capacity * maximum;
    std::optional<std::uint16_t> fixed_reservation = std::nullopt;
    bool immortal = false;
    bool operator==(const description&) const = default;
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
    [[nodiscard]] static std::expected<layout, error> prepare(description);
    const description& schema() const noexcept {
        return description_;
    }
    static constexpr unsigned capacity() noexcept {
        return slot_capacity;
    }
    unsigned extent_limit() const noexcept {
        return description_.extent_limit;
    }
    bool fixed() const noexcept {
        return geometry_.fixed;
    }
    unsigned fixed_width() const noexcept {
        return description_.fixed_reservation.value_or(0);
    }
    bool has_tombstones() const noexcept {
        return geometry_.tombstones != 0;
    }
    bool has_extent() const noexcept {
        return geometry_.control_bytes > 2;
    }
    unsigned width_bytes() const noexcept {
        return geometry_.width_bytes;
    }
    unsigned offset_bytes() const noexcept {
        return geometry_.offset_bytes;
    }
    unsigned control_bytes() const noexcept {
        return geometry_.control_bytes;
    }
    unsigned directory_bytes() const noexcept {
        return geometry_.directory;
    }
    unsigned tombstone_bytes() const noexcept {
        return geometry_.tombstones;
    }
    unsigned geometry_offset() const noexcept {
        return geometry_.tombstones;
    }
    unsigned group_stride() const noexcept {
        return geometry_.stride;
    }
    unsigned lengths_alignment() const noexcept {
        return geometry_.width_bytes;
    }
    /// Initialized/readable reservation widths include complete G16 groups.
    unsigned lengths_bytes(unsigned assigned) const noexcept;
    unsigned lengths_bytes() const noexcept {
        return lengths_bytes(slot_capacity);
    }
    /// Conservative journal slots; admission can accept a smaller exact bound.
    unsigned append_effect_capacity() const noexcept {
        return 8;
    }
    std::array<byte, 8> encode() const noexcept;
    [[nodiscard]] static std::expected<layout, error> decode(bytes);
};
} // namespace ikea::boundlist::sidecar
