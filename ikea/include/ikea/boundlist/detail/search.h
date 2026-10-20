#pragma once
#include <ikea/boundlist/types.h>
#include <ikea/detail/filter/compare.h>
#include <algorithm>
#include <bit>
#include <cstring>

namespace ikea::boundlist::detail {
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

template <class T>
constexpr unsigned convergence_lanes = [] {
    if constexpr (!std::unsigned_integral<T>)
        return 4u;
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
    else
        return unsigned(64 / sizeof(T));
#elif defined(__AVX2__)
    else
        return unsigned(32 / sizeof(T));
#else
    else
        return unsigned(16 / sizeof(T));
#endif
}();

// lo..hi is the unresolved bisection window; hi itself is a known qualifying
// endpoint or the count sentinel. Wide reads stay within the actual array,
// shifting the final load left rather than asking for an unreadable tail.
template <edge E, class T>
[[gnu::always_inline]] unsigned transition_window(const T *data, unsigned n, unsigned lo,
                                                  unsigned hi, const T &p) {
    if (lo == hi)
        return hi;
    constexpr auto lanes = convergence_lanes<T>;
    const auto take = std::min(n, lanes);
    const auto base = std::min(lo, n - take);
    auto mask = native_mask<E>(data + base, take, p);
    mask &= native_bits<T>(low_bits(hi - base) & ~low_bits(lo - base));
    return mask ? base + std::countr_zero(mask) : hi;
}
template <edge E, bool Select = false, class T>
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
    return transition_window<E>(data, n, lo, hi, p);
}

template <edge E, bool Select = false, class T>
[[gnu::always_inline]] unsigned hybrid_array(const T *data, unsigned n, const T &p) {
    return hybrid_window<E, Select>(data, n, 0, n, p);
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

} // namespace ikea::boundlist::detail
