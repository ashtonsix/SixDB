// Leaves over a runs block (body.h): n >= 2 u8 ends, then n run values of T.
// Every leaf works per run, never per row, except unpack. Run lengths come
// from adjacent ends 16 runs at a time with no carried dependency.
#pragma once

#include "packed.h"

namespace bodies {

template <class T> struct runs {
    typedef u8 e16 __attribute__((ext_vector_type(16)));
    typedef bool m16 __attribute__((ext_vector_type(16)));
    typedef T t16 __attribute__((ext_vector_type(16)));
    using W = std::conditional_t<sizeof(T) <= 2, u32, u64>;
    typedef W w16 __attribute__((ext_vector_type(16)));
    typedef u16 h16 __attribute__((ext_vector_type(16)));

    static const T *values(const u8 *body, unsigned n) { return reinterpret_cast<const T *>(body + n); }
    template <class V> static V load(const void *p) {
        V x;
        std::memcpy(&x, p, sizeof(V));
        return x;
    }
    // Lengths of runs r0..r0+15 (lanes past n are garbage; callers mask).
    static e16 lengths(const u8 *e, unsigned r0) {
        const e16 end = load<e16>(e + r0);
        const e16 prev = r0 ? load<e16>(e + r0 - 1)
                            : __builtin_shufflevector(end, e16(255), 16, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
        return end - prev;  // u8 wrap: the first run's is e[0] + 1
    }
    static m16 live(unsigned r0, unsigned n) {
        const e16 iota = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        return __builtin_convertvector(iota < static_cast<u8>(n - r0 > 16 ? 16 : n - r0), m16);
    }

    // u64 for values up to 32 bits; u128 for 64-bit values (by halves).
    using total = std::conditional_t<sizeof(T) == 8, u128, u64>;
    static total sum(const u8 *body, unsigned n) {
        const u8 *v = body + n;
        if constexpr (sizeof(T) == 8) {
            w16 lo = 0, hi = 0;
            for (unsigned r0 = 0; r0 < n; r0 += 16) {
                const w16 len = __builtin_convertvector(lengths(body, r0), w16) * (__builtin_convertvector(live(r0, n), w16) & 1);
                const w16 x = load<t16>(v + sizeof(T) * r0);
                lo += (x & 0xffffffffu) * len;
                hi += (x >> 32) * len;
            }
            return static_cast<u128>(__builtin_reduce_add(lo)) + (static_cast<u128>(__builtin_reduce_add(hi)) << 32);
        } else {
            w16 s = 0;
            for (unsigned r0 = 0; r0 < n; r0 += 16) {
                const w16 len = __builtin_convertvector(lengths(body, r0), w16);
                const w16 x = __builtin_convertvector(load<t16>(v + sizeof(T) * r0), w16);
                s += x * len * (__builtin_convertvector(live(r0, n), w16) & 1);
            }
            return __builtin_reduce_add(s);
        }
    }
    static unsigned count_ge(const u8 *body, unsigned n, u64 c) {
        h16 s = 0;
        const u8 *v = body + n;
        for (unsigned r0 = 0; r0 < n; r0 += 16) {
            const h16 len = __builtin_convertvector(lengths(body, r0), h16);
            const t16 x = load<t16>(v + sizeof(T) * r0);
            const m16 keep = live(r0, n) & __builtin_convertvector(x >= static_cast<T>(c), m16);
            s += len * (__builtin_convertvector(keep, h16) & 1);
        }
        return __builtin_reduce_add(s);
    }
    // Row mask, one toggle per run: toggle bits where the pass state changes,
    // then a shift prefix XOR.
    static void mask_ge_every_run(const u8 *body, unsigned n, u64 c, u64 *m) {
        const T *v = values(body, n);
        u64 t[4] = {0, 0, 0, 0};
        unsigned prev = 0, start = 0;
        for (unsigned r = 0; r < n; ++r) {
            const unsigned p = load_le<T>(v + r) >= c;
            t[start >> 6] ^= u64{p ^ prev} << (start & 63);
            prev = p;
            start = unsigned{body[r]} + 1;
        }
        u64 carry = 0;
        for (unsigned w = 0; w < 4; ++w) {
            u64 x = t[w];
            x ^= x << 1, x ^= x << 2, x ^= x << 4, x ^= x << 8, x ^= x << 16, x ^= x << 32;
            x ^= carry;
            carry = u64{0} - (x >> 63);
            m[w] = x;
        }
    }
    // The same mask with work proportional to pass-state changes: pass bits
    // for 16 runs per compare, transitions t = p ^ (p << 1), one toggle bit
    // per transition at its run's start (ascending, so one word register at a
    // time), then a carry-less prefix XOR per word.
    static void mask_ge(const u8 *body, unsigned n, u64 c, u64 *m) {
        const u8 *v = body + n;
        u64 p[4] = {0, 0, 0, 0};
        for (unsigned r0 = 0; r0 < n; r0 += 16) {
            const m16 pass = live(r0, n) & __builtin_convertvector(load<t16>(v + sizeof(T) * r0) >= static_cast<T>(c), m16);
            p[r0 >> 6] |= u64{__builtin_bit_cast(u16, pass)} << (r0 & 63);
        }
        u64 t[4] = {0, 0, 0, 0}, word = 0, in = 0;
        u64 carry = 0;
        for (unsigned q = 0; q < 4 && 64 * q < n; ++q) {
            u64 x = p[q] ^ (p[q] << 1 | carry);
            carry = p[q] >> 63;
            if (n - 64 * q < 64)
                x &= (u64{1} << (n - 64 * q)) - 1;
            while (x) {
                const unsigned r = 64 * q + static_cast<unsigned>(std::countr_zero(x));
                x &= x - 1;
                const unsigned s = r ? unsigned{body[r - 1]} + 1 : 0;
                if ((s >> 6) != in)
                    t[in] = word, word = 0, in = s >> 6;
                word ^= u64{1} << (s & 63);
            }
        }
        t[in] = word;
        u64 prefix = 0;
        for (unsigned w = 0; w < 4; ++w) {
            const u64 x = prefix_xor(t[w]) ^ prefix;
            prefix = u64{0} - (x >> 63);
            m[w] = x;
        }
    }
    static u64 prefix_xor(u64 x) {
#if defined(__PCLMUL__)
        return static_cast<u64>(_mm_cvtsi128_si64(_mm_clmulepi64_si128(_mm_cvtsi64_si128(static_cast<long long>(x)),
                                                                        _mm_set1_epi64x(-1), 0)));
#elif defined(__ARM_FEATURE_AES)
        return vgetq_lane_u64(vreinterpretq_u64_p128(vmull_p64(x, ~u64{0})), 0);
#else
        x ^= x << 1, x ^= x << 2, x ^= x << 4, x ^= x << 8, x ^= x << 16, x ^= x << 32;
        return x;
#endif
    }
    // Selected rows: C(x) selected rows below x, per run C(end) - C(start).
    static total masked_sum_scalar(const u8 *body, unsigned n, const u64 *sel) {
        u32 pre[5] = {0};
        for (unsigned w = 0; w < 4; ++w)
            pre[w + 1] = pre[w] + static_cast<u32>(std::popcount(sel[w]));
        const T *v = values(body, n);
        total s = 0;
        u32 below = 0;
        for (unsigned r = 0; r + 1 < n; ++r) {
            const unsigned x = unsigned{body[r]} + 1, w = x >> 6;
            const u32 c = pre[w] + static_cast<u32>(std::popcount(sel[w] & ((u64{1} << (x & 63)) - 1)));
            s += total{load_le<T>(v + r)} * (c - below);
            below = c;
        }
        return s + total{load_le<T>(v + n - 1)} * (pre[4] - below);
    }
    // Selected rows by Abel summation over run starts s_r: with d_r =
    // v_r - v_{r-1}, SUM = sum_r d_r (selected - C(s_r)), where C(s) counts
    // selected rows below s from a byte prefix-count table and the selection
    // bytes, 16 runs per step (wrapping arithmetic; the true SUM fits).
    static total masked_sum_prefix(const u8 *body, unsigned n, const u64 *sel)
        requires(sizeof(T) <= 4)
    {
        alignas(16) u8 bytes[32], below[32];
        std::memcpy(bytes, sel, 32);
        // Exclusive prefix of the selection bytes' popcounts, 16 lanes at a time.
        const e16 z = 0;
        e16 lo = __builtin_elementwise_popcount(load<e16>(bytes)), hi = __builtin_elementwise_popcount(load<e16>(bytes + 16));
        const auto inclusive = [&](e16 x) {
            x += __builtin_shufflevector(z, x, 0, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30);
            x += __builtin_shufflevector(z, x, 0, 1, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29);
            x += __builtin_shufflevector(z, x, 0, 1, 2, 3, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27);
            x += __builtin_shufflevector(z, x, 0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23);
            return x;
        };
        const e16 lo_in = inclusive(lo), hi_in = inclusive(hi);
        const e16 lo_ex = lo_in - lo, hi_ex = hi_in - hi + lo_in[15];
        std::memcpy(below, &lo_ex, 16), std::memcpy(below + 16, &hi_ex, 16);
        const W selected = static_cast<W>(std::popcount(sel[0]) + std::popcount(sel[1]) + std::popcount(sel[2]) + std::popcount(sel[3]));
        alignas(16) static constexpr u8 low[32] = {0, 1, 3, 7, 15, 31, 63, 127};
        const u8 *v = body + n;
        w16 s = 0;
        for (unsigned r0 = 0; r0 < n; r0 += 16) {
            // Starts: e[r - 1] + 1, and 0 for the first run (255 + 1 wraps to 0).
            const e16 end = load<e16>(body + r0);
            const e16 start = (r0 ? load<e16>(body + r0 - 1)
                                  : __builtin_shufflevector(end, e16(255), 16, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14)) +
                              1;
            const e16 k = start >> 3;
            const e16 c = simd::lookup32(below, k) +
                          __builtin_elementwise_popcount(simd::lookup32(bytes, k) & simd::lookup32(low, start & 7));
            const w16 x = __builtin_convertvector(load<t16>(v + sizeof(T) * r0), w16);
            const w16 prev = r0 ? __builtin_convertvector(load<t16>(v + sizeof(T) * (r0 - 1)), w16)
                                : __builtin_shufflevector(x, w16(0), 16, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
            const w16 live_w = __builtin_convertvector(live(r0, n), w16) & 1;
            s += (x - prev) * (selected - __builtin_convertvector(c, w16)) * live_w;
        }
        return __builtin_reduce_add(s);
    }
    static total masked_sum(const u8 *body, unsigned n, const u64 *sel) {
        if constexpr (sizeof(T) <= 4)
            return masked_sum_prefix(body, n, sel);
        else
            return masked_sum_scalar(body, n, sel);
    }
    static t16 on(unsigned r0, unsigned n) {
        return (__builtin_convertvector(live(r0, n), t16) & 1) * static_cast<T>(~T(0));
    }
    static u64 max(const u8 *body, unsigned n) {
        t16 m = 0;
        for (unsigned r0 = 0; r0 < n; r0 += 16)
            m = __builtin_elementwise_max(m, load<t16>(body + n + sizeof(T) * r0) & on(r0, n));
        return __builtin_reduce_max(m);
    }
    static u64 min(const u8 *body, unsigned n) {
        t16 m = ~t16(0);
        for (unsigned r0 = 0; r0 < n; r0 += 16)
            m = __builtin_elementwise_min(m, load<t16>(body + n + sizeof(T) * r0) | ~on(r0, n));
        return __builtin_reduce_min(m);
    }
    // First row with value >= c: the start of the first passing run.
    static unsigned first_ge(const u8 *body, unsigned n, u64 c) {
        for (unsigned r0 = 0; r0 < n; r0 += 16) {
            const m16 pass = live(r0, n) & __builtin_convertvector(load<t16>(body + n + sizeof(T) * r0) >= static_cast<T>(c), m16);
            if (const u16 b = __builtin_bit_cast(u16, pass)) {
                const unsigned r = r0 + static_cast<unsigned>(std::countr_zero(b));
                return r ? unsigned{body[r - 1]} + 1 : 0;
            }
        }
        return block_rows;
    }
    // With a run-start bitmap beside the body (evidence: 32 bytes, bit i set
    // where a run starts at row i; run_starts builds it).
    static void run_starts(const u8 *body, unsigned n, u64 *s) {
        s[0] = 1, s[1] = s[2] = s[3] = 0;
        for (unsigned r = 0; r + 1 < n; ++r) {
            const unsigned x = unsigned{body[r]} + 1;
            s[x >> 6] |= u64{1} << (x & 63);
        }
    }
    // Pass transitions deposited at run starts (PDEP), then a prefix XOR:
    // no per-run loop.
    static void mask_ge_starts(const u8 *body, unsigned n, const u64 *starts, u64 c, u64 *m) {
        const u8 *v = body + n;
        u64 p[5] = {0, 0, 0, 0, 0};
        for (unsigned r0 = 0; r0 < n; r0 += 16) {
            const m16 pass = live(r0, n) & __builtin_convertvector(load<t16>(v + sizeof(T) * r0) >= static_cast<T>(c), m16);
            p[r0 >> 6] |= u64{__builtin_bit_cast(u16, pass)} << (r0 & 63);
        }
        u64 t[5], carry = 0;
        for (unsigned q = 0; q < 4; ++q)
            t[q] = p[q] ^ (p[q] << 1 | carry), carry = p[q] >> 63;
        t[4] = 0;
        unsigned rank = 0;
        u64 prefix = 0;
        for (unsigned w = 0; w < 4; ++w) {
            const unsigned k = static_cast<unsigned>(std::popcount(starts[w]));
            const unsigned q = rank >> 6, sh = rank & 63;
            const u64 bits = sh ? (t[q] >> sh | t[q + 1] << (64 - sh)) : t[q];
            const u64 x = prefix_xor(pdep(bits, starts[w])) ^ prefix;
            prefix = u64{0} - (x >> 63);
            m[w] = x;
            rank += k;
        }
    }
    static u64 point_starts(const u8 *body, unsigned n, const u64 *starts, unsigned row) {
        const unsigned w = row >> 6;
        unsigned idx = static_cast<unsigned>(std::popcount(starts[w] & (~u64{0} >> (63 - (row & 63)))));
        for (unsigned q = 0; q < w; ++q)
            idx += static_cast<unsigned>(std::popcount(starts[q]));
        return load_le<T>(values(body, n) + idx - 1);
    }
    static u64 pdep(u64 x, u64 m) {
#if defined(__BMI2__)
        return _pdep_u64(x, m);
#else
        u64 r = 0;
        for (u64 bit = 1; m; bit <<= 1, m &= m - 1)
            r |= (x & bit) ? (m & (0 - m)) : 0;
        return r;
#endif
    }
    // The run holding row: the count of ends below it, accumulated in lanes.
    static u64 point(const u8 *body, unsigned n, unsigned row) {
        const e16 iota = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        e16 before = 0;
        for (unsigned r0 = 0; r0 < n; r0 += 16) {
            const auto below = (load<e16>(body + r0) < static_cast<u8>(row)) & (iota < static_cast<u8>(n - r0 > 16 ? 16 : n - r0));
            before += __builtin_bit_cast(e16, below) & 1;
        }
        return load_le<T>(values(body, n) + __builtin_reduce_add(__builtin_convertvector(before, h16)));
    }
    // Rows of each run written by 16-row splat stores in run order, two
    // unconditionally (runs average 256 / n rows), so a store past a run's
    // end is overwritten by the next run; out needs 32 rows of slack.
    template <class U, class F> static void fill(const u8 *body, unsigned n, U *out, F &&value) {
        typedef U splat __attribute__((ext_vector_type(16)));
        const T *v = values(body, n);
        unsigned start = 0;
        for (unsigned r = 0; r < n; ++r) {
            const unsigned end = unsigned{body[r]} + 1;
            const splat x = static_cast<U>(value(load_le<T>(v + r)));
            std::memcpy(out + start, &x, sizeof x);
            std::memcpy(out + start + 16, &x, sizeof x);
            for (unsigned i = start + 32; i < end; i += 16)
                std::memcpy(out + i, &x, sizeof x);
            start = end;
        }
    }
    // Row mask by scattering one toggle byte per run start (independent byte
    // stores), byte compares to bits, then a prefix XOR.
    static void mask_ge_scatter(const u8 *body, unsigned n, u64 c, u64 *m) {
        alignas(64) u8 toggle[block_rows];
        std::memset(toggle, 0, sizeof toggle);
        const T *v = values(body, n);
        unsigned prev = load_le<T>(v) >= c;
        toggle[0] = static_cast<u8>(prev);
        for (unsigned r = 1; r < n; ++r) {
            const unsigned p = load_le<T>(v + r) >= c;
            toggle[unsigned{body[r - 1]} + 1] = static_cast<u8>(p ^ prev);
            prev = p;
        }
        u64 prefix = 0;
        for (unsigned g = 0; g < 4; ++g) {
            const u64 x = prefix_xor(simd::ge(simd::load(toggle + 64 * g), 1)) ^ prefix;
            prefix = u64{0} - (x >> 63);
            m[g] = x;
        }
    }
    static void unpack(const u8 *body, unsigned n, u32 *out) {
        fill<u32>(body, n, out, [](T x) { return x; });
    }
    // Row mask by filling pass bytes, then byte compares to bits.
    static void mask_ge_fill(const u8 *body, unsigned n, u64 c, u64 *m) {
        alignas(64) u8 rows[block_rows + 32];
        fill<u8>(body, n, rows, [c](T x) { return x >= c ? 0xff : 0; });
        for (unsigned g = 0; g < 4; ++g)
            m[g] = simd::ge(simd::load(rows + 64 * g), 0x80);
    }
    // Selected rows by expanding run values into a row lane, then the lane leaf.
    static u64 masked_sum_fill(const u8 *body, unsigned n, const u64 *sel) {
        alignas(64) T rows[block_rows + 32];
        fill<T>(body, n, rows, [](T x) { return x; });
        return lanes<T>::masked_sum(reinterpret_cast<const u8 *>(rows), sel);
    }
};
}  // namespace bodies
