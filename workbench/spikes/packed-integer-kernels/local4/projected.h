#pragma once
#include <ikea/seriespack/native_avx2.h>

#if !defined(__AVX2__) || defined(__GFNI__) || defined(__AVX512F__)
#error This experiment is restricted to the AVX2-only feature ceiling.
#endif

namespace ikea::seriespack::avx2 {
// BEGIN LOCAL4 PROJECTED HELPER
// Only each qword's low32 bits are meaningful. The low32 bits of moved<<28
// in the final exchange are zero, so compute just the surviving projection.
[[gnu::always_inline]] inline __m256i local4_projected_transpose(__m256i x) noexcept {
    const auto exchange = [&]<unsigned Shift, std::uint64_t Mask>() {
        const auto moved = _mm256_and_si256(_mm256_xor_si256(x, _mm256_srli_epi64(x, Shift)),
                                          _mm256_set1_epi64x(Mask));
        x = _mm256_xor_si256(x, _mm256_xor_si256(moved, _mm256_slli_epi64(moved, Shift)));
    };
    exchange.template operator()<7, 0x00aa00aa00aa00aaULL>();
    exchange.template operator()<14, 0x0000cccc0000ccccULL>();
    const auto mask = _mm256_set1_epi64x(0x00000000f0f0f0f0ULL);
    return _mm256_or_si256(_mm256_andnot_si256(mask, x),
                           _mm256_and_si256(mask, _mm256_srli_epi64(x, 28)));
}
// END LOCAL4 PROJECTED HELPER
}
