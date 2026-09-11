#include <ikea/seriespack/view.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <numeric>

namespace ikea::seriespack {
namespace {

struct occupied_stream {
    std::uintptr_t base;
    std::size_t stride;
    std::size_t width;
    std::size_t tiles;
    std::size_t envelope;
};

// Admission is cold. Wide intermediates let address differences and Euclidean
// arithmetic stay defined over the entire size_t address space.
using wide_int = __int128;
using wide_uint = unsigned __int128;

std::size_t modular_inverse(std::size_t a, std::size_t modulus) noexcept {
    wide_int old_r = a;
    wide_int r = modulus;
    wide_int old_s = 1;
    wide_int s = 0;
    while (r != 0) {
        const auto quotient = old_r / r;
        const auto next_r = old_r - quotient * r;
        const auto next_s = old_s - quotient * s;
        old_r = r;
        r = next_r;
        old_s = s;
        s = next_s;
    }
    old_s %= modulus;
    if (old_s < 0) old_s += modulus;
    return static_cast<std::size_t>(old_s);
}

bool overlap(const occupied_stream& a, const occupied_stream& b) noexcept {
    if (a.base + a.envelope <= b.base || b.base + b.envelope <= a.base) return false;
    const auto divisor = std::gcd(a.stride, b.stride);
    const auto step = b.stride / divisor;
    const auto inverse = step == 1 ? 0 : modular_inverse(a.stride / divisor, step);
    const wide_int base_difference = wide_int(b.base) - a.base;
    const wide_int b_last = wide_int(b.tiles - 1) * b.stride;

    // Two intervals intersect exactly when their start difference lies in
    // [1-a.width,b.width-1]. For each such byte difference, solve
    // a.stride*i - b.stride*j = c under both finite tile bounds. This costs at
    // most the sum of tile byte widths, even for arrays with billions of tiles.
    for (wide_int offset = 1 - wide_int(a.width); offset < wide_int(b.width); ++offset) {
        const wide_int c = base_difference + offset;
        if (c % divisor != 0) continue;
        const wide_int low = std::max<wide_int>(0, c > 0 ? (c + a.stride - 1) / a.stride : 0);
        if (c + b_last < 0) continue;
        const wide_int high = std::min<wide_int>(a.tiles - 1, (c + b_last) / a.stride);
        if (low > high) continue;
        if (step == 1) return true;
        wide_int remainder = (c / divisor) % step;
        if (remainder < 0) remainder += step;
        const auto first = static_cast<std::size_t>(
            (wide_uint(remainder) * inverse) % step);
        const auto low_remainder = static_cast<std::size_t>(low % step);
        const auto distance = first >= low_remainder ? first - low_remainder
                                                     : step - (low_remainder - first);
        if (wide_int(distance) <= high - low) return true;
    }
    return false;
}

} // namespace

std::expected<void, error>
validate_placement(description layout, std::size_t n,
                   basic_placement<const std::byte> placement) noexcept {
    const auto extents = required_extents(layout, n, placement.payload.stride,
        {placement.heads[0].stride, placement.heads[1].stride});
    if (!extents) return std::unexpected(extents.error());
    if (n == 0) return {};

    std::array<occupied_stream, 3> streams{};
    std::size_t count = 0;
    const auto add = [&](basic_plane<const std::byte> plane, std::size_t bytes,
                         std::size_t envelope) -> std::expected<void, error> {
        if (envelope == 0) return {};
        if (plane.bytes.size() < envelope || plane.bytes.data() == nullptr)
            return std::unexpected(error::insufficient_storage);
        const auto base = reinterpret_cast<std::uintptr_t>(plane.bytes.data());
        if (envelope > std::numeric_limits<std::uintptr_t>::max() - base)
            return std::unexpected(error::overflow);
        streams[count++] = {base, plane.stride, bytes, extents->tiles, envelope};
        return {};
    };
    auto status = add(placement.payload, tile_bytes(layout), extents->payload_envelope);
    if (!status) return status;
    if (layout.storage == geometry::striped &&
        reinterpret_cast<std::uintptr_t>(placement.payload.bytes.data()) % 32 != 0)
        return std::unexpected(error::misaligned_storage);
    for (unsigned h = 0; h < layout.head_bits / 8; ++h) {
        status = add(placement.heads[h], tile_values(layout), extents->head_envelopes[h]);
        if (!status) return status;
    }
    for (std::size_t a = 0; a < count; ++a)
        for (std::size_t b = a + 1; b < count; ++b)
            if (overlap(streams[a], streams[b]))
                return std::unexpected(error::overlapping_storage);
    return {};
}

} // namespace ikea::seriespack
