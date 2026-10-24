// The bounds stage, as composed: choose an inlier window [lo, lo + 2^K)
// for a series, with values outside it as exceptions, priced by the
// caller's cost_model (bounds.h). This header holds only the chosen path;
// fast.h keeps the alternatives it was measured against (README.md).
//
//   choose()  FOR's minimum and maximum; a strided sample sorted in one
//             vector proposes a window by its exclusion ladder; one pass over
//             window-local saturating lanes prices that window, its width
//             neighbours and the caller's prior. The returned counts are an
//             upper bound when tightening moved the window; the emission pass
//             recounts.
//   min_exhaustive() (fast.h) is the cheap arm where lo = minimum suffices.
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
    if (!vbits)
        return full;
    if (n < 2 * S) {
        // A short series is its own sample: the oracle costs about as much.
        std::vector<u64> s(x, x + n);
        std::sort(s.begin(), s.end());
        return exact_sorted(s.data(), n, c, vbits);
    }
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

// The stage's entry point: S sampled values (16 is faster, 32 closer to
// the oracle), the caller's costs, its sample costs (sample_cost(c, n / S))
// and the previous series' window, or null.
// Any length n: blocks up to 256 values stage on the stack, longer series
// (a group's run sizes or values as one series) in a reused buffer, and
// series shorter than 2S use the exact oracle.
template <std::size_t S = 32>
choice choose(const u64 *x, std::size_t n, const cost_model &c, const cost_model &sc, const choice *prior) {
    if (!c.pin_lo)
        return guess_local<S>(x, n, c, sc, prior);
    // lo pinned: every width at that lo, on lanes as narrow as the maximum.
    const auto [mn, mx] = fast::minmax(x, n);
    const u64 lo = c.pinned_lo;
    if (mn < lo)
        std::abort();  // values below a pinned lo are outside the admitted set
    const unsigned vbits = bits(mx - lo), full_w = std::max(c.min_width, vbits);
    choice best{lo, full_w, 0, 0, c(full_w, 0, 0, vbits)};
    auto run = [&]<class L>(L) {
        alignas(64) L r[256];
        thread_local std::vector<L> longer;
        L *p = n <= 256 ? r : (longer.resize(n), longer.data());
        for (std::size_t i = 0; i < n; ++i)
            p[i] = static_cast<L>(x[i] - lo);
        for (unsigned K = c.min_width; K < vbits; ++K) {
            std::array<u32, 1> low, high;
            count<L, 1>(p, n, {clip<L>(0, K)}, low, high);
            const choice t{lo, K, 0, high[0], c(K, 0, high[0], vbits)};
            if (better(t, best))
                best = t;
        }
    };
    if (vbits <= 8)
        run(std::uint8_t{});
    else if (vbits <= 16)
        run(std::uint16_t{});
    else if (vbits <= 32)
        run(std::uint32_t{});
    else
        run(u64{});
    return best;
}

}  // namespace frame_bounds::fast
