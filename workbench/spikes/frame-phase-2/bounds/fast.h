// The guess-verify-refine bounds stage on narrow residuals. After the block's
// minimum and maximum (FOR's own pass), residuals x - min are staged at the
// narrowest lane that holds them, so every later pass compares 8-, 16- or
// 32-bit lanes rather than 64-bit ones. Windows are in residual coordinates.
//
//   guesses   the oracle on an S-value strided sample (bodies scaled by the
//             stride) and the sample's mode, the previous block's window,
//             optionally the exact choice among <= T-1 exceptions a side
//             (order statistics)
//   verify    one counting pass prices every guess; the cheapest is moved to
//             start at its inliers' minimum and narrowed to their extent
//   refine    the tightened window and widths K-2, K-1, K+1 anchored at
//             either end, one pass a round, while that improves
//
// The emission pass that pulls exceptions out recounts the final window, so
// the bytes returned are an upper bound only after tightening moved it.
#pragma once
#include "bounds.h"

#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace frame_bounds::fast {

template <class V> struct win {
    V lo, hi;
    unsigned K;
};
// A window in residual coordinates, clipped to V (the residuals' range).
template <class V> win<V> clip(u64 lo, unsigned K) {
    constexpr u64 vmax = static_cast<V>(~V{0});
    const u64 hi = lo > ~u64{0} - mask(K) ? ~u64{0} : lo + mask(K);
    return {static_cast<V>(std::min(lo, vmax)), static_cast<V>(std::min(hi, vmax)), K};
}

// One 64-byte register of V lanes (clang vector extension; four NEON
// registers on AArch64). Compares yield all-ones lanes, so subtracting them
// counts; a lane sees at most n / L values, so n <= 255 L.
template <class V> using reg = V __attribute__((vector_size(64)));
template <class V> reg<V> load(const V *p) {
    reg<V> v;
    std::memcpy(&v, p, sizeof v);
    return v;
}
template <class V> u32 lanes_sum(const reg<V> &v) {
    // Widen before reducing: byte lanes would wrap.
    using wide = u32 __attribute__((vector_size(64 / sizeof(V) * 4)));
    return __builtin_reduce_add(__builtin_convertvector(v, wide));
}

// The series' minimum and maximum (FOR's own pass).
inline range minmax(const u64 *x, std::size_t n) {
    reg<u64> mn = ~reg<u64>{}, mx{};
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        const reg<u64> v = load(x + i);
        mn = mn < v ? mn : v, mx = mx > v ? mx : v;
    }
    u64 a = __builtin_reduce_min(mn), b = __builtin_reduce_max(mx);
    for (; i < n; ++i)
        a = std::min(a, x[i]), b = std::max(b, x[i]);
    return {a, b};
}

// Counts below and above each of W windows, a window at a time (fusing
// all windows into one pass spills their accumulators). Lane counters hold
// at most n / L, so whole registers are counted in chunks of 255 registers.
template <class V, std::size_t W> void count(const V *r, std::size_t n, const std::array<win<V>, W> &w, std::array<u32, W> &low, std::array<u32, W> &high) {
    constexpr std::size_t L = 64 / sizeof(V), chunk = 255 * L;
    const std::size_t whole = n / L * L;
    low.fill(0), high.fill(0);
    for (std::size_t k = 0; k < W; ++k) {
        const V a = w[k].lo, b = w[k].hi;
        for (std::size_t c0 = 0; c0 < whole; c0 += chunk) {
            reg<V> lo{}, hi{};
            for (std::size_t i = c0; i < std::min(whole, c0 + chunk); i += L) {
                const reg<V> x = load(r + i);
                lo -= reinterpret_cast<reg<V>>(x < a), hi -= reinterpret_cast<reg<V>>(x > b);
            }
            low[k] += lanes_sum(lo), high[k] += lanes_sum(hi);
        }
        for (std::size_t i = whole; i < n; ++i)
            low[k] += r[i] < a, high[k] += r[i] > b;
    }
}

template <class V, std::size_t W> std::array<choice, W> price(const V *r, std::size_t n, const std::array<win<V>, W> &w, unsigned vbits, const cost_model &c) {
    std::array<u32, W> low, high;
    count<V, W>(r, n, w, low, high);
    std::array<choice, W> out;
    for (std::size_t k = 0; k < W; ++k)
        out[k] = {w[k].lo, w[k].K, low[k], high[k], c(w[k].K, low[k], high[k], vbits)};
    return out;
}

// price() fused with tighten(): one pass gives every window's exceptions
// and its inliers' extent, so the cheapest window is tightened without
// another pass. The tightened window keeps every inlier and may admit more
// values above, so its counts are an upper bound.
template <class V, std::size_t W>
std::array<choice, W> price_tight(const V *r, std::size_t n, const std::array<win<V>, W> &w, unsigned vbits, const cost_model &c, std::array<choice, W> &tight) {
    constexpr std::size_t L = 64 / sizeof(V), chunk = 255 * L;
    constexpr V top = static_cast<V>(~V{0});
    const std::size_t whole = n / L * L;
    std::array<u32, W> low{}, high{};
    std::array<reg<V>, W> mn, mx{};
    // One window at a time: four accumulators stay in registers (all W at
    // once spills, 4W 64-byte accumulators).
    for (std::size_t k = 0; k < W; ++k) {
        mn[k] = top - reg<V>{};
        for (std::size_t c0 = 0; c0 < whole; c0 += chunk) {
            reg<V> lo{}, hi{};
            for (std::size_t i = c0; i < std::min(whole, c0 + chunk); i += L) {
                const reg<V> x = load(r + i);
                const reg<V> below = reinterpret_cast<reg<V>>(x < w[k].lo), above = reinterpret_cast<reg<V>>(x > w[k].hi);
                const reg<V> out = below | above;
                lo -= below, hi -= above;
                mn[k] = mn[k] < (x | out) ? mn[k] : (x | out);
                mx[k] = mx[k] > (x & ~out) ? mx[k] : (x & ~out);
            }
            low[k] += lanes_sum(lo), high[k] += lanes_sum(hi);
        }
    }
    std::array<V, W> in_min, in_max;
    for (std::size_t k = 0; k < W; ++k)
        in_min[k] = __builtin_reduce_min(mn[k]), in_max[k] = __builtin_reduce_max(mx[k]);
    for (std::size_t i = whole; i < n; ++i)
        for (std::size_t k = 0; k < W; ++k) {
            const bool below = r[i] < w[k].lo, above = r[i] > w[k].hi;
            low[k] += below, high[k] += above;
            if (!below && !above)
                in_min[k] = std::min(in_min[k], r[i]), in_max[k] = std::max(in_max[k], r[i]);
        }
    std::array<choice, W> out;
    for (std::size_t k = 0; k < W; ++k) {
        out[k] = {w[k].lo, w[k].K, low[k], high[k], c(w[k].K, low[k], high[k], vbits)};
        if (in_max[k] < in_min[k])
            tight[k] = out[k];  // no inliers
        else {
            const unsigned K = std::max(c.min_width, bits(static_cast<u64>(in_max[k] - in_min[k])));
            tight[k] = {in_min[k], K, low[k], high[k], c(K, low[k], high[k], vbits)};
        }
    }
    return out;
}

// The window moved to its inliers' minimum and narrowed to their extent.
template <class V> choice tighten(const V *r, std::size_t n, const choice &ch, unsigned vbits, const cost_model &c) {
    constexpr V top = static_cast<V>(~V{0});
    const win<V> w = clip<V>(ch.lo, ch.width);
    reg<V> mn = top - reg<V>{}, mx{};
    constexpr std::size_t L = 64 / sizeof(V);
    std::size_t i = 0;
    for (; i + L <= n; i += L) {
        const reg<V> x = load(r + i);
        const reg<V> in = reinterpret_cast<reg<V>>((x >= w.lo) & (x <= w.hi));
        const reg<V> lo_x = (x & in) | (~in & top), hi_x = x & in;
        mn = mn < lo_x ? mn : lo_x;
        mx = mx > hi_x ? mx : hi_x;
    }
    V in_min = __builtin_reduce_min(mn), in_max = __builtin_reduce_max(mx);
    for (; i < n; ++i)
        if (r[i] >= w.lo && r[i] <= w.hi)
            in_min = std::min(in_min, r[i]), in_max = std::max(in_max, r[i]);
    if (in_max < in_min)
        return ch;
    const unsigned K = std::max(c.min_width, bits(static_cast<u64>(in_max - in_min)));
    return {in_min, K, ch.low, ch.high, c(K, ch.low, ch.high, vbits)};
}

// Batcher's odd-even merge sort as a fixed network of branch-free
// compare-exchanges (191 for 32 values), unrolled at compile time.
template <std::size_t S> constexpr std::size_t network_size() {
    std::size_t m = 0;
    for (std::size_t p = 1; p < S; p <<= 1)
        for (std::size_t k = p; k >= 1; k >>= 1)
            for (std::size_t j = k % p; j + k < S; j += 2 * k)
                for (std::size_t i = 0; i < k && i + j + k < S; ++i)
                    m += (i + j) / (2 * p) == (i + j + k) / (2 * p);
    return m;
}
template <std::size_t S> constexpr auto network() {
    std::array<std::pair<std::size_t, std::size_t>, network_size<S>()> out{};
    std::size_t m = 0;
    for (std::size_t p = 1; p < S; p <<= 1)
        for (std::size_t k = p; k >= 1; k >>= 1)
            for (std::size_t j = k % p; j + k < S; j += 2 * k)
                for (std::size_t i = 0; i < k && i + j + k < S; ++i)
                    if ((i + j) / (2 * p) == (i + j + k) / (2 * p))
                        out[m++] = {i + j, i + j + k};
    return out;
}
template <class V> inline void exchange(V &x, V &y) {
    const V lo = std::min(x, y), hi = std::max(x, y);
    x = lo, y = hi;
}
template <class V, std::size_t S> void network_sort(std::array<V, S> &a) {
    static constexpr auto net = network<S>();
    [&]<std::size_t... I>(std::index_sequence<I...>) { (exchange(a[net[I].first], a[net[I].second]), ...); }(std::make_index_sequence<net.size()>{});
}

// Bitonic sort of S values held as one vector (S lanes of V), every step a
// compile-time shuffle plus a lane-wise min and max: 15 steps for S = 32.
template <class V, std::size_t S> using lanes = V __attribute__((vector_size(S * sizeof(V))));
template <std::size_t S, std::size_t K, std::size_t J, class W> W bitonic_step(W v) {
    const W p = [&]<std::size_t... I>(std::index_sequence<I...>) { return __builtin_shufflevector(v, v, (I ^ J)...); }(std::make_index_sequence<S>{});
    const W lo = v < p ? v : p, hi = v < p ? p : v;
    // Lane i keeps the minimum when it is the lower partner of an ascending
    // pair or the upper partner of a descending one.
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
        return __builtin_shufflevector(lo, hi, ((((I & J) == 0) == ((I & K) == 0)) ? I : I + S)...);
    }(std::make_index_sequence<S>{});
}
template <std::size_t S, std::size_t K, std::size_t J, class W> W bitonic_stage(W v) {
    v = bitonic_step<S, K, J>(v);
    if constexpr (J > 1)
        return bitonic_stage<S, K, J / 2>(v);
    else
        return v;
}
template <std::size_t S, std::size_t K, class W> W bitonic(W v) {
    v = bitonic_stage<S, K, K / 2>(v);
    if constexpr (K < S)
        return bitonic<S, K * 2>(v);
    else
        return v;
}
template <class V, std::size_t S> void vector_sort(std::array<V, S> &a) {
    static_assert(std::has_single_bit(S));
    lanes<V, S> v;
    std::memcpy(&v, a.data(), sizeof v);
    v = bitonic<S, 2>(v);
    std::memcpy(a.data(), &v, sizeof v);
}

// Sorts a small array by rank: each value's position is the number of values
// below it plus equal values before it. Branch-free; the inner loop vectorises.
template <class V, std::size_t S> std::array<V, S> rank_sort(const std::array<V, S> &s) {
    std::array<V, S> out;
    for (std::size_t i = 0; i < S; ++i) {
        unsigned at = 0;
        for (std::size_t j = 0; j < S; ++j)
            at += (s[j] < s[i]) | ((s[j] == s[i]) & (j < i));
        out[at] = s[i];
    }
    return out;
}

// The oracle over a sorted S-value sample, restricted to at most T-1
// distinct sampled values excluded a side (each sampled value stands for
// n / S rows). Distinct values matter: on run-heavy blocks the best choice
// excludes every occurrence of a value.
template <class V> struct sampled {  // the sample oracle's choice, priced on the sample
    choice best;
    V mode;  // the most frequent sampled value
};
template <unsigned T, class V, std::size_t S> sampled<V> sample_guess(const V *r, std::size_t n, const cost_model &sc, unsigned vbits) {
    std::array<V, S> s;
    const std::size_t stride = n / S;
    for (std::size_t i = 0; i < S; ++i)
        s[i] = r[i * stride];
    vector_sort(s);
    std::array<V, S> d;
    std::array<unsigned, S + 1> below;  // sampled values below d[k]
    unsigned D = 0;
    for (unsigned i = 0; i < S; ++i)
        if (!i || s[i] != s[i - 1])
            below[D] = i, d[D++] = s[i];
    below[D] = S;
    unsigned mode = 0;
    for (unsigned k = 1; k < D; ++k)
        if (below[k + 1] - below[k] > below[mode + 1] - below[mode])
            mode = k;
    // Bytes per excluded sampled value, with and without values below lo
    // (the sampled cost model has no header).
    const unsigned e_hi = sc.position_bytes + sc.exception_penalty + (vbits + 7) / 8, e_lo = e_hi + (vbits % 8 == 0);
    std::size_t best_bytes = ~std::size_t{0};
    unsigned bi = 0, bj = 0;
    for (unsigned i = 0; i < T && i < D; ++i)
        for (unsigned j = 0; j < T && i + j < D; ++j) {
            const unsigned K = std::max(sc.min_width, bits(static_cast<u64>(d[D - 1 - j] - d[i])));
            const unsigned ex = below[i] + S - below[D - j];
            const std::size_t bytes = ex * (n / S) > sc.max_exceptions ? std::size_t{1} << 40 : sc.body[K] + ex * (below[i] ? e_lo : e_hi);
            if (bytes < best_bytes)
                best_bytes = bytes, bi = i, bj = j;
        }
    const unsigned K = std::max(sc.min_width, bits(static_cast<u64>(d[D - 1 - bj] - d[bi])));
    const choice best{d[bi], K, below[bi], static_cast<u32>(S - below[D - bj]), best_bytes};
    return {best, d[mode]};
}

// The sorted sample read at the prior's quantiles: the prior excluded
// `low` and `high` of n values, so this block's window is guessed as the
// sampled values that many positions in from either end, snapped to
// distinct values (a window cannot split a run of equal values) with their
// distinct neighbours, priced on the sample; the mode as before.
template <class V, std::size_t S>
sampled<V> sample_quantiles(const V *r, std::size_t n, const cost_model &sc, unsigned vbits, unsigned low, unsigned high, bool prior_known = true) {
    static_assert(S <= 64);
    using W = lanes<V, S>;
    W v;
    const std::size_t stride = n / S;
    for (std::size_t i = 0; i < S; ++i)
        v[i] = r[i * stride];
    v = bitonic<S, 2>(v);
    // Bit k of `starts` marks the first sampled value of each distinct value,
    // so a run is the span between consecutive set bits. Everything below is
    // bit arithmetic on that mask; the sorted values stay in the register.
    const W prev = [&]<std::size_t... I>(std::index_sequence<I...>) { return __builtin_shufflevector(v, v, (I ? I - 1 : 0)...); }(std::make_index_sequence<S>{});
    const auto differ = v != prev;
    std::uint64_t starts = 1;
    for (std::size_t k = 1; k < S; ++k)
        starts |= static_cast<std::uint64_t>(differ[k] & 1) << k;
    const std::uint64_t all = S == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << S) - 1;
    auto first_of = [&](unsigned i) { return 63u - static_cast<unsigned>(std::countl_zero(starts & ((std::uint64_t{2} << i) - 1))); };
    auto last_of = [&](unsigned i) {
        const std::uint64_t later = starts & all & ~((std::uint64_t{2} << i) - 1);
        return later ? static_cast<unsigned>(std::countr_zero(later)) - 1 : static_cast<unsigned>(S - 1);
    };
    // Window ends at the predicted positions and their distinct neighbours.
    const unsigned il = std::min<unsigned>(S - 1, static_cast<unsigned>((low * S + n / 2) / n)),
                   ih = std::min<unsigned>(S - 1, static_cast<unsigned>((high * S + n / 2) / n));
    // Window ends: the first D distinct values from the bottom and the last D
    // from the top (rare outliers and small clusters at either end, read by
    // clearing set bits), plus the prior's predicted positions and their
    // distinct neighbours.
    constexpr unsigned D = 5;
    std::array<unsigned, D + 3> los, his;
    unsigned nl = 0, nh = 0;
    for (std::uint64_t m = starts; m && nl < D; m &= m - 1)
        los[nl++] = static_cast<unsigned>(std::countr_zero(m));
    for (std::uint64_t m = starts & all; m && nh < D;) {
        const unsigned at = 63u - static_cast<unsigned>(std::countl_zero(m));  // last run's first index
        his[nh++] = last_of(at);
        m &= ~(std::uint64_t{1} << at);
    }
    if (prior_known) {
        const unsigned a0 = first_of(il);
        los[nl++] = a0;
        if (a0)
            los[nl++] = first_of(a0 - 1);
        if (last_of(a0) + 1 < S)
            los[nl++] = last_of(a0) + 1;
        const unsigned z0 = last_of(S - 1 - ih);
        his[nh++] = z0;
        if (z0 + 1 < S)
            his[nh++] = last_of(z0 + 1);
        if (first_of(z0))
            his[nh++] = first_of(z0) - 1;
    }
    const unsigned e_hi = sc.position_bytes + sc.exception_penalty + (vbits + 7) / 8, e_lo = e_hi + (vbits % 8 == 0);
    std::size_t best_bytes = ~std::size_t{0};
    unsigned bi = 0, bz = S - 1;
    for (unsigned a = 0; a < nl; ++a)
        for (unsigned z = 0; z < nh; ++z) {
            const unsigned i = los[a], j = his[z];
            if (j < i)
                continue;
            const unsigned K = std::max(sc.min_width, bits(static_cast<u64>(v[j] - v[i])));
            const unsigned ex = i + (S - 1 - j);
            const std::size_t bytes = ex * (n / S) > sc.max_exceptions ? std::size_t{1} << 40 : sc.body[K] + ex * (i ? e_lo : e_hi);
            if (bytes < best_bytes)
                best_bytes = bytes, bi = i, bz = j;
        }
    // Mode: the longest run, from the gaps between set bits; skipped when
    // every sampled value is distinct.
    unsigned mode = 0;
    if (static_cast<std::size_t>(std::popcount(starts)) < S) {
        unsigned longest = 0;
        for (std::uint64_t m = starts; m;) {
            const unsigned at = static_cast<unsigned>(std::countr_zero(m));
            m &= m - 1;
            const unsigned end = m ? static_cast<unsigned>(std::countr_zero(m)) : static_cast<unsigned>(S);
            if (end - at > longest)
                longest = end - at, mode = at;
        }
    }
    const unsigned K = std::max(sc.min_width, bits(static_cast<u64>(v[bz] - v[bi])));
    return {{v[bi], K, bi, static_cast<u32>(S - 1 - bz), best_bytes}, v[mode]};
}

// The sample oracle without a pair search. In the sorted sample, the best
// window that excludes m sampled values (on either side) is the narrowest
// span of S - m consecutive values: v[i + S-1-m] - v[i], minimised over the
// m + 1 starts i. Each m is one compile-time shift, a subtraction and a
// lane-wise minimum, so a ladder of m costs a few dozen vector operations.
// Runs need no deduplication: a window's values include all equal values.
template <std::size_t S, std::size_t M, class W> W spans(const W &v) {
    using V = std::remove_cvref_t<decltype(v[0])>;
    const W ahead = [&]<std::size_t... I>(std::index_sequence<I...>) { return __builtin_shufflevector(v, v, (I + S - 1 - M < S ? I + S - 1 - M : S - 1)...); }(
        std::make_index_sequence<S>{});
    // Starts beyond m would run past the end.
    constexpr W beyond = []<std::size_t... I>(std::index_sequence<I...>) { return W{(I <= M ? V{0} : static_cast<V>(~V{0}))...}; }(std::make_index_sequence<S>{});
    return (ahead - v) | beyond;
}
// Returns the cheapest rung, and the cheapest rung excluding at least half
// the sample (a dominant value or cluster), as two guesses.
template <class V, std::size_t S> std::pair<choice, choice> sample_ladder(const V *r, std::size_t n, const cost_model &sc, unsigned vbits) {
    using W = lanes<V, S>;
    W v;
    const std::size_t stride = n / S;
    for (std::size_t i = 0; i < S; ++i)
        v[i] = r[i * stride];
    v = bitonic<S, 2>(v);
    const unsigned e = sc.position_bytes + sc.exception_penalty + (vbits + 8) / 8;  // sign bit: either side
    // Every rung's narrowest span; positions only for the two winners.
    std::array<V, S - 1> least;
    [&]<std::size_t... M>(std::index_sequence<M...>) { ((least[M] = __builtin_reduce_min(spans<S, M>(v))), ...); }(std::make_index_sequence<S - 1>{});
    std::size_t best_bytes = ~std::size_t{0}, dom_bytes = ~std::size_t{0};
    unsigned bm = 0, dm = S - 2;
    for (unsigned m = 0; m + 1 < S; ++m) {
        const unsigned K = std::max(sc.min_width, bits(static_cast<u64>(least[m])));
        const std::size_t bytes = m * (n / S) > sc.max_exceptions ? std::size_t{1} << 40 : sc.body[K] + m * e;
        if (bytes < best_bytes)
            best_bytes = bytes, bm = m;
        if (2 * m >= S && bytes < dom_bytes)
            dom_bytes = bytes, dm = m;
    }
    auto window = [&](unsigned m, std::size_t bytes) {
        unsigned at = 0;
        while (at < m && static_cast<V>(v[at + S - 1 - m] - v[at]) != least[m])
            ++at;
        return choice{v[at], std::max(sc.min_width, bits(static_cast<u64>(least[m]))), at, m - at, bytes};
    };
    return {window(bm, best_bytes), window(dm, dom_bytes)};
}

// What the stage saw, for designing a bad-guess test (census only).
struct trace {
    int winner = -1;           // index of the winning guess: sample, prior, full or extremes, mode
    std::size_t guessed = 0;   // the winning guess's priced bytes
    std::size_t sample = 0;    // the sample oracle's bytes, rescaled to the block
    std::size_t refined = 0;   // bytes after refinement
    unsigned rounds = 0;       // refinement rounds that improved
};

struct options {
    unsigned refine_rounds = 1;
    bool extremes = false;  // add the order-statistics guess (T-1 exceptions a side)
    bool sample = true;     // the sample oracle and mode guesses
    // Instead of solving the sample, read it at the quantiles the prior's
    // exception counts predict (+-1 sampled value), plus the mode.
    bool quantiles = false;
    // Solve the sample by its exclusion ladder (narrowest spans), no pairs.
    bool ladder = false;
    // guess_lean: the sample chooses, one pass verifies.
    bool lean = false;
    trace *t = nullptr;
};

template <unsigned T, std::size_t S, class V>
choice guess_narrow(const V *r, std::size_t n, unsigned vbits, const cost_model &c, const cost_model &sc, const choice *prior, const options &o) {
    const choice full{0, std::max(c.min_width, vbits), 0, 0, c(std::max(c.min_width, vbits), 0, 0, vbits)};
    sampled<V> smp{{0, full.width, 0, 0, full.bytes}, 0};
    choice ladder_dominant = full;
    if (o.sample && n < 2 * S) {
        // A short series is its own sample: the oracle costs about as much.
        std::vector<u64> s(r, r + n);
        std::sort(s.begin(), s.end());
        smp.best = exact_sorted(s.data(), n, c, vbits);
        smp.mode = r[0];
    } else if (o.sample && o.ladder) {
        const auto [best, dominant] = sample_ladder<V, S>(r, n, sc, vbits);
        smp.best = best;
        ladder_dominant = dominant;
    } else if (o.sample && o.quantiles)
        smp = sample_quantiles<V, S>(r, n, sc, vbits, prior ? prior->low : 0, prior ? prior->high : 0, prior != nullptr);
    else if (o.sample)
        smp = sample_guess<T, V, S>(r, n, sc, vbits);
    // The sampled mode at the narrowest width: a block dominated by one value
    // keeps it and lets everything else be an exception.
    std::array<win<V>, 4> w{clip<V>(smp.best.lo, smp.best.width), clip<V>(prior ? prior->lo : 0, prior ? prior->width : full.width), clip<V>(0, full.width),
                            o.sample ? (o.ladder ? clip<V>(ladder_dominant.lo, ladder_dominant.width) : clip<V>(smp.mode, c.min_width)) : clip<V>(0, full.width)};
    if (o.extremes) {
        const auto e = order_stats<T>(r, n);
        const choice os = from_extremes<T>(e, n, vbits, c);
        w[2] = clip<V>(os.lo, os.width);
    }
    choice best = full, best_tight = full;
    std::array<choice, 4> tight4;
    const auto priced = price_tight<V, 4>(r, n, w, vbits, c, tight4);
    for (std::size_t k = 0; k < 4; ++k)
        if (better(priced[k], best)) {
            best = priced[k], best_tight = tight4[k];
            if (o.t)
                o.t->winner = static_cast<int>(k);
        }
    if (o.t)
        o.t->guessed = best.bytes, o.t->sample = smp.best.bytes * (n / S);
    // The tightened window is priced again (exactly) with its neighbours.
    best = best_tight;
    for (unsigned round = 0; round < o.refine_rounds; ++round) {
        const u64 lo = best.lo, top = lo > ~u64{0} - mask(best.width) ? ~u64{0} : lo + mask(best.width);
        std::array<win<V>, 7> rw;
        rw[0] = clip<V>(lo, best.width);
        std::size_t k = 1;
        for (int d : {-2, -1, 1}) {
            const unsigned K = static_cast<unsigned>(std::clamp(static_cast<int>(best.width) + d, static_cast<int>(c.min_width), 64));
            rw[k++] = clip<V>(lo, K);
            rw[k++] = clip<V>(top - std::min(top, mask(K)), K);
        }
        std::array<choice, 7> tight7;
        const auto t = price_tight<V, 7>(r, n, rw, vbits, c, tight7);
        std::size_t at = 0;
        for (std::size_t q = 1; q < 7; ++q)
            if (better(t[q], t[at]))
                at = q;
        if (at == 0) {
            best = t[0];
            break;
        }
        if (o.t)
            ++o.t->rounds;
        best = tight7[at];
    }
    if (o.t)
        o.t->refined = best.bytes;
    return best;
}

// The lean path: the sample proposes and the block verifies in one pass.
// The sorted sample's exclusion ladder picks a window; the block prices it
// (tightened), its width neighbours and the prior window together.
template <class V, std::size_t S>
choice guess_lean(const V *r, std::size_t n, unsigned vbits, const cost_model &c, const cost_model &sc, const choice *prior) {
    using W = lanes<V, S>;
    const unsigned full_w = std::max(c.min_width, vbits);
    W v;
    const std::size_t stride = n / S;
    for (std::size_t i = 0; i < S; ++i)
        v[i] = r[i * stride];
    v = bitonic<S, 2>(v);
    const unsigned e = sc.position_bytes + sc.exception_penalty + (vbits + 8) / 8;
    auto sample_bytes = [&](unsigned K, unsigned ex) { return ex * (n / S) > sc.max_exceptions ? std::size_t{1} << 40 : std::size_t{sc.body[K]} + ex * e; };
    std::array<V, S - 1> least;
    [&]<std::size_t... M>(std::index_sequence<M...>) { ((least[M] = __builtin_reduce_min(spans<S, M>(v))), ...); }(std::make_index_sequence<S - 1>{});
    std::size_t best_bytes = sample_bytes(full_w, 0);
    unsigned bm = 0;
    for (unsigned m = 1; m + 1 < S; ++m) {
        const std::size_t b = sample_bytes(std::max(c.min_width, bits(static_cast<u64>(least[m]))), m);
        if (b < best_bytes)
            best_bytes = b, bm = m;
    }
    unsigned at = 0;
    while (at < bm && static_cast<V>(v[at + S - 1 - bm] - v[at]) != least[bm])
        ++at;
    win<V> guess = clip<V>(v[at], std::max(c.min_width, bits(static_cast<u64>(least[bm]))));
    // One pass over the block: the sample's guess (with its inliers' extent),
    // its width neighbours at either end, and the prior window, whose block
    // counts the sample cannot predict well on heavy tails.
    const u64 lo = guess.lo, top = lo > ~u64{0} - mask(guess.K) ? ~u64{0} : lo + mask(guess.K);
    std::array<win<V>, 7> w{guess};
    std::size_t k = 1;
    for (int d : {-1, 1}) {
        const unsigned K = static_cast<unsigned>(std::clamp(static_cast<int>(guess.K) + d, static_cast<int>(c.min_width), 64));
        w[k++] = clip<V>(lo, K);
        w[k++] = clip<V>(top - std::min(top, mask(K)), K);
    }
    w[k++] = clip<V>(lo, static_cast<unsigned>(std::max(static_cast<int>(c.min_width), static_cast<int>(guess.K) - 2)));
    w[k++] = prior ? clip<V>(prior->lo, prior->width) : clip<V>(0, full_w);
    std::array<choice, 7> tight;
    price_tight<V, 7>(r, n, w, vbits, c, tight);
    choice best{0, full_w, 0, 0, c(full_w, 0, 0, vbits)};
    for (std::size_t q = 0; q < 7; ++q)
        if (better(tight[q], best))
            best = tight[q];
    return best;
}

// Stages x - min at the narrowest lane that holds it and calls
// f(residuals, vbits); f's choice comes back in absolute coordinates.
template <class F> choice with_residuals(const u64 *x, std::size_t n, F &&f) {
    const auto [mn, mx] = fast::minmax(x, n);
    const unsigned vbits = bits(mx - mn);
    auto run = [&]<class V>(V) {
        alignas(64) V block[256];
        thread_local std::vector<V> longer;
        V *r = n <= 256 ? block : (longer.resize(n), longer.data());
        for (std::size_t i = 0; i < n; ++i)
            r[i] = static_cast<V>(x[i] - mn);
        choice out = f(static_cast<const V *>(r), vbits, mn);
        out.lo += mn;
        return out;
    };
    if (vbits <= 8)
        return run(std::uint8_t{});
    if (vbits <= 16)
        return run(std::uint16_t{});
    if (vbits <= 32)
        return run(std::uint32_t{});
    return run(u64{});
}

// Entry point over a block of u64 values. prior is the previous block's
// window in absolute coordinates, or null.
template <unsigned T, std::size_t S>
choice guess(const u64 *x, std::size_t n, const cost_model &c, const cost_model &sc, const choice *prior, const options &o = {}) {
    return with_residuals(x, n, [&]<class V>(const V *r, unsigned vbits, u64 mn) {
        // The prior moved into this block's residual coordinates; one that
        // ends below this block's minimum or starts above its maximum is dropped.
        const u64 mx = mn + mask(vbits);
        choice p;
        const choice *pp = nullptr;
        if (prior && (prior->lo > ~u64{0} - mask(prior->width) || prior->lo + mask(prior->width) >= mn) && prior->lo <= mx) {
            p = *prior;
            p.lo = prior->lo > mn ? prior->lo - mn : 0;
            pp = &p;
        }
        if (o.lean && n >= 2 * S)
            return guess_lean<V, S>(r, n, vbits, c, sc, pp);
        return guess_narrow<T, S, V>(r, n, vbits, c, sc, pp, o);
    });
}

// Today's stage (lo = minimum, every width) on the same narrow residuals:
// the control that separates the lane width from the choice of method.
inline choice min_exhaustive(const u64 *x, std::size_t n, const cost_model &c) {
    return with_residuals(x, n, [&]<class V>(const V *r, unsigned vbits, u64) {
        const unsigned full = std::max(c.min_width, vbits);
        choice best{0, full, 0, 0, c(full, 0, 0, vbits)};
        for (unsigned K = c.min_width; K < vbits; ++K) {
            std::array<u32, 1> low, high;
            count<V, 1>(r, n, {clip<V>(0, K)}, low, high);
            const choice t{0, K, 0, high[0], c(K, 0, high[0], vbits)};
            if (better(t, best))
                best = t;
        }
        return best;
    });
}

// Guess from the previous block first, verify, and fall back to the sample
// oracle only when the guess looks bad: when its bytes jump above the
// previous block's by more than `jump`, or every `refresh` blocks so a
// stale prior cannot persist (Calico Keyset refreshes every 16th box).
struct gate {
    double jump = 1.03;
    unsigned refresh = 16;
};
struct gate_state {
    choice prior;
    bool valid = false;
    unsigned since_refresh = 0;
    std::size_t fallbacks = 0, refreshes = 0;
};
template <unsigned T, std::size_t S>
choice gated(const u64 *x, std::size_t n, const cost_model &c, const cost_model &sc, gate_state &st, const gate &g) {
    choice out;
    if (!st.valid || ++st.since_refresh >= g.refresh) {
        out = guess<T, S>(x, n, c, sc, st.valid ? &st.prior : nullptr);
        st.since_refresh = 0, ++st.refreshes;
    } else {
        out = guess<T, S>(x, n, c, sc, &st.prior, {1, false, false});
        if (static_cast<double>(out.bytes) > g.jump * static_cast<double>(st.prior.bytes)) {
            const choice f = guess<T, S>(x, n, c, sc, &st.prior);
            ++st.fallbacks;
            if (better(f, out))
                out = f;
        }
    }
    st.prior = out, st.valid = true;
    return out;
}

// Window-local lanes. Every window the lean pass prices lies within
// [lo - 2^(K+1), lo + 2^(K+2)), so values far outside cannot change any
// compare: subtracting a base below that range with saturation, and
// clamping at the lane's top, keeps every window's counts exact while the
// lanes need only K + 3 bits. A 57-bit column whose window is 10 bits wide
// is priced in u16 lanes.
template <class L> void stage_local(const u64 *x, std::size_t n, u64 base, L *out) {
    constexpr u64 top = static_cast<L>(~L{0});
    for (std::size_t i = 0; i < n; ++i) {
        const u64 d = x[i] > base ? x[i] - base : 0;
        out[i] = static_cast<L>(d < top ? d : top);
    }
}

// The sample proposes from raw values; one window-local pass verifies.
// Returns the full width without touching the block again when the sample
// finds no exclusion worth making and there is no prior.
template <std::size_t S>
choice guess_local(const u64 *x, std::size_t n, const cost_model &c, const cost_model &sc, const choice *prior) {
    const auto [mn, mx] = fast::minmax(x, n);
    const unsigned vbits = bits(mx - mn), full_w = std::max(c.min_width, vbits);
    const choice full{mn, full_w, 0, 0, c(full_w, 0, 0, vbits)};
    if (!vbits || n < 2 * S)
        return guess<7, S>(x, n, c, sc, prior);
    // The sample in u64 lanes relative to the minimum (it is only S values).
    // Coarse u16 samples (dropping low bits above 16) lost narrow windows in
    // wide domains, and a saturated zoom invented spans at its edges.
    using W = lanes<u64, S>;
    W v;
    const std::size_t stride = n / S;
    for (std::size_t i = 0; i < S; ++i)
        v[i] = x[i * stride] - mn;
    v = bitonic<S, 2>(v);
    const unsigned e = sc.position_bytes + sc.exception_penalty + (vbits + 8) / 8;
    auto sample_bytes = [&](unsigned K, unsigned ex) { return ex * (n / S) > sc.max_exceptions ? std::size_t{1} << 40 : std::size_t{sc.body[K]} + ex * e; };
    std::array<u64, S - 1> least;
    [&]<std::size_t... M>(std::index_sequence<M...>) { ((least[M] = __builtin_reduce_min(spans<S, M>(v))), ...); }(std::make_index_sequence<S - 1>{});
    std::size_t best_bytes = sample_bytes(full_w, 0);
    unsigned bm = 0;
    for (unsigned m = 1; m + 1 < S; ++m) {
        const std::size_t b = sample_bytes(std::max(c.min_width, bits(least[m])), m);
        if (b < best_bytes)
            best_bytes = b, bm = m;
    }
    // No exclusion pays on the sample and the prior agrees: keep the full
    // width without another pass (hashes, uniform values).
    if (bm == 0 && (!prior || prior->width >= vbits))
        return full;
    unsigned at = 0;
    while (at < bm && v[at + S - 1 - bm] - v[at] != least[bm])
        ++at;
    const u64 lo = mn + v[at];
    const unsigned K = std::max(c.min_width, bits(least[bm]));
    if (K + 3 >= 64) {
        // Too wide for local lanes: price the guess against the full width.
        const choice g = realise(x, n, lo, K, vbits, c);
        return better(g, full) ? g : full;
    }
    const u64 base = lo > mask(K + 1) + 1 ? lo - mask(K + 1) - 1 : 0;
    auto run = [&]<class L>(L) {
        alignas(64) L r[256];
        thread_local std::vector<L> longer;
        L *p = n <= 256 ? r : (longer.resize(n), longer.data());
        stage_local<L>(x, n, base, p);
        const u64 l = lo - base, t = l + mask(K);
        std::array<win<L>, 7> w{clip<L>(l, K)};
        std::size_t k = 1;
        for (int d : {-1, 1}) {
            const unsigned Kd = static_cast<unsigned>(std::clamp(static_cast<int>(K) + d, static_cast<int>(c.min_width), 64));
            w[k++] = clip<L>(l, Kd);
            w[k++] = clip<L>(t - std::min(t, mask(Kd)), Kd);
        }
        w[k++] = clip<L>(l, static_cast<unsigned>(std::max(static_cast<int>(c.min_width), static_cast<int>(K) - 2)));
        // The prior counts exactly only inside the local range; outside it,
        // the window is priced as the guess again.
        constexpr u64 lmax = static_cast<L>(~L{0});
        const bool prior_local = prior && prior->width < 8 * sizeof(L) && prior->lo > base && prior->lo - base < lmax - mask(prior->width);
        w[k++] = prior_local ? clip<L>(prior->lo - base, prior->width) : w[0];
        std::array<choice, 7> tight;
        price_tight<L, 7>(p, n, w, vbits, c, tight);
        choice best = full;
        for (const auto &q : tight)
            if (better(q, best))
                best = {q.lo + base, q.width, q.low, q.high, q.bytes};
        return best;
    };
    if (K + 3 <= 8)
        return run(std::uint8_t{});
    if (K + 3 <= 16)
        return run(std::uint16_t{});
    if (K + 3 <= 32)
        return run(std::uint32_t{});
    return run(u64{});
}

}  // namespace frame_bounds::fast
