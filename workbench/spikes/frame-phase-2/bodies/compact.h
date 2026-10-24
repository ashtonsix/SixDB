// Filter to an encoded result: the selected rows of a packed block appended,
// in order, to a plane256 stream of the same width (and so the same base),
// without decoding. Per 64-row group: each byte slice is compressed (VBMI2
// compress, else an 8-row shuffle table) and each plane word is bit-extracted
// (PEXT or SVE2 BEXT, else a loop over the selected bits). The stream is a run of
// plane256 blocks; out_rows counts the rows appended.
//
// The native control compacts the narrow array into a narrow array.
#pragma once

#include "packed.h"

#if defined(__BMI2__)
#include <immintrin.h>
#endif

namespace bodies {

namespace detail {
inline u64 pext(u64 x, u64 m) { return bodies::pext(x, m); }
// shuffle[m] lists the set bit positions of m, then zeros.
struct shuffle_table {
    alignas(8) u8 idx[256][8] = {};
    constexpr shuffle_table() {
        for (unsigned m = 0; m < 256; ++m) {
            unsigned n = 0;
            for (unsigned i = 0; i < 8; ++i)
                if (m >> i & 1)
                    idx[m][n++] = static_cast<u8>(i);
        }
    }
};
inline constexpr shuffle_table shuffles{};

// The selected bytes of 64, in order, to out (which has 8 bytes of slack).
inline void compress_bytes(simd::v64 v, u64 sel, u8 *out) {
#if BODIES_ISA_AVX512 && defined(__AVX512VBMI2__)
    _mm512_storeu_si512(out, _mm512_maskz_compress_epi8(sel, v.x));
#else
    alignas(64) u8 x[64];
    simd::store(x, v);
    for (unsigned q = 0; q < 8; ++q) {
        const unsigned m = static_cast<u8>(sel >> (8 * q));
#if BODIES_ISA_NEON
        const uint8x8_t r = vtbl1_u8(vld1_u8(x + 8 * q), vld1_u8(shuffles.idx[m]));
        vst1_u8(out, r);
#else
        const __m128i r = _mm_shuffle_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i *>(x + 8 * q)),
                                           _mm_loadl_epi64(reinterpret_cast<const __m128i *>(shuffles.idx[m])));
        _mm_storel_epi64(reinterpret_cast<__m128i *>(out), r);
#endif
        out += std::popcount(m);
    }
#endif
}

// OR n (<= 64) low bits of x into a 256-bit plane at bit f (f + n <= 256).
inline void put_bits(u8 *plane, unsigned f, u64 x, unsigned n) {
    if (!n)
        return;
    x &= n == 64 ? ~u64{0} : (u64{1} << n) - 1;
    const unsigned w = f >> 6, sh = f & 63;
    u64 *p = reinterpret_cast<u64 *>(plane);  // plane256 planes are 8-byte aligned in an aligned stream
    p[w] |= x << sh;
    if (sh && sh + n > 64)
        p[w + 1] |= x >> (64 - sh);
}
}  // namespace detail

template <layout L, unsigned K> struct compactor {
    static constexpr unsigned B = K / 8, R = K % 8;
    static constexpr std::size_t stride = 32 * K;
    // Append the selected rows of body to the stream at out.
    static void compact(const u8 *body, const u64 *sel, u8 *out, std::size_t &out_rows) {
        for (unsigned g = 0; g < 4; ++g) {
            const u64 s = sel[g];
            if (!s)
                continue;
            const unsigned n = static_cast<unsigned>(std::popcount(s));
            const group<K> x = fetch<L, K>(body, g);
            const std::size_t at = out_rows;
            u8 *ob = out + (at / block_rows) * stride;
            const unsigned f = at % block_rows, first = std::min(n, block_rows - f);
            for (unsigned j = 0; j < B; ++j) {
#if BODIES_ISA_AVX512 && defined(__AVX512VBMI2__)
                // Compress in a register, then masked stores of the n bytes
                // (slices of one block interleave, so no over-store).
                const __m512i z = _mm512_maskz_compress_epi8(s, x.s[j].x);
                _mm512_mask_storeu_epi8(ob + 256 * j + f, first == 64 ? ~u64{0} : (u64{1} << first) - 1, z);
                if (first < n) {
                    const __m512i rest = _mm512_maskz_compress_epi8(~((u64{1} << first) - 1) & ~u64{0}, z);
                    _mm512_mask_storeu_epi8(ob + stride + 256 * j, (u64{1} << (n - first)) - 1, rest);
                }
#else
                alignas(64) u8 c[72];
                detail::compress_bytes(x.s[j], s, c);
                if (f + 64 <= block_rows)
                    std::memcpy(ob + 256 * j + f, c, 64);  // within this slice; later rows overwrite
                else
                    std::memcpy(ob + 256 * j + f, c, first);
                if (first < n)
                    std::memcpy(ob + stride + 256 * j, c + first, n - first);
#endif
            }
            // Planes are ORed into, so a block's planes are cleared as rows
            // first reach it. Concatenating a block's four group extracts in
            // registers and ORing once per block was slower (Zen 5 v6: K = 5
            // 39 vs 19 ns): u128 variable shifts and variable deposit loops.
            if (f == 0)
                std::memset(ob + 256 * B, 0, 32 * R);
            if (first < n)
                std::memset(ob + stride + 256 * B, 0, 32 * R);
            for (unsigned b = 0; b < R; ++b) {
                const u64 bits = detail::pext(x.w[b], s);
                detail::put_bits(ob + 256 * B + 32 * b, f, bits, first);
                if (first < n)
                    detail::put_bits(ob + stride + 256 * B + 32 * b, 0, bits >> first, n - first);
            }
            out_rows += n;
        }
    }
};

// Native: the selected values of a lane array appended to a lane array.
template <class T> struct lane_compactor {
    static void compact(const u8 *body, const u64 *sel, u8 *out, std::size_t &out_rows) {
        T *o = reinterpret_cast<T *>(out) + out_rows;
        const T *x = reinterpret_cast<const T *>(body);
        std::size_t n = 0;
#if BODIES_ISA_AVX512 && defined(__AVX512VBMI2__)
        constexpr unsigned per = 64 / sizeof(T);
        for (unsigned v = 0; v < block_rows / per; ++v) {
            const u64 m = sel[v * per / 64] >> (v * per % 64) & (per == 64 ? ~u64{0} : (u64{1} << per) - 1);
            const __m512i d = _mm512_loadu_si512(x + v * per);
            if constexpr (sizeof(T) == 1)
                _mm512_storeu_si512(o + n, _mm512_maskz_compress_epi8(m, d));
            else if constexpr (sizeof(T) == 2)
                _mm512_storeu_si512(o + n, _mm512_maskz_compress_epi16(static_cast<__mmask32>(m), d));
            else if constexpr (sizeof(T) == 4)
                _mm512_storeu_si512(o + n, _mm512_maskz_compress_epi32(static_cast<__mmask16>(m), d));
            else
                _mm512_storeu_si512(o + n, _mm512_maskz_compress_epi64(static_cast<__mmask8>(m), d));
            n += static_cast<unsigned>(std::popcount(m));
        }
#else
        if constexpr (sizeof(T) == 1) {
            for (unsigned g = 0; g < 4; ++g) {
                detail::compress_bytes(simd::load(body + 64 * g), sel[g], reinterpret_cast<u8 *>(o + n));
                n += static_cast<unsigned>(std::popcount(sel[g]));
            }
        } else {
            // Branch-free: store every row, advance past the selected ones.
            for (unsigned i = 0; i < block_rows; ++i) {
                o[n] = x[i];
                n += sel[i / 64] >> (i % 64) & 1;
            }
        }
#endif
        out_rows += n;
    }
};
}  // namespace bodies
