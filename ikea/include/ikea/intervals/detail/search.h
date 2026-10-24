#pragma once
#include <ikea/intervals/types.h>
#include <ikea/detail/filter/compare.h>
#include <algorithm>
#include <bit>
#include <cstring>

namespace ikea::intervals::detail {
template <class T>
[[gnu::always_inline]] decltype(auto) read_element(const T *data, unsigned index) {
    if constexpr (std::unsigned_integral<T>) {
        T result;
        std::memcpy(&result, reinterpret_cast<const std::byte *>(data) + index * sizeof(T),
                    sizeof(T));
        return result;
    } else
        return data[index];
}
// One key-dependent position selection. Unlike a C++ ternary, the native-word
// candidate guarantees cmov/csel; loop control remains separate. Other ordered
// domains retain comparison semantics without requiring an integer encoding.
template <edge E, class T>
[[gnu::always_inline]] unsigned select_position(const T &value, const T &point, unsigned next,
                                                unsigned base) {
    if constexpr (std::unsigned_integral<T> && sizeof(T) <= 8) {
        const std::uint64_t a = value, b = point;
#if defined(__x86_64__)
        if constexpr (E == edge::at_or_after)
            asm("cmpq %[b], %[a]\n\tcmovbl %k[next], %k[base]"
                : [base] "+r"(base)
                : [a] "r"(a), [b] "r"(b), [next] "r"(next)
                : "cc");
        else
            asm("cmpq %[b], %[a]\n\tcmovbel %k[next], %k[base]"
                : [base] "+r"(base)
                : [a] "r"(a), [b] "r"(b), [next] "r"(next)
                : "cc");
        return base;
#elif defined(__aarch64__)
        if constexpr (E == edge::at_or_after)
            asm("cmp %x[a], %x[b]\n\tcsel %w[base], %w[next], %w[base], lo"
                : [base] "+r"(base)
                : [a] "r"(a), [b] "r"(b), [next] "r"(next)
                : "cc");
        else
            asm("cmp %x[a], %x[b]\n\tcsel %w[base], %w[next], %w[base], ls"
                : [base] "+r"(base)
                : [a] "r"(a), [b] "r"(b), [next] "r"(next)
                : "cc");
        return base;
#endif
    }
    return meets<E>(value, point) ? base : next;
}
template <class T>
using native_bits =
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
    std::conditional_t<sizeof(T) == 1, std::uint64_t, unsigned>;
#else
    unsigned;
#endif
template <edge E, class T>
[[gnu::always_inline]] native_bits<T> native_mask(const T *data, unsigned count, const T &query) {
    native_bits<T> result = 0;
    unsigned i = 0;
    constexpr auto Op =
        E == edge::after ? ikea::comparison::greater : ikea::comparison::greater_equal;
    if constexpr (std::unsigned_integral<T>) {
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
        {
            constexpr unsigned lanes = 64 / sizeof(T);
            for (; i + lanes <= count; i += lanes)
                result |= native_bits<T>(ikea::detail::filter::bits<sizeof(T), Op>(
                              _mm512_loadu_si512(data + i), query))
                          << i;
        }
#endif
#if defined(__AVX2__)
        {
            constexpr unsigned lanes = 32 / sizeof(T);
            for (; i + lanes <= count; i += lanes)
                result |=
                    native_bits<T>(ikea::detail::filter::bits<sizeof(T), Op>(
                        _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data + i)), query))
                    << i;
        }
        {
            constexpr unsigned lanes = 16 / sizeof(T);
            for (; i + lanes <= count; i += lanes)
                result |= native_bits<T>(ikea::detail::filter::bits<sizeof(T), Op>(
                              _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + i)), query))
                          << i;
        }
#elif defined(__aarch64__)
        constexpr unsigned lanes = 16 / sizeof(T);
        for (; i + lanes <= count; i += lanes)
            result |= ikea::detail::filter::bits<sizeof(T), Op>(
                          vld1q_u8(reinterpret_cast<const std::uint8_t *>(data + i)), query)
                      << i;
#endif
    }
    for (; i < count; ++i)
        result |= native_bits<T>(meets<E>(read_element(data, i), query)) << i;
    return result;
}

// Native windows converge to one 64-byte span and are resolved by counting it:
// over ordered endpoints, the rank below the query is the number of elements
// that do not meet it. Other ordered domains keep a short comparison tail.
template <class T>
constexpr unsigned convergence_lanes = std::unsigned_integral<T> ? unsigned(64 / sizeof(T)) : 4u;

// Elements below point among lanes [from, to) of the Bytes-byte span at data.
// Partial=false counts every lane. Spans wider than a register combine
// several; NEON accumulates lane counts and reduces once.
template <edge E, unsigned Bytes, bool Partial = false, class T>
[[gnu::always_inline]] unsigned count_below(const T *data, T point, unsigned from = 0,
                                            unsigned to = Bytes / sizeof(T)) {
    static_assert(std::unsigned_integral<T> && Bytes % 16 == 0);
#if defined(__aarch64__)
    constexpr unsigned lanes = 16 / sizeof(T);
    auto acc = vdupq_n_u8(0);
    using L = std::conditional_t<
        sizeof(T) == 1, uint8x16_t,
        std::conditional_t<sizeof(T) == 2, uint16x8_t,
                           std::conditional_t<sizeof(T) == 4, uint32x4_t, uint64x2_t>>>;
    auto as = [](uint8x16_t x) __attribute__((always_inline)) {
        if constexpr (sizeof(T) == 1)
            return x;
        else if constexpr (sizeof(T) == 2)
            return vreinterpretq_u16_u8(x);
        else if constexpr (sizeof(T) == 4)
            return vreinterpretq_u32_u8(x);
        else
            return vreinterpretq_u64_u8(x);
    };
    auto bytes = [](L x) __attribute__((always_inline)) {
        if constexpr (sizeof(T) == 1)
            return x;
        else if constexpr (sizeof(T) == 2)
            return vreinterpretq_u8_u16(x);
        else if constexpr (sizeof(T) == 4)
            return vreinterpretq_u8_u32(x);
        else
            return vreinterpretq_u8_u64(x);
    };
    auto splat = [](std::uint64_t x) __attribute__((always_inline)) -> L {
        if constexpr (sizeof(T) == 1)
            return vdupq_n_u8(std::uint8_t(x));
        else if constexpr (sizeof(T) == 2)
            return vdupq_n_u16(std::uint16_t(x));
        else if constexpr (sizeof(T) == 4)
            return vdupq_n_u32(std::uint32_t(x));
        else
            return vdupq_n_u64(x);
    };
    // Lanes below point under edge E (Strict: always <, for lane ranges).
    auto below = []<bool Strict = false>(L x, L q) __attribute__((always_inline)) -> L {
        constexpr bool lt = Strict || E == edge::at_or_after;
        if constexpr (sizeof(T) == 1)
            return lt ? vcltq_u8(x, q) : vcleq_u8(x, q);
        else if constexpr (sizeof(T) == 2)
            return lt ? vcltq_u16(x, q) : vcleq_u16(x, q);
        else if constexpr (sizeof(T) == 4)
            return lt ? vcltq_u32(x, q) : vcleq_u32(x, q);
        else
            return lt ? vcltq_u64(x, q) : vcleq_u64(x, q);
    };
    const auto q = splat(point);
#pragma clang loop unroll(full)
    for (unsigned r = 0; r < Bytes / 16; ++r) {
        auto m = below(as(vld1q_u8(reinterpret_cast<const std::uint8_t *>(data + r * lanes))), q);
        if constexpr (Partial) {
            static constexpr std::uint8_t ramp[16] = {0, 1, 2,  3,  4,  5,  6,  7,
                                                      8, 9, 10, 11, 12, 13, 14, 15};
            L index;
            if constexpr (sizeof(T) == 1)
                index = vld1q_u8(ramp);
            else if constexpr (sizeof(T) == 2)
                index = vmovl_u8(vld1_u8(ramp));
            else if constexpr (sizeof(T) == 4)
                index = vmovl_u16(vget_low_u16(vmovl_u8(vld1_u8(ramp))));
            else
                index = vmovl_u32(vget_low_u32(vmovl_u16(vget_low_u16(vmovl_u8(vld1_u8(ramp))))));
            const unsigned lo = from > r * lanes ? from - r * lanes : 0;
            const unsigned hi = to > r * lanes ? to - r * lanes : 0;
            const auto inside = bytes(below.template operator()<true>(index, splat(hi))) &
                                ~bytes(below.template operator()<true>(index, splat(lo)));
            m = as(bytes(m) & inside);
        }
        acc = vsubq_u8(acc, bytes(m));
    }
    // Each lane holds a multiple of the element's all-ones byte count.
    return unsigned(vaddlvq_u8(acc)) / sizeof(T);
#else
    constexpr auto Op =
        E == edge::at_or_after ? ikea::comparison::less : ikea::comparison::less_equal;
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
    constexpr unsigned R = Bytes >= 64 ? 64 : Bytes;
#else
    constexpr unsigned R = Bytes >= 32 ? 32 : 16;
#endif
    constexpr unsigned lanes = R / sizeof(T);
    unsigned total = 0;
#pragma clang loop unroll(full)
    for (unsigned r = 0; r < Bytes / R; ++r) {
        const T *p = data + r * lanes;
        std::uint64_t bits;
        if constexpr (R == 64)
            bits = ikea::detail::filter::bits<sizeof(T), Op>(_mm512_loadu_si512(p), point);
        else if constexpr (R == 32)
            bits = ikea::detail::filter::bits<sizeof(T), Op>(
                _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p)), point);
        else
            bits = ikea::detail::filter::bits<sizeof(T), Op>(
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(p)), point);
        if constexpr (Partial) {
            const unsigned lo = std::min(lanes, from > r * lanes ? from - r * lanes : 0);
            const unsigned hi = std::min(lanes, to > r * lanes ? to - r * lanes : 0);
            bits &= low_bits(hi) & ~low_bits(lo);
        }
        total += unsigned(std::popcount(bits));
    }
    return total;
#endif
}

// Two Piece-byte loads, at data and ending at data+n, in one 16-byte register.
// Requires Piece <= n*size < 2*Piece; the overlap counts once. Assembled in
// registers: a store and reload would add a forwarding stall to every lookup.
template <edge E, unsigned Piece, class T>
[[gnu::always_inline]] unsigned count_pieces(const T *data, unsigned n, T point) {
    static_assert(Piece == 8 || Piece == 4);
    constexpr unsigned half = Piece / sizeof(T);
    std::uint64_t lo = 0, hi = 0;
    std::memcpy(&lo, data, Piece);
    std::memcpy(&hi, reinterpret_cast<const std::byte *>(data) + n * sizeof(T) - Piece, Piece);
    if constexpr (Piece == 4) {
        lo |= hi << 32;
        hi = 0;
    }
    // Lanes [0, half) and [half + (2*half - n), 2*half).
    const unsigned skip = 3 * half - n;
#if defined(__aarch64__)
    const auto x = vcombine_u64(vcreate_u64(lo), vcreate_u64(hi));
    std::uint64_t below;
    if constexpr (sizeof(T) == 1) {
        const auto q = vdupq_n_u8(point), v = vreinterpretq_u8_u64(x);
        below = ikea::detail::filter::byte_bits(E == edge::at_or_after ? vcltq_u8(v, q)
                                                                       : vcleq_u8(v, q));
    } else if constexpr (sizeof(T) == 2) {
        const auto q = vdupq_n_u16(point), v = vreinterpretq_u16_u64(x);
        const auto m = E == edge::at_or_after ? vcltq_u16(v, q) : vcleq_u16(v, q);
        below = vaddvq_u16(vandq_u16(m, uint16x8_t{1, 2, 4, 8, 16, 32, 64, 128}));
    } else {
        const auto q = vdupq_n_u32(point), v = vreinterpretq_u32_u64(x);
        const auto m = E == edge::at_or_after ? vcltq_u32(v, q) : vcleq_u32(v, q);
        below = vaddvq_u32(vandq_u32(m, uint32x4_t{1, 2, 4, 8}));
    }
#else
    constexpr auto Op =
        E == edge::at_or_after ? ikea::comparison::less : ikea::comparison::less_equal;
    const std::uint64_t below = ikea::detail::filter::bits<sizeof(T), Op>(
        _mm_set_epi64x(std::int64_t(hi), std::int64_t(lo)), point);
#endif
    return unsigned(
        std::popcount(below & (low_bits(half) | (low_bits(2 * half) & ~low_bits(skip)))));
}
// One to three elements: a predicated scalar count over clamped indices,
// reading only [data, data+n) with no control depending on n.
template <edge E, class T>
[[gnu::always_inline]] unsigned count_tiny(const T *data, unsigned n, T point) {
    unsigned total = 0;
#pragma clang loop unroll(full)
    for (unsigned i = 0; i < 3; ++i)
        total += unsigned(i < n) &
                 unsigned(!meets<E>(read_element(data, std::min(i, n - 1)), point));
    return total;
}

// Rank below point in an ordered array shorter than one span. Reads stay in
// [data, data+n) unless Readable grants max(n*size, Readable) initialized,
// readable, read-isolated bytes from data. AVX-512 masked loads suppress the
// excluded lanes' accesses entirely.
template <edge E, unsigned Readable = 0, class T>
[[gnu::always_inline]] unsigned count_short(const T *data, unsigned n, T point) {
    constexpr unsigned half = 32 / sizeof(T);
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
    constexpr int cmp = E == edge::at_or_after ? _MM_CMPINT_LT : _MM_CMPINT_LE;
    const auto k = low_bits(n);
    if constexpr (sizeof(T) == 1)
        return unsigned(std::popcount(_mm512_mask_cmp_epu8_mask(
            k, _mm512_maskz_loadu_epi8(k, data), _mm512_set1_epi8(char(point)), cmp)));
    else if constexpr (sizeof(T) == 2)
        return unsigned(std::popcount(unsigned(_mm512_mask_cmp_epu16_mask(
            __mmask32(k), _mm512_maskz_loadu_epi16(__mmask32(k), data),
            _mm512_set1_epi16(short(point)), cmp))));
    else if constexpr (sizeof(T) == 4)
        return unsigned(std::popcount(unsigned(_mm512_mask_cmp_epu32_mask(
            __mmask16(k), _mm512_maskz_loadu_epi32(__mmask16(k), data),
            _mm512_set1_epi32(int(point)), cmp))));
    else
        return unsigned(std::popcount(unsigned(_mm512_mask_cmp_epu64_mask(
            __mmask8(k), _mm512_maskz_loadu_epi64(__mmask8(k), data),
            _mm512_set1_epi64(std::int64_t(point)), cmp))));
#else
    if (n >= half)
        return count_below<E, 32>(data, point) +
               count_below<E, 32, true>(data + (n - half), point, 2 * half - n, half);
    if constexpr (Readable >= 32)
        return count_below<E, 32, true>(data, point, 0, n);
    else {
        const auto bytes = n * sizeof(T);
        if (bytes >= 16)
            return count_below<E, 16>(data, point) +
                   count_below<E, 16, true>(data + (n - 16 / sizeof(T)), point,
                                            32 / sizeof(T) - n, 16 / sizeof(T));
        // Up to three elements: scalar compares have the shortest latency.
        if (n <= 3)
            return n ? count_tiny<E>(data, n, point) : 0;
        if constexpr (sizeof(T) <= 2)
            if (bytes >= 8)
                return count_pieces<E, 8>(data, n, point);
        if constexpr (sizeof(T) == 1)
            return count_pieces<E, 4>(data, n, point);
        else
            __builtin_unreachable(); // wider elements: four or more fill 16 bytes
    }
#endif
}

// lo..hi is the unresolved bisection window; hi itself is a known qualifying
// endpoint or the count sentinel. Native windows count one 64-byte span at a
// base clamped inside the array (it may touch two cache lines); shorter arrays
// count themselves whole. Either count is the global rank, within [lo, hi].
template <edge E, unsigned Readable = 0, class T>
[[gnu::always_inline]] unsigned transition_window(const T *data, unsigned n, unsigned lo,
                                                  unsigned hi, const T &p) {
    // A resolved window needs no read; ready phases pass no backing at all.
    if (lo == hi)
        return hi;
    if constexpr (std::unsigned_integral<T>) {
        constexpr auto lanes = convergence_lanes<T>;
        if (n < lanes)
            return count_short<E, Readable>(data, n, p);
        const auto base = std::min(lo, n - lanes);
        return base + count_below<E, 64>(data + base, p);
    } else {
        for (; lo < hi; ++lo)
            if (meets<E>(read_element(data, lo), p))
                break;
        return lo;
    }
}
template <edge E, bool Select = false, unsigned Readable = 0, class T>
[[gnu::always_inline]] unsigned hybrid_window(const T *data, unsigned n, unsigned lo, unsigned hi,
                                              const T &p) {
    if constexpr (Select) {
        unsigned remaining = hi - lo;
        while (remaining > convergence_lanes<T>) {
            const auto half = remaining / 2, mid = lo + half;
            lo = select_position<E>(read_element(data, mid), p, mid, lo);
            remaining -= half;
        }
        hi = lo + remaining;
    } else {
        while (hi - lo > convergence_lanes<T>) {
            const auto mid = lo + (hi - lo) / 2;
            if (meets<E>(read_element(data, mid), p))
                hi = mid;
            else
                lo = mid + 1;
        }
    }
    return transition_window<E, Readable>(data, n, lo, hi, p);
}

template <edge E, bool Select = false, unsigned Readable = 0, class T>
[[gnu::always_inline]] unsigned hybrid_array(const T *data, unsigned n, const T &p) {
    return hybrid_window<E, Select, Readable>(data, n, 0, n, p);
}

// Capacity is a private readable-tail proof, not a logical checkpoint count.
// Inactive values are arbitrary and must never influence the selected rank.
template <edge E, unsigned Capacity = 0, class T>
[[gnu::always_inline]] unsigned checkpoint_boundary(const T *data, unsigned n, T point) {
    if constexpr (Capacity) {
        static_assert(Capacity <= 32);
        __builtin_assume(n <= Capacity);
        const auto mask = native_mask<E>(data, Capacity, point) & unsigned(low_bits(n));
        return mask ? unsigned(std::countr_zero(mask)) : n;
    } else
        return hybrid_array<E, true>(data, n, point);
}

} // namespace ikea::intervals::detail
