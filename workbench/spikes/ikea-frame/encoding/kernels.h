// Shared typed operations over one Local SeriesPack plane, indexed by width.
// One catalogue entry per width replaces per-block erased decoder objects: an
// operation derives everything from the block's own offset/count/width, so a
// column retains only its descriptors and payload. Every entry calls maintained
// SeriesPack kernels; no packed-bit algorithm is copied.
//
// Trusted owner handoff: the caller admits width 1..64, complete occupied
// tiles for the requested rows, and source placement for the call.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace frame_encoding {
struct plane_operations {
    std::uint64_t (*point)(const std::uint8_t *, std::size_t row);
    void (*range)(const std::uint8_t *, std::size_t first, std::size_t take, std::uint64_t *out);
    // Sum of rows [0, take), modulo 2^64; exact when take * (2^K - 1) < 2^64.
    std::uint64_t (*sum)(const std::uint8_t *, unsigned take);
    // Count of rows in [0, take) with value >= rhs.
    std::uint64_t (*count_ge)(const std::uint8_t *, unsigned take, std::uint64_t rhs);
};

extern const std::array<plane_operations, 65> plane_catalogue;

inline const plane_operations &plane_kernels(unsigned width) {
    return plane_catalogue[width];
}
inline constexpr std::size_t plane_catalogue_bytes() {
    return sizeof(plane_catalogue);
}
}  // namespace frame_encoding
