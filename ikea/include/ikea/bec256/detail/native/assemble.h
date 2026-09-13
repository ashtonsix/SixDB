#pragma once
#include <immintrin.h>

namespace ikea::bec256::detail {
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
template <unsigned N> [[gnu::always_inline]] inline __m512i previous_lanes(__m512i value) {
    static_assert(N == 1 || N == 2 || N == 4);
    if constexpr (N == 1)
        return _mm512_maskz_permutexvar_epi64(0xfe, _mm512_setr_epi64(0, 0, 1, 2, 3, 4, 5, 6),
                                              value);
    if constexpr (N == 2)
        return _mm512_maskz_permutexvar_epi64(0xfc, _mm512_setr_epi64(0, 0, 0, 1, 2, 3, 4, 5),
                                              value);
    if constexpr (N == 4)
        return _mm512_maskz_permutexvar_epi64(0xf0, _mm512_setr_epi64(0, 0, 0, 0, 0, 1, 2, 3),
                                              value);
}
// Eight ordered groups, each <=56 bits. Prefix widths locate their first bit.
// Groups starting in the same output word form contiguous segments. Reduce
// their left contributions, compress segment ends, then join the carry from
// each preceding word. A group cannot skip an output word, so compression
// preserves output coordinates even with zero-width groups.
[[gnu::always_inline]] inline __m512i assemble(__m512i values, __m512i widths) {
    auto prefix = _mm512_add_epi64(widths, previous_lanes<1>(widths));
    prefix = _mm512_add_epi64(prefix, previous_lanes<2>(prefix));
    prefix = _mm512_add_epi64(prefix, previous_lanes<4>(prefix));
    auto offsets = _mm512_sub_epi64(prefix, widths);
    auto index = _mm512_srli_epi64(offsets, 6);
    auto shift = _mm512_and_si512(offsets, _mm512_set1_epi64(63));
    auto left = _mm512_sllv_epi64(values, shift);
    auto right = _mm512_srlv_epi64(values, _mm512_sub_epi64(_mm512_set1_epi64(64), shift));
    auto m1 = _mm512_mask_cmpeq_epi64_mask(0xfe, index, previous_lanes<1>(index));
    left = _mm512_mask_or_epi64(left, m1, left, previous_lanes<1>(left));
    auto m2 = _mm512_mask_cmpeq_epi64_mask(0xfc, index, previous_lanes<2>(index));
    left = _mm512_mask_or_epi64(left, m2, left, previous_lanes<2>(left));
    auto m4 = _mm512_mask_cmpeq_epi64_mask(0xf0, index, previous_lanes<4>(index));
    left = _mm512_mask_or_epi64(left, m4, left, previous_lanes<4>(left));
    auto next = _mm512_permutexvar_epi64(_mm512_setr_epi64(1, 2, 3, 4, 5, 6, 7, 7), index);
    auto ends = __mmask8(_mm512_cmpneq_epi64_mask(index, next) | 0x80);
    return _mm512_or_si512(_mm512_maskz_compress_epi64(ends, left),
                           previous_lanes<1>(_mm512_maskz_compress_epi64(ends, right)));
}
#endif
} // namespace ikea::bec256::detail
