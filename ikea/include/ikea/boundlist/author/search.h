#pragma once
#include <ikea/boundlist/search.h>
#include <ikea/boundlist/detail/prediction.h>
#include <array>

namespace ikea::boundlist::detail {
template <bool Inclusive>
[[gnu::always_inline]] unsigned scan_bytes(const std::uint8_t *data, unsigned n,
                                           std::uint8_t point) {
    unsigned i = 0, result = 0;
    [[maybe_unused]] constexpr auto Op =
        Inclusive ? ikea::comparison::less : ikea::comparison::less_equal;
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
    for (; i + 64 <= n; i += 64)
        result += std::popcount(
            std::uint64_t(ikea::detail::filter::bits<1, Op>(_mm512_loadu_si512(data + i), point)));
#endif
#if defined(__AVX2__)
    for (; i + 32 <= n; i += 32)
        result += std::popcount(unsigned(ikea::detail::filter::bits<1, Op>(
            _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data + i)), point)));
    for (; i + 16 <= n; i += 16)
        result += std::popcount(unsigned(ikea::detail::filter::bits<1, Op>(
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + i)), point)));
    if (i + 8 <= n) {
        result += std::popcount(
            unsigned(ikea::detail::filter::bits<1, Op>(
                _mm_loadl_epi64(reinterpret_cast<const __m128i *>(data + i)), point)) &
            255u);
        i += 8;
    }
#elif defined(__aarch64__)
    const auto q = vdupq_n_u8(point);
    auto count = [&](const std::uint8_t *p) __attribute__((always_inline)) {
        const auto x = vld1q_u8(p);
        return vshrq_n_u8(Inclusive ? vcltq_u8(x, q) : vcleq_u8(x, q), 7);
    };
    for (; i + 64 <= n; i += 64)
        result += vaddlvq_u8(vaddq_u8(vaddq_u8(count(data + i), count(data + i + 16)),
                                      vaddq_u8(count(data + i + 32), count(data + i + 48))));
    for (; i + 16 <= n; i += 16)
        result += vaddlvq_u8(count(data + i));
    if (i + 8 <= n) {
        const auto x = vld1_u8(data + i), query = vdup_n_u8(point);
        result += vaddlv_u8(vshr_n_u8(Inclusive ? vclt_u8(x, query) : vcle_u8(x, query), 7));
        i += 8;
    }
#endif
    for (; i < n; ++i)
        result += Inclusive ? data[i] < point : data[i] <= point;
    return result;
}
template <bool Inclusive, class T>
[[gnu::always_inline]] unsigned scan_count(const T *data, unsigned n, T point) {
    static_assert(std::is_unsigned_v<T>);
    // This independently measured recipe admits at most65535 ranks; callers
    // validate capacity outside query loops.
    __builtin_assume(n <= 65535);
    if constexpr (sizeof(T) == 1)
        return scan_bytes<Inclusive>(data, n, point);
    else {
        unsigned i = 0, result = 0;
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__) && defined(__AVX512DQ__)
        constexpr unsigned lanes = 64 / sizeof(T);
        auto a = _mm512_setzero_si512(), b = a, c = a, d = a;
        const auto q = [&] {
            if constexpr (sizeof(T) == 2)
                return _mm512_set1_epi16(point);
            else if constexpr (sizeof(T) == 4)
                return _mm512_set1_epi32(point);
            else
                return _mm512_set1_epi64(point);
        }();
        auto add = [&](auto x, auto y) __attribute__((always_inline)) {
            if constexpr (sizeof(T) == 2)
                return _mm512_add_epi16(x, y);
            else if constexpr (sizeof(T) == 4)
                return _mm512_add_epi32(x, y);
            else
                return _mm512_add_epi64(x, y);
        };
        auto accumulate = [&](auto acc, const T *p) __attribute__((always_inline)) {
            const auto x = _mm512_loadu_si512(p);
            if constexpr (sizeof(T) == 2)
                return _mm512_sub_epi16(acc, _mm512_movm_epi16(_mm512_cmp_epu16_mask(
                                                 x, q, Inclusive ? _MM_CMPINT_LT : _MM_CMPINT_LE)));
            else if constexpr (sizeof(T) == 4)
                return _mm512_sub_epi32(acc, _mm512_movm_epi32(_mm512_cmp_epu32_mask(
                                                 x, q, Inclusive ? _MM_CMPINT_LT : _MM_CMPINT_LE)));
            else
                return _mm512_sub_epi64(acc, _mm512_movm_epi64(_mm512_cmp_epu64_mask(
                                                 x, q, Inclusive ? _MM_CMPINT_LT : _MM_CMPINT_LE)));
        };
        for (; i + 4 * lanes <= n; i += 4 * lanes) {
            a = accumulate(a, data + i);
            b = accumulate(b, data + i + lanes);
            c = accumulate(c, data + i + 2 * lanes);
            d = accumulate(d, data + i + 3 * lanes);
        }
        a = add(add(a, b), add(c, d));
        for (; i + lanes <= n; i += lanes)
            a = accumulate(a, data + i);
        if constexpr (sizeof(T) == 2)
            result = _mm512_reduce_add_epi32(_mm512_madd_epi16(a, _mm512_set1_epi16(1)));
        else if constexpr (sizeof(T) == 4)
            result = _mm512_reduce_add_epi32(a);
        else
            result = unsigned(_mm512_reduce_add_epi64(a));
#elif defined(__AVX2__)
        constexpr unsigned lanes = 32 / sizeof(T);
        auto a = _mm256_setzero_si256(), b = a, c = a, d = a;
        const auto q = [&] {
            if constexpr (sizeof(T) == 2)
                return _mm256_set1_epi16(point ^ T(1u << 15));
            else if constexpr (sizeof(T) == 4)
                return _mm256_set1_epi32(point ^ T(1u << 31));
            else
                return _mm256_set1_epi64x(point ^ T(1ULL << 63));
        }();
        auto add = [&](auto x, auto y) __attribute__((always_inline)) {
            if constexpr (sizeof(T) == 2)
                return _mm256_add_epi16(x, y);
            else if constexpr (sizeof(T) == 4)
                return _mm256_add_epi32(x, y);
            else
                return _mm256_add_epi64(x, y);
        };
        auto accumulate = [&](auto acc, const T *p) __attribute__((always_inline)) {
            const auto v = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p));
            if constexpr (sizeof(T) == 2) {
                const auto x = _mm256_xor_si256(v, _mm256_set1_epi16(INT16_MIN));
                const auto pred =
                    Inclusive ? _mm256_cmpgt_epi16(q, x)
                              : _mm256_xor_si256(_mm256_cmpgt_epi16(x, q), _mm256_set1_epi32(-1));
                return _mm256_sub_epi16(acc, pred);
            } else if constexpr (sizeof(T) == 4) {
                const auto x = _mm256_xor_si256(v, _mm256_set1_epi32(INT32_MIN));
                const auto pred =
                    Inclusive ? _mm256_cmpgt_epi32(q, x)
                              : _mm256_xor_si256(_mm256_cmpgt_epi32(x, q), _mm256_set1_epi32(-1));
                return _mm256_sub_epi32(acc, pred);
            } else {
                const auto x = _mm256_xor_si256(v, _mm256_set1_epi64x(INT64_MIN));
                const auto pred =
                    Inclusive ? _mm256_cmpgt_epi64(q, x)
                              : _mm256_xor_si256(_mm256_cmpgt_epi64(x, q), _mm256_set1_epi32(-1));
                return _mm256_sub_epi64(acc, pred);
            }
        };
        for (; i + 4 * lanes <= n; i += 4 * lanes) {
            a = accumulate(a, data + i);
            b = accumulate(b, data + i + lanes);
            c = accumulate(c, data + i + 2 * lanes);
            d = accumulate(d, data + i + 3 * lanes);
        }
        a = add(add(a, b), add(c, d));
        for (; i + lanes <= n; i += lanes)
            a = accumulate(a, data + i);
        if constexpr (sizeof(T) == 2)
            a = _mm256_madd_epi16(a, _mm256_set1_epi16(1));
        auto sum = _mm_add_epi64(_mm256_castsi256_si128(a), _mm256_extracti128_si256(a, 1));
        if constexpr (sizeof(T) != 8) {
            // Add at 32-bit granularity; the preceding 64-bit addition cannot carry
            // from a low lane because all counts are <=65535.
            sum = _mm_add_epi32(sum, _mm_srli_si128(sum, 8));
            sum = _mm_add_epi32(sum, _mm_srli_si128(sum, 4));
            result = unsigned(_mm_cvtsi128_si32(sum));
        } else
            result = unsigned(_mm_cvtsi128_si64(sum) + _mm_extract_epi64(sum, 1));
#elif defined(__aarch64__)
        constexpr unsigned lanes = 16 / sizeof(T);
        using vector =
            std::conditional_t<sizeof(T) == 2, uint16x8_t,
                               std::conditional_t<sizeof(T) == 4, uint32x4_t, uint64x2_t>>;
        auto zero = [&]() -> vector {
            if constexpr (sizeof(T) == 2)
                return vdupq_n_u16(0);
            else if constexpr (sizeof(T) == 4)
                return vdupq_n_u32(0);
            else
                return vdupq_n_u64(0);
        };
        auto a = zero(), b = a, c = a, d = a;
        const auto q = [&]() -> vector {
            if constexpr (sizeof(T) == 2)
                return vdupq_n_u16(point);
            else if constexpr (sizeof(T) == 4)
                return vdupq_n_u32(point);
            else
                return vdupq_n_u64(point);
        }();
        auto add = [&](vector x, vector y) __attribute__((always_inline)) -> vector {
            if constexpr (sizeof(T) == 2)
                return vaddq_u16(x, y);
            else if constexpr (sizeof(T) == 4)
                return vaddq_u32(x, y);
            else
                return vaddq_u64(x, y);
        };
        auto accumulate = [&](vector acc, const T *p) __attribute__((always_inline)) -> vector {
            if constexpr (sizeof(T) == 2) {
                const auto x = vld1q_u16(p);
                return vsubq_u16(acc, Inclusive ? vcltq_u16(x, q) : vcleq_u16(x, q));
            } else if constexpr (sizeof(T) == 4) {
                const auto x = vld1q_u32(p);
                return vsubq_u32(acc, Inclusive ? vcltq_u32(x, q) : vcleq_u32(x, q));
            } else {
                const auto x = vld1q_u64(p);
                return vsubq_u64(acc, Inclusive ? vcltq_u64(x, q) : vcleq_u64(x, q));
            }
        };
        for (; i + 4 * lanes <= n; i += 4 * lanes) {
            a = accumulate(a, data + i);
            b = accumulate(b, data + i + lanes);
            c = accumulate(c, data + i + 2 * lanes);
            d = accumulate(d, data + i + 3 * lanes);
        }
        a = add(add(a, b), add(c, d));
        for (; i + lanes <= n; i += lanes)
            a = accumulate(a, data + i);
        if constexpr (sizeof(T) == 2)
            result = vaddvq_u16(a);
        else if constexpr (sizeof(T) == 4)
            result = vaddvq_u32(a);
        else
            result = unsigned(vaddvq_u64(a));
#endif
        for (; i < n; ++i) {
            const auto value = read_element(data, i);
            result += Inclusive ? value < point : !(point < value);
        }
        return result;
    }
}
} // namespace ikea::boundlist::detail
namespace ikea::boundlist::author {
namespace detail {
template <class Phase, class Source> struct selected_search {
    Phase phase;
    const Source *source;
    bool has_local_read() const { return phase.has_local_read(); }
    auto local_window() const { return phase.local_window(); }
    [[gnu::always_inline]] unsigned finish() const {
        return boundlist::detail::next(*source, phase.finish(), source->size());
    }
};
template <edge E, class Source, class Q>
[[gnu::always_inline]] auto prepare(const Source &source, const Q &point) {
    auto phase = boundlist::detail::prepare<E>(source, point);
    if constexpr (requires { source.selection_source(); }) {
        using Named = std::remove_cvref_t<decltype(source.selection_source())>;
        return selected_search<decltype(phase), Named>{phase, &source.selection_source()};
    } else if constexpr (requires { source.next_eligible(0u, source.size()); })
        return selected_search<decltype(phase), Source>{phase, &source};
    else
        return phase;
}
} // namespace detail
/// Same answer as the ordinary call. The local window is a useful next prefetch
/// target, not an ownership/read grant or a promise of readiness. Built-in
/// geometric phases snapshot routing and borrow backing, not the source/query
/// objects. Population selection also borrows the named source through finish.
/// All completion dependencies must remain one coherent retained version.
template <class Source, class Q>
[[gnu::always_inline]] auto prepare_lower_bound(const Source &source, const Q &point) {
    return detail::prepare<edge::at_or_after>(source, point);
}
template <class Source, class Q>
[[gnu::always_inline]] auto prepare_upper_bound(const Source &source, const Q &point) {
    return detail::prepare<edge::after>(source, point);
}
template <class Source, class Q> void prepare_lower_bound(const Source &&, const Q &) = delete;
template <class Source, class Q> void prepare_upper_bound(const Source &&, const Q &) = delete;

/// Opt-in small hot-list execution. Count is admitted once at construction;
/// ordering, initialized elements and lifetime follow direct_source. Ordinary
/// lower_bound/upper_bound calls and population composition are unchanged.
template <class T> struct scan_source : direct_source<T> {
    scan_source(const T *data, unsigned count) : direct_source<T>{data, count} {
        assert(count <= 65535);
    }
    template <edge E, std::integral Q> [[gnu::always_inline]] unsigned boundary(Q point) const {
        const boundlist::detail::prepared_query<T, Q> q(point);
        if (q.relation)
            return q.relation < 0 ? 0 : this->count;
        return boundlist::detail::scan_count<E == edge::at_or_after>(this->data, this->count,
                                                                     q.value);
    }
};
namespace detail {
template <edge E, class T, std::size_t N>
[[gnu::always_inline]] std::array<unsigned, N>
finish_many(const std::array<direct_source<T>, N> &routing, const std::array<T, N> &points,
            std::array<unsigned, N> &lo, std::array<unsigned, N> &remaining) {
    std::array<unsigned, N> result{};
    bool pending;
    do {
        pending = false;
#pragma clang loop unroll(full)
        for (unsigned i = 0; i < N; ++i)
            if (remaining[i] > boundlist::detail::convergence_lanes<T>) {
                pending = true;
                const auto half = remaining[i] / 2, mid = lo[i] + half;
                lo[i] = boundlist::detail::select_position<E>(
                    boundlist::detail::read_element(routing[i].data, mid), points[i], mid, lo[i]);
                remaining[i] -= half;
            }
    } while (pending);
#pragma clang loop unroll(full)
    for (unsigned i = 0; i < N; ++i)
        result[i] = boundlist::detail::transition_window<E>(routing[i].data, routing[i].count,
                                                            lo[i], lo[i] + remaining[i], points[i]);
    return result;
}
} // namespace detail

/// Prepare a native source once for repeated searches of one coherent snapshot.
/// Derived extrema/model become stale after any endpoint/count/placement change.
/// No encoded bytes, allocation or algorithm argument are added. Other domains
/// retain their source's ordinary ordered comparison path.
template <class T> auto bind_search(direct_source<T> source) {
    if constexpr (std::unsigned_integral<T> && (sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8))
        return boundlist::detail::numeric_binding<T>(source);
    else
        return source;
}

/// Real interleaved searches, with domain preparation included. Sources may name
/// the same or different lists. Query order and duplicates are preserved.
template <edge E, class T, class Q, std::size_t N>
[[gnu::always_inline]] std::array<unsigned, N>
boundaries(const std::array<direct_source<T>, N> &sources, const std::array<Q, N> &queries) {
    static_assert(std::unsigned_integral<T> && std::integral<Q>);
    std::array<unsigned, N> lo{}, remaining{};
    std::array<T, N> points{};
#pragma clang loop unroll(full)
    for (unsigned i = 0; i < N; ++i) {
        const boundlist::detail::prepared_query<T, Q> p(queries[i]);
        points[i] = p.value;
        if (p.relation)
            lo[i] = p.relation < 0 ? 0 : sources[i].count;
        else
            remaining[i] = sources[i].count;
    }
    return detail::finish_many<E>(sources, points, lo, remaining);
}
template <class T, class Q, std::size_t N>
[[gnu::always_inline]] auto lower_bounds(const std::array<direct_source<T>, N> &sources,
                                         const std::array<Q, N> &queries) {
    return boundaries<edge::at_or_after>(sources, queries);
}
template <class T, class Q, std::size_t N>
[[gnu::always_inline]] auto upper_bounds(const std::array<direct_source<T>, N> &sources,
                                         const std::array<Q, N> &queries) {
    return boundaries<edge::after>(sources, queries);
}

template <edge E, class T, class Q, std::size_t N>
[[gnu::always_inline]] std::array<unsigned, N>
boundaries(const std::array<boundlist::detail::numeric_binding<T>, N> &sources,
           const std::array<Q, N> &queries) {
    static_assert(std::integral<Q>);
    std::array<direct_source<T>, N> routing;
    std::array<T, N> points;
    std::array<unsigned, N> lo{}, remaining{};
#pragma clang loop unroll(full)
    for (unsigned i = 0; i < N; ++i) {
        const auto phase = sources[i].template prepare<E>(queries[i]);
        const auto interval = phase.corrected_interval();
        routing[i] = {phase.data, phase.count};
        points[i] = phase.point;
        lo[i] = interval.first;
        remaining[i] = interval.limit - interval.first;
    }
    return detail::finish_many<E>(routing, points, lo, remaining);
}
template <class T, class Q, std::size_t N>
[[gnu::always_inline]] auto
lower_bounds(const std::array<boundlist::detail::numeric_binding<T>, N> &sources,
             const std::array<Q, N> &queries) {
    return boundaries<edge::at_or_after>(sources, queries);
}
template <class T, class Q, std::size_t N>
[[gnu::always_inline]] auto
upper_bounds(const std::array<boundlist::detail::numeric_binding<T>, N> &sources,
             const std::array<Q, N> &queries) {
    return boundaries<edge::after>(sources, queries);
}
} // namespace ikea::boundlist::author
