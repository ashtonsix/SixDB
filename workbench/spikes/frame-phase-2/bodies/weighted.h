// Weighted code SUM: sum of D[code] over a block of dictionary codes (and
// over selected rows), for the dictionary seam: a code body is an integer
// body whose values index a part dictionary D of u64 weights.
//
//   plane layouts, K <= 6: the bit-sliced 2^K histogram (sort.h) dotted with D.
//   plane layouts, K 7..16: codes decoded to u16 lanes in registers, then a
//     gather of D (VPGATHERDQ on AVX-512, scalar loads elsewhere).
//   runs: one D lookup per run, times its length.
//   lanes: the native control, codes in a byte lane, gathered the same way.
//
// Results are u128 (256 weights of up to 64 bits).
#pragma once

#include "sort.h"

namespace bodies {

namespace detail {
// Sum of D[code[i]] for i < 256 (optionally where the row bit is set).
inline u128 gather_sum(const u16 *code, const u64 *D, const u64 *sel) {
#if BODIES_ISA_AVX512
    __m512i lo = _mm512_setzero_si512(), hi = _mm512_setzero_si512();
    const __m512i low = _mm512_set1_epi64(0xffffffff);
    for (unsigned i = 0; i < block_rows; i += 8) {
        const __m256i idx = _mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i *>(code + i)));
        const __mmask8 keep = sel ? static_cast<__mmask8>(sel[i / 64] >> (i % 64)) : __mmask8(0xff);
        const __m512i d = _mm512_mask_i32gather_epi64(_mm512_setzero_si512(), keep, idx, D, 8);
        lo = _mm512_add_epi64(lo, _mm512_and_si512(d, low));
        hi = _mm512_add_epi64(hi, _mm512_srli_epi64(d, 32));
    }
    return static_cast<u128>(static_cast<u64>(_mm512_reduce_add_epi64(lo))) +
           (static_cast<u128>(static_cast<u64>(_mm512_reduce_add_epi64(hi))) << 32);
#else
    u64 lo = 0, hi = 0;
    for (unsigned i = 0; i < block_rows; ++i) {
        const u64 d = D[code[i]] & (sel ? u64{0} - (sel[i / 64] >> (i % 64) & 1) : ~u64{0});
        lo += d & 0xffffffffu, hi += d >> 32;
    }
    return static_cast<u128>(lo) + (static_cast<u128>(hi) << 32);
#endif
}
}  // namespace detail

// A dictionary prepared for byte-table lookups (a query preparation, built
// once per part dictionary of at most 256 entries): byte b of every weight
// as a 256-entry table, for the `bytes` low bytes that any weight uses.
struct byte_tables {
    alignas(64) u8 t[8][256] = {};
    unsigned bytes = 0;
    byte_tables(const u64 *D, unsigned entries) {
        u64 all = 0;
        for (unsigned v = 0; v < entries; ++v) {
            all |= D[v];
            for (unsigned b = 0; b < 8; ++b)
                t[b][v] = static_cast<u8>(D[v] >> (8 * b));
        }
        bytes = all ? (64 - static_cast<unsigned>(std::countl_zero(all)) + 7) / 8 : 1;
    }
};

namespace detail {
// Sums of the prepared weights over 256 codes (bytes, < 2^K), one byte table
// at a time so each table is loaded once per block.
template <unsigned K> inline void table_sums(const u8 *codes, const byte_tables &w, simd::acc *a, const u64 *sel = nullptr) {
    simd::v64 c[4];
    for (unsigned g = 0; g < 4; ++g)
        c[g] = simd::load(codes + 64 * g);
    for (unsigned b = 0; b < w.bytes; ++b) {
        const u8 *t = w.t[b];
#if BODIES_ISA_AVX512 && defined(__AVX512VBMI__)
        const __m512i t0 = _mm512_loadu_si512(t), t1 = _mm512_loadu_si512(t + 64), t2 = _mm512_loadu_si512(t + 128),
                      t3 = _mm512_loadu_si512(t + 192);
        for (unsigned g = 0; g < 4; ++g) {
            __m512i x;
            if constexpr (K <= 6)
                x = _mm512_permutexvar_epi8(c[g].x, t0);
            else if constexpr (K == 7)
                x = _mm512_permutex2var_epi8(t0, c[g].x, t1);
            else
                x = _mm512_mask_mov_epi8(_mm512_permutex2var_epi8(t0, c[g].x, t1), _mm512_movepi8_mask(c[g].x),
                                         _mm512_permutex2var_epi8(t2, c[g].x, t3));
            if (sel)
                a[b].add_masked({x}, sel[g]);
            else
                a[b].add({x});
        }
#elif BODIES_ISA_NEON
        const uint8x16x4_t t0 = vld1q_u8_x4(t);
        uint8x16x4_t t1 = t0, t2 = t0, t3 = t0;
        if constexpr (K > 6)
            t1 = vld1q_u8_x4(t + 64);
        if constexpr (K > 7)
            t2 = vld1q_u8_x4(t + 128), t3 = vld1q_u8_x4(t + 192);
        for (unsigned g = 0; g < 4; ++g) {
            simd::v64 x;
            for (unsigned q = 0; q < 4; ++q) {
                const uint8x16_t i = c[g].x.val[q];
                uint8x16_t r = vqtbl4q_u8(t0, i);
                if constexpr (K > 6)
                    r = vqtbx4q_u8(r, t1, vsubq_u8(i, vdupq_n_u8(64)));
                if constexpr (K > 7) {
                    r = vqtbx4q_u8(r, t2, vsubq_u8(i, vdupq_n_u8(128)));
                    r = vqtbx4q_u8(r, t3, vsubq_u8(i, vdupq_n_u8(192)));
                }
                x.x.val[q] = r;
            }
            if (sel)
                a[b].add_masked(x, sel[g]);
            else
                a[b].add(x);
        }
#else
        for (unsigned g = 0; g < 4; ++g) {
            alignas(64) u8 x[64];
            for (unsigned i = 0; i < 64; ++i)
                x[i] = t[codes[64 * g + i]];
            if (sel)
                a[b].add_masked(simd::load(x), sel[g]);
            else
                a[b].add(simd::load(x));
        }
#endif
    }
}
}  // namespace detail

template <layout L, unsigned K> struct packed_weighted {
    static_assert(K >= 1 && K <= 16);
    static u128 weighted_sum(const u8 *body, const u64 *D) {
        if constexpr (K <= 6) {
            u32 counts[1u << K];
            packed_histogram<L, K>::histogram(body, counts);
            u128 s = 0;
            for (unsigned v = 0; v < (1u << K); ++v)
                s += static_cast<u128>(D[v]) * counts[v];
            return s;
        } else {
            alignas(64) u16 code[block_rows];
            packed<L, K>::unpack_to(body, code);
            return detail::gather_sum(code, D, nullptr);
        }
    }
    // With prepared byte tables (K <= 8): no gathers, no histogram.
    static u128 weighted_sum(const u8 *body, const byte_tables &w)
        requires(K <= 8)
    {
        alignas(64) u8 code[block_rows];
        packed<L, K>::unpack_to(body, code);
        simd::acc a[8];
        detail::table_sums<K>(code, w, a);
        u128 s = 0;
        for (unsigned b = 0; b < w.bytes; ++b)
            s += static_cast<u128>(a[b].total()) << (8 * b);
        return s;
    }
    static u128 masked_weighted_sum(const u8 *body, const u64 *sel, const byte_tables &w)
        requires(K <= 8)
    {
        alignas(64) u8 code[block_rows];
        packed<L, K>::unpack_to(body, code);
        simd::acc a[8];
        detail::table_sums<K>(code, w, a, sel);
        u128 s = 0;
        for (unsigned b = 0; b < w.bytes; ++b)
            s += static_cast<u128>(a[b].total()) << (8 * b);
        return s;
    }
    static u128 masked_weighted_sum(const u8 *body, const u64 *sel, const u64 *D) {
        alignas(64) u16 code[block_rows];
        packed<L, K>::unpack_to(body, code);
        return detail::gather_sum(code, D, sel);
    }
};

template <class T> struct lane_weighted {
    static u128 weighted_sum(const u8 *body, const u64 *D) {
        alignas(64) u16 code[block_rows];
        for (unsigned i = 0; i < block_rows; ++i)
            code[i] = load_le<T>(body + sizeof(T) * i);
        return detail::gather_sum(code, D, nullptr);
    }
};

template <class T> struct runs_weighted {
    static u128 weighted_sum(const u8 *body, unsigned n, const u64 *D) {
        const T *v = runs<T>::values(body, n);
        u128 s = 0;
        unsigned start = 0;
        for (unsigned r = 0; r < n; ++r) {
            const unsigned end = unsigned{body[r]} + 1;
            s += static_cast<u128>(D[load_le<T>(v + r)]) * (end - start);
            start = end;
        }
        return s;
    }
    // Selected rows: each run's weight times its selected rows, counted from
    // a per-word prefix of the selection (as runs<T>::masked_sum_scalar).
    static u128 masked_weighted_sum(const u8 *body, unsigned n, const u64 *sel, const u64 *D) {
        u32 pre[5] = {0};
        for (unsigned q = 0; q < 4; ++q)
            pre[q + 1] = pre[q] + static_cast<u32>(std::popcount(sel[q]));
        const T *v = runs<T>::values(body, n);
        u128 s = 0;
        u32 below = 0;
        for (unsigned r = 0; r + 1 < n; ++r) {
            const unsigned x = unsigned{body[r]} + 1, q = x >> 6;
            const u32 c = pre[q] + static_cast<u32>(std::popcount(sel[q] & ((u64{1} << (x & 63)) - 1)));
            s += static_cast<u128>(D[load_le<T>(v + r)]) * (c - below);
            below = c;
        }
        return s + static_cast<u128>(D[load_le<T>(v + n - 1)]) * (pre[4] - below);
    }
};
}  // namespace bodies
