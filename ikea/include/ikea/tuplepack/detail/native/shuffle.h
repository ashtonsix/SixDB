#pragma once
#include <ikea/tuplepack/detail/plan.h>
#include <ikea/tuplepack/detail/native/types.h>
#include <type_traits>

#if defined(__aarch64__) || defined(__AVX2__)
namespace ikea::tuplepack::native::native_detail {
#if defined(__aarch64__)
template <unsigned I>
[[gnu::always_inline]] inline vector16 apply16(native_packet source, const detail::shuffle& p) {
    const auto index = vld1q_u8(p.index.data() + 16 * I);
    uint8x16_t value;
    if (p.routes <= 1)
        value = vqtbl1q_u8(source.a, index);
    else if (p.routes <= 3)
        value = vqtbl2q_u8({{source.a, source.b}}, index);
    else if (p.routes <= 7)
        value = vqtbl3q_u8({{source.a, source.b, source.c}}, index);
    else
        value = vqtbl4q_u8({{source.a, source.b, source.c, source.d}}, index);
    if (p.shifting)
        value = vshlq_u8(value, vld1q_s8(p.shift.data() + 16 * I));
    if (p.masking)
        value = vandq_u8(value, vld1q_u8(p.mask.data() + 16 * I));
    return value;
}
#endif
#if defined(__AVX2__) && !defined(__AVX512VBMI__)
template <unsigned I, bool Left>
[[gnu::always_inline]] inline __m256i apply32(native_packet source, const detail::shuffle& p) {
    auto result = _mm256_setzero_si256();
    auto append = [&]<unsigned Part>(std::integral_constant<unsigned, Part>) {
        if (!(p.routes & (1u << Part)))
            return;
        auto half = _mm256_extracti128_si256(Part < 2 ? source.a : source.b, Part % 2);
        auto table = _mm256_broadcastsi128_si256(half);
        auto index = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(p.avx2_index[Part].data() + I * 32));
        result = _mm256_or_si256(result, _mm256_shuffle_epi8(table, index));
    };
    append(std::integral_constant<unsigned, 0>{});
    append(std::integral_constant<unsigned, 1>{});
    append(std::integral_constant<unsigned, 2>{});
    append(std::integral_constant<unsigned, 3>{});
    if (p.shifting) {
        const auto low = _mm256_set1_epi16(255);
        const auto high = _mm256_set1_epi16(short(0xff00));
        const auto ef =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p.even_factor.data() + I * 16));
        const auto of =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p.odd_factor.data() + I * 16));
        auto even = _mm256_mullo_epi16(_mm256_and_si256(result, low), ef);
        __m256i odd;
        if constexpr (Left) {
            even = _mm256_and_si256(even, low);
            odd = _mm256_mullo_epi16(_mm256_and_si256(result, high), of);
        } else {
            even = _mm256_srli_epi16(even, 8);
            odd = _mm256_mullo_epi16(_mm256_srli_epi16(result, 8), of);
        }
        result = _mm256_or_si256(even, _mm256_and_si256(odd, high));
    }
    if (p.masking)
        result = _mm256_and_si256(
            result, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p.mask.data() + I * 32)));
    return result;
}
#endif

template <bool Left>
[[gnu::always_inline]] inline native_packet apply(native_packet source, const detail::shuffle& p) {
#if defined(__aarch64__)
    return {apply16<0>(source, p), apply16<1>(source, p), apply16<2>(source, p),
            apply16<3>(source, p)};
#elif defined(__AVX512VBMI__)
    auto result = _mm512_permutexvar_epi8(_mm512_loadu_si512(p.index.data()), source);
    if (p.shifting)
        result = _mm512_multishift_epi64_epi8(_mm512_loadu_si512(p.bit_index.data()), result);
    if (p.masking)
        result = _mm512_and_si512(result, _mm512_loadu_si512(p.mask.data()));
    return result;
#else
    return {apply32<0, Left>(source, p), apply32<1, Left>(source, p)};
#endif
}
} // namespace ikea::tuplepack::native::native_detail
#endif
