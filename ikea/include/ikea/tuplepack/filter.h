#pragma once
#include <ikea/comparison.h>
#include <ikea/detail/filter/compare.h>
#include <ikea/tuplepack/author/execution.h>

namespace ikea::tuplepack {
/// One bit per decoded packet byte, in the plan's packet order. The caller
/// supplies activity in those same coordinates, excluding holes/unused slots.
/// Multi-byte number interpretation remains with the enclosing composition.
template <comparison Op, unsigned N>
[[gnu::always_inline]] inline std::uint64_t
filter_values(native::packet_for<N> value, std::uint64_t rhs,
              std::uint64_t active = ~std::uint64_t{0}) {
    if (rhs > 255)
        return ikea::compare<Op>(0, rhs) ? active & (~std::uint64_t{0} >> (64 - N)) : 0;
    std::uint64_t out = 0;
    if constexpr (N == 8) {
#if defined(__aarch64__)
        out = ikea::detail::filter::bits<1, Op>(
            vreinterpretq_u8_u64(vcombine_u64(vcreate_u64(value), vdup_n_u64(0))), rhs);
#elif defined(__AVX2__)
        out = ikea::detail::filter::bits<1, Op>(_mm_cvtsi64_si128(value), rhs);
#else
        for (unsigned i = 0; i < 8; ++i)
            out |= std::uint64_t(ikea::compare<Op>((value >> (i * 8)) & 255, rhs)) << i;
#endif
        out &= 255;
    }
#if defined(__aarch64__) || defined(__AVX2__)
    else {
#if defined(__aarch64__)
        out = ikea::detail::filter::bits<1, Op>(value.a, rhs) |
              (std::uint64_t(ikea::detail::filter::bits<1, Op>(value.b, rhs)) << 16) |
              (std::uint64_t(ikea::detail::filter::bits<1, Op>(value.c, rhs)) << 32) |
              (std::uint64_t(ikea::detail::filter::bits<1, Op>(value.d, rhs)) << 48);
#elif defined(__AVX512VBMI__)
        out = ikea::detail::filter::bits<1, Op>(value, rhs);
#else
        out = ikea::detail::filter::bits<1, Op>(value.a, rhs) |
              (std::uint64_t(ikea::detail::filter::bits<1, Op>(value.b, rhs)) << 32);
#endif
    }
#endif
    return out & active;
}

/// A single-code projection gives one predicate result per original row.
/// Borrows the named plan/view, as native_reader does; copies retain that
/// borrowing. Prepare once outside the consumer's hot loop.
template <unsigned N, class Byte, unsigned Rows = 1> class field_filter {
    native_reader<N, Byte, Rows> read_;
    code code_;
    explicit field_filter(const read_operation<N, Byte, Rows>& source)
        : read_(source), code_(source.plan().controls().codes[0]) {}

  public:
    [[nodiscard]] static std::expected<field_filter, error>
    bind(const read_operation<N, Byte, Rows>& source) {
        if (source.plan().ordering().map_size() != 1 || !source.plan().read_bytes())
            return std::unexpected(error::map);
        return field_filter(source);
    }
    template <comparison Op>
    /// Requires first <= source size, activity within Rows, and every active
    /// row in bounds. Empty activity requires no payload storage.
    [[gnu::always_inline]] std::uint64_t
    filter_unchecked(std::size_t first, std::uint64_t rhs,
                     std::uint64_t active = detail::all_rows<Rows>) const {
        if (!active)
            return 0;
        const unsigned limit = (1u << code_.width) - 1;
        if (rhs > limit)
            return ikea::compare<Op>(0, rhs) ? active : 0;
        if constexpr (Rows == 1) {
            const unsigned encoded =
                read_.source().row_unchecked(first)[code_.offset] & (limit << code_.shift);
            return ikea::compare<Op>(encoded, rhs << code_.shift) ? active : 0;
        }
#if defined(__aarch64__) || defined(__AVX2__)
        // A whole tight byte-column admits an encoded comparison. The shift
        // is order-preserving; mask away neighboring fields, not the query.
        if constexpr (Rows >= 16) {
            if (active == detail::all_rows<Rows> && read_.source().stride() == 1) {
                const auto* p = read_.source().row_unchecked(first);
                const auto mask = limit << code_.shift;
                const auto query = rhs << code_.shift;
#if defined(__aarch64__)
                std::uint64_t bits = 0;
#pragma clang loop unroll(full)
                for (unsigned i = 0; i < Rows / 16; ++i)
                    bits |= std::uint64_t(ikea::detail::filter::bits<1, Op>(
                                vandq_u8(vld1q_u8(p + i * 16), vdupq_n_u8(mask)), query))
                            << (i * 16);
                return bits;
#else
                if constexpr (Rows == 16)
                    return ikea::detail::filter::bits<1, Op>(
                        _mm_and_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)),
                                      _mm_set1_epi8(mask)),
                        query);
#if defined(__AVX512BW__)
                else if constexpr (Rows == 64)
                    return ikea::detail::filter::bits<1, Op>(
                        _mm512_and_si512(_mm512_loadu_si512(p), _mm512_set1_epi8(mask)), query);
#endif
                else {
                    std::uint64_t bits = 0;
                    [&]<std::size_t... I>(std::index_sequence<I...>) {
                        ((bits |=
                          std::uint64_t(ikea::detail::filter::bits<1, Op>(
                              _mm256_and_si256(
                                  _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + I * 32)),
                                  _mm256_set1_epi8(mask)),
                              query))
                          << (I * 32)),
                         ...);
                    }(std::make_index_sequence<Rows / 32>{});
                    return bits;
                }
#endif
            }
        }
#endif
        return filter_values<Op, N>(read_.get_unchecked(first, active), rhs, active);
    }
    template <comparison Op>
    [[nodiscard]] std::expected<std::uint64_t, error>
    filter(std::size_t first, std::uint64_t rhs,
           std::uint64_t active = detail::all_rows<Rows>) const {
        if (auto admitted = read_.admit(first, active); !admitted)
            return std::unexpected(admitted.error());
        return filter_unchecked<Op>(first, rhs, active);
    }
};
template <unsigned N, class Byte, unsigned Rows>
auto bind_filter(const read_operation<N, Byte, Rows>& source) {
    return field_filter<N, Byte, Rows>::bind(source);
}
} // namespace ikea::tuplepack
