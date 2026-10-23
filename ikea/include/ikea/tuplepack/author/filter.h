#pragma once
#include <ikea/comparison.h>
#include <ikea/detail/filter/compare.h>
#include <ikea/tuplepack/author/native.h>

namespace ikea::tuplepack::author {
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

} // namespace ikea::tuplepack::author
