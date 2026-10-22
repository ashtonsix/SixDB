// Shared typed operations over one Local SeriesPack plane, indexed by width.
// One catalogue entry per width replaces per-block erased decoder objects: an
// operation derives everything from the block's own offset/count/width, so a
// column retains only its descriptors and payload. Every entry calls maintained
// SeriesPack kernels; no packed-bit algorithm is copied.
//
// Trusted owner handoff: the caller admits width 1..64, the read grant named at
// each entry for the requested rows, and source placement for the call.
// Grants, by the occupied rows n of the plane (plane_bytes(K, n) bytes):
//   exact tail   reads only the occupied 8-row tiles of the requested rows;
//                valid for any plane, including short ones (run values,
//                exception values, dictionaries, packed checkpoints).
//   full window  reads complete native 16-row windows: rows [0, take) need
//                ceil(take / 16) * 16 occupied rows, as a full 256-row plane
//                (packed, patched, delta and delta_patched residuals) has.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace frame_encoding {
struct plane_operations {
    // Exact tail.
    std::uint64_t (*point)(const std::uint8_t *, std::size_t row);
    // Rows [first, first + take) into u64 lanes; exact tail, arbitrary edges.
    void (*range)(const std::uint8_t *, std::size_t first, std::size_t take, std::uint64_t *out);
    // Sum of rows [0, take), modulo 2^64; exact when take * (2^K - 1) < 2^64.
    // Full window: not valid for short planes such as run values (Ikea LEAD's
    // caution).
    std::uint64_t (*sum)(const std::uint8_t *, unsigned take);
    // Count of rows in [0, take) with value >= rhs; exact tail.
    std::uint64_t (*count_ge)(const std::uint8_t *, unsigned take, std::uint64_t rhs);
    // The range above into the narrowest lane that holds K bits, for narrow and
    // weighted reductions; exact tail. Null where the lane is narrower than K:
    // range8 for K <= 8, range16 for K <= 16, range32 for K <= 32.
    void (*range8)(const std::uint8_t *, std::size_t first, std::size_t take, std::uint8_t *out);
    void (*range16)(const std::uint8_t *, std::size_t first, std::size_t take, std::uint16_t *out);
    void (*range32)(const std::uint8_t *, std::size_t first, std::size_t take, std::uint32_t *out);
};

extern const std::array<plane_operations, 65> plane_catalogue;

inline const plane_operations &plane_kernels(unsigned width) {
    return plane_catalogue[width];
}
inline constexpr std::size_t plane_catalogue_bytes() {
    return sizeof(plane_catalogue);
}
}  // namespace frame_encoding
