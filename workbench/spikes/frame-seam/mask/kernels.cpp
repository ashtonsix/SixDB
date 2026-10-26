// Masked SUM and summary over one decoded 256-row block: the Frame fallback
// formulations tried in the seam spike, plus neutral-contribution variants,
// as separate out-of-line functions so their loops can be read in assembly.
// Only <cstdint>, so the same file cross-compiles for x86-64 AVX2.
//
// x: n <= 256 decoded values. words: the block's selection, bit i of
// words[i / 64] selects row i, clear past n.
#include <cstdint>

using u64 = std::uint64_t;
using u128 = unsigned __int128;
struct summ {
    u128 sum;
    u64 count, min, max;
};
#define KERNEL extern "C" __attribute__((noinline))

// Kept Frame fallback (stage.cpp, request::rows_of(b).for_each): visit each
// selected row, add to a u128.
KERNEL u128 sum_bitwalk(const u64 *x, const u64 *words, unsigned n) {
    u128 s = 0;
    for (unsigned w = 0; w * 64 < n; ++w)
        for (u64 bits = words[w]; bits; bits &= bits - 1)
            s += x[w * 64 + __builtin_ctzll(bits)];
    return s;
}

// Rejected Frame attempt, verbatim loop: keep = all-ones or zero from the
// row's bit, low/high 32-bit halves in u64, one fold. Inner trip count is
// the runtime k = min(64, n - 64w).
KERNEL u128 sum_rejected_and(const u64 *x, const u64 *words, unsigned n) {
    u64 lo = 0, hi = 0;
    for (unsigned w = 0; w * 64 < n; ++w)
        for (unsigned i = 0, k = n - w * 64 < 64 ? n - w * 64 : 64; i < k; ++i) {
            const u64 keep = 0 - (words[w] >> i & 1), v = x[w * 64 + i] & keep;
            lo += v & 0xffffffff, hi += v >> 32;
        }
    return (static_cast<u128>(hi) << 32) + lo;
}

// The same neutral AND with a fixed 64-row trip for whole words (the tail
// word keeps a runtime bound), and the word hoisted out of the loop.
KERNEL u128 sum_fixed_and(const u64 *x, const u64 *words, unsigned n) {
    u64 lo = 0, hi = 0;
    unsigned w = 0;
    for (; w * 64 + 64 <= n; ++w) {
        const u64 bits = words[w], *y = x + w * 64;
        for (unsigned i = 0; i < 64; ++i) {
            const u64 v = y[i] & (0 - (bits >> i & 1));
            lo += v & 0xffffffff, hi += v >> 32;
        }
    }
    for (unsigned i = w * 64; i < n; ++i) {
        const u64 v = x[i] & (0 - (words[w] >> (i % 64) & 1));
        lo += v & 0xffffffff, hi += v >> 32;
    }
    return (static_cast<u128>(hi) << 32) + lo;
}

// Two passes: expand the mask to one all-ones/zero lane per row, then a dense
// AND-and-sum over the whole block. Charges the expansion.
KERNEL u128 sum_expand(const u64 *x, const u64 *words, unsigned n) {
    u64 keep[256];
    for (unsigned i = 0; i < 256; ++i)
        keep[i] = 0 - (words[i / 64] >> (i % 64) & 1);
    u64 lo = 0, hi = 0;
    for (unsigned i = 0; i < n; ++i) {
        const u64 v = x[i] & keep[i];
        lo += v & 0xffffffff, hi += v >> 32;
    }
    return (static_cast<u128>(hi) << 32) + lo;
}

// Unmasked reference: the kept whole-block sum (block_sum in stage.cpp).
KERNEL u128 sum_dense(const u64 *x, const u64 *, unsigned n) {
    u64 lo = 0, hi = 0;
    for (unsigned r = 0; r < n; ++r)
        lo += x[r] & 0xffffffff, hi += x[r] >> 32;
    return (static_cast<u128>(hi) << 32) + lo;
}

// Kept Frame fallback for summary: visit each selected row.
KERNEL void summ_bitwalk(const u64 *x, const u64 *words, unsigned n, summ *out) {
    for (unsigned w = 0; w * 64 < n; ++w)
        for (u64 bits = words[w]; bits; bits &= bits - 1) {
            const u64 v = x[w * 64 + __builtin_ctzll(bits)];
            out->sum += v, ++out->count;
            out->min = v < out->min ? v : out->min, out->max = v > out->max ? v : out->max;
        }
}

// Rejected Frame attempt, verbatim loop: multiply halves by the 0/1 bit, and
// min/max as conditional selects guarded by the bit.
KERNEL void summ_rejected(const u64 *x, const u64 *words, unsigned n, summ *out) {
    u64 lo = 0, hi = 0, mn = out->min, mx = out->max, count = 0;
    for (unsigned w = 0; w * 64 < n; ++w) {
        const u64 bits = words[w], *y = x + w * 64;
        const unsigned k = n - w * 64 < 64 ? n - w * 64 : 64;
        for (unsigned i = 0; i < k; ++i) {
            const u64 on = bits >> i & 1, v = y[i];
            lo += (v & 0xffffffff) * on, hi += (v >> 32) * on;
            mn = on && v < mn ? v : mn, mx = on && v > mx ? v : mx;
        }
        count += __builtin_popcountll(bits);
    }
    out->sum += (static_cast<u128>(hi) << 32) + lo, out->count += count, out->min = mn, out->max = mx;
}

// Neutral identities: an unselected row contributes 0 to the sum, UINT64_MAX
// to MIN and 0 to MAX, so each lane is one AND/OR and an unconditional
// min/max; fixed 64-row trips.
KERNEL void summ_identity(const u64 *x, const u64 *words, unsigned n, summ *out) {
    u64 lo = 0, hi = 0, mn = out->min, mx = out->max, count = 0;
    unsigned w = 0;
    const auto row = [&](u64 v, u64 keep) {
        const u64 s = v & keep;
        lo += s & 0xffffffff, hi += s >> 32;
        const u64 low = v | ~keep;
        mn = low < mn ? low : mn, mx = s > mx ? s : mx;
    };
    for (; w * 64 + 64 <= n; ++w) {
        const u64 bits = words[w], *y = x + w * 64;
        for (unsigned i = 0; i < 64; ++i)
            row(y[i], 0 - (bits >> i & 1));
        count += __builtin_popcountll(bits);
    }
    for (unsigned i = w * 64; i < n; ++i)
        row(x[i], 0 - (words[w] >> (i % 64) & 1));
    if (w * 64 < n)
        count += __builtin_popcountll(words[w]);
    out->sum += (static_cast<u128>(hi) << 32) + lo, out->count += count, out->min = mn, out->max = mx;
}

// Unmasked reference: the kept whole-block summary.
KERNEL void summ_dense(const u64 *x, const u64 *, unsigned n, summ *out) {
    u64 lo = 0, hi = 0, mn = out->min, mx = out->max;
    for (unsigned r = 0; r < n; ++r) {
        lo += x[r] & 0xffffffff, hi += x[r] >> 32;
        mn = x[r] < mn ? x[r] : mn, mx = x[r] > mx ? x[r] : mx;
    }
    out->sum += (static_cast<u128>(hi) << 32) + lo, out->count += n, out->min = mn, out->max = mx;
}

// Density-adaptive: the neutral form above its NEON crossover with the bit
// walk (about 40% selected for SUM, 50% for the summary), the bit walk
// below; one popcount per block decides.
static unsigned selected_rows(const u64 *words, unsigned n) {
    unsigned c = 0;
    for (unsigned w = 0; w * 64 < n; ++w)
        c += __builtin_popcountll(words[w]);
    return c;
}
KERNEL u128 sum_adaptive(const u64 *x, const u64 *words, unsigned n) {
    return selected_rows(words, n) * 5 > n * 2 ? sum_fixed_and(x, words, n) : sum_bitwalk(x, words, n);
}
KERNEL void summ_adaptive(const u64 *x, const u64 *words, unsigned n, summ *out) {
    return selected_rows(words, n) * 2 > n ? summ_identity(x, words, n, out) : summ_bitwalk(x, words, n, out);
}
