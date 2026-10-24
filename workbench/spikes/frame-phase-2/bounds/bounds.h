// The bounds stage: choose an inlier window [lo, lo + 2^K) for a series of
// unsigned values; values outside it become exceptions (position, value
// relative to lo). The enclosing composition prices a choice through
// cost_model (SeriesPack body bytes per width, exception bytes), so the stage
// never assumes width is monotone in bytes (notebook/pfor-bounds.md).
//
// Every method returns a choice whose bytes are the composition's real cost
// for that (lo, K); exact() is the oracle over every lo and K.
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace frame_bounds {
using u64 = std::uint64_t;
using u32 = std::uint32_t;

inline unsigned bits(u64 x) { return static_cast<unsigned>(std::bit_width(x)); }
inline u64 mask(unsigned K) { return K >= 64 ? ~u64{0} : (u64{1} << K) - 1; }

struct cost_model {
    std::array<u32, 65> body{};  // bytes for the series' fields at width K
    unsigned position_bytes = 1;
    unsigned header_bytes = 1;  // exception count, when there are any
    unsigned min_width = 1;     // narrowest packed width the composition admits
    // Engine's read-time weight: bytes charged per exception on top of its
    // storage (patching costs points and scans; runs/README.md).
    unsigned exception_penalty = 0;
    // A block's exception capacity: positions fit one 64-byte register of u8
    // lanes, so a point finds its patch with one compare. More is infeasible.
    unsigned max_exceptions = 256;
    // An admitted-set restriction: lo fixed (run sizes minus one keep 0).
    bool pin_lo = false;
    u64 pinned_lo = 0;
    // Exception values are stored relative to lo at whole bytes, with a sign
    // bit once any value lies below lo. value_bits covers max - min.
    std::size_t operator()(unsigned K, std::size_t low, std::size_t high, unsigned value_bits) const {
        const std::size_t ex = low + high;
        if (!ex)
            return body[K];
        if (ex > max_exceptions)
            return std::size_t{1} << 40;
        const unsigned vb = (value_bits + (low != 0) + 7) / 8;
        return body[K] + header_bytes + ex * (position_bytes + vb + exception_penalty);
    }
};

struct choice {
    u64 lo = 0;
    unsigned width = 0;
    u32 low = 0, high = 0;  // exceptions below lo, above lo + 2^K - 1
    std::size_t bytes = ~std::size_t{0};
};
inline bool better(const choice &a, const choice &b) {
    return a.bytes != b.bytes ? a.bytes < b.bytes : a.low + a.high < b.low + b.high;
}

struct range {
    u64 min, max;
};
inline range minmax(const u64 *x, std::size_t n) {
    u64 a = ~u64{0}, b = 0;
    for (std::size_t i = 0; i < n; ++i)
        a = std::min(a, x[i]), b = std::max(b, x[i]);
    return {a, b};
}

// The exact cost of a window: one counting pass.
inline choice realise(const u64 *x, std::size_t n, u64 lo, unsigned K, unsigned value_bits, const cost_model &c) {
    const u64 hi = lo > ~u64{0} - mask(K) ? ~u64{0} : lo + mask(K);
    std::size_t low = 0, high = 0;
    for (std::size_t i = 0; i < n; ++i)
        low += x[i] < lo, high += x[i] > hi;
    return {lo, K, static_cast<u32>(low), static_cast<u32>(high), c(K, low, high, value_bits)};
}

// One pass over a proposed window: its exceptions on each side, and the
// window moved to start at its inliers' minimum and narrowed to their extent.
// The moved window keeps every inlier and may admit more values above, so
// its exceptions are at most those counted (realise() gives them exactly).
inline choice evaluate(const u64 *x, std::size_t n, u64 lo, unsigned K, unsigned value_bits, const cost_model &c) {
    const u64 hi = lo > ~u64{0} - mask(K) ? ~u64{0} : lo + mask(K);
    std::size_t low = 0, high = 0;
    u64 in_min = ~u64{0}, in_max = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const bool below = x[i] < lo, above = x[i] > hi;
        low += below, high += above;
        const bool in = !below & !above;
        in_min = std::min(in_min, in ? x[i] : ~u64{0});
        in_max = std::max(in_max, in ? x[i] : u64{0});
    }
    if (low + high == n)  // no inliers: everything an exception at lo
        return {lo, c.min_width, static_cast<u32>(low), static_cast<u32>(high), c(c.min_width, low, high, value_bits)};
    const unsigned k = std::max(c.min_width, bits(in_max - in_min));
    return {in_min, k, static_cast<u32>(low), static_cast<u32>(high), c(k, low, high, value_bits)};
}

// Oracle: sort, then for every width the window holding the most values
// (two pointers), plus the window anchored at the minimum (no sign bit).
// value_bits sizes exceptions (the whole series' span, for a sample).
inline choice exact_sorted(const u64 *s, std::size_t n, const cost_model &c, unsigned value_bits) {
    const unsigned vbits = bits(s[n - 1] - s[0]);
    const unsigned full = std::max(c.min_width, vbits);
    choice best{s[0], full, 0, 0, c(full, 0, 0, value_bits)};
    for (unsigned K = c.min_width; K < vbits; ++K) {
        const u64 w = mask(K);
        std::size_t j = 0, most = 0, at = 0;
        for (std::size_t i = 0; i < n; ++i) {
            while (s[i] - s[j] > w)
                ++j;
            if (i - j + 1 > most)
                most = i - j + 1, at = j;
        }
        // Among windows holding `most`, the leftmost is found first; any
        // window starting at index 0 is the no-sign-bit candidate.
        const choice a{s[at], K, static_cast<u32>(at), static_cast<u32>(n - at - most), c(K, at, n - at - most, value_bits)};
        const std::size_t in0 = std::upper_bound(s, s + n, s[0] + w) - s;
        const choice b{s[0], K, 0, static_cast<u32>(n - in0), c(K, 0, n - in0, value_bits)};
        if (better(a, best))
            best = a;
        if (better(b, best))
            best = b;
    }
    return best;
}
inline choice exact(const u64 *x, std::size_t n, const cost_model &c, std::vector<u64> &s) {
    s.assign(x, x + n);
    std::sort(s.begin(), s.end());
    return exact_sorted(s.data(), n, c, bits(s[n - 1] - s[0]));
}

// Today's runs-spike stage: lo = minimum, one counting pass per width.
inline choice min_exhaustive(const u64 *x, std::size_t n, const cost_model &c) {
    const auto [mn, mx] = minmax(x, n);
    const unsigned vbits = bits(mx - mn), full = std::max(c.min_width, vbits);
    choice best{mn, full, 0, 0, c(full, 0, 0, vbits)};
    for (unsigned K = c.min_width; K < vbits; ++K) {
        std::size_t ex = 0;
        for (std::size_t i = 0; i < n; ++i)
            ex += (x[i] - mn) >> K != 0;
        const choice t{mn, K, 0, static_cast<u32>(ex), c(K, 0, ex, vbits)};
        if (better(t, best))
            best = t;
    }
    return best;
}

// Calico Keyset POST's policy: lo = minimum, widths prior-1..prior+1, with a
// full search when there is no prior (the caller forces one every 16 blocks).
inline choice min_prior(const u64 *x, std::size_t n, const cost_model &c, int prior) {
    if (prior < 0)
        return min_exhaustive(x, n, c);
    const auto [mn, mx] = minmax(x, n);
    const unsigned vbits = bits(mx - mn), full = std::max(c.min_width, vbits);
    choice best{mn, full, 0, 0, c(full, 0, 0, vbits)};
    for (int d = -1; d <= 1; ++d) {
        const int K = prior + d;
        if (K < static_cast<int>(c.min_width) || K >= static_cast<int>(vbits))
            continue;
        std::size_t ex = 0;
        for (std::size_t i = 0; i < n; ++i)
            ex += (x[i] - mn) >> K != 0;
        const choice t{mn, static_cast<unsigned>(K), 0, static_cast<u32>(ex), c(K, 0, ex, vbits)};
        if (better(t, best))
            best = t;
    }
    return best;
}

// The T smallest and T largest values (with multiplicity), ascending and
// descending. Each of eight lanes keeps its own T extremes through a
// min/max insertion chain; the union of lanes holds the global ones.
template <unsigned T, class V = u64> struct extremes {
    std::array<V, T> small, large;
};
template <unsigned T, class V = u64> extremes<T, V> order_stats(const V *x, std::size_t n) {
    constexpr unsigned L = 8;
    V lo[T][L], hi[T][L];
    for (unsigned t = 0; t < T; ++t)
        for (unsigned l = 0; l < L; ++l)
            lo[t][l] = static_cast<V>(~V{0}), hi[t][l] = 0;
    std::size_t i = 0;
    for (; i + L <= n; i += L) {
        V a[L], b[L];
        for (unsigned l = 0; l < L; ++l)
            a[l] = b[l] = x[i + l];
        for (unsigned t = 0; t < T; ++t)
            for (unsigned l = 0; l < L; ++l) {
                const V m = std::min(lo[t][l], a[l]), M = std::max(hi[t][l], b[l]);
                a[l] = std::max(lo[t][l], a[l]), b[l] = std::min(hi[t][l], b[l]);
                lo[t][l] = m, hi[t][l] = M;
            }
    }
    for (; i < n; ++i) {
        V a = x[i], b = x[i];
        for (unsigned t = 0; t < T; ++t) {
            const V m = std::min(lo[t][0], a), M = std::max(hi[t][0], b);
            a = std::max(lo[t][0], a), b = std::min(hi[t][0], b);
            lo[t][0] = m, hi[t][0] = M;
        }
    }
    V *fl = &lo[0][0], *fh = &hi[0][0];
    std::partial_sort(fl, fl + T, fl + T * L);
    std::partial_sort(fh, fh + T, fh + T * L, std::greater<>{});
    extremes<T, V> e;
    std::copy(fl, fl + T, e.small.begin());
    std::copy(fh, fh + T, e.large.begin());
    return e;
}

// Exact over every choice with at most T-1 exceptions on each side: the
// inliers are a contiguous stretch of the sorted values, so i low and j high
// exceptions fix the window to [small[i], large[j]]. Returns the enumerated
// cost (an upper bound: the window at that width can hold more values).
template <unsigned T, class V> choice from_extremes(const extremes<T, V> &e, std::size_t n, unsigned vbits, const cost_model &c) {
    choice best;
    for (unsigned i = 0; i < T && i < n; ++i)
        for (unsigned j = 0; j < T && i + j < n; ++j) {
            if (e.large[j] < e.small[i])
                continue;
            const unsigned K = std::max(c.min_width, bits(static_cast<u64>(e.large[j] - e.small[i])));
            const choice t{e.small[i], K, i, j, c(K, i, j, vbits)};
            if (better(t, best))
                best = t;
        }
    return best;
}

// The T smallest and T largest distinct values with their multiplicities
// (fewer when the series has fewer distinct values). Reference form via a
// sorted copy; the census uses it to price the idea before making it fast.
template <unsigned T> struct distinct_extremes {
    unsigned small_n = 0, large_n = 0;
    std::array<u64, T> small, large;
    std::array<u32, T> small_count, large_count;
};
template <unsigned T> distinct_extremes<T> distinct_stats_sorted(const std::vector<u64> &s) {
    distinct_extremes<T> e;
    for (std::size_t i = 0; i < s.size() && e.small_n < T; ++i)
        if (i && s[i] == s[i - 1])
            ++e.small_count[e.small_n - 1];
        else if (e.small_n < T)
            e.small[e.small_n] = s[i], e.small_count[e.small_n++] = 1;
    // A value in the final slot may be undercounted by the loop's exit; recount it.
    e.small_count[e.small_n - 1] = static_cast<u32>(std::upper_bound(s.begin(), s.end(), e.small[e.small_n - 1]) -
                                                    std::lower_bound(s.begin(), s.end(), e.small[e.small_n - 1]));
    for (std::size_t r = 0; r < s.size() && e.large_n < T; ++r) {
        const std::size_t i = s.size() - 1 - r;
        if (r && s[i] == s[i + 1])
            ++e.large_count[e.large_n - 1];
        else
            e.large[e.large_n] = s[i], e.large_count[e.large_n++] = 1;
    }
    e.large_count[e.large_n - 1] = static_cast<u32>(std::upper_bound(s.begin(), s.end(), e.large[e.large_n - 1]) -
                                                    std::lower_bound(s.begin(), s.end(), e.large[e.large_n - 1]));
    return e;
}

// Exact over every choice that excludes at most T-1 distinct values a side.
template <unsigned T> choice from_distinct(const distinct_extremes<T> &e, unsigned vbits, const cost_model &c) {
    choice best;
    std::size_t low = 0;
    for (unsigned i = 0; i < e.small_n; low += e.small_count[i++]) {
        std::size_t high = 0;
        for (unsigned j = 0; j < e.large_n; high += e.large_count[j++]) {
            if (e.large[j] < e.small[i])
                break;
            const unsigned K = std::max(c.min_width, bits(e.large[j] - e.small[i]));
            const choice t{e.small[i], K, static_cast<u32>(low), static_cast<u32>(high), c(K, low, high, vbits)};
            if (better(t, best))
                best = t;
        }
    }
    return best;
}

// The oracle on every stride-th value, priced as if each sampled value
// stood for stride values: bodies shrink by the stride, exceptions do not.
inline cost_model sample_cost(const cost_model &c, std::size_t stride) {
    cost_model sc = c;
    for (auto &b : sc.body)
        b = static_cast<u32>(b / stride);
    sc.header_bytes = 0;
    return sc;
}
template <std::size_t S> choice sample_guess(const u64 *x, std::size_t n, const cost_model &sc, unsigned value_bits) {
    std::array<u64, S> s;
    const std::size_t stride = n / S;
    for (std::size_t i = 0; i < S; ++i)
        s[i] = x[i * stride];
    std::sort(s.begin(), s.end());
    return exact_sorted(s.data(), S, sc, value_bits);
}

// Prices W windows in one counting pass.
template <std::size_t W> std::array<choice, W> realise_all(const u64 *x, std::size_t n, const std::array<std::pair<u64, unsigned>, W> &w,
                                                            unsigned value_bits, const cost_model &c) {
    std::array<u64, W> lo, hi;
    std::array<u32, W> low{}, high{};
    for (std::size_t k = 0; k < W; ++k)
        lo[k] = w[k].first, hi[k] = lo[k] > ~u64{0} - mask(w[k].second) ? ~u64{0} : lo[k] + mask(w[k].second);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = 0; k < W; ++k)
            low[k] += x[i] < lo[k], high[k] += x[i] > hi[k];
    std::array<choice, W> out;
    for (std::size_t k = 0; k < W; ++k)
        out[k] = {lo[k], w[k].second, low[k], high[k], c(w[k].second, low[k], high[k], value_bits)};
    return out;
}

// Local search from a choice: widths K-2..K+1 anchored at either end of the
// current window, priced in one pass, repeated while it improves. For a fixed
// end, narrowing only removes inliers (pfor-bounds.md), so each step is a
// monotone move along the width; anchoring at both ends covers either tail.
inline choice refine(const u64 *x, std::size_t n, const cost_model &c, unsigned vbits, choice best, unsigned rounds) {
    for (unsigned r = 0; r < rounds; ++r) {
        const u64 lo = best.lo, top = best.lo + mask(best.width);
        std::array<std::pair<u64, unsigned>, 6> w;
        std::size_t k = 0;
        for (int d : {-2, -1, 1}) {
            const int K = static_cast<int>(best.width) + d;
            const unsigned Ku = static_cast<unsigned>(std::clamp(K, static_cast<int>(c.min_width), 64));
            w[k++] = {lo, Ku};
            w[k++] = {top - std::min(top, mask(Ku)), Ku};
        }
        const auto t = realise_all<6>(x, n, w, vbits, c);
        choice next = best;
        for (const auto &q : t)
            if (better(q, next))
                next = q;
        if (!better(next, best))
            break;
        best = evaluate(x, n, next.lo, next.width, vbits, c);
    }
    return best;
}

// Guess, verify, refine: order statistics (exact over <= T-1 exceptions a
// side), the oracle on an S-value sample, and the previous block's window,
// priced in one counting pass; the cheapest is tightened to its inliers.
// The caller's emission pass recounts the tightened window's exceptions.
struct window {
    u64 lo = 0;
    unsigned width = 0;
    bool valid = false;
    u32 low = 0, high = 0;  // the window's exceptions in its own block
};
template <unsigned T, std::size_t S> choice guess(const u64 *x, std::size_t n, const cost_model &c, const cost_model &sc, const window &prior, unsigned refine_rounds = 0) {
    const auto e = order_stats<T>(x, n);
    const unsigned vbits = bits(e.large[0] - e.small[0]);
    const choice os = from_extremes<T>(e, n, vbits, c);
    const choice smp = sample_guess<S>(x, n, sc, vbits);
    const auto full = std::max(c.min_width, vbits);
    const auto r = realise_all<4>(
        x, n, {{{os.lo, os.width}, {smp.lo, smp.width}, {prior.valid ? prior.lo : e.small[0], prior.valid ? prior.width : full}, {e.small[0], full}}}, vbits,
        c);
    choice best = r[0];
    for (const auto &t : r)
        if (better(t, best))
            best = t;
    best = evaluate(x, n, best.lo, best.width, vbits, c);
    return refine_rounds ? refine(x, n, c, vbits, realise(x, n, best.lo, best.width, vbits, c), refine_rounds) : best;
}

}  // namespace frame_bounds
