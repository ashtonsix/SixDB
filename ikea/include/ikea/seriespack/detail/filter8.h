#pragma once
#include <ikea/detail/filter/compare.h>
#include <ikea/seriespack/author/expression.h>
#include <ikea/seriespack/author/local_equal.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace ikea::seriespack::detail {
/// Local residual planes already contain eight original-position bits. This
/// evaluates the predicate in that representation without transposing values.
template <comparison Op, unsigned K>
[[gnu::always_inline]] inline unsigned filter_planes8(const std::uint8_t* p, std::uint64_t rhs) {
    static_assert(K > 0 && K < 8);
#if defined(__aarch64__)
    if constexpr (Op == comparison::equal && K >= 4)
        return author::local_equal8<K>(rhs)(load<K>(p));
#endif
    unsigned result = Op == comparison::equal ? 255 : 0;
    each<K>([&](auto bit) {
        const unsigned plane = p[bit];
        const unsigned query = 0u - unsigned((rhs >> bit) & 1);
        if constexpr (Op == comparison::equal)
            result &= plane ^ ~query;
        else {
            constexpr bool reverse = Op == comparison::greater || Op == comparison::less_equal;
            const unsigned a = reverse ? plane : ~plane;
            const unsigned b = reverse ? ~query : query;
            result = (a & b) | ((a | b) & result);
        }
    });
    if constexpr (Op == comparison::greater_equal || Op == comparison::less_equal)
        return ~result & 255;
    else
        return result & 255;
}

template <comparison Op, unsigned K>
[[gnu::always_inline]] inline unsigned filter_local8(const std::uint8_t* p, std::uint64_t rhs) {
    if (rhs >= (std::uint64_t{1} << K))
        return ikea::compare<Op>(0, rhs) ? 255 : 0;
    return filter_planes8<Op, K>(p, rhs);
}

#if defined(__aarch64__) || defined(__AVX2__)
// Expand only bytes inside this eight-row body. An overlapping final load
// borrows preceding body bytes, never padding, another tile or a residual.
template <unsigned Q, unsigned First>
[[gnu::always_inline]] inline auto filter_body_packet(const std::uint8_t* p) {
    constexpr unsigned L = sizeof(uint_for<Q * 8>), lanes = 16 / L;
    constexpr unsigned start = First * Q;
    constexpr unsigned back = start + 16 > 8 * Q ? start + 16 - 8 * Q : 0;
#if defined(__aarch64__)
    if constexpr (Q == 1)
        return vcombine_u8(vld1_u8(p), vdup_n_u8(0));
    else if constexpr (Q == L)
        return vld1q_u8(p + start);
#else
    if constexpr (Q == 1)
        return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
    else if constexpr (Q == L)
        return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + start));
#endif
    else {
        static constexpr auto map = [] {
            std::array<std::uint8_t, 16> indices{};
            indices.fill(255);
            for (unsigned lane = 0; lane < lanes; ++lane)
                for (unsigned b = 0; b < Q; ++b)
                    indices[lane * L + b] = back + lane * Q + b;
            return indices;
        }();
#if defined(__aarch64__)
        return vqtbl1q_u8(vld1q_u8(p + start - back), vld1q_u8(map.data()));
#else
        return _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p + start - back)),
                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(map.data())));
#endif
    }
}

template <comparison Op, unsigned Q>
[[gnu::always_inline]] inline unsigned filter_body8(const std::uint8_t* p, std::uint64_t rhs) {
    constexpr unsigned L = sizeof(uint_for<Q * 8>);
    unsigned result = 0;
#if defined(__aarch64__)
    each<(L + 1) / 2>([&](auto part) {
        result |= ikea::detail::filter::bits<L, Op>(filter_body_packet<Q, part * (16 / L)>(p), rhs)
                  << (part * (16 / L));
    });
#else
    if constexpr (L <= 2)
        result = ikea::detail::filter::bits<L, Op>(filter_body_packet<Q, 0>(p), rhs);
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
    else if constexpr (Q == 8)
        result = ikea::detail::filter::bits<8, Op>(_mm512_loadu_si512(p), rhs);
#endif
    else
        each<L / 4>([&](auto part) {
            const auto x = [&] {
                if constexpr (Q == L)
                    return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + part * 32));
                else {
                    const auto lo = filter_body_packet<Q, part * (32 / L)>(p);
                    const auto hi = filter_body_packet<Q, part * (32 / L) + 16 / L>(p);
                    return _mm256_inserti128_si256(_mm256_castsi128_si256(lo), hi, 1);
                }
            }();
            result |= ikea::detail::filter::bits<L, Op>(x, rhs) << (part * (32 / L));
        });
#endif
    return result & 255;
}

// Striped residuals occupy eight consecutive byte lanes per participating
// stripe. Reconstruct those lanes without loading the following eight rows.
template <comparison Op, class F>
[[gnu::always_inline]] inline unsigned filter_stripe8(const std::uint8_t* tile, unsigned lane,
                                                      std::uint64_t rhs) {
#if defined(__aarch64__)
    auto x = vdupq_n_u8(0);
#else
    auto x = _mm_setzero_si128();
#endif
    each<F::tail>([&](auto bit) {
        const auto pos = tail_bit<F::tail>(lane / 32, bit);
        const auto* p = tile + stripe_offset<F>(pos / 8) + lane % 32;
#if defined(__aarch64__)
        const auto bytes = vcombine_u8(vld1_u8(p), vdup_n_u8(0));
        const auto yes = vtstq_u8(bytes, vdupq_n_u8(1u << (pos % 8)));
        x = vorrq_u8(x, vandq_u8(yes, vdupq_n_u8(1u << bit)));
#else
        const auto bytes = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
        const auto mask = _mm_set1_epi8(1u << (pos % 8));
        const auto yes = _mm_cmpeq_epi8(_mm_and_si128(bytes, mask), mask);
        x = _mm_or_si128(x, _mm_and_si128(yes, _mm_set1_epi8(1u << bit)));
#endif
    });
    return ikea::detail::filter::bits<1, Op>(x, rhs) & 255;
}
#endif
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
// Contiguous standard words need no tile reconstruction. Masked final loads
// preserve exact bounds for e.g. 24 u16s (48 bytes), including page ends.
template <comparison Op, unsigned K>
[[gnu::always_inline]] inline std::uint64_t filter_words(const std::uint8_t* p, unsigned count,
                                                         std::uint64_t rhs) {
    constexpr unsigned bytes = K / 8, lanes = 64 / bytes;
    std::uint64_t out = 0;
    each<bytes>([&](auto part) __attribute__((always_inline)) {
        constexpr unsigned first = part * lanes;
        if (first < count) {
            const auto n = std::min(lanes, count - first);
            const auto live = ~std::uint64_t{0} >> (64 - n);
            const auto* at = p + first * bytes;
            const auto x = [&] {
                if constexpr (bytes == 1)
                    return _mm512_maskz_loadu_epi8(live, at);
                if constexpr (bytes == 2)
                    return _mm512_maskz_loadu_epi16(live, at);
                if constexpr (bytes == 4)
                    return _mm512_maskz_loadu_epi32(live, at);
                if constexpr (bytes == 8)
                    return _mm512_maskz_loadu_epi64(live, at);
            }();
            out |= (ikea::detail::filter::bits<bytes, Op>(x, rhs) & live) << first;
        }
    });
    return out;
}
#endif
} // namespace ikea::seriespack::detail

namespace ikea::seriespack::author {
namespace detail {
template <class E> inline constexpr bool guarded_filter = false;
}
namespace tile_filter {
// Exact Local tail word, shared by ordinary filtering and reader searches.
template <class S>
[[gnu::always_inline]] std::uint64_t
local_plane_word(const leaf<S, field_kind::tail>& node, std::size_t row) {
    using F = typename S::layout_type;
    static_assert(F::storage == geometry::local);
    const auto* tile = [&] {
        if constexpr (requires { node.source.data; })
            return ikea::to_u8(node.source.data) + (row / F::tile_rows) * F::tile_bytes;
        else {
            const auto& stream = node.source.stream(0);
            return ikea::to_u8(stream.bytes).data() + (row / F::tile_rows) * stream.stride;
        }
    }();
    return ikea::seriespack::detail::load<F::tail>(tile + 8 * F::body);
}
template <class E> inline constexpr bool dense_words = false;
template <unsigned K, class S, class T, class H0, class H1>
inline constexpr bool dense_words<
    value_expression<K, 0, payload_expression<K, leaf<S, field_kind::body>, T>, H0, H1>> =
    K >= 8 && std::has_single_bit(K) && S::layout_type::width == K &&
    requires(const S& s) { s.data; };

// Limit the default direct16 selection to native Local leaves. Authored
// replacements retain their own read lowering; native8 itself supports them.
template <class E> inline constexpr bool direct_local = false;
template <unsigned K, class B, class T>
inline constexpr bool
    direct_local<payload_expression<K, leaf<B, field_kind::body>, leaf<T, field_kind::tail>>> =
        K > 8 && K % 8 != 0 &&
        B::layout_type::storage == geometry::local&& T::layout_type::storage == geometry::local;

template <comparison Op, class E>
[[gnu::always_inline]] unsigned evaluate(const E& source, std::size_t row, std::uint64_t rhs);

template <comparison Op, unsigned Shift, class High, class Low>
[[gnu::always_inline]] unsigned joined(High high, Low low, std::uint64_t rhs) {
    const auto equal = high.template operator()<comparison::equal>(rhs >> Shift);
    const auto tail = low.template operator()<Op>(rhs & (~std::uint64_t{0} >> (64 - Shift)));
    if constexpr (Op == comparison::equal)
        return equal & tail;
    else {
        constexpr auto strict = Op == comparison::less || Op == comparison::less_equal
                                    ? comparison::less
                                    : comparison::greater;
        return high.template operator()<strict>(rhs >> Shift) | (equal & tail);
    }
}

template <comparison Op, class E>
[[gnu::always_inline]] unsigned evaluate(const E& source, std::size_t row, std::uint64_t rhs) {
    constexpr unsigned K = E::bit_width;
    if constexpr (detail::guarded_filter<E>)
        return evaluate<Op>(source.expression, row, rhs);
    else if constexpr (requires { E::heads; }) {
        constexpr auto H = E::heads;
        if constexpr (H == 0)
            return evaluate<Op>(source.payload, row, rhs);
        else {
            auto high = [&]<comparison P>(std::uint64_t q) __attribute__((always_inline)) {
                if constexpr (H == 8)
                    return evaluate<P>(source.head0, row, q);
                else
                    return joined<P, 8>(
                        [&]<comparison C>(auto v) __attribute__((always_inline)) {
                            return evaluate<C>(source.head0, row, v);
                        },
                        [&]<comparison C>(auto v) __attribute__((always_inline)) {
                            return evaluate<C>(source.head1, row, v);
                        },
                        q);
            };
            if constexpr (K == H)
                return high.template operator()<Op>(rhs);
            else
                return joined<Op, K - H>(
                    high,
                    [&]<comparison C>(auto q) __attribute__((always_inline)) {
                        return evaluate<C>(source.payload, row, q);
                    },
                    rhs);
        }
    } else if constexpr (requires {
                             source.body;
                             source.tail;
                         }) {
        if constexpr (K < 8)
            return evaluate<Op>(source.tail, row, rhs);
        else if constexpr (K % 8 == 0)
            return evaluate<Op>(source.body, row, rhs);
        else
            return joined<Op, K % 8>(
                [&]<comparison C>(auto q)
                    __attribute__((always_inline)) { return evaluate<C>(source.body, row, q); },
                [&]<comparison C>(auto q)
                    __attribute__((always_inline)) { return evaluate<C>(source.tail, row, q); },
                rhs);
    }
#if defined(__aarch64__) || defined(__AVX2__)
    else if constexpr (requires { E::kind; }) {
        using F = typename E::layout_type;
        constexpr auto Field = E::kind;
        if constexpr (Field == field_kind::head0 || Field == field_kind::head1) {
            const auto& plane = source.source.stream(Field == field_kind::head0 ? 1 : 2);
            return ikea::seriespack::detail::filter_body8<Op, 1>(
                ikea::to_u8(plane.bytes).data() + (row / F::tile_rows) * plane.stride + row % F::tile_rows, rhs);
        } else {
            const auto* base = [&] {
                if constexpr (requires { source.source.data; })
                    return ikea::to_u8(source.source.data);
                else
                    return ikea::to_u8(source.source.stream(0).bytes).data();
            }();
            const auto stride = [&] {
                if constexpr (requires { source.source.data; })
                    return F::tile_bytes;
                else
                    return source.source.stream(0).stride;
            }();
            const auto* tile = base + (row / F::tile_rows) * stride;
            if constexpr (Field == field_kind::body)
                return ikea::seriespack::detail::filter_body8<Op, F::body>(
                    tile + ikea::seriespack::detail::body_offset<F>(row % F::tile_rows), rhs);
            else if constexpr (F::storage == geometry::local)
                return ikea::seriespack::detail::filter_planes8<Op, F::tail>(tile + 8 * F::body,
                                                                             rhs);
            else
                return ikea::seriespack::detail::filter_stripe8<Op, F>(tile, row % F::tile_rows,
                                                                       rhs);
        }
    }
#endif
    else {
        scalar_ops ops;
        unsigned result = 0;
        for (unsigned i = 0; i < 8; ++i)
            result |= unsigned(ikea::compare<Op>(
                          author::read(ops, source, row + i, true).value, rhs))
                      << i;
        return result;
    }
}
} // namespace tile_filter

/// Trusted complete eight-row window, first divisible by eight. Reads remain
/// inside occupied fields of these rows; no following tile or padding is needed.
template <comparison Op, class E>
[[gnu::always_inline]] inline std::uint8_t filter8_unchecked(const E& source, std::size_t first,
                                                             std::uint64_t rhs,
                                                             std::uint8_t active = 255) {
    static_assert(E::bit_width > 0 && E::bit_width <= 64);
    if (!active)
        return 0;
    if constexpr (E::bit_width < 64)
        if (rhs >> E::bit_width)
            return ikea::compare<Op>(0, rhs) ? active : 0;
    return tile_filter::evaluate<Op>(source, first, rhs) & active;
}
} // namespace ikea::seriespack::author
