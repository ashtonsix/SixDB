#pragma once
#include <ikea/streampack/types.h>
#include <ikea/streampack/detail/compact/description.h>

namespace ikea::streampack {
enum class placement : std::uint8_t { grouped, separated };

/// Geometry plus an optional used-length companion. With full, U is derived
/// from R and value mutations must fill the reservation exactly.
struct description {
    geometry family = geometry::sizes;
    placement lengths = placement::grouped;
    std::uint16_t maximum = 65535;
    std::uint32_t byte_limit = slot_capacity * maximum;
    std::optional<std::uint16_t> fixed_reservation = std::nullopt;
    bool full = false;
    bool immortal = false;
    bool operator==(const description &) const = default;
};

/// Shared immutable value law. StreamPack owns compact geometry preparation,
/// retirement, U placement and the complete value interpretation.
class layout {
    description description_;
    ikea::streampack::detail::compact::detail::geometry_sizes geometry_;
    unsigned used_offset_, stride_;
    std::uint8_t alignment_, used_bytes_;
    bool used_;
    layout(description d, ikea::streampack::detail::compact::detail::geometry_sizes g)
        : description_(d), geometry_(g) {}
    [[nodiscard]] static std::expected<layout, error> prepare_impl(description, bool legacy);

  public:
    [[nodiscard]] static std::expected<layout, error> prepare(description);
    const description &schema() const noexcept { return description_; }
    static constexpr unsigned capacity() noexcept { return slot_capacity; }
    unsigned extent_limit() const noexcept { return description_.byte_limit; }
    bool fixed() const noexcept { return geometry_.fixed; }
    unsigned fixed_width() const noexcept { return description_.fixed_reservation.value_or(0); }
    bool has_used() const noexcept { return used_; }
    bool has_tombstones() const noexcept { return geometry_.tombstones != 0; }
    bool has_extent() const noexcept { return geometry_.control_bytes > 2; }
    unsigned width_bytes() const noexcept { return geometry_.width_bytes; }
    unsigned offset_bytes() const noexcept { return geometry_.offset_bytes; }
    unsigned control_bytes() const noexcept { return geometry_.control_bytes; }
    unsigned directory_bytes() const noexcept { return geometry_.directory; }
    unsigned tombstone_bytes() const noexcept { return geometry_.tombstones; }
    unsigned geometry_offset() const noexcept { return geometry_.tombstones; }
    unsigned used_offset() const noexcept { return used_offset_; }
    unsigned used_bytes() const noexcept { return used_bytes_; }
    unsigned geometry_lengths_offset() const noexcept { return 0; }
    unsigned used_group_stride() const noexcept {
        return description_.lengths == placement::grouped ? stride_ : 16 * used_bytes_;
    }
    unsigned group_stride() const noexcept { return stride_; }
    unsigned lengths_alignment() const noexcept { return alignment_; }
    unsigned lengths_bytes(unsigned assigned) const noexcept;
    unsigned lengths_bytes() const noexcept { return lengths_bytes(slot_capacity); }
    unsigned append_effect_capacity() const noexcept { return 8; }
    unsigned pack_effect_capacity(unsigned count) const noexcept { return count * 4 + 1; }
    std::array<byte, 8> encode() const noexcept;
    [[nodiscard]] static std::expected<layout, error> decode(bytes);
};
namespace detail {
template <class Width> unsigned used_position(const layout &l, unsigned rank) noexcept {
    return l.used_offset() + (rank / 16) * l.used_group_stride() + (rank % 16) * sizeof(Width);
}
} // namespace detail
} // namespace ikea::streampack
