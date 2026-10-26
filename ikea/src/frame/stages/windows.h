// Bound's window choice: an inlier window [lo, lo + 2^K) for a block, values
// outside it exceptions, priced by the composition's real bytes. This is the
// path chosen by the bounds study (workbench/spikes/frame-phase-2/bounds:
// stage.h and its README), which keeps the alternatives it was measured
// against: FOR's minimum and maximum; a strided sample sorted in one vector
// proposes a window from its exclusion ladder; one pass over window-local
// saturating lanes prices that window, its width neighbours and the previous
// block's window. Construction only; no read path calls this.
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace ikea::frame::detail::windows {
using u64 = std::uint64_t;
using u32 = std::uint32_t;

inline constexpr std::size_t block_values = 256; // choose() takes at most one block

inline unsigned bits(u64 x) { return static_cast<unsigned>(std::bit_width(x)); }
inline u64 mask(unsigned K) { return K >= 64 ? ~u64{0} : (u64{1} << K) - 1; }

/// What a window costs in the enclosing composition. Widths need not be
/// monotone in bytes (17 Bulk fields: K1 32 bytes, K8 24).
struct cost_model {
    std::array<u32, 65> body{}; // bytes for the block's fields at width K
    unsigned position_bytes = 1;
    unsigned header_bytes = 1; // exception count, when there are any
    unsigned min_width = 1;
    unsigned exception_penalty = 0; // Engine's read-time charge per exception
    unsigned max_exceptions = 256;
    /// The cheapest admitted width at least value_bits wide.
    unsigned full_width(unsigned value_bits) const {
        unsigned best = std::max(min_width, value_bits);
        for (unsigned K = best + 1; K <= 64; ++K)
            if (body[K] < body[best])
                best = K;
        return best;
    }
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
    u32 low = 0, high = 0; // exceptions below lo, above lo + 2^K - 1
    std::size_t bytes = ~std::size_t{0};
};
inline bool better(const choice &a, const choice &b) {
    return a.bytes != b.bytes ? a.bytes < b.bytes : a.low + a.high < b.low + b.high;
}

// The exact cost of a window: one counting pass.
inline choice realise(const u64 *x, std::size_t n, u64 lo, unsigned K, unsigned value_bits, const cost_model &c) {
    const u64 hi = lo > ~u64{0} - mask(K) ? ~u64{0} : lo + mask(K);
    std::size_t low = 0, high = 0;
    for (std::size_t i = 0; i < n; ++i)
        low += x[i] < lo, high += x[i] > hi;
    return {lo, K, static_cast<u32>(low), static_cast<u32>(high), c(K, low, high, value_bits)};
}

// The oracle over a sorted series: for every width the window holding the
// most values (two pointers), plus the window anchored at the minimum.
inline choice exact_sorted(const u64 *s, std::size_t n, const cost_model &c, unsigned value_bits) {
    const unsigned vbits = bits(s[n - 1] - s[0]);
    const unsigned full = c.full_width(vbits);
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

/// Costs for a sample standing for stride values each: bodies shrink by the
/// stride, exceptions do not.
inline cost_model sample_cost(const cost_model &c, std::size_t stride) {
    cost_model sc = c;
    for (auto &b : sc.body)
        b = static_cast<u32>(b / stride);
    sc.header_bytes = 0;
    return sc;
}

template <class V> struct win {
    V lo, hi;
    unsigned K;
};
// A window in residual coordinates, clipped to V.
template <class V> win<V> clip(u64 lo, unsigned K) {
    constexpr u64 vmax = static_cast<V>(~V{0});
    const u64 hi = lo > ~u64{0} - mask(K) ? ~u64{0} : lo + mask(K);
    return {static_cast<V>(std::min(lo, vmax)), static_cast<V>(std::min(hi, vmax)), K};
}

// One 64-byte register of V lanes. Compares yield all-ones lanes, so
// subtracting them counts; a lane sees at most n / L values, so n <= 255 L.
template <class V> using reg = V __attribute__((vector_size(64)));
template <class V> reg<V> load(const V *p) {
    reg<V> v;
    std::memcpy(&v, p, sizeof v);
    return v;
}
template <class V> u32 lanes_sum(const reg<V> &v) {
    using wide = u32 __attribute__((vector_size(64 / sizeof(V) * 4)));
    return __builtin_reduce_add(__builtin_convertvector(v, wide));
}

struct range {
    u64 min, max;
};
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

// Every window's exceptions and its inliers' extent in one pass per window,
// so the cheapest is tightened without another pass. The tightened window
// keeps every inlier and may admit more values above, so its counts are an
// upper bound; the emission pass recounts.
template <class V, std::size_t W>
void price_tight(const V *r, std::size_t n, const std::array<win<V>, W> &w, unsigned vbits, const cost_model &c,
                 std::array<choice, W> &tight) {
    constexpr std::size_t L = 64 / sizeof(V), chunk = 255 * L;
    constexpr V top = static_cast<V>(~V{0});
    const std::size_t whole = n / L * L;
    std::array<u32, W> low{}, high{};
    std::array<reg<V>, W> mn, mx{};
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
    for (std::size_t k = 0; k < W; ++k) {
        const choice priced{w[k].lo, w[k].K, low[k], high[k], c(w[k].K, low[k], high[k], vbits)};
        if (in_max[k] < in_min[k])
            tight[k] = priced; // no inliers
        else {
            const unsigned K = std::max(c.min_width, bits(static_cast<u64>(in_max[k] - in_min[k])));
            tight[k] = {in_min[k], K, low[k], high[k], c(K, low[k], high[k], vbits)};
        }
    }
}

// Bitonic sort of S values held as one vector: every step a compile-time
// shuffle plus a lane-wise min and max.
template <class V, std::size_t S> using lanes = V __attribute__((vector_size(S * sizeof(V))));
template <std::size_t S, std::size_t K, std::size_t J, class W> W bitonic_step(W v) {
    const W p = [&]<std::size_t... I>(std::index_sequence<I...>) { return __builtin_shufflevector(v, v, (I ^ J)...); }(
        std::make_index_sequence<S>{});
    const W lo = v < p ? v : p, hi = v < p ? p : v;
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

// In the sorted sample, the best window excluding m sampled values is the
// narrowest span of S - m consecutive values: v[i + S-1-m] - v[i] over the
// m + 1 starts i, one compile-time shift and a lane-wise minimum per m.
template <std::size_t S, std::size_t M, class W> W spans(const W &v) {
    using V = std::remove_cvref_t<decltype(v[0])>;
    const W ahead = [&]<std::size_t... I>(std::index_sequence<I...>) {
        return __builtin_shufflevector(v, v, (I + S - 1 - M < S ? I + S - 1 - M : S - 1)...);
    }(std::make_index_sequence<S>{});
    constexpr W beyond = []<std::size_t... I>(std::index_sequence<I...>) {
        return W{(I <= M ? V{0} : static_cast<V>(~V{0}))...};
    }(std::make_index_sequence<S>{});
    return (ahead - v) | beyond;
}
// Every window priced lies within [lo - 2^(K+1), lo + 2^(K+2)), so values
// far outside cannot change a compare: subtracting a base below that range
// with saturation, and clamping at the lane's top, keeps the counts exact in
// lanes of K + 3 bits.
template <class L> void stage_local(const u64 *x, std::size_t n, u64 base, L *out) {
    constexpr u64 top = static_cast<L>(~L{0});
    for (std::size_t i = 0; i < n; ++i) {
        const u64 d = x[i] > base ? x[i] - base : 0;
        out[i] = static_cast<L>(d < top ? d : top);
    }
}

/// The window for one block of at most 256 values: S sampled values, the
/// composition's costs, sample costs (sample_cost(c, n / S)) and the previous
/// block's window, or null.
template <std::size_t S = 32>
choice choose(const u64 *x, std::size_t n, const cost_model &c, const cost_model &sc, const choice *prior) {
    const auto [mn, mx] = minmax(x, n);
    const unsigned vbits = bits(mx - mn), full_w = c.full_width(vbits);
    const choice full{mn, full_w, 0, 0, c(full_w, 0, 0, vbits)};
    if (!vbits)
        return full;
    if (n < 2 * S) {
        // A short series is its own sample.
        std::array<u64, 2 * S> s;
        std::copy_n(x, n, s.begin());
        std::sort(s.begin(), s.begin() + n);
        return exact_sorted(s.data(), n, c, vbits);
    }
    using W = lanes<u64, S>;
    W v;
    const std::size_t stride = n / S;
    for (std::size_t i = 0; i < S; ++i)
        v[i] = x[i * stride] - mn;
    v = bitonic<S, 2>(v);
    const unsigned e = sc.position_bytes + sc.exception_penalty + (vbits + 8) / 8;
    auto sample_bytes = [&](unsigned K, unsigned ex) {
        return ex * (n / S) > sc.max_exceptions ? std::size_t{1} << 40 : std::size_t{sc.body[K]} + ex * e;
    };
    std::array<u64, S - 1> least;
    [&]<std::size_t... M>(std::index_sequence<M...>) {
        ((least[M] = __builtin_reduce_min(spans<S, M>(v))), ...);
    }(std::make_index_sequence<S - 1>{});
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
        const choice g = realise(x, n, lo, K, vbits, c);
        return better(g, full) ? g : full;
    }
    const u64 base = lo > mask(K + 1) + 1 ? lo - mask(K + 1) - 1 : 0;
    auto run = [&]<class L>(L) {
        alignas(64) L r[block_values];
        stage_local<L>(x, n, base, r);
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
        const bool prior_local =
            prior && prior->width < 8 * sizeof(L) && prior->lo > base && prior->lo - base < lmax - mask(prior->width);
        w[k++] = prior_local ? clip<L>(prior->lo - base, prior->width) : w[0];
        std::array<choice, 7> tight;
        price_tight<L, 7>(r, n, w, vbits, c, tight);
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

} // namespace ikea::frame::detail::windows
