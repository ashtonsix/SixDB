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
enum class placement : std::uint8_t { grouped, separated };

/// Schema facts, independent of execution ISA. The byte limit can seal a block
/// before its rank capacity. Omitted facts cannot subsequently be mutated.
struct description {
    geometry family = geometry::width;
    placement lengths = placement::grouped;
    std::uint16_t maximum = 65535;
    std::uint32_t byte_limit = slot_capacity * maximum;
    std::optional<std::uint16_t> fixed_reservation = std::nullopt;
    bool full = false;
    bool immortal = false;
    bool operator==(const description&) const = default;
};

/// Immutable schema preparation, shared by blocks. Persist encode()'s portable
/// description, never this object's native representation or borrowed pointers.
class layout {
    description description_;
    std::uint32_t directory_, lengths_, geometry_offset_, used_offset_, stride_;
    std::uint16_t tombstones_;
    std::uint8_t width_bytes_, offset_bytes_, control_bytes_, alignment_;
    bool fixed_, used_;
    layout() = default;

  public:
    [[nodiscard]] static std::expected<layout, error> prepare(description);
    const description& schema() const noexcept {
        return description_;
    }
    static constexpr unsigned capacity() noexcept {
        return slot_capacity;
    }
    bool fixed() const noexcept {
        return fixed_;
    }
    unsigned fixed_width() const noexcept {
        return description_.fixed_reservation.value_or(0);
    }
    bool has_used() const noexcept {
        return used_;
    }
    bool has_tombstones() const noexcept {
        return tombstones_ != 0;
    }
    bool has_extent() const noexcept {
        return control_bytes_ > 2;
    }
    unsigned width_bytes() const noexcept {
        return width_bytes_;
    }
    unsigned offset_bytes() const noexcept {
        return offset_bytes_;
    }
    unsigned control_bytes() const noexcept {
        return control_bytes_;
    }
    unsigned directory_bytes() const noexcept {
        return directory_;
    }
    unsigned tombstone_bytes() const noexcept {
        return tombstones_;
    }
    unsigned geometry_offset() const noexcept {
        return geometry_offset_;
    }
    unsigned used_offset() const noexcept {
        return used_offset_;
    }
    unsigned group_stride() const noexcept {
        return stride_;
    }
    unsigned lengths_alignment() const noexcept {
        return alignment_;
    }
    /// Initialized/readable lengths include complete groups of sixteen. In the
    /// separated Width recipe the U origin follows full-capacity R storage.
    unsigned lengths_bytes(unsigned assigned) const noexcept;
    unsigned lengths_bytes() const noexcept {
        return lengths_;
    }
    /// Conservative journal slots; admission can accept a smaller exact bound.
    unsigned append_effect_capacity() const noexcept {
        return 8;
    }
    unsigned pack_effect_capacity(unsigned count) const noexcept {
        return count * 4 + 1;
    }
    std::array<byte, 8> encode() const noexcept;
    [[nodiscard]] static std::expected<layout, error> decode(bytes);
};
} // namespace ikea::boundlist::sidecar
