#pragma once
// Candidate boundary searches over one borrowed, ordered native array. Every
// candidate returns the same rank as Intervals' direct_source boundary<E>.
// Names: W is the register span used per count (16/32/64 bytes, emulated with
// several native registers where wider than the ISA), S the fixed window a
// hybrid search resolves by counting, Small the policy for windows under W.
#include <ikea/intervals/search.h>
#include <ikea/intervals/detail/prediction.h>
#include <ikea/detail/filter/compare.h>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>

namespace probe {
using ikea::intervals::edge;
namespace iv = ikea::intervals;

template <edge E, class T> [[gnu::always_inline]] inline bool below(T value, T point) {
    return !iv::detail::meets<E>(value, point);
}
template <class T> [[gnu::always_inline]] inline T element(const T *p, unsigned i) {
    return iv::detail::read_element(p, i);
}
template <class T> [[gnu::always_inline]] inline const T *advance(const T *p, std::ptrdiff_t bytes) {
    return reinterpret_cast<const T *>(reinterpret_cast<const std::byte *>(p) + bytes);
}
// Lanes [from, to) of one register holding Lanes elements.
[[gnu::always_inline]] inline std::uint64_t range_bits(unsigned from, unsigned to) {
    return iv::detail::low_bits(to) & ~iv::detail::low_bits(from);
}

#if defined(__aarch64__)
template <class T> struct neon;
template <> struct neon<std::uint8_t> {
    using v = uint8x16_t;
    static v load(const void *p) { return vld1q_u8(static_cast<const std::uint8_t *>(p)); }
    static v dup(unsigned x) { return vdupq_n_u8(std::uint8_t(x)); }
    static v lt(v a, v b) { return vcltq_u8(a, b); }
    static v le(v a, v b) { return vcleq_u8(a, b); }
    static v ge(v a, v b) { return vcgeq_u8(a, b); }
    static v sub(v a, v b) { return vsubq_u8(a, b); }
    static v band(v a, v b) { return vandq_u8(a, b); }
    static v bor(v a, v b) { return vorrq_u8(a, b); }
    static v index() { return v{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}; }
    static v zero() { return vdupq_n_u8(0); }
    static unsigned sum(v a) { return vaddlvq_u8(a); }
    static v from64(std::uint64_t lo, std::uint64_t hi) {
        return vreinterpretq_u8_u64(vcombine_u64(vcreate_u64(lo), vcreate_u64(hi)));
    }
};
template <> struct neon<std::uint16_t> {
    using v = uint16x8_t;
    static v load(const void *p) { return vld1q_u16(static_cast<const std::uint16_t *>(p)); }
    static v dup(unsigned x) { return vdupq_n_u16(std::uint16_t(x)); }
    static v lt(v a, v b) { return vcltq_u16(a, b); }
    static v le(v a, v b) { return vcleq_u16(a, b); }
    static v ge(v a, v b) { return vcgeq_u16(a, b); }
    static v sub(v a, v b) { return vsubq_u16(a, b); }
    static v band(v a, v b) { return vandq_u16(a, b); }
    static v bor(v a, v b) { return vorrq_u16(a, b); }
    static v index() { return v{0, 1, 2, 3, 4, 5, 6, 7}; }
    static v zero() { return vdupq_n_u16(0); }
    static unsigned sum(v a) { return vaddlvq_u16(a); }
    static v from64(std::uint64_t lo, std::uint64_t hi) {
        return vreinterpretq_u16_u64(vcombine_u64(vcreate_u64(lo), vcreate_u64(hi)));
    }
};
template <> struct neon<std::uint32_t> {
    using v = uint32x4_t;
    static v load(const void *p) { return vld1q_u32(static_cast<const std::uint32_t *>(p)); }
    static v dup(unsigned x) { return vdupq_n_u32(x); }
    static v lt(v a, v b) { return vcltq_u32(a, b); }
    static v le(v a, v b) { return vcleq_u32(a, b); }
    static v ge(v a, v b) { return vcgeq_u32(a, b); }
    static v sub(v a, v b) { return vsubq_u32(a, b); }
    static v band(v a, v b) { return vandq_u32(a, b); }
    static v bor(v a, v b) { return vorrq_u32(a, b); }
    static v index() { return v{0, 1, 2, 3}; }
    static v zero() { return vdupq_n_u32(0); }
    static unsigned sum(v a) { return vaddvq_u32(a); }
    static v from64(std::uint64_t lo, std::uint64_t hi) {
        return vreinterpretq_u32_u64(vcombine_u64(vcreate_u64(lo), vcreate_u64(hi)));
    }
};
template <> struct neon<std::uint64_t> {
    using v = uint64x2_t;
    static v load(const void *p) { return vld1q_u64(static_cast<const std::uint64_t *>(p)); }
    static v dup(std::uint64_t x) { return vdupq_n_u64(x); }
    static v lt(v a, v b) { return vcltq_u64(a, b); }
    static v le(v a, v b) { return vcleq_u64(a, b); }
    static v ge(v a, v b) { return vcgeq_u64(a, b); }
    static v sub(v a, v b) { return vsubq_u64(a, b); }
    static v band(v a, v b) { return vandq_u64(a, b); }
    static v bor(v a, v b) { return vorrq_u64(a, b); }
    static v index() { return v{0, 1}; }
    static v zero() { return vdupq_n_u64(0); }
    static unsigned sum(v a) { return unsigned(vaddvq_u64(a)); }
    static v from64(std::uint64_t lo, std::uint64_t hi) {
        return vcombine_u64(vcreate_u64(lo), vcreate_u64(hi));
    }
};
template <edge E, class T>
[[gnu::always_inline]] inline typename neon<T>::v neon_below(typename neon<T>::v x, T point) {
    using N = neon<T>;
    return E == edge::at_or_after ? N::lt(x, N::dup(point)) : N::le(x, N::dup(point));
}
#endif

// Number of lanes in [from, to) of the W-byte span at p whose element is below
// point. Partial=false asserts the whole span counts.
template <edge E, unsigned W, bool Partial = false, class T>
[[gnu::always_inline]] inline unsigned count(const T *p, T point, unsigned from = 0,
                                             unsigned to = W / sizeof(T)) {
    static_assert(W % 16 == 0);
#if defined(__aarch64__)
    using N = neon<T>;
    constexpr unsigned lanes = 16 / sizeof(T);
    auto acc = N::zero();
#pragma clang loop unroll(full)
    for (unsigned r = 0; r < W / 16; ++r) {
        auto m = neon_below<E>(N::load(p + r * lanes), point);
        if constexpr (Partial) {
            const auto index = N::index();
            const auto lo = N::dup(from > r * lanes ? from - r * lanes : 0);
            const auto hi = N::dup(to > r * lanes ? to - r * lanes : 0);
            m = N::band(m, N::band(N::ge(index, lo), N::lt(index, hi)));
        }
        acc = N::sub(acc, m);
    }
    return N::sum(acc);
#else
    constexpr auto Op =
        E == edge::at_or_after ? ikea::comparison::less : ikea::comparison::less_equal;
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
    constexpr unsigned R = W >= 64 ? 64 : W;
#else
    constexpr unsigned R = W >= 32 ? 32 : 16;
#endif
    constexpr unsigned lanes = R / sizeof(T);
    unsigned total = 0;
#pragma clang loop unroll(full)
    for (unsigned r = 0; r < W / R; ++r) {
        std::uint64_t bits;
        const T *q = p + r * lanes;
        if constexpr (R == 64)
            bits = ikea::detail::filter::bits<sizeof(T), Op>(_mm512_loadu_si512(q), point);
        else if constexpr (R == 32)
            bits = ikea::detail::filter::bits<sizeof(T), Op>(
                _mm256_loadu_si256(reinterpret_cast<const __m256i *>(q)), point);
        else
            bits = ikea::detail::filter::bits<sizeof(T), Op>(
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(q)), point);
        if constexpr (Partial) {
            const unsigned lo = std::min(lanes, from > r * lanes ? from - r * lanes : 0);
            const unsigned hi = std::min(lanes, to > r * lanes ? to - r * lanes : 0);
            bits &= range_bits(lo, hi);
        }
        total += std::popcount(bits);
    }
    return total;
#endif
}

// Two B-byte loads, at p and ending at p+n, assembled into one 16-byte vector:
// low lanes [0,B/size), high lanes cover the last B/size elements. Overlap is
// excluded from the high half. Requires B <= n*size < 2B.
template <edge E, unsigned B, class T>
[[gnu::always_inline]] inline unsigned count_pair(const T *p, unsigned n, T point) {
    constexpr unsigned half = B / sizeof(T);
    std::uint64_t lo = 0, hi = 0;
    std::memcpy(&lo, p, B);
    std::memcpy(&hi, advance(p, std::ptrdiff_t(n * sizeof(T)) - B), B);
    // Valid: [0, half) and [half + (2*half - n), 2*half).
    const unsigned skip = half + (2 * half - n);
#if defined(__aarch64__)
    using N = neon<T>;
    typename N::v x;
    if constexpr (B == 8)
        x = N::from64(lo, hi);
    else
        x = N::from64(lo | (hi << (8 * B)), 0);
    const auto index = N::index();
    auto m = neon_below<E>(x, point);
    m = N::band(m, N::bor(N::lt(index, N::dup(half)),
                          N::band(N::ge(index, N::dup(skip)), N::lt(index, N::dup(2 * half)))));
    return N::sum(N::sub(N::zero(), m));
#else
    constexpr auto Op =
        E == edge::at_or_after ? ikea::comparison::less : ikea::comparison::less_equal;
    __m128i x;
    if constexpr (B == 8)
        x = _mm_set_epi64x(std::int64_t(hi), std::int64_t(lo));
    else
        x = _mm_cvtsi64_si128(std::int64_t(lo | (hi << (8 * B))));
    const std::uint64_t bits = ikea::detail::filter::bits<sizeof(T), Op>(x, point);
    return std::popcount(bits & (iv::detail::low_bits(half) | range_bits(skip, 2 * half)));
#endif
}

enum class small {
    scalar, // branchless per-element loop (today's fallback)
    halves, // overlapping half-width loads, recursively, then 8/4/2-byte pairs
    page,   // one full-span load kept inside the array's page (forward or backward)
    masked, // AVX-512 fault-suppressing masked load
    tail,   // full-span load under an explicit readable-tail grant of W bytes
};

template <edge E, small Small, unsigned W, class T>
[[gnu::always_inline]] inline unsigned short_window(const T *p, unsigned n, T point) {
    constexpr unsigned lanes = W / sizeof(T);
    if constexpr (Small == small::scalar) {
        unsigned total = 0;
        for (unsigned i = 0; i < n; ++i)
            total += below<E>(element(p, i), point);
        return total;
    } else if constexpr (Small == small::tail) {
        return count<E, W, true>(p, point, 0, n);
    } else if constexpr (Small == small::page) {
        if (!n)
            return 0;
        // A span inside the page holding p cannot fault, whichever direction.
        // Never reads before the page or after it; lanes outside [0,n) excluded.
        static_assert(W <= 2048);
        if ((reinterpret_cast<std::uintptr_t>(p) & 4095) <= 4096 - W)
            return count<E, W, true>(p, point, 0, n);
        return count<E, W, true>(advance(p, std::ptrdiff_t(n * sizeof(T)) - W), point, lanes - n,
                                 lanes);
    } else if constexpr (Small == small::masked) {
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
        const auto k = iv::detail::low_bits(n);
        constexpr int cmp = E == edge::at_or_after ? _MM_CMPINT_LT : _MM_CMPINT_LE;
        if constexpr (W == 64) {
            if constexpr (sizeof(T) == 1)
                return std::popcount(_mm512_mask_cmp_epu8_mask(
                    k, _mm512_maskz_loadu_epi8(k, p), _mm512_set1_epi8(char(point)), cmp));
            else if constexpr (sizeof(T) == 2)
                return std::popcount(unsigned(_mm512_mask_cmp_epu16_mask(
                    __mmask32(k), _mm512_maskz_loadu_epi16(__mmask32(k), p),
                    _mm512_set1_epi16(short(point)), cmp)));
            else if constexpr (sizeof(T) == 4)
                return std::popcount(unsigned(_mm512_mask_cmp_epu32_mask(
                    __mmask16(k), _mm512_maskz_loadu_epi32(__mmask16(k), p),
                    _mm512_set1_epi32(int(point)), cmp)));
            else
                return std::popcount(unsigned(_mm512_mask_cmp_epu64_mask(
                    __mmask8(k), _mm512_maskz_loadu_epi64(__mmask8(k), p),
                    _mm512_set1_epi64(std::int64_t(point)), cmp)));
        } else if constexpr (W == 32) {
            if constexpr (sizeof(T) == 1)
                return std::popcount(unsigned(_mm256_mask_cmp_epu8_mask(
                    __mmask32(k), _mm256_maskz_loadu_epi8(__mmask32(k), p),
                    _mm256_set1_epi8(char(point)), cmp)));
            else if constexpr (sizeof(T) == 2)
                return std::popcount(unsigned(_mm256_mask_cmp_epu16_mask(
                    __mmask16(k), _mm256_maskz_loadu_epi16(__mmask16(k), p),
                    _mm256_set1_epi16(short(point)), cmp)));
            else if constexpr (sizeof(T) == 4)
                return std::popcount(unsigned(_mm256_mask_cmp_epu32_mask(
                    __mmask8(k), _mm256_maskz_loadu_epi32(__mmask8(k), p),
                    _mm256_set1_epi32(int(point)), cmp)));
            else
                return std::popcount(unsigned(_mm256_mask_cmp_epu64_mask(
                    __mmask8(k), _mm256_maskz_loadu_epi64(__mmask8(k), p),
                    _mm256_set1_epi64x(std::int64_t(point)), cmp)));
        } else
            static_assert(W == 32 || W == 64);
#else
        static_assert(Small != small::masked, "masked loads need AVX-512BW/VL");
        return 0;
#endif
    } else {
        // halves
        if constexpr (W > 16) {
            if (n >= lanes / 2)
                return count<E, W / 2>(p, point) +
                       count<E, W / 2, true>(advance(p, std::ptrdiff_t((n - lanes / 2) * sizeof(T))),
                                             point, lanes - n, lanes / 2);
            return short_window<E, small::halves, W / 2>(p, n, point);
        } else {
            const auto bytes = n * sizeof(T);
            if (bytes >= 8)
                return count_pair<E, 8>(p, n, point);
            if constexpr (sizeof(T) <= 2)
                if (bytes >= 4)
                    return count_pair<E, 4>(p, n, point);
            if constexpr (sizeof(T) == 1)
                if (bytes >= 2)
                    return count_pair<E, 2>(p, n, point);
            return n ? unsigned(below<E>(element(p, 0), point)) : 0;
        }
    }
}

// Linear count with W-byte spans; the final span overlaps backwards.
template <edge E, unsigned W, small Small, class T>
[[gnu::always_inline]] inline unsigned scan(const T *p, unsigned n, T point) {
    constexpr unsigned lanes = W / sizeof(T);
    if (n < lanes)
        return short_window<E, Small, W>(p, n, point);
    if constexpr (Small == small::tail) {
        unsigned total = 0, i = 0;
        for (; i + lanes <= n; i += lanes)
            total += count<E, W>(p + i, point);
        if (i < n)
            total += count<E, W, true>(p + i, point, 0, n - i);
        return total;
    } else {
        unsigned total = 0, i = 0;
        for (; i + lanes <= n; i += lanes)
            total += count<E, W>(p + i, point);
        if (i < n)
            total += count<E, W, true>(p + (n - lanes), point, lanes - (n - i), lanes);
        return total;
    }
}

// Fixed-trip exact count of an array shorter than S bytes: S/W spans whose
// addresses are clamped inside the array (overlapping backwards), each counting
// only lanes not already covered. Below one span, the single span stays in the
// page holding p (forward or backward). No branch depends on n, and no load
// leaves the array's page.
template <edge E, unsigned S, unsigned W, class T>
[[gnu::always_inline]] inline unsigned count_clamped(const T *p, unsigned n, T point) {
    constexpr int L = W / sizeof(T);
    if (!n) // an empty array admits no readable byte at all
        return 0;
    const int c = int(n);
    const bool forward = (reinterpret_cast<std::uintptr_t>(p) & 4095) <= 4096 - W;
    const bool narrow = c < L;
    // Over a sorted array, a window at a with c below-lanes yields
    // clamp(answer, a, a+L): the answer is the largest a+c over windows with
    // c > 0. Only the first span can hold lanes outside the array.
    const int first = narrow && !forward ? c - L : 0;
    const unsigned from = narrow && !forward ? unsigned(L - c) : 0, to = narrow && forward ? n : L;
    unsigned answer = count<E, W, true>(p + first, point, from, to);
#pragma clang loop unroll(full)
    for (int i = 1; i < int(S / W); ++i) {
        const int at = narrow ? first : std::min(i * L, c - L);
        const unsigned below = count<E, W>(p + at, point);
        answer = std::max(answer, !narrow && below ? unsigned(at) + below : 0u);
    }
    return answer;
}
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
// Fixed-trip exact count with fault-suppressing masked loads: S/64 spans.
template <edge E, unsigned S, class T>
[[gnu::always_inline]] inline unsigned count_masked(const T *p, unsigned n, T point) {
    constexpr unsigned L = 64 / sizeof(T);
    constexpr int cmp = E == edge::at_or_after ? _MM_CMPINT_LT : _MM_CMPINT_LE;
    unsigned total = 0;
#pragma clang loop unroll(full)
    for (unsigned i = 0; i < S / 64; ++i) {
        const unsigned take = n > i * L ? std::min(L, n - i * L) : 0;
        const auto k = iv::detail::low_bits(take);
        const T *q = p + i * L;
        if constexpr (sizeof(T) == 1)
            total += std::popcount(_mm512_mask_cmp_epu8_mask(
                k, _mm512_maskz_loadu_epi8(k, q), _mm512_set1_epi8(char(point)), cmp));
        else if constexpr (sizeof(T) == 2)
            total += std::popcount(unsigned(_mm512_mask_cmp_epu16_mask(
                __mmask32(k), _mm512_maskz_loadu_epi16(__mmask32(k), q),
                _mm512_set1_epi16(short(point)), cmp)));
        else if constexpr (sizeof(T) == 4)
            total += std::popcount(unsigned(_mm512_mask_cmp_epu32_mask(
                __mmask16(k), _mm512_maskz_loadu_epi32(__mmask16(k), q),
                _mm512_set1_epi32(int(point)), cmp)));
        else
            total += std::popcount(unsigned(_mm512_mask_cmp_epu64_mask(
                __mmask8(k), _mm512_maskz_loadu_epi64(__mmask8(k), q),
                _mm512_set1_epi64(std::int64_t(point)), cmp)));
    }
    return total;
}
#endif

enum class fit { scan, fixed, clamp, masked };
// Branchless bisection until the unresolved window fits S bytes, then one
// fixed S-byte count over a window clamped inside the array. Arrays smaller
// than S either scan (exact reads) or count one fixed S-byte span (tail grant).
template <edge E, unsigned S, unsigned W, small Small, fit Fit = fit::scan, class T>
[[gnu::always_inline]] inline unsigned hybrid(const T *p, unsigned n, T point) {
    constexpr unsigned span = S / sizeof(T);
    if (n >= span) {
        unsigned lo = 0, remaining = n;
        while (remaining > span) {
            const auto half = remaining / 2, mid = lo + half;
            lo = iv::detail::select_position<E>(element(p, mid), point, mid, lo);
            remaining -= half;
        }
        const auto base = std::min(lo, n - span);
        return base + count<E, S>(p + base, point);
    }
    if constexpr (Fit == fit::fixed)
        return count<E, S, true>(p, point, 0, n);
    else if constexpr (Fit == fit::clamp)
        return count_clamped<E, S, W>(p, n, point);
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
    else if constexpr (Fit == fit::masked)
        return count_masked<E, S>(p, n, point);
#endif
    else
        return scan<E, W, Small>(p, n, point);
}

// hybrid over a known window: the answer lies in [lo, hi], hi qualifying or n.
template <edge E, unsigned S, class T>
[[gnu::always_inline]] inline unsigned hybrid_between(const T *p, unsigned n, unsigned lo,
                                                      unsigned hi, T point) {
    constexpr unsigned span = S / sizeof(T);
    unsigned remaining = hi - lo;
    while (remaining > span) {
        const auto half = remaining / 2, mid = lo + half;
        lo = iv::detail::select_position<E>(element(p, mid), point, mid, lo);
        remaining -= half;
    }
    const auto base = std::min(lo, n - span);
    return base + count<E, S>(p + base, point);
}
// One interpolation stage from the end values, bracketed by two checked probes
// about 2*sqrt(n) apart; a failed bracket keeps the correct side. Below Min*span
// elements this is the plain hybrid.
template <edge E, unsigned S, unsigned W, small Small, unsigned Min = 4, class T>
[[gnu::always_inline]] inline unsigned lerp_hybrid(const T *p, unsigned n, T point) {
    constexpr unsigned span = S / sizeof(T);
    if (n < Min * span)
        return hybrid<E, S, W, Small>(p, n, point);
    const T first = element(p, 0), last = element(p, n - 1);
    if (iv::detail::meets<E>(first, point))
        return 0;
    if (!iv::detail::meets<E>(last, point))
        return n;
    // first < point <= last (or <), so last > first and the ratio is finite.
    const float ratio = float(n - 1) / float(last - first);
    const auto estimate = std::min(n - 1, unsigned(float(T(point - first)) * ratio));
    const unsigned delta = std::max(span, 2u << ((std::bit_width(n) + 1) / 2));
    const unsigned lo = estimate > delta ? estimate - delta : 0;
    const unsigned hi = std::min(n, estimate + delta);
    const auto window = iv::detail::checked_bracket<E>(p, n, point, iv::rank_window{lo, hi});
    return hybrid_between<E, S>(p, n, window.first, window.limit, point);
}

// Branching bisection to an S-byte window, then the same fixed count. Branches
// let a dependent consumer run ahead on predictions; the count does not.
template <edge E, unsigned S, unsigned W, class T>
[[gnu::always_inline]] inline unsigned branchy_hybrid(const T *p, unsigned n, T point) {
    constexpr unsigned span = S / sizeof(T);
    if (n < span)
        return count_clamped<E, S, W>(p, n, point);
    unsigned lo = 0, hi = n;
    while (hi - lo > span) {
        const auto mid = lo + (hi - lo) / 2;
        if (iv::detail::meets<E>(element(p, mid), point))
            hi = mid;
        else
            lo = mid + 1;
    }
    const auto base = std::min(lo, n - span);
    return base + count<E, S>(p + base, point);
}

template <edge E, class T>
[[gnu::always_inline]] inline unsigned branchless(const T *p, unsigned n, T point) {
    if (!n)
        return 0;
    unsigned lo = 0, remaining = n;
    while (remaining > 1) {
        const auto half = remaining / 2, mid = lo + half;
        lo = iv::detail::select_position<E>(element(p, mid), point, mid, lo);
        remaining -= half;
    }
    return lo + below<E>(element(p, lo), point);
}

template <edge E, class T>
[[gnu::always_inline]] inline unsigned branchy(const T *p, unsigned n, T point) {
    unsigned lo = 0, hi = n;
    while (lo < hi) {
        const auto mid = lo + (hi - lo) / 2;
        if (iv::detail::meets<E>(element(p, mid), point))
            hi = mid;
        else
            lo = mid + 1;
    }
    return lo;
}
// Clamped-index predicated loop: always Lanes iterations, reads only [0, n).
template <edge E, unsigned Lanes, class T>
[[gnu::always_inline]] inline unsigned count_predicated(const T *p, unsigned n, T point) {
    if (!n)
        return 0;
    unsigned total = 0;
#pragma clang loop unroll(full)
    for (unsigned i = 0; i < Lanes; ++i)
        total += unsigned(i < n) & unsigned(below<E>(element(p, std::min(i, n - 1)), point));
    return total;
}

// Production-shaped composition. Arrays of at least one 64-byte line bisect
// branchlessly to one line and count it at a base clamped inside the array.
// Shorter arrays: AVX-512 counts one fault-suppressing masked line. Elsewhere,
// [32B, 64B) counts two overlapping 32-byte spans; under 32 bytes either one
// 32-byte span under a readable Halo after the array, or exact pieces.
enum class tiny { halves, predicated };
template <edge E, unsigned Halo, tiny Tiny = tiny::halves, class T>
[[gnu::always_inline]] inline unsigned proposed(const T *p, unsigned n, T point) {
    constexpr unsigned span = 64 / sizeof(T), half = 32 / sizeof(T);
    if (n >= span) {
        unsigned lo = 0, remaining = n;
        while (remaining > span) {
            const auto h = remaining / 2, mid = lo + h;
            lo = iv::detail::select_position<E>(element(p, mid), point, mid, lo);
            remaining -= h;
        }
        const auto base = std::min(lo, n - span);
        return base + count<E, 64>(p + base, point);
    }
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
    return short_window<E, small::masked, 64>(p, n, point);
#else
    if (n >= half)
        return count<E, 32>(p, point) +
               count<E, 32, true>(p + (n - half), point, span - n, half);
    if constexpr (Halo >= 32)
        return count<E, 32, true>(p, point, 0, n);
    else if constexpr (Tiny == tiny::predicated)
        return count_predicated<E, half>(p, n, point);
    else
        return short_window<E, small::halves, 32>(p, n, point);
#endif
}
} // namespace probe
