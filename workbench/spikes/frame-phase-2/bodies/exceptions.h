// Packed FOR with encode-time exceptions: a plane256 body at width K whose
// exception slots hold 0, then n (1..64) ascending u8 positions and n
// exception values (u32, relative to the block base, any width). Every leaf
// is the packed leaf plus a correction over the n exceptions; nothing is
// decoded and patched.
//
// Layout: [plane256 K][n: u8][positions: n][pad to 4][values: n * u32]
#pragma once

#include "packed.h"

namespace bodies {

constexpr std::size_t patched_bytes(unsigned k, unsigned n) { return 32 * k + 1 + (n + 3) / 4 * 4 + 4 * std::size_t{n}; }

// Values >= 2^k become exceptions (at most 64); returns false if too many.
inline bool encode_patched(unsigned k, const u64 *v, u8 *out, unsigned &n) {
    u64 r[block_rows];
    u8 pos[64];
    u32 val[64];
    n = 0;
    const u64 top = (u64{1} << k) - 1;
    for (unsigned i = 0; i < block_rows; ++i) {
        if (v[i] > top) {
            if (n == 64 || v[i] > 0xffffffffu)
                return false;
            pos[n] = static_cast<u8>(i), val[n] = static_cast<u32>(v[i]), ++n;
            r[i] = 0;
        } else {
            r[i] = v[i];
        }
    }
    encode(layout::plane256, k, r, out);
    u8 *t = out + 32 * k;
    t[0] = static_cast<u8>(n);
    std::memcpy(t + 1, pos, n);
    std::memset(t + 1 + n, 0, (n + 3) / 4 * 4 - n);
    std::memcpy(t + 1 + (n + 3) / 4 * 4, val, 4 * std::size_t{n});
    return true;
}

template <unsigned K> struct patched {
    using P = packed<layout::plane256, K>;
    static constexpr u64 top = (u64{1} << K) - 1;
    struct tail {
        unsigned n;
        const u8 *pos;
        const u8 *val;  // u32, unaligned
        u32 at(unsigned i) const { return load_le<u32>(val + 4 * i); }
    };
    static tail exceptions(const u8 *body) {
        const u8 *t = body + 32 * K;
        const unsigned n = t[0];
        return {n, t + 1, t + 1 + (n + 3) / 4 * 4};
    }
    typedef u32 x16 __attribute__((ext_vector_type(16)));
    typedef u8 p16 __attribute__((ext_vector_type(16)));
    typedef bool m16 __attribute__((ext_vector_type(16)));
    static m16 live(unsigned i0, unsigned n) {
        const p16 iota = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        return __builtin_convertvector(iota < static_cast<u8>(n - i0 > 16 ? 16 : n - i0), m16);
    }
    template <class V> static V load(const u8 *p) {
        V x;
        std::memcpy(&x, p, sizeof x);
        return x;
    }

    static u64 sum(const u8 *body) {
        const tail e = exceptions(body);
        typedef u64 w16 __attribute__((ext_vector_type(16)));
        w16 s = 0;
        for (unsigned i0 = 0; i0 < e.n; i0 += 16)
            s += __builtin_convertvector(load<x16>(e.val + 4 * i0), w16) * (__builtin_convertvector(live(i0, e.n), w16) & 1);
        return P::sum(body) + __builtin_reduce_add(s);
    }
    // Exceptions >= c, and slots (stored 0) that the packed leaf counted.
    static unsigned count_ge(const u8 *body, u64 c) {
        const tail e = exceptions(body);
        if (c == 0)
            return block_rows;
        const unsigned n = c <= top ? P::count_ge(body, c) : 0;
        if (c > 0xffffffffu)
            return n;
        x16 hits = 0;
        for (unsigned i0 = 0; i0 < e.n; i0 += 16) {
            const m16 pass = live(i0, e.n) & __builtin_convertvector(load<x16>(e.val + 4 * i0) >= static_cast<u32>(c), m16);
            hits += __builtin_convertvector(pass, x16) & 1;
        }
        return n + __builtin_reduce_add(hits);
    }
    static unsigned count_eq(const u8 *body, u64 c) {
        const tail e = exceptions(body);
        unsigned n = c <= top ? P::count_eq(body, c) : 0;
        if (c == 0)
            n -= e.n;  // slots hold 0; exceptions are never 0
        for (unsigned i = 0; i < e.n; ++i)
            n += e.at(i) == c;
        return n;
    }
    // Packed mask, then each exception's bit set to its own comparison.
    static void mask_ge(const u8 *body, u64 c, u64 *m) {
        const tail e = exceptions(body);
        if (c <= top) {
            P::mask_ge(body, c, m);
        } else {
            m[0] = m[1] = m[2] = m[3] = 0;
        }
        for (unsigned i = 0; i < e.n; ++i) {
            const unsigned r = e.pos[i];
            const u64 bit = u64{1} << (r & 63), pass = u64{0} - (e.at(i) >= c);
            m[r >> 6] = (m[r >> 6] & ~bit) | (bit & pass);
        }
    }
    static u64 masked_sum(const u8 *body, const u64 *sel) {
        const tail e = exceptions(body);
        u64 s = P::masked_sum(body, sel);
        for (unsigned i = 0; i < e.n; ++i) {
            const unsigned r = e.pos[i];
            s += u64{e.at(i)} & (u64{0} - (sel[r >> 6] >> (r & 63) & 1));
        }
        return s;
    }
    // The packed point, unless row is an exception: one 16-position compare
    // per 16 exceptions.
    static u64 point(const u8 *body, unsigned row) {
        const tail e = exceptions(body);
        for (unsigned i0 = 0; i0 < e.n; i0 += 16) {
            const m16 hit = live(i0, e.n) & __builtin_convertvector(load<p16>(e.pos + i0) == static_cast<u8>(row), m16);
            if (const u16 h = __builtin_bit_cast(u16, hit))
                return e.at(i0 + static_cast<unsigned>(std::countr_zero(h)));
        }
        return P::point(body, row);
    }
    static u64 max(const u8 *body) {
        const tail e = exceptions(body);
        u64 m = P::max(body);
        for (unsigned i = 0; i < e.n; ++i)
            m = std::max<u64>(m, e.at(i));
        return m;
    }
    // MIN over the packed rows that are not exception slots, and the exceptions.
    static u64 min(const u8 *body) {
        const tail e = exceptions(body);
        u64 rows[4] = {~u64{0}, ~u64{0}, ~u64{0}, ~u64{0}};
        u64 m = ~u64{0};
        for (unsigned i = 0; i < e.n; ++i) {
            rows[e.pos[i] >> 6] &= ~(u64{1} << (e.pos[i] & 63));
            m = std::min<u64>(m, e.at(i));
        }
        if (e.n == block_rows)
            return m;
        return std::min(m, P::template extremum<false>(body, rows));
    }
    static unsigned first_ge(const u8 *body, u64 c) {
        if (c == 0)
            return 0;
        const tail e = exceptions(body);
        unsigned first = c <= top ? P::first_ge(body, c) : block_rows;
        for (unsigned i = 0; i < e.n && e.pos[i] < first; ++i)
            if (e.at(i) >= c)
                return e.pos[i];
        return first;
    }
    static void unpack(const u8 *body, u32 *out) {
        const tail e = exceptions(body);
        P::unpack(body, out);
        for (unsigned i = 0; i < e.n; ++i)
            out[e.pos[i]] = e.at(i);
    }
};
}  // namespace bodies
