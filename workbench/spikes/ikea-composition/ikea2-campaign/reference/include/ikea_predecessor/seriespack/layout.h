#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <numeric>
#include <type_traits>

namespace ikea_predecessor::seriespack {

/// Version of the encoded bit layout, independent of schema or visible data version.
inline constexpr unsigned wire_version = 1;
/// Payload bit arrangement. Enumerators are C++ choices, not stable wire tags.
enum class geometry { local8, striped };
/// Construction policy; ARM recipes choose layouts, not a required reader ISA.
enum class preset { compact, bulk, bulk_arm, filter, filter_arm };

enum class error {
    invalid_description,
    invalid_range,
    insufficient_storage,
    invalid_stride,
    misaligned_storage,
    overlapping_storage,
    overflow,
    invalid_value,
    insufficient_effect_storage,
    unsupported,
    invalid_selection,
    invalid_output_type,
};

/// Unsigned [0, 2^width) wire domain; width 64 admits every u64 bit pattern.
/// No length, addresses, signed/NULL meaning or execution target is stored here.
struct description {
    /// Logical value width in bits, 1..64, including any heads.
    unsigned width;
    /// Leading bits stored in separate byte planes: 0, 8 or 16, at most width.
    unsigned head_bits;
    /// Geometry of the low (width - head_bits) payload bits.
    geometry storage;
    unsigned version = wire_version;

    constexpr bool operator==(const description&) const = default;
};

/// Whether a remaining payload width, in bits, has a defined striped geometry.
[[nodiscard]] constexpr bool supports_stripes(unsigned width) noexcept {
    return (width >= 1 && width <= 7) || width == 10 || width == 12 ||
           width == 14 || width == 15 || width == 20;
}

/// Checks version, widths and supported geometry without inspecting storage.
[[nodiscard]] constexpr std::expected<description, error>
validate(description layout) noexcept {
    if (layout.version != wire_version || layout.width < 1 || layout.width > 64 ||
        (layout.head_bits != 0 && layout.head_bits != 8 && layout.head_bits != 16) ||
        layout.head_bits > layout.width) {
        return std::unexpected(error::invalid_description);
    }
    switch (layout.storage) {
        case geometry::local8: return layout;
        case geometry::striped:
            if (supports_stripes(layout.width - layout.head_bits)) return layout;
            break;
    }
    return std::unexpected(error::invalid_description);
}

/// Resolves explicit width/heads for construction without reading values.
/// Retain the result with its bytes; changing a recipe does not reinterpret them.
[[nodiscard]] constexpr std::expected<description, error>
resolve(unsigned width, preset choice = preset::compact, unsigned head_bits = 0) noexcept {
    auto candidate = validate({width, head_bits, geometry::local8});
    if (!candidate) return candidate;
    const unsigned payload = width - head_bits;
    bool arm = false;
    switch (choice) {
        case preset::compact: return candidate;
        case preset::filter_arm: arm = true; [[fallthrough]];
        case preset::filter:
            if (head_bits == 0) return std::unexpected(error::invalid_description);
            break;
        case preset::bulk_arm: arm = true; break;
        case preset::bulk: break;
        default: return std::unexpected(error::invalid_description);
    }
    if ((payload >= 1 && payload <= 7) || payload == 12 || payload == 20 ||
        (arm && (payload == 10 || payload == 14 || payload == 15))) {
        candidate->storage = geometry::striped;
    }
    return candidate;
}

/// Remaining low bits per value; may be zero. Requires a validated description.
[[nodiscard]] constexpr unsigned payload_width(description layout) noexcept {
    return layout.width - layout.head_bits;
}

/// Positions per storage tile, independent of execution grain. Requires valid layout.
[[nodiscard]] constexpr std::size_t tile_values(description layout) noexcept {
    if (layout.storage == geometry::local8) return 8;
    const unsigned tail = payload_width(layout) % 8;
    return 32 * (8 / std::gcd(tail, 8u));
}

/// Payload bytes per tile, excluding heads/gaps; zero if all-head. Requires valid layout.
[[nodiscard]] constexpr std::size_t tile_bytes(description layout) noexcept {
    return tile_values(layout) * payload_width(layout) / 8;
}

/// W counts low payload bits, excluding heads; W == 0 is an empty local payload.
template<unsigned W, geometry G = geometry::local8>
struct payload_layout {
    static_assert(W <= 64);
    static_assert(G == geometry::local8 || (G == geometry::striped && supports_stripes(W)));
    static constexpr unsigned width = W;
    static constexpr unsigned body_bytes = W / 8;
    static constexpr unsigned tail_bits = W % 8;
    static constexpr geometry storage = G;
    static constexpr std::size_t tile_values =
        G == geometry::local8 ? 8 : 32 * (8 / std::gcd(tail_bits, 8u));
    static constexpr std::size_t tile_bytes = tile_values * W / 8;
};

/// Smallest standard unsigned scalar for K <= 64; does not select native lanes.
template<unsigned K>
using scalar_for_width = std::conditional_t<(K <= 8), std::uint8_t,
    std::conditional_t<(K <= 16), std::uint16_t,
    std::conditional_t<(K <= 32), std::uint32_t, std::uint64_t>>>;

/// Static preset resolution; K is full width and H leading head width, in bits.
template<unsigned K, preset P = preset::compact, unsigned H = 0>
struct format {
    static_assert(resolve(K, P, H).has_value(), "invalid SeriesPack format");
    static constexpr description layout = *resolve(K, P, H);
    using scalar_type = scalar_for_width<K>;
    using payload = payload_layout<K - H, layout.storage>;
};

/// Static explicit description; G arranges the K-H payload bits.
template<unsigned K, geometry G, unsigned H = 0>
struct static_format {
    static_assert(validate({K, H, G}).has_value(), "invalid SeriesPack format");
    static constexpr description layout{K, H, G};
    using scalar_type = scalar_for_width<K>;
    using payload = payload_layout<K - H, G>;
};

/// Complete-tile requirements. Envelopes include gaps between tiles; byte counts
/// include only occupied storage. All sizes except tiles/capacity are in bytes.
struct extent_info {
    std::size_t tiles{};
    /// Logical positions covered by complete tiles, including unused final slack.
    std::size_t capacity{};
    std::size_t payload_envelope{};
    std::size_t payload_bytes{};
    std::array<std::size_t, 2> head_envelopes{};
    std::array<std::size_t, 2> head_bytes{};
};

/// Storage for n logical values, rounded to complete tiles; strides are in bytes.
/// Checks layout, strides and overflow without inspecting storage. Empty arrays
/// and absent streams have zero extents; byte counts exclude foreign stride gaps.
[[nodiscard]] constexpr std::expected<extent_info, error>
required_extents(description layout, std::size_t n, std::size_t payload_stride,
                 std::array<std::size_t, 2> head_strides = {}) noexcept {
    if (!validate(layout)) return std::unexpected(error::invalid_description);
    extent_info result;
    if (n == 0) return result;
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    const auto values = tile_values(layout);
    const auto bytes = tile_bytes(layout);
    result.tiles = n / values + (n % values != 0);
    if (result.tiles > maximum / values) return std::unexpected(error::overflow);
    result.capacity = result.tiles * values;

    const auto envelope = [&](std::size_t stride, std::size_t bytes_per_tile)
        -> std::expected<std::size_t, error> {
        if (stride < bytes_per_tile) return std::unexpected(error::invalid_stride);
        if (result.tiles - 1 > (maximum - bytes_per_tile) / stride)
            return std::unexpected(error::overflow);
        return (result.tiles - 1) * stride + bytes_per_tile;
    };
    if (bytes != 0) {
        if (layout.storage == geometry::striped && payload_stride % 32 != 0)
            return std::unexpected(error::invalid_stride);
        const auto size = envelope(payload_stride, bytes);
        if (!size) return std::unexpected(size.error());
        if (result.tiles > maximum / bytes) return std::unexpected(error::overflow);
        result.payload_envelope = *size;
        result.payload_bytes = result.tiles * bytes;
    }
    for (unsigned h = 0; h < layout.head_bits / 8; ++h) {
        const auto size = envelope(head_strides[h], values);
        if (!size) return std::unexpected(size.error());
        result.head_envelopes[h] = *size;
        result.head_bytes[h] = result.capacity;
    }
    return result;
}

} // namespace ikea_predecessor::seriespack
