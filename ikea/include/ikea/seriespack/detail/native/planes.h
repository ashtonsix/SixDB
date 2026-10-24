#pragma once
#include <ikea/comparison.h>
#include <ikea/seriespack/layout.h>
#include <ikea/seriespack/detail/wire.h>
#include <algorithm>
#include <array>
#include <cstdint>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace ikea::seriespack::detail {
/// Byte t * R + B - Offset of a 16-tile run for each tile t (wrapping below
/// Offset, so a table lookup leaves those lanes out).
template <unsigned R, unsigned B, unsigned Offset>
inline constexpr auto plane_index = [] {
    std::array<std::uint8_t, 16> i{};
    for (unsigned t = 0; t < 16; ++t)
        i[t] = static_cast<std::uint8_t>(t * R + B - Offset);
    return i;
}();
/// Byte 8t + Lane of a 16 x 8-byte run for each t.
template <unsigned Lane>
inline constexpr auto tail_index = [] {
    std::array<std::uint8_t, 16> i{};
    for (unsigned t = 0; t < 16; ++t)
        i[t] = static_cast<std::uint8_t>(8 * t + Lane);
    return i;
}();
/// Comparison of dense Local tiles (no body, or a one- or two-byte body) against a
/// constant without reconstructing values. A Local tile stores plane b of its
/// eight rows as one byte, so sixteen tiles' plane b, gathered into one
/// vector, is a bit-sliced operand: equality and order follow from the
/// planes, most significant first, and each result byte is already the tile's
/// 8-row mask. A body is compared as a byte or u16 lane and its 8-row result
/// packed to the same mask first.
template <class F> struct local_planes {
#if defined(__aarch64__)
    static constexpr bool supported = F::storage == geometry::local && F::heads == 0 && F::body <= 2 && F::tail > 0;
#else
    static constexpr bool supported = false;
#endif
    static constexpr unsigned R = F::tail, rows = 128, block = 16 * F::tile_bytes;
#if defined(__aarch64__)
    /// Plane b of sixteen consecutive tiles from p.
    [[gnu::always_inline]] static std::array<uint8x16_t, R> planes(const std::uint8_t* p) {
        std::array<uint8x16_t, R> out;
        if constexpr (F::body) {
            // The last eight bytes of each tile end with its R planes: byte
            // 8 - R + b of tile t, at 8t + 8 - R + b of the 128 loaded bytes.
            uint8x16x4_t lo, hi;
            detail::each<8>([&](auto v) __attribute__((always_inline)) {
                const auto x = vcombine_u8(vld1_u8(p + (2 * v + 1) * F::tile_bytes - 8),
                                           vld1_u8(p + (2 * v + 2) * F::tile_bytes - 8));
                if constexpr (v < 4)
                    lo.val[v] = x;
                else
                    hi.val[v - 4] = x;
            });
            detail::each<R>([&](auto b) __attribute__((always_inline)) {
                const auto index = vld1q_u8((tail_index<8 - R + b>.data()));
                out[b] = vqtbx4q_u8(vqtbl4q_u8(lo, index), hi, vsubq_u8(index, vdupq_n_u8(64)));
            });
        } else if constexpr (R == 1)
            out[0] = vld1q_u8(p);
        else if constexpr (R == 2) {
            const auto x = vld2q_u8(p);
            out = {x.val[0], x.val[1]};
        } else if constexpr (R == 3) {
            const auto x = vld3q_u8(p);
            out = {x.val[0], x.val[1], x.val[2]};
        } else if constexpr (R == 4) {
            const auto x = vld4q_u8(p);
            out = {x.val[0], x.val[1], x.val[2], x.val[3]};
        } else {
            // 16R bytes: indices t * R + b split between a 64-byte and a
            // (16R - 64)-byte table; out-of-range lanes of each lookup are 0
            // (tbl) or kept (tbx).
            const uint8x16x4_t a = {{vld1q_u8(p), vld1q_u8(p + 16), vld1q_u8(p + 32), vld1q_u8(p + 48)}};
            detail::each<R>([&](auto b) __attribute__((always_inline)) {
                auto x = vqtbl4q_u8(a, vld1q_u8((plane_index<R, b, 0>.data())));
                if constexpr (R == 5) {
                    x = vqtbx1q_u8(x, vld1q_u8(p + 64), vld1q_u8((plane_index<R, b, 64>.data())));
                } else if constexpr (R == 6) {
                    const uint8x16x2_t c = {{vld1q_u8(p + 64), vld1q_u8(p + 80)}};
                    x = vqtbx2q_u8(x, c, vld1q_u8((plane_index<R, b, 64>.data())));
                } else {
                    const uint8x16x3_t c = {{vld1q_u8(p + 64), vld1q_u8(p + 80), vld1q_u8(p + 96)}};
                    x = vqtbx3q_u8(x, c, vld1q_u8((plane_index<R, b, 64>.data())));
                }
                out[b] = x;
            });
        }
        return out;
    }
    /// Op against rhs (< 2^payload) for 128 rows from tile start p: two words.
    template <comparison Op> [[gnu::always_inline]] static uint8x16_t compare(const std::uint8_t* p, std::uint64_t rhs) {
        const auto x = planes(p);
        auto eq = vdupq_n_u8(0xff), gt = vdupq_n_u8(0);
        if constexpr (F::body) {
            // Each tile's eight body bytes against the body of rhs, packed to
            // bit r of byte t by bit weights and three pairwise additions.
            static constexpr std::uint8_t weights[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
            const auto w = vld1q_u8(weights);
            std::array<uint8x16_t, 8> g, e;
            if constexpr (F::body == 1) {
                const auto high = vdupq_n_u8(static_cast<std::uint8_t>(rhs >> R));
                detail::each<8>([&](auto v) __attribute__((always_inline)) {
                    const auto body =
                        vcombine_u8(vld1_u8(p + 2 * v * F::tile_bytes), vld1_u8(p + (2 * v + 1) * F::tile_bytes));
                    g[v] = vandq_u8(vcgtq_u8(body, high), w);
                    e[v] = vandq_u8(vceqq_u8(body, high), w);
                });
            } else {
                // Eight little-endian u16 bodies per tile, narrowed to byte masks.
                const auto high = vdupq_n_u16(static_cast<std::uint16_t>(rhs >> R));
                detail::each<8>([&](auto v) __attribute__((always_inline)) {
                    const auto a = vreinterpretq_u16_u8(vld1q_u8(p + 2 * v * F::tile_bytes));
                    const auto b = vreinterpretq_u16_u8(vld1q_u8(p + (2 * v + 1) * F::tile_bytes));
                    g[v] = vandq_u8(vcombine_u8(vmovn_u16(vcgtq_u16(a, high)), vmovn_u16(vcgtq_u16(b, high))), w);
                    e[v] = vandq_u8(vcombine_u8(vmovn_u16(vceqq_u16(a, high)), vmovn_u16(vceqq_u16(b, high))), w);
                });
            }
            const auto pack = [](const std::array<uint8x16_t, 8>& a) __attribute__((always_inline)) {
                const auto b0 = vpaddq_u8(a[0], a[1]), b1 = vpaddq_u8(a[2], a[3]), b2 = vpaddq_u8(a[4], a[5]),
                           b3 = vpaddq_u8(a[6], a[7]);
                return vpaddq_u8(vpaddq_u8(b0, b1), vpaddq_u8(b2, b3));
            };
            gt = pack(g);
            eq = pack(e);
        }
        detail::each<R>([&](auto j) __attribute__((always_inline)) {
            constexpr unsigned b = R - 1 - j;
            const auto bit = vdupq_n_u8(static_cast<std::uint8_t>(0 - (rhs >> b & 1)));
            gt = vorrq_u8(gt, vbicq_u8(vandq_u8(eq, x[b]), bit));
            eq = vbicq_u8(eq, veorq_u8(x[b], bit));
        });
        if constexpr (Op == comparison::equal)
            return eq;
        else if constexpr (Op == comparison::greater)
            return gt;
        else if constexpr (Op == comparison::greater_equal)
            return vorrq_u8(gt, eq);
        else if constexpr (Op == comparison::less)
            return vmvnq_u8(vorrq_u8(gt, eq));
        else
            return vmvnq_u8(gt);
    }
    /// emit(w, word) for words w0, w0 + 1, ... over n blocks of 128 rows.
    template <comparison Op, class Emit>
    [[gnu::always_inline]] static void run(const std::uint8_t* p, std::size_t n, std::uint64_t rhs, std::size_t w0,
                                           Emit&& emit) {
        for (std::size_t k = 0; k < n; ++k, p += block, w0 += 2) {
            const auto m = vreinterpretq_u64_u8(compare<Op>(p, rhs));
            emit(w0, vgetq_lane_u64(m, 0));
            emit(w0 + 1, vgetq_lane_u64(m, 1));
        }
    }
    /// Rows satisfying Op over n blocks of 128 rows from tile start p.
    template <comparison Op>
    [[gnu::always_inline]] static std::uint64_t count(const std::uint8_t* p, std::size_t n, std::uint64_t rhs) {
        std::uint64_t total = 0;
        while (n) {
            const auto m = std::min<std::size_t>(n, 31);  // byte popcounts stay below 256
            auto acc = vdupq_n_u8(0);
            for (std::size_t k = 0; k < m; ++k, p += block)
                acc = vaddq_u8(acc, vcntq_u8(compare<Op>(p, rhs)));
            total += vaddlvq_u8(acc);
            n -= m;
        }
        return total;
    }
#endif
};
} // namespace ikea::seriespack::detail
