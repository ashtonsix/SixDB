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
    // Row-selected forms over one full 256-row plane; selected names rows by
    // bit i % 64 of word i / 64. masked_sum is modulo 2^64 (exact for
    // K <= 56) and reads only the native 16-row windows that hold a selected
    // row (full window within them). masked_count_ge/eq count selected rows
    // with value >= or == rhs through 64-row filters whose active mask is the
    // selection word; a word with no selected row reads nothing.
    std::uint64_t (*masked_sum)(const std::uint8_t *, const std::uint64_t *selected);
    std::uint64_t (*masked_count_ge)(const std::uint8_t *, const std::uint64_t *selected, std::uint64_t rhs);
    std::uint64_t (*masked_count_eq)(const std::uint8_t *, const std::uint64_t *selected, std::uint64_t rhs);
};

// One width-indexed array per operation (8-byte stride), so a hot reader that
// needs one operation indexes only that array (Ikea LEAD's code-shape point: a
// bundled seven-pointer entry has a 56-byte stride). The bundle below is an
// inline view; loads of operations a caller doesn't use are dead.
struct plane_catalogue_t {
    std::array<decltype(plane_operations::point), 65> point;
    std::array<decltype(plane_operations::range), 65> range;
    std::array<decltype(plane_operations::sum), 65> sum;
    std::array<decltype(plane_operations::count_ge), 65> count_ge;
    std::array<decltype(plane_operations::range8), 65> range8;
    std::array<decltype(plane_operations::range16), 65> range16;
    std::array<decltype(plane_operations::range32), 65> range32;
    std::array<decltype(plane_operations::masked_sum), 65> masked_sum;
    std::array<decltype(plane_operations::masked_count_ge), 65> masked_count_ge;
    std::array<decltype(plane_operations::masked_count_eq), 65> masked_count_eq;
};
extern const plane_catalogue_t plane_catalogue;

inline plane_operations plane_kernels(unsigned width) {
    const auto &c = plane_catalogue;
    return {c.point[width], c.range[width],   c.sum[width],    c.count_ge[width],
            c.range8[width], c.range16[width], c.range32[width], c.masked_sum[width],
            c.masked_count_ge[width], c.masked_count_eq[width]};
}
inline constexpr std::size_t plane_catalogue_bytes() {
    return sizeof(plane_catalogue);
}
}  // namespace frame_encoding
