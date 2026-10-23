#pragma once
#include <ikea/comparison.h>
#include <ikea/detail/filter/compare.h>
#include <ikea/seriespack/author/expression.h>
#include <ikea/seriespack/detail/filter8.h>

namespace ikea::seriespack {
namespace author {
#if defined(__aarch64__) || defined(__AVX2__)
/// Sixteen reconstructed unsigned values; bit i names original lane i.
/// No storage access, allocation or scheduling boundary. Values fit K bits.
template <comparison Op, unsigned K>
[[gnu::always_inline]] inline std::uint16_t
filter_values(native::values<K> value, std::uint64_t rhs, std::uint16_t active = 0xffff) {
    if constexpr (K < 64)
        if (rhs >= (std::uint64_t{1} << K))
            return ikea::compare<Op>(0, rhs) ? active : 0;
    constexpr unsigned bytes = sizeof(uint_for<K>);
#if defined(__aarch64__)
    constexpr unsigned lanes = 16 / bytes;
#else
    constexpr unsigned lanes = bytes == 1 ? 16 : 32 / bytes;
#endif
    unsigned out = 0;
    ikea::seriespack::detail::each<native::values<K>::parts>([&](auto p) {
        out |= ikea::detail::filter::bits<bytes, Op>(value.v[p], rhs) << (p * lanes);
    });
    return out & active;
}
#endif

#if defined(__aarch64__) ||                                                                        \
    (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
template <comparison Op, unsigned K, unsigned N>
[[gnu::always_inline]] inline std::uint64_t
filter_values(native_group::values<K, N> value, std::uint64_t rhs,
              std::uint64_t active = ~std::uint64_t{0}) {
    active &= ~std::uint64_t{0} >> (64 - N);
    if constexpr (K < 64)
        if (rhs >= (std::uint64_t{1} << K))
            return ikea::compare<Op>(0, rhs) ? active : 0;
    std::uint64_t result = 0;
#if defined(__aarch64__)
    ikea::seriespack::detail::each<N / 16>([&](auto p) {
        result |= std::uint64_t(filter_values<Op>(value.block[p], rhs)) << (p * 16);
    });
#else
    constexpr unsigned bytes = sizeof(uint_for<K>), lanes = 64 / bytes;
    ikea::seriespack::detail::each<native_group::values<K, N>::parts>([&](auto p) {
        result |= ikea::detail::filter::bits<bytes, Op>(value.v[p], rhs) << (p * lanes);
    });
#endif
    return result & active;
}
#endif

} // namespace author

namespace detail {
// Native paired measurements favor bitplanes for equality and short ordered
// predicates. GFNI reconstruction sustains better throughput above one bit.
template <comparison Op, unsigned K>
inline constexpr bool use_packed_filter =
#if defined(__GFNI__)
    K == 1;
#else
    K <= (Op == comparison::equal ? 7 : 5);
#endif

} // namespace detail

namespace author {
/// Select head-first filtering without changing value meaning or ownership.
/// Apply after child substitution. Other reads retain the expression's meaning.
template <class Expression> struct guarded_expression {
    static constexpr unsigned bit_width = Expression::bit_width;
    Expression expression;
};
template <unsigned K, class P, class H0, class H1>
auto guarded(value_expression<K, 8, P, H0, H1> expression) {
    static_assert(K > 8, "A guarded head needs a payload to skip");
    return guarded_expression<decltype(expression)>{std::move(expression)};
}
template <class Ops, class E, class Rows, class Mask>
[[gnu::always_inline]] auto read(Ops& ops, const guarded_expression<E>& source, Rows rows,
                                 Mask active) {
    return author::read(ops, source.expression, rows, active);
}
namespace detail {
template <class E> inline constexpr bool guarded_filter<guarded_expression<E>> = true;
} // namespace detail

/// The guard applies to complete eight-row tiles as well as sixteen-row groups.
template <comparison Op, class E>
[[gnu::always_inline]] inline std::uint8_t filter8_unchecked(const guarded_expression<E>& guarded,
                                                             std::size_t first, std::uint64_t rhs,
                                                             std::uint8_t active = 255) {
    if (!active)
        return 0;
    constexpr unsigned K = E::bit_width, low_bits = K - 8;
    if constexpr (K < 64)
        if (rhs >> K)
            return ikea::compare<Op>(0, rhs) ? active : 0;
    const auto& expression = guarded.expression;
    const auto query = rhs >> low_bits;
    unsigned resolved = 0;
    if constexpr (Op != comparison::equal) {
        constexpr auto strict = Op == comparison::less || Op == comparison::less_equal
                                    ? comparison::less
                                    : comparison::greater;
        resolved = filter8_unchecked<strict>(expression.head0, first, query, active);
    }
    const auto equal = filter8_unchecked<comparison::equal>(expression.head0, first, query, active);
    if (!equal)
        return resolved;
    constexpr auto low_mask = ~std::uint64_t{0} >> (64 - low_bits);
    return resolved | (equal & filter8_unchecked<Op>(expression.payload, first, rhs & low_mask));
}

/// Trusted complete aligned 16-row window. Empty activity reads no source.
/// A nonempty window may read all occupied fields of the sixteen source rows.
template <comparison Op, class Expression>
[[gnu::always_inline]] inline std::uint16_t filter16_unchecked(const Expression& expression,
                                                               std::size_t first, std::uint64_t rhs,
                                                               std::uint16_t active = 0xffff) {
    static_assert(Expression::bit_width > 0 && Expression::bit_width <= 64);
    if (!active)
        return 0;
    if constexpr (Expression::bit_width < 64)
        if (rhs >= (std::uint64_t{1} << Expression::bit_width))
            return ikea::compare<Op>(0, rhs) ? active : 0;
    if constexpr (requires { Expression::heads; }) {
        if constexpr (Expression::heads == 0)
            return filter16_unchecked<Op>(expression.payload, first, rhs, active);
    }
    if constexpr (requires { expression.tail; }) {
        if constexpr (Expression::bit_width < 8)
            return filter16_unchecked<Op>(expression.tail, first, rhs, active);
    }
#if defined(__aarch64__) || defined(__AVX2__)
    // Direct bodies avoid word promotion on AVX2 and NEON. Full AVX-512
    // reconstruction has better reciprocal throughput; NEON ordered compares
    // also prefer it for wide bodies and many narrow residual planes.
    constexpr bool direct = [] {
#if defined(__AVX512BW__) && defined(__AVX512VL__)
        return false;
#elif defined(__aarch64__)
        constexpr auto k = Expression::bit_width;
        return Op == comparison::equal || (k <= 40 && (k > 32 || k % 8 <= 5));
#else
        return true;
#endif
    }();
    if constexpr (direct && tile_filter::direct_local<Expression>)
        return (unsigned(filter8_unchecked<Op>(expression, first, rhs)) |
                (unsigned(filter8_unchecked<Op>(expression, first + 8, rhs)) << 8)) &
               active;
#endif
    if constexpr (requires { Expression::kind; }) {
        using F = typename Expression::layout_type;
        if constexpr (Expression::kind == field_kind::tail && F::storage == geometry::local &&
                      ikea::seriespack::detail::use_packed_filter<Op, Expression::bit_width>) {
            const auto* base = [&] {
                if constexpr (requires { expression.source.data; })
                    return ikea::to_u8(expression.source.data);
                else
                    return ikea::to_u8(expression.source.stream(0).bytes).data();
            }();
            const auto stride = [&] {
                if constexpr (requires { expression.source.data; })
                    return F::tile_bytes;
                else
                    return expression.source.stream(0).stride;
            }();
            const auto* p = base + (first / 8) * stride + 8 * F::body;
            return (ikea::seriespack::detail::filter_planes8<Op, Expression::bit_width>(p, rhs) |
                    (ikea::seriespack::detail::filter_planes8<Op, Expression::bit_width>(p + stride,
                                                                                         rhs)
                     << 8)) &
                   active;
        }
    }
#if defined(__aarch64__) || defined(__AVX2__)
    native_ops ops;
    return filter_values<Op>(author::read(ops, expression, first, active), rhs, active);
#else
    scalar_ops ops;
    unsigned out = 0;
    for (unsigned i = 0; i < 16; ++i)
        if ((active & (1u << i)) &&
            ikea::compare<Op>(author::read(ops, expression, first + i, true).value, rhs))
            out |= 1u << i;
    return out;
#endif
}

/// Same coordinates and admission as the ordinary body. Resolve complete
/// aligned groups at the eight-bit head before evaluating the selected payload.
template <comparison Op, class E>
[[gnu::always_inline]] inline std::uint16_t filter16_unchecked(const guarded_expression<E>& guarded,
                                                               std::size_t first, std::uint64_t rhs,
                                                               std::uint16_t active = 0xffff) {
    if (!active)
        return 0;
    constexpr unsigned K = E::bit_width, low_bits = K - 8;
    if constexpr (K < 64)
        if (rhs >= (std::uint64_t{1} << K))
            return ikea::compare<Op>(0, rhs) ? active : 0;
    const auto& expression = guarded.expression;
#if defined(__aarch64__) || defined(__AVX2__)
    native_ops ops;
    const auto high = author::read(ops, expression.head0, first, active);
    const auto query = rhs >> low_bits;
    unsigned resolved = 0;
    if constexpr (Op != comparison::equal) {
        constexpr auto strict = Op == comparison::less || Op == comparison::less_equal
                                    ? comparison::less
                                    : comparison::greater;
        resolved = filter_values<strict>(high, query, active);
    }
    unsigned equal;
#if defined(__aarch64__)
    const auto lanes = ikea::detail::filter::lanes<1, comparison::equal>(high.v[0], query);
    if (active == 0xffff) {
        // Avoid scalar mask reduction on the common fully rejected path.
        if (!vmaxvq_u8(lanes))
            return resolved;
        equal = ikea::detail::filter::byte_bits(lanes);
    } else {
        equal = ikea::detail::filter::byte_bits(lanes) & active;
        if (!equal)
            return resolved;
    }
#else
    equal = filter_values<comparison::equal>(high, query, active);
    if (!equal)
        return resolved;
#endif
    // Compare all payload lanes once; sparse activity must not select a slower
    // gather recipe. The enclosing complete-window contract permits these reads.
    constexpr auto low_mask = ~std::uint64_t{0} >> (64 - low_bits);
    return resolved | (equal & filter16_unchecked<Op>(expression.payload, first, rhs & low_mask));
#else
    return filter16_unchecked<Op>(expression, first, rhs, active);
#endif
}

template <comparison Op, class Expression>
[[gnu::always_inline]] inline std::uint64_t
filter_unchecked(const Expression& expression, std::size_t first, unsigned count, std::uint64_t rhs,
                 std::uint64_t active = ~std::uint64_t{0}) {
    active &= count ? ~std::uint64_t{0} >> (64 - count) : 0;
    if (!active)
        return 0;
    if constexpr (Expression::bit_width < 64)
        if (rhs >> Expression::bit_width)
            return ikea::compare<Op>(0, rhs) ? active : 0;
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
    if constexpr (tile_filter::dense_words<Expression>) {
        constexpr unsigned bytes = Expression::bit_width / 8;
        if (first % 8 == 0 && count % 8 == 0 && count * bytes > 32)
            return ikea::seriespack::detail::filter_words<Op, Expression::bit_width>(
                       ikea::to_u8(expression.payload.body.source.data) + first * bytes, count, rhs) &
                   active;
    }
    if constexpr (!detail::guarded_filter<Expression> && Expression::bit_width >= 8 &&
                  std::has_single_bit(Expression::bit_width)) {
        if (count == 64 && first % 16 == 0) {
            wide_ops<64> ops;
            return filter_values<Op>(author::read(ops, expression, first, ~std::uint64_t{0}),
                                     rhs, active);
        }
    }
#endif
    std::uint64_t result = 0;
    unsigned i = 0;
    scalar_ops ops;
    auto point = [&] __attribute__((always_inline)) {
        if ((active & (std::uint64_t{1} << i)) &&
            ikea::compare<Op>(author::read(ops, expression, first + i, true).value, rhs))
            result |= std::uint64_t{1} << i;
        ++i;
    };
    auto tile = [&] __attribute__((always_inline)) {
        const auto live =
            detail::guarded_filter<Expression> ? std::uint8_t(active >> i) : std::uint8_t{255};
        result |= std::uint64_t(filter8_unchecked<Op>(expression, first + i, rhs, live)) << i;
        i += 8;
    };
    while (i < count && (first + i) % 8)
        point();
    if ((first + i) % 16 && count - i >= 8)
        tile();
    // A mask contains at most four complete16 groups. Expand these explicitly:
    // a mixed-step while loop retained scalar fallback/control in fixed N=24
    // consumers even when first and count were visible constants.
    ikea::seriespack::detail::each<4>([&](auto) __attribute__((always_inline)) {
        if (count - i >= 16) {
            const auto live = detail::guarded_filter<Expression> ? std::uint16_t(active >> i)
                                                                 : std::uint16_t{65535};
            result |= std::uint64_t(filter16_unchecked<Op>(expression, first + i, rhs, live)) << i;
            i += 16;
        }
    });
    if (count - i >= 8)
        tile();
    while (i < count)
        point();
    return result & active;
}
} // namespace author
} // namespace ikea::seriespack
