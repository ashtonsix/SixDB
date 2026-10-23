// Narrow-key sort and partition: the 2^K-bin histogram of a block's
// residuals (K <= 8), the core of a radix partition and of a counting sort.
// A sorted block of K-bit keys is then a runs body built from the histogram
// alone (to_sorted_runs), with no row movement.
//
//   plane layouts, K <= 6: bit-sliced, a prefix tree of row masks: level t
//     holds the 2^t masks of rows whose top t residual bits equal each
//     prefix, each one AND of its parent with a plane or its complement,
//     so 2^(K+1) - 2 four-word ANDs and 2^K popcounts in all.
//   plane layouts, K 7..8: unpacked, then counted (four sub-histograms).
//   lanes: counted (four sub-histograms).
//   runs: one add per run.
#pragma once

#include "packed.h"
#include "runs.h"

namespace bodies {

namespace detail {
inline void count_bytes(const u8 *x, unsigned n, u32 *counts, unsigned bins) {
    // Four sub-histograms against store-to-load chains; only `bins` zeroed.
    alignas(64) u32 h[4][256];
    for (unsigned q = 0; q < 4; ++q)
        std::memset(h[q], 0, 4 * bins);
    unsigned i = 0;
    for (; i + 4 <= n; i += 4)
        ++h[0][x[i]], ++h[1][x[i + 1]], ++h[2][x[i + 2]], ++h[3][x[i + 3]];
    for (; i < n; ++i)
        ++h[0][x[i]];
    for (unsigned v = 0; v < bins; ++v)
        counts[v] = h[0][v] + h[1][v] + h[2][v] + h[3][v];
}
}  // namespace detail

template <layout L, unsigned K> struct packed_histogram {
    static_assert(K >= 1 && K <= 8);
    using P = packed<L, K>;
    using v4 = typename P::v4;
    static void histogram(const u8 *body, u32 *counts) {
        if constexpr (K <= 6) {
            const typename P::groups gs(body);
            v4 w[K];
            for (unsigned b = 0; b < K; ++b)
                w[b] = P::plane_words(body, gs.x, b);  // K < 8: every bit is a plane
            v4 level[1u << K];
            level[0] = ~v4(0);
            for (unsigned t = 0; t < K; ++t) {
                const unsigned b = K - 1 - t;  // split on the next bit down
                for (unsigned i = (1u << t); i-- > 0;) {
                    level[2 * i + 1] = level[i] & w[b];
                    level[2 * i] = level[i] & ~w[b];
                }
            }
            for (unsigned v = 0; v < (1u << K); ++v)
                counts[v] = P::popcount4(level[v]);
        } else {
            alignas(64) u32 x[block_rows];
            P::unpack(body, x);
            alignas(64) u8 y[block_rows];
            for (unsigned i = 0; i < block_rows; ++i)
                y[i] = static_cast<u8>(x[i]);
            detail::count_bytes(y, block_rows, counts, 1u << K);
        }
    }
};

struct lane_histogram {
    static void histogram(const u8 *body, unsigned bins, u32 *counts) { detail::count_bytes(body, block_rows, counts, bins); }
};

template <class T> struct runs_histogram {
    static void histogram(const u8 *body, unsigned n, unsigned bins, u32 *counts) {
        std::memset(counts, 0, 4 * bins);
        const T *v = runs<T>::values(body, n);
        unsigned start = 0;
        for (unsigned r = 0; r < n; ++r) {
            const unsigned end = unsigned{body[r]} + 1;
            counts[load_le<T>(v + r)] += end - start;
            start = end;
        }
    }
};

// The sorted block as a runs body (u8 values): one run per present key.
// Returns n (1 means a constant block; out then holds one end and value).
inline unsigned to_sorted_runs(const u32 *counts, unsigned bins, u8 *out) {
    u8 values[256];
    unsigned n = 0, end = 0;
    for (unsigned v = 0; v < bins; ++v)
        if (counts[v]) {
            end += counts[v];
            out[n] = static_cast<u8>(end - 1);
            values[n++] = static_cast<u8>(v);
        }
    std::memcpy(out + n, values, n);
    return n;
}
}  // namespace bodies
