#pragma once

#include <ikea/seriespack/composition_neon.h>
#include <ikea/seriespack/composition_x86.h>
#include <cstdint>

// Workbench result-representation experiment. This is not an Ikea interface.
namespace seriespack_measurement::diagnostics::reduction_carrier {
namespace sp = ikea::seriespack;
using U = std::uint64_t;

// Experimental result representations only. Each value represents one logical
// modulo-u64 sum, interpreted by adding its native u64 lanes modulo 2^64.
// It owns the value; it neither borrows producer storage nor mutates its Ops.
#if defined(__AVX2__)
struct avx2_sum_carrier {
    __m256i partials;
    [[gnu::always_inline]] static inline avx2_sum_carrier zero() {
        return {_mm256_setzero_si256()};
    }
    [[gnu::always_inline]] inline avx2_sum_carrier plus(avx2_sum_carrier rhs) const {
        return {_mm256_add_epi64(partials, rhs.partials)};
    }
    [[gnu::always_inline]] inline U finish() const {
        const auto halves = _mm_add_epi64(_mm256_castsi256_si128(partials),
                                          _mm256_extracti128_si256(partials, 1));
        return static_cast<U>(_mm_cvtsi128_si64(
            _mm_add_epi64(halves, _mm_srli_si128(halves, 8))));
    }
};
template<class F, unsigned Begin>
struct avx2_carrier_ops : sp::avx2::composition_ops<F, Begin> {
    using base = sp::avx2::composition_ops<F, Begin>;
    using typename base::value_type;
    using typename base::mask_type;
    [[gnu::always_inline]] avx2_sum_carrier sum(value_type values, mask_type selected,
                                               sp::modulo_u64_sum) const {
        const auto kept = _mm256_and_si256(values, selected);
        if constexpr (base::L == 1)
            return {_mm256_sad_epu8(kept, _mm256_setzero_si256())};
        else if constexpr (base::L == 2) {
            const auto pairs = _mm256_add_epi32(
                _mm256_and_si256(kept, _mm256_set1_epi32(0xffff)),
                _mm256_srli_epi32(kept, 16));
            // u32 is sufficient within one fragment, but the carried sum must
            // use u64 lanes before it can survive arbitrarily many fragments.
            return {_mm256_add_epi64(
                _mm256_and_si256(pairs, _mm256_set1_epi64x(0xffffffffULL)),
                _mm256_srli_epi64(pairs, 32))};
        } else if constexpr (base::L == 4)
            return {_mm256_add_epi64(
                _mm256_and_si256(kept, _mm256_set1_epi64x(0xffffffffULL)),
                _mm256_srli_epi64(kept, 32))};
        else return {kept};
    }
};
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
struct avx512_sum_carrier {
    __m512i partials;
    [[gnu::always_inline]] static inline avx512_sum_carrier zero() {
        return {_mm512_setzero_si512()};
    }
    [[gnu::always_inline]] inline avx512_sum_carrier plus(avx512_sum_carrier rhs) const {
        return {_mm512_add_epi64(partials, rhs.partials)};
    }
    [[gnu::always_inline]] inline U finish() const {
        return static_cast<U>(_mm512_reduce_add_epi64(partials));
    }
};
template<class F, unsigned Begin>
struct avx512_carrier_ops : sp::avx512::composition_ops<F, Begin> {
    using base = sp::avx512::composition_ops<F, Begin>;
    using typename base::value_type;
    using typename base::mask_type;
    [[gnu::always_inline]] avx512_sum_carrier sum(value_type values, mask_type selected,
                                                 sp::modulo_u64_sum) const {
        value_type kept;
        if constexpr (base::L == 1) kept = _mm512_maskz_mov_epi8(selected, values);
        if constexpr (base::L == 2) kept = _mm512_maskz_mov_epi16(selected, values);
        if constexpr (base::L == 4) kept = _mm512_maskz_mov_epi32(selected, values);
        if constexpr (base::L == 8) kept = _mm512_maskz_mov_epi64(selected, values);
        if constexpr (base::L == 1)
            return {_mm512_sad_epu8(kept, _mm512_setzero_si512())};
        else if constexpr (base::L == 2) {
            const auto pairs = _mm512_add_epi32(
                _mm512_and_si512(kept, _mm512_set1_epi32(0xffff)),
                _mm512_srli_epi32(kept, 16));
            return {_mm512_add_epi64(
                _mm512_and_si512(pairs, _mm512_set1_epi64(0xffffffffULL)),
                _mm512_srli_epi64(pairs, 32))};
        } else if constexpr (base::L == 4)
            return {_mm512_add_epi64(
                _mm512_and_si512(kept, _mm512_set1_epi64(0xffffffffULL)),
                _mm512_srli_epi64(kept, 32))};
        else return {kept};
    }
};
#endif
#endif
#if defined(__aarch64__)
struct neon_sum_carrier {
    uint64x2_t partials;
    [[gnu::always_inline]] static inline neon_sum_carrier zero() {
        return {vdupq_n_u64(0)};
    }
    [[gnu::always_inline]] inline neon_sum_carrier plus(neon_sum_carrier rhs) const {
        return {vaddq_u64(partials, rhs.partials)};
    }
    [[gnu::always_inline]] inline U finish() const { return vaddvq_u64(partials); }
};
template<class F, unsigned Begin>
struct neon_carrier_ops : sp::neon::composition_ops<F, Begin> {
    using base = sp::neon::composition_ops<F, Begin>;
    using typename base::value_type;
    using typename base::mask_type;
    [[gnu::always_inline]] neon_sum_carrier sum(value_type values, mask_type selected,
                                               sp::modulo_u64_sum) const {
        const auto kept = vandq_u8(values, selected);
        if constexpr (base::L == 1)
            return {vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(kept)))};
        else if constexpr (base::L == 2)
            return {vpaddlq_u32(vpaddlq_u16(vreinterpretq_u16_u8(kept)))};
        else if constexpr (base::L == 4)
            return {vpaddlq_u32(vreinterpretq_u32_u8(kept))};
        else return {vreinterpretq_u64_u8(kept)};
    }
};
#endif

} // namespace seriespack_measurement::diagnostics::reduction_carrier
