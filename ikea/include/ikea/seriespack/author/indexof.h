#pragma once
#include <ikea/seriespack/author/filter.h>
#include <algorithm>
#include <bit>

namespace ikea::seriespack::author {
namespace detail {
template <class E> inline constexpr bool local_index = false;
template <unsigned K, class P, class H0, class H1>
inline constexpr bool local_index<value_expression<K, 0, P, H0, H1>> =
    tile_filter::direct_local<P> && K % 8 >= 4
#if defined(__GFNI__)
    // Retain the wide comparison for bodies up to 24 bits.
    && K > 32
#endif
    ;
}
/// First active equality match relative to first, or count when absent.
/// The complete window is readable and contains at most 64 values.
template <class Expression>
[[gnu::always_inline]] inline unsigned
indexof_unchecked(const Expression& expression, std::size_t first, unsigned count,
                  std::uint64_t rhs, std::uint64_t active = ~std::uint64_t{0}) {
#if defined(__aarch64__) || \
    (defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)) || \
    (defined(__AVX2__) && defined(__BMI2__) && !defined(__GFNI__) && \
     (SIXDB_TUNE_ZEN5 || SIXDB_TUNE_GRANITE_RAPIDS))
    if constexpr (detail::local_index<Expression>) {
        if ((first % 8) == 0 && (count % 8) == 0) {
            active &= count ? ~std::uint64_t{0} >> (64 - count) : 0;
            if (!active || (rhs >> Expression::bit_width))
                return count;
            constexpr unsigned R = Expression::bit_width % 8;
            const local_equal8<R> residual(rhs & ((std::uint64_t{1} << R) - 1));
#if defined(__aarch64__)
            for (unsigned i = 0; i < count; i += 8) {
                const auto body = filter8_unchecked<comparison::equal>(
                    expression.payload.body, first + i, rhs >> R, active >> i);
                if (body) {
                    const auto exact = body & residual(tile_filter::local_plane_word(
                        expression.payload.tail, first + i));
                    if (exact)
                        return i + std::countr_zero(unsigned(exact));
                }
            }
            return count;
#else
            // Evaluate all tiles: unpredictable tile-hit branches destroy
            // reciprocal throughput. The admitted window is fully readable.
            std::uint64_t matches = 0;
            for (unsigned i = 0; i < count; i += 8) {
                const auto body = filter8_unchecked<comparison::equal>(
                    expression.payload.body, first + i, rhs >> R);
                const auto exact = body & residual(tile_filter::local_plane_word(
                    expression.payload.tail, first + i));
                matches |= std::uint64_t(exact) << i;
            }
            return std::min(unsigned(std::countr_zero(matches & active)), count);
#endif
        }
    }
#endif
    const auto matches = author::filter_unchecked<comparison::equal>(expression, first,
                                                                          count, rhs, active);
    return std::min(unsigned(std::countr_zero(matches)), count);
}
} // namespace ikea::seriespack::author
