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

// Counts below and above each of W windows. Lane counters hold at most
// n / L, so whole registers are counted in chunks of 255 registers.
template <class V, std::size_t W> void count(const V *r, std::size_t n, const std::array<win<V>, W> &w, std::array<u32, W> &low, std::array<u32, W> &high) {
    constexpr std::size_t L = 64 / sizeof(V), chunk = 255 * L;
    const std::size_t whole = n / L * L;
    for (std::size_t k = 0; k < W; ++k) {
        const V a = w[k].lo, b = w[k].hi;
        u32 sl = 0, sh = 0;
        for (std::size_t c0 = 0; c0 < whole; c0 += chunk) {
            reg<V> lo{}, hi{};
            for (std::size_t i = c0; i < std::min(whole, c0 + chunk); i += L) {
                const reg<V> x = load(r + i);
                lo -= reinterpret_cast<reg<V>>(x < a), hi -= reinterpret_cast<reg<V>>(x > b);
            }
            sl += lanes_sum(lo), sh += lanes_sum(hi);
        }
        for (std::size_t i = whole; i < n; ++i)
            sl += r[i] < a, sh += r[i] > b;
        low[k] = sl, high[k] = sh;
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
            const std::size_t bytes = sc.body[K] + ex * (below[i] ? e_lo : e_hi);
            if (bytes < best_bytes)
                best_bytes = bytes, bi = i, bj = j;
        }
    const unsigned K = std::max(sc.min_width, bits(static_cast<u64>(d[D - 1 - bj] - d[bi])));
    const choice best{d[bi], K, below[bi], static_cast<u32>(S - below[D - bj]), best_bytes};
    return {best, d[mode]};
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
    trace *t = nullptr;
};

template <unsigned T, std::size_t S, class V>
choice guess_narrow(const V *r, std::size_t n, unsigned vbits, const cost_model &c, const cost_model &sc, const choice *prior, const options &o) {
    const choice full{0, std::max(c.min_width, vbits), 0, 0, c(std::max(c.min_width, vbits), 0, 0, vbits)};
    sampled<V> smp{{0, full.width, 0, 0, full.bytes}, 0};
    if (o.sample && n < 2 * S) {
        // A short series is its own sample: the oracle costs about as much.
        std::vector<u64> s(r, r + n);
        std::sort(s.begin(), s.end());
        smp.best = exact_sorted(s.data(), n, c, vbits);
        smp.mode = r[0];
    } else if (o.sample)
        smp = sample_guess<T, V, S>(r, n, sc, vbits);
    // The sampled mode at the narrowest width: a block dominated by one value
    // keeps it and lets everything else be an exception.
    std::array<win<V>, 4> w{clip<V>(smp.best.lo, smp.best.width), clip<V>(prior ? prior->lo : 0, prior ? prior->width : full.width), clip<V>(0, full.width),
                            o.sample ? clip<V>(smp.mode, c.min_width) : clip<V>(0, full.width)};
    if (o.extremes) {
        const auto e = order_stats<T>(r, n);
        const choice os = from_extremes<T>(e, n, vbits, c);
        w[2] = clip<V>(os.lo, os.width);
    }
    choice best = full;
    const auto priced = price<V, 4>(r, n, w, vbits, c);
    for (std::size_t k = 0; k < 4; ++k)
        if (better(priced[k], best)) {
            best = priced[k];
            if (o.t)
                o.t->winner = static_cast<int>(k);
        }
    if (o.t)
        o.t->guessed = best.bytes, o.t->sample = smp.best.bytes * (n / S);
    // Tightening keeps the inliers but may admit more values above, so the
    // tightened window is priced again with its neighbours.
    best = tighten<V>(r, n, best, vbits, c);
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
        const auto t = price<V, 7>(r, n, rw, vbits, c);
        choice next = t[0];
        for (const auto &q : t)
            if (better(q, next))
                next = q;
        if (!better(next, t[0])) {
            best = t[0];
            break;
        }
        if (o.t)
            ++o.t->rounds;
        best = tighten<V>(r, n, next, vbits, c);
    }
    if (o.t)
        o.t->refined = best.bytes;
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

}  // namespace frame_bounds::fast
