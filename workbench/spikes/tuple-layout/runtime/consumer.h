#pragma once
#include "native.h"

namespace tuple_runtime {
inline constexpr auto weights = [] {
    std::array<byte, 64> w{};
    for (unsigned i = 0; i < 64; ++i) w[i] = i + 1;
    return w;
}();
[[gnu::always_inline]] inline std::uint64_t weighted(native_packet value) {
#if defined(__aarch64__)
    auto part = [](uint8x16_t x, unsigned offset) {
        const auto w = vld1q_u8(weights.data() + offset);
        return std::uint64_t(vaddlvq_u16(vmull_u8(vget_low_u8(x), vget_low_u8(w)))) +
               vaddlvq_u16(vmull_high_u8(x, w));
    };
    return part(value.a, 0) + part(value.b, 16) + part(value.c, 32) + part(value.d, 48);
#elif defined(__AVX512VBMI__)
    // Each adjacent pair is <=255*(63+64), so signed saturation cannot occur.
    const auto pairs = _mm512_maddubs_epi16(value, _mm512_loadu_si512(weights.data()));
    return std::uint64_t(_mm512_reduce_add_epi32(_mm512_madd_epi16(pairs, _mm512_set1_epi16(1))));
#else
    auto part = [](__m256i x, unsigned offset) {
        auto p = _mm256_maddubs_epi16(x, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(weights.data() + offset)));
        auto sums = _mm256_madd_epi16(p, _mm256_set1_epi16(1));
        auto sum = _mm_add_epi32(_mm256_castsi256_si128(sums), _mm256_extracti128_si256(sums, 1));
        sum = _mm_hadd_epi32(sum, sum);
        sum = _mm_hadd_epi32(sum, sum);
        return std::uint64_t(_mm_cvtsi128_si32(sum));
    };
    return part(value.a, 0) + part(value.b, 32);
#endif
}
} // namespace tuple_runtime
