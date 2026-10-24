// Integer body images for front C: one 256-row block of FOR residuals at
// width K, in four physical layouts, and run bodies. Residuals are already
// relative to the block base; a leaf returns a partial result in that domain.
//
// K = 8B + R. Following SeriesPack's law, the B upper whole bytes of a value
// (its "body") are bytes and the R low bits are bit planes.
//
//   local8   SeriesPack Local: 32 tiles of 8 rows; tile t is K bytes at tK:
//            8 rows of B little-endian body bytes, then R plane bytes
//            (bit i of plane byte b is bit b of tile row i).
//   plane64  64-row tiles: tile g is 8K bytes at 8Kg: B byte slices of 64
//            bytes (slice j holds byte j of each row's body), then R plane
//            words (u64, bit i = bit b of row 64g + i).
//   plane256 One 256-row tile: B byte slices of 256 bytes, then R planes of
//            32 bytes. The same 64-row pieces as plane64, grouped by kind.
//   lane     Byte-lane rounded: 256 little-endian values of 8, 16, 32 or 64
//            bits (SeriesPack Local at a byte-multiple width is this array).
//
// All but lane occupy 32K bytes per block. The plane layouts are new; the
// leaves read every layout as 64-row groups of (slices, plane words).
//
// Runs (n >= 2 runs): n u8 ends (exclusive end minus one, ascending, the last
// 255) followed by n run values in a byte lane of 1, 2 or 4 bytes.
#pragma once

#include "simd.h"

#include <array>
#include <cassert>
#include <span>
#include <vector>

namespace bodies {

enum class layout : u8 { local8, plane64, plane256, lane };
inline const char *layout_name(layout l) {
    switch (l) {
    case layout::local8: return "local8";
    case layout::plane64: return "plane64";
    case layout::plane256: return "plane256";
    case layout::lane: return "lane";
    }
    return "?";
}

inline constexpr unsigned lane_bits(unsigned k) { return k <= 8 ? 8 : k <= 16 ? 16 : k <= 32 ? 32 : 64; }
inline constexpr std::size_t block_bytes(layout l, unsigned k) {
    return l == layout::lane ? std::size_t{block_rows} * lane_bits(k) / 8 : std::size_t{32} * k;
}

// Byte offsets of the 64-row pieces of group g in the plane layouts.
template <layout L, unsigned K> struct planes {
    static constexpr unsigned B = K / 8, R = K % 8;
    static_assert(L == layout::plane64 || L == layout::plane256);
    static constexpr unsigned slice(unsigned j, unsigned g) {
        return L == layout::plane256 ? 256 * j + 64 * g : 8 * K * g + 64 * j;
    }
    static constexpr unsigned word(unsigned b, unsigned g) {
        return L == layout::plane256 ? 256 * B + 32 * b + 8 * g : 8 * K * g + 64 * B + 8 * b;
    }
};

// Encode 256 residuals (each < 2^k) into out (block_bytes(l, k) bytes).
inline void encode(layout l, unsigned k, const u64 *v, u8 *out) {
    const unsigned B = k / 8, R = k % 8;
    std::memset(out, 0, block_bytes(l, k));
    switch (l) {
    case layout::lane: {
        const unsigned w = lane_bits(k) / 8;
        for (unsigned i = 0; i < block_rows; ++i)
            std::memcpy(out + i * w, &v[i], w);
        return;
    }
    case layout::local8:
        for (unsigned i = 0; i < block_rows; ++i) {
            u8 *tile = out + (i / 8) * k;
            const u64 body = v[i] >> R;
            for (unsigned j = 0; j < B; ++j)
                tile[(i % 8) * B + j] = static_cast<u8>(body >> (8 * j));
            for (unsigned b = 0; b < R; ++b)
                tile[8 * B + b] |= static_cast<u8>((v[i] >> b & 1) << (i % 8));
        }
        return;
    case layout::plane64:
    case layout::plane256:
        for (unsigned i = 0; i < block_rows; ++i) {
            const unsigned g = i / 64, r = i % 64;
            const u64 body = v[i] >> R;
            for (unsigned j = 0; j < B; ++j) {
                const unsigned at = l == layout::plane256 ? 256 * j + 64 * g : 8 * k * g + 64 * j;
                out[at + r] = static_cast<u8>(body >> (8 * j));
            }
            for (unsigned b = 0; b < R; ++b) {
                const unsigned at = l == layout::plane256 ? 256 * B + 32 * b + 8 * g : 8 * k * g + 64 * B + 8 * b;
                out[at + r / 8] |= static_cast<u8>((v[i] >> b & 1) << (r % 8));
            }
        }
        return;
    }
}

// Runs: lane bytes for run values of width w.
inline constexpr unsigned run_lane(unsigned w) { return w <= 8 ? 1 : w <= 16 ? 2 : w <= 32 ? 4 : 8; }
inline std::size_t runs_bytes(unsigned n, unsigned w) { return n + std::size_t{n} * run_lane(w); }

// Encode the runs of 256 residuals; returns n (the caller has checked n >= 2). out needs runs_bytes(n, w) bytes.
inline unsigned encode_runs(const u64 *v, unsigned w, u8 *out) {
    unsigned n = 0;
    for (unsigned i = 0; i < block_rows; ++i)
        if (i + 1 == block_rows || v[i + 1] != v[i])
            out[n++] = static_cast<u8>(i);
    const unsigned lane = run_lane(w);
    unsigned r = 0;
    for (unsigned i = 0; i < block_rows; ++i)
        if (i + 1 == block_rows || v[i + 1] != v[i]) {
            std::memcpy(out + n + r * lane, &v[i], lane);
            ++r;
        }
    return n;
}
inline unsigned count_runs(const u64 *v) {
    unsigned n = 1;
    for (unsigned i = 1; i < block_rows; ++i)
        n += v[i] != v[i - 1];
    return n;
}
}  // namespace bodies

namespace bodies {
// The exact boundary route: a leaf whose loads may run past its body (local8,
// runs, patched; see README "Read and write envelopes") reads a copy of the
// body in a zero-tailed stack buffer instead. Plane and lane leaves read
// exactly and need no copy. Use it only where the image cannot admit the halo
// (a sliced or protected image, a mutable neighbour); the copy costs one pass
// over the body.
inline constexpr std::size_t max_halo = 128;  // 15 u64 run values, rounded up
template <class F> decltype(auto) with_exact_body(const u8 *body, std::size_t bytes, F &&leaf) {
    constexpr std::size_t largest = block_rows + 8 * block_rows;  // runs of u64 values, n = 256
    alignas(64) u8 copy[largest + max_halo];
    assert(bytes <= largest);
    std::memcpy(copy, body, bytes);
    std::memset(copy + bytes, 0, max_halo);
    return leaf(static_cast<const u8 *>(copy));
}
}  // namespace bodies
