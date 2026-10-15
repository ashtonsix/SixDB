#pragma once
#include <ikea/comparison.h>
#include <ikea/detail/filter/compare.h>
#include <ikea/seriespack/author/expression.h>

namespace ikea::seriespack {
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
    detail::each<native::values<K>::parts>([&](auto p) {
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
    detail::each<N / 16>([&](auto p) {
        result |= std::uint64_t(filter_values<Op>(value.block[p], rhs)) << (p * 16);
    });
#else
    constexpr unsigned bytes = sizeof(uint_for<K>), lanes = 64 / bytes;
    detail::each<native_group::values<K, N>::parts>([&](auto p) {
        result |= ikea::detail::filter::bits<bytes, Op>(value.v[p], rhs) << (p * lanes);
    });
#endif
    return result & active;
}
#endif

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

/// Local residual planes already contain eight original-position bits. This
/// evaluates the predicate in that representation without transposing values.
template <comparison Op, unsigned K>
[[gnu::always_inline]] inline unsigned filter_planes8(const std::uint8_t* p, std::uint64_t rhs) {
    static_assert(K > 0 && K < 8);
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
} // namespace detail

namespace composition {
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
    if constexpr (Expression::bit_width < 8) {
        if constexpr (requires { expression.payload; })
            return filter16_unchecked<Op>(expression.payload, first, rhs, active);
        else if constexpr (requires { expression.tail; })
            return filter16_unchecked<Op>(expression.tail, first, rhs, active);
    }
    if constexpr (requires { Expression::kind; }) {
        using F = typename Expression::format_type;
        if constexpr (Expression::kind == field_kind::tail && F::storage == geometry::local &&
                      ikea::seriespack::detail::use_packed_filter<Op, Expression::bit_width>) {
            const auto* base = [&] {
                if constexpr (requires { expression.source.data; })
                    return expression.source.data;
                else
                    return expression.source.stream(0).bytes.data();
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
    return filter_values<Op>(composition::read(ops, expression, first, active), rhs, active);
#else
    scalar_ops ops;
    unsigned out = 0;
    for (unsigned i = 0; i < 16; ++i)
        if ((active & (1u << i)) &&
            ikea::compare<Op>(composition::read(ops, expression, first + i, true).value, rhs))
            out |= 1u << i;
    return out;
#endif
}

template <comparison Op, class Expression>
[[gnu::always_inline]] inline std::uint64_t
filter_unchecked(const Expression& expression, std::size_t first, unsigned count, std::uint64_t rhs,
                 std::uint64_t active = ~std::uint64_t{0}) {
    if (!active)
        return 0;
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
    if constexpr (Expression::bit_width >= 8 && std::has_single_bit(Expression::bit_width)) {
        if (count == 64 && first % 16 == 0) {
            wide_ops<64> ops;
            return filter_values<Op>(composition::read(ops, expression, first, active), rhs,
                                     active);
        }
    }
#endif
    std::uint64_t result = 0;
    unsigned i = 0;
    scalar_ops ops;
    while (i < count) {
        if ((first + i) % 16 == 0 && count - i >= 16) {
            result |= std::uint64_t(filter16_unchecked<Op>(expression, first + i, rhs, active >> i))
                      << i;
            i += 16;
        } else {
            if ((active & (std::uint64_t{1} << i)) &&
                ikea::compare<Op>(composition::read(ops, expression, first + i, true).value, rhs))
                result |= std::uint64_t{1} << i;
            ++i;
        }
    }
    return result;
}
} // namespace composition
} // namespace ikea::seriespack
