#pragma once
#include <ikea/tuplepack/detail/plan.h>
#include <ikea/tuplepack/detail/native/types.h>
#include <algorithm>
#include <cstring>
#include <type_traits>
#include <ikea/tuplepack/detail/native/memory.h>
#if defined(__AVX2__) && !defined(__AVX512VBMI__)
namespace ikea::tuplepack::native::native_detail {
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
} // namespace ikea::tuplepack::native::native_detail
#endif
