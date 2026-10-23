// Leaves over one packed FOR block (256 residuals at width K), in every
// layout of body.h. Each leaf reads the block as four 64-row groups of
// (B byte slices, R plane words) and works on them directly: sums add byte
// sums and weighted plane popcounts, comparisons run byte comparisons on the
// slices and a bit-sliced comparison on the planes, and nothing is unpacked
// unless the operation's result is unpacked values.
//
// local8 groups are transposed into that form in registers (VBMI permutes on
// AVX-512, table lookups on NEON, a byte gather elsewhere).
#pragma once

#include "body.h"

namespace bodies {

template <unsigned K> struct group {
    static constexpr unsigned B = K / 8, R = K % 8;
    simd::v64 s[B ? B : 1];
    u64 w[R ? R : 1];
};

namespace detail {
// Source byte (within the 8K-byte local8 group) of output byte i: slices
// first (B * 64 bytes), then the R plane words (8 bytes each).
template <unsigned K> constexpr unsigned local8_source(unsigned i) {
    constexpr unsigned B = K / 8;
    if (i < 64 * B) {
        const unsigned j = i / 64, r = i % 64;
        return (r / 8) * K + (r % 8) * B + j;
    }
    const unsigned b = (i - 64 * B) / 8, t = (i - 64 * B) % 8;
    return t * K + 8 * B + b;
}
template <unsigned K> constexpr unsigned local8_outputs = (64 * (K / 8) + 8 * (K % 8) + 63) / 64;
template <unsigned K> struct local8_table {
    // idx[o][i]: source byte of output vector o, byte i (0xff = none).
    std::array<std::array<u8, 64>, local8_outputs<K>> idx{};
    constexpr local8_table() {
        constexpr unsigned total = 64 * (K / 8) + 8 * (K % 8);
        for (unsigned o = 0; o < local8_outputs<K>; ++o)
            for (unsigned i = 0; i < 64; ++i)
                idx[o][i] = 64 * o + i < total ? static_cast<u8>(local8_source<K>(64 * o + i)) : u8{0xff};
    }
};
template <unsigned K> inline constexpr local8_table<K> local8_tables{};

template <unsigned K> inline group<K> from_bytes(const u8 *t) {
    constexpr unsigned B = K / 8, R = K % 8;
    group<K> x;
    for (unsigned j = 0; j < B; ++j)
        x.s[j] = simd::load(t + 64 * j);
    for (unsigned b = 0; b < R; ++b)
        x.w[b] = load_le<u64>(t + 64 * B + 8 * b);
    return x;
}

// Group g of a local8 block. Images are padded so that reading up to 256
// bytes past a block's end is safe.
template <unsigned K> inline group<K> local8_group(const u8 *body, unsigned g) {
    constexpr unsigned outputs = local8_outputs<K>;
    const u8 *src = body + 8 * K * g;
    alignas(64) u8 t[64 * outputs];
#if BODIES_ISA_AVX512 && defined(__AVX512VBMI__)
    constexpr unsigned windows = (8 * K + 63) / 64;
    __m512i a[windows + 1];
    for (unsigned w = 0; w < windows; ++w)
        a[w] = _mm512_loadu_si512(src + 64 * w);
    a[windows] = _mm512_setzero_si512();
    for (unsigned o = 0; o < outputs; ++o) {
        const __m512i idx = _mm512_loadu_si512(local8_tables<K>.idx[o].data());
        // The low seven index bits pick a byte of a window pair; bit 7 the pair.
        __m512i r = _mm512_permutex2var_epi8(a[0], idx, a[windows > 1 ? 1 : 0]);
        if constexpr (windows > 2) {
            const __mmask64 hi = _mm512_test_epi8_mask(idx, _mm512_set1_epi8(static_cast<char>(0x80)));
            r = _mm512_mask_mov_epi8(r, hi, _mm512_permutex2var_epi8(a[2], idx, a[windows > 3 ? 3 : windows]));
        }
        _mm512_store_si512(t + 64 * o, r);
    }
#elif BODIES_ISA_NEON
    constexpr unsigned windows = (8 * K + 63) / 64;
    uint8x16x4_t a[windows];
    for (unsigned w = 0; w < windows; ++w)
        a[w] = vld1q_u8_x4(src + 64 * w);
    for (unsigned o = 0; o < outputs; ++o)
        for (unsigned q = 0; q < 4; ++q) {
            const uint8x16_t idx = vld1q_u8(local8_tables<K>.idx[o].data() + 16 * q);
            uint8x16_t r = vqtbl4q_u8(a[0], idx);
            for (unsigned w = 1; w < windows; ++w)
                r = vqtbx4q_u8(r, a[w], vsubq_u8(idx, vdupq_n_u8(static_cast<u8>(64 * w))));
            vst1q_u8(t + 64 * o + 16 * q, r);
        }
#else
    constexpr unsigned total = 64 * (K / 8) + 8 * (K % 8);
    for (unsigned i = 0; i < total; ++i)
        t[i] = src[local8_tables<K>.idx[i / 64][i % 64]];
#endif
    return from_bytes<K>(t);
}
}  // namespace detail

template <layout L, unsigned K> inline group<K> fetch(const u8 *body, unsigned g) {
    if constexpr (L == layout::local8) {
        return detail::local8_group<K>(body, g);
    } else {
        using P = planes<L, K>;
        group<K> x;
        for (unsigned j = 0; j < P::B; ++j)
            x.s[j] = simd::load(body + P::slice(j, g));
        for (unsigned b = 0; b < P::R; ++b)
            x.w[b] = load_le<u64>(body + P::word(b, g));
        return x;
    }
}

// Leaves over a packed block in a bit layout (local8, plane64, plane256).
template <layout L, unsigned K> struct packed {
    static_assert(L != layout::lane && K >= 1 && K <= 32);
    static constexpr unsigned B = K / 8, R = K % 8;

    typedef u64 v4 __attribute__((ext_vector_type(4)));  // one word per 64-row group
    typedef u64 v8 __attribute__((ext_vector_type(8)));

    // Plane b's words for the four groups.
    static v4 plane_words(const u8 *body, const group<K> *xs, unsigned b) {
        if constexpr (L == layout::plane256) {
            v4 w;
            std::memcpy(&w, body + 256 * B + 32 * b, 32);
            return w;
        } else {
            return v4{xs[0].w[b], xs[1].w[b], xs[2].w[b], xs[3].w[b]};
        }
    }
    static simd::v64 slice(const u8 *body, const group<K> *xs, unsigned j, unsigned g) {
        if constexpr (L == layout::plane256)
            return simd::load(body + planes<L, K>::slice(j, g));
        else
            return xs[g].s[j];
    }
    // The plane layouts' groups, or nothing for plane256 (read in place).
    struct groups {
        group<K> x[L == layout::plane256 ? 1 : 4];
        explicit groups(const u8 *body) {
            if constexpr (L != layout::plane256)
                for (unsigned g = 0; g < 4; ++g)
                    x[g] = fetch<L, K>(body, g);
        }
    };

    static u64 sum(const u8 *body) {
        const groups gs(body);
        simd::acc a[B ? B : 1];
        for (unsigned g = 0; g < 4; ++g)
            for (unsigned j = 0; j < B; ++j)
                a[j].add(slice(body, gs.x, j, g));
        u64 p = 0;
        if constexpr (L == layout::plane256 && R > 0) {
            // The R planes are 32R contiguous bytes: popcount whole 64-byte
            // vectors, qword q of vector i weighted by its plane (2i + q / 4).
            v8 acc = 0;
            const u8 *res = body + 256 * B;
            for (unsigned i = 0; i < (R + 1) / 2; ++i) {
                v8 x = 0;
                std::memcpy(&x, res + 64 * i, 2 * i + 1 < R ? 64 : 32);
                const v8 shift = {2 * i, 2 * i, 2 * i, 2 * i, 2 * i + 1, 2 * i + 1, 2 * i + 1, 2 * i + 1};
                acc += __builtin_elementwise_popcount(x) << shift;
            }
            p = __builtin_reduce_add(acc);
        } else {
            v4 acc = 0;
            for (unsigned b = 0; b < R; ++b)
                acc += __builtin_elementwise_popcount(plane_words(body, gs.x, b)) << b;
            p = __builtin_reduce_add(acc);
        }
        for (unsigned j = 0; j < B; ++j)
            p += a[j].total() << (R + 8 * j);
        return p;
    }

    // Rows whose residual is >= c (eq_only: == c), one word per group: byte
    // comparisons on the slices from the top byte, then a branch-free
    // bit-sliced comparison over the planes, four groups per vector.
    template <bool eq_only> static v4 compare(const u8 *body, u64 c) {
        const groups gs(body);
        v4 gt = 0, eq = ~v4(0);
        if constexpr (B > 0) {
            u64 g4[4], e4[4];
            for (unsigned g = 0; g < 4; ++g) {
                u64 gt1 = 0, eq1 = ~u64{0};
                for (unsigned j = B; j-- > 0;) {
                    const u8 cj = static_cast<u8>(c >> (R + 8 * j));
                    const simd::v64 x = slice(body, gs.x, j, g);
                    if constexpr (!eq_only)
                        gt1 |= eq1 & simd::gt(x, cj);
                    eq1 &= simd::eq(x, cj);
                }
                g4[g] = gt1, e4[g] = eq1;
            }
            gt = v4{g4[0], g4[1], g4[2], g4[3]};
            eq = v4{e4[0], e4[1], e4[2], e4[3]};
        }
        for (unsigned b = R; b-- > 0;) {
            const v4 w = plane_words(body, gs.x, b);
            const u64 cb = u64{0} - (c >> b & 1);
            if constexpr (!eq_only)
                gt |= eq & w & ~cb;
            eq &= ~(w ^ cb);
        }
        if constexpr (eq_only)
            return eq;
        else
            return gt | eq;
    }
    static unsigned popcount4(v4 x) {
        return static_cast<unsigned>(__builtin_reduce_add(__builtin_elementwise_popcount(x)));
    }
    static void mask_ge(const u8 *body, u64 c, u64 *m) {
        const v4 r = compare<false>(body, c);
        std::memcpy(m, &r, 32);
    }
    static unsigned count_ge(const u8 *body, u64 c) { return popcount4(compare<false>(body, c)); }
    static unsigned count_eq(const u8 *body, u64 c) { return popcount4(compare<true>(body, c)); }

    // Exact MAX (or MIN) without decoding: the extreme byte of each slice
    // among the remaining candidate rows, from the top byte, then one
    // bit-sliced step per plane (keep the candidates with the bit set, if any).
    // rows (optional) limits the candidates; it must not be empty.
    template <bool is_max> static u64 extremum(const u8 *body, const u64 *rows = nullptr) {
        typedef u8 bytes __attribute__((ext_vector_type(64)));
        const groups gs(body);
        u64 cand[4] = {~u64{0}, ~u64{0}, ~u64{0}, ~u64{0}};
        if (rows)
            for (unsigned g = 0; g < 4; ++g)
                cand[g] = rows[g];
        u64 v = 0;
        if constexpr (B > 0) {
            // Top slice over every row: elementwise extremes, one reduction.
            // Few rows then share the extreme top byte; read those as points.
            if (!rows) {
                bytes m = __builtin_bit_cast(bytes, slice(body, gs.x, B - 1, 0));
                for (unsigned g = 1; g < 4; ++g) {
                    const bytes x = __builtin_bit_cast(bytes, slice(body, gs.x, B - 1, g));
                    m = is_max ? __builtin_elementwise_max(m, x) : __builtin_elementwise_min(m, x);
                }
                const u8 best = is_max ? __builtin_reduce_max(m) : __builtin_reduce_min(m);
                v |= u64{best} << (R + 8 * (B - 1));
                unsigned left = 0;
                for (unsigned g = 0; g < 4; ++g) {
                    cand[g] = simd::eq(slice(body, gs.x, B - 1, g), best);
                    left += static_cast<unsigned>(std::popcount(cand[g]));
                }
                if (left <= 8) {
                    u64 e = is_max ? 0 : ~u64{0};
                    for (unsigned g = 0; g < 4; ++g)
                        for (u64 c = cand[g]; c; c &= c - 1) {
                            const u64 x = point(body, 64 * g + static_cast<unsigned>(std::countr_zero(c)));
                            e = is_max ? std::max(e, x) : std::min(e, x);
                        }
                    return e;
                }
            }
        }
        for (unsigned j = (rows ? B : B - (B > 0)); j-- > 0;) {
            bytes x[4];
            u8 best = is_max ? 0 : 255;
            for (unsigned g = 0; g < 4; ++g) {
                x[g] = __builtin_bit_cast(bytes, slice(body, gs.x, j, g));
                const bytes keep = __builtin_bit_cast(bytes, simd::expand(cand[g]));
                best = is_max ? std::max<u8>(best, __builtin_reduce_max(x[g] & keep))
                              : std::min<u8>(best, __builtin_reduce_min(x[g] | ~keep));
            }
            for (unsigned g = 0; g < 4; ++g)
                cand[g] &= simd::eq(__builtin_bit_cast(simd::v64, x[g]), best);
            v |= u64{best} << (R + 8 * j);
        }
        v4 c = {cand[0], cand[1], cand[2], cand[3]};
        for (unsigned b = R; b-- > 0;) {
            const v4 w = plane_words(body, gs.x, b);
            const v4 t = is_max ? c & w : c & ~w;
            const bool any = __builtin_reduce_or(t) != 0;
            c = any ? t : c;
            v |= u64{any == is_max} << b;
        }
        return v;
    }
    static u64 max(const u8 *body) { return extremum<true>(body); }
    static u64 min(const u8 *body) { return extremum<false>(body); }
    // First row with residual >= c, or 256.
    static unsigned first_ge(const u8 *body, u64 c) {
        const v4 r = compare<false>(body, c);
        for (unsigned g = 0; g < 4; ++g)
            if (r[g])
                return 64 * g + static_cast<unsigned>(std::countr_zero(r[g]));
        return block_rows;
    }
    static u64 masked_sum(const u8 *body, const u64 *sel) {
        const groups gs(body);
        simd::acc a[B ? B : 1];
        for (unsigned g = 0; g < 4; ++g)
            if (sel[g])
                for (unsigned j = 0; j < B; ++j)
                    a[j].add_masked(slice(body, gs.x, j, g), sel[g]);
        v4 s;
        std::memcpy(&s, sel, 32);
        v4 acc = 0;
        for (unsigned b = 0; b < R; ++b)
            acc += __builtin_elementwise_popcount(plane_words(body, gs.x, b) & s) << b;
        u64 p = __builtin_reduce_add(acc);
        for (unsigned j = 0; j < B; ++j)
            p += a[j].total() << (R + 8 * j);
        return p;
    }

    static u64 point(const u8 *body, unsigned row) {
        const unsigned g = row / 64, i = row % 64;
        u64 v = 0;
        if constexpr (L == layout::local8) {
            const u8 *tile = body + (row / 8) * K;
            for (unsigned j = 0; j < B; ++j)
                v |= u64{tile[(row % 8) * B + j]} << (R + 8 * j);
            if constexpr (R > 0) {
                // Bit row%8 of each plane byte, gathered to bits 0..R-1: the
                // multiply lands bit 8k at bit 56 + k with no carries.
                const u64 y = load_le<u64>(tile + 8 * B) >> (row % 8) & (0x0101010101010101ull >> (64 - 8 * R));
                v |= (y * 0x0102040810204080ull) >> 56;
            }
        } else {
            using P = planes<L, K>;
            for (unsigned j = 0; j < B; ++j)
                v |= u64{body[P::slice(j, g) + i]} << (R + 8 * j);
            for (unsigned b = 0; b < R; ++b)
                v |= u64{static_cast<u8>(body[P::word(b, g) + i / 8] >> (i % 8)) & 1u} << b;
        }
        return v;
    }

    static void unpack(const u8 *body, u32 *out) {
        typedef u8 bytes __attribute__((ext_vector_type(64)));
        typedef u32 words __attribute__((ext_vector_type(64)));
        for (unsigned g = 0; g < 4; ++g) {
            const group<K> x = fetch<L, K>(body, g);
            words v = 0;
            if constexpr (R) {
                simd::v64 res = simd::and_(simd::expand(x.w[0]), simd::splat(1));
                for (unsigned b = 1; b < R; ++b)
                    res = simd::or_(res, simd::and_(simd::expand(x.w[b]), simd::splat(static_cast<u8>(1u << b))));
                v = __builtin_convertvector(__builtin_bit_cast(bytes, res), words);
            }
            for (unsigned j = 0; j < B; ++j)
                v |= __builtin_convertvector(__builtin_bit_cast(bytes, x.s[j]), words) << (R + 8 * j);
            std::memcpy(out + 64 * g, &v, sizeof v);
        }
    }
};

// Leaves over a byte-lane block: 256 values of T. The native narrow array is
// the same leaf over a global-FOR array.
template <class T> struct lanes {
    static constexpr unsigned N = 64 / sizeof(T);  // rows per 64-byte vector
    typedef T vec __attribute__((ext_vector_type(N)));
    typedef bool mask __attribute__((ext_vector_type(N)));
    using bits = std::conditional_t<N == 64, u64, std::conditional_t<N == 32, u32, std::conditional_t<N == 16, u16, u8>>>;
    static vec load(const u8 *p) {
        vec x;
        std::memcpy(&x, p, 64);
        return x;
    }
    static bits ge_bits(vec x, T c) { return __builtin_bit_cast(bits, __builtin_convertvector(x >= c, mask)); }
    static bits eq_bits(vec x, T c) { return __builtin_bit_cast(bits, __builtin_convertvector(x == c, mask)); }

    // Accumulator lanes: u32 for 8- and 16-bit values, u64 above (a 64-bit
    // lane's SUM is modulo 2^64, as the oracle's).
    using A = std::conditional_t<sizeof(T) <= 2, u32, u64>;
    typedef A avec __attribute__((ext_vector_type(N)));
    static u64 sum(const u8 *body) {
        if constexpr (sizeof(T) == 1) {
            simd::acc a;
            for (unsigned v = 0; v < 4; ++v)
                a.add(simd::load(body + 64 * v));
            return a.total();
        } else if constexpr (sizeof(T) == 4) {
            // 16-bit halves in u32 lanes: no widening (16 values per lane).
            vec lo = 0, hi = 0;
            for (unsigned v = 0; v < 16; ++v) {
                const vec x = load(body + 64 * v);
                lo += x & 0xffffu, hi += x >> 16;
            }
            typedef u64 w16 __attribute__((ext_vector_type(16)));
            return __builtin_reduce_add(__builtin_convertvector(lo, w16)) + (__builtin_reduce_add(__builtin_convertvector(hi, w16)) << 16);
        } else {
            avec acc = 0;
            for (unsigned v = 0; v < 4 * sizeof(T); ++v)
                acc += __builtin_convertvector(load(body + 64 * v), avec);
            return __builtin_reduce_add(acc);
        }
    }
    // Lanes kept where their row bit is set.
    static vec keep(vec x, u64 rows) {
#if BODIES_ISA_AVX512
        const mask k = __builtin_bit_cast(mask, static_cast<bits>(rows));
        typedef std::make_signed_t<T> S;
        typedef S svec __attribute__((ext_vector_type(N)));
        return x & __builtin_bit_cast(vec, -__builtin_convertvector(k, svec));
#else
        static_assert(N <= 32);  // u8 lanes take the simd path
#if BODIES_ISA_NEON
        // Per 16-byte register, its rows' bits duplicated and tested.
        if constexpr (sizeof(T) == 2) {
            auto q = __builtin_bit_cast(uint16x8x4_t, x);
            const uint16x8_t bit = {1, 2, 4, 8, 16, 32, 64, 128};
            for (unsigned i = 0; i < 4; ++i)
                q.val[i] = vandq_u16(q.val[i], vtstq_u16(vdupq_n_u16(static_cast<u16>(rows >> (8 * i))), bit));
            return __builtin_bit_cast(vec, q);
        } else if constexpr (sizeof(T) == 4) {
            auto q = __builtin_bit_cast(uint32x4x4_t, x);
            const uint32x4_t bit = {1, 2, 4, 8};
            for (unsigned i = 0; i < 4; ++i)
                q.val[i] = vandq_u32(q.val[i], vtstq_u32(vdupq_n_u32(static_cast<u32>(rows >> (4 * i))), bit));
            return __builtin_bit_cast(vec, q);
        } else {
            auto q = __builtin_bit_cast(uint64x2x4_t, x);
            const uint64x2_t bit = {1, 2};
            for (unsigned i = 0; i < 4; ++i)
                q.val[i] = vandq_u64(q.val[i], vtstq_u64(vdupq_n_u64(rows >> (2 * i)), bit));
            return __builtin_bit_cast(vec, q);
        }
#else
        typedef u32 bvec __attribute__((ext_vector_type(N)));
        bvec bit;
        for (unsigned i = 0; i < N; ++i)
            bit[i] = 1u << i;
        const bvec r = static_cast<u32>(rows);
        return x & __builtin_convertvector((r & bit) != 0, vec);
#endif
#endif
    }
    static void mask_ge(const u8 *body, u64 c, u64 *m) {
        for (unsigned g = 0; g < 4; ++g) {
            u64 w = 0;
            for (unsigned v = 0; v < sizeof(T); ++v)
                w |= u64{ge_bits(load(body + 64 * (sizeof(T) * g + v)), static_cast<T>(c))} << (N * v);
            m[g] = w;
        }
    }
    // Counts: row bits and popcount where comparisons give masks (AVX-512);
    // elsewhere lane masks subtracted into a lane accumulator.
    template <bool eq> static unsigned count(const u8 *body, u64 c) {
#if BODIES_ISA_AVX512
        unsigned n = 0;
        for (unsigned v = 0; v < 4 * sizeof(T); ++v) {
            const vec x = load(body + 64 * v);
            n += static_cast<unsigned>(std::popcount(eq ? eq_bits(x, static_cast<T>(c)) : ge_bits(x, static_cast<T>(c))));
        }
        return n;
#else
        vec a = 0;
        for (unsigned v = 0; v < 4 * sizeof(T); ++v) {
            const vec x = load(body + 64 * v);
            if constexpr (eq)
                a -= __builtin_bit_cast(vec, x == static_cast<T>(c));
            else
                a -= __builtin_bit_cast(vec, x >= static_cast<T>(c));
        }
        typedef u32 wide __attribute__((ext_vector_type(N)));
        return __builtin_reduce_add(__builtin_convertvector(a, wide));
#endif
    }
    static unsigned count_ge(const u8 *body, u64 c) { return count<false>(body, c); }
    static unsigned count_eq(const u8 *body, u64 c) { return count<true>(body, c); }
    static u64 masked_sum(const u8 *body, const u64 *sel) {
        if constexpr (sizeof(T) == 1) {
            simd::acc a;
            for (unsigned g = 0; g < 4; ++g)
                if (sel[g])
                    a.add_masked(simd::load(body + 64 * g), sel[g]);
            return a.total();
        } else {
            avec s = 0;
            for (unsigned g = 0; g < 4; ++g) {
                if (!sel[g])
                    continue;
                for (unsigned v = 0; v < sizeof(T); ++v)
                    s += __builtin_convertvector(keep(load(body + 64 * (sizeof(T) * g + v)), sel[g] >> (N * v)), avec);
            }
            return __builtin_reduce_add(s);
        }
    }
    static u64 max(const u8 *body) {
        vec m = 0;
        for (unsigned v = 0; v < 4 * sizeof(T); ++v)
            m = __builtin_elementwise_max(m, load(body + 64 * v));
        return __builtin_reduce_max(m);
    }
    static u64 min(const u8 *body) {
        vec m = ~vec(0);
        for (unsigned v = 0; v < 4 * sizeof(T); ++v)
            m = __builtin_elementwise_min(m, load(body + 64 * v));
        return __builtin_reduce_min(m);
    }
    static unsigned first_ge(const u8 *body, u64 c) {
        for (unsigned v = 0; v < 4 * sizeof(T); ++v)
            if (const bits b = ge_bits(load(body + 64 * v), static_cast<T>(c)))
                return N * v + static_cast<unsigned>(std::countr_zero(b));
        return block_rows;
    }
    static u64 point(const u8 *body, unsigned row) { return load_le<T>(body + sizeof(T) * row); }
    static void unpack(const u8 *body, u32 *out) {
        typedef u32 v16 __attribute__((ext_vector_type(16)));
        typedef T t16 __attribute__((ext_vector_type(16)));
        for (unsigned i = 0; i < block_rows; i += 16) {
            t16 x;
            std::memcpy(&x, body + sizeof(T) * i, sizeof(x));
            const v16 w = __builtin_convertvector(x, v16);
            std::memcpy(out + i, &w, sizeof(w));
        }
    }
};
}  // namespace bodies
