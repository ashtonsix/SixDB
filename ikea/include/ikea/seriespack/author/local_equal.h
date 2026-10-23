#pragma once
#include <cstdint>
#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__GFNI__) || defined(__BMI2__)
#include <immintrin.h>
#endif

namespace ikea::seriespack::author {
/// Prepared equality for eight Local bitplane rows, with an in-domain query.
/// The low K bytes of the supplied word hold the planes in wire order; upper
/// bytes are ignored. This arithmetic body performs no memory access. The
/// caller owns any proof needed to load the word, including its byte order.
template <unsigned K> class local_equal8 {
    static_assert(K > 0 && K < 8);
#if defined(__GFNI__)
    __m128i query_;
#else
    std::uint64_t query_;
#endif

  public:
    [[gnu::always_inline]] explicit local_equal8(std::uint64_t query) {
#if defined(__GFNI__)
        query_ = _mm_gf2p8affine_epi64_epi8(_mm_set1_epi8(query),
                                          _mm_set1_epi64x(0x8040201008040201ULL), 0);
#elif defined(__aarch64__)
        const auto weights = vreinterpret_u8_u64(vcreate_u64(0x8040201008040201ULL));
        auto flags = vtst_u8(vdup_n_u8(query), weights);
        // Preserve CMTST: complement folding otherwise splits it into two ops.
        asm("" : "+w"(flags));
        query_ = vget_lane_u64(vreinterpret_u64_u8(flags), 0);
#else
#if defined(__BMI2__) && (SIXDB_TUNE_ZEN5 || SIXDB_TUNE_GRANITE_RAPIDS)
        // These targets have fast PDEP. Do not impose it on older BMI2 CPUs.
        if constexpr (K >= 4) {
            query_ = _pdep_u64(query, 0x0101010101010101ULL) * 255;
            return;
        }
#endif
        query_ = 0;
        for (unsigned b = 0; b < K; ++b)
            query_ |= ((std::uint64_t{0} - ((query >> b) & 1)) & 255) << (8 * b);
#endif
    }

    [[gnu::always_inline]] std::uint8_t operator()(std::uint64_t planes) const {
#if defined(__GFNI__)
        const auto values = _mm_gf2p8affine_epi64_epi8(
            _mm_set1_epi64x(0x8040201008040201ULL), _mm_set1_epi64x(planes), 0);
        const auto difference = _mm_xor_si128(values, query_);
        const auto significant = _mm_set1_epi8(static_cast<char>(255u << (8 - K)));
#if defined(__AVX512BW__) && defined(__AVX512VL__)
        return _mm_testn_epi8_mask(difference, significant);
#else
        return _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_and_si128(difference, significant),
                                               _mm_setzero_si128()));
#endif
#else
        auto x = ~(planes ^ query_);
#if defined(__aarch64__)
        // Preserve EON and the first 64-bit shifted AND. Narrowing that AND
        // early adds a standalone shift; retaining later high bits is wrong.
        asm("" : "+r"(x));
#endif
        if constexpr (K == 1)
            return x;
        else if constexpr (K == 2) {
            auto y = std::uint16_t(x);
            return y & (y >> 8);
        } else if constexpr (K == 3) {
            auto y = std::uint16_t(x & (x >> 8));
            return y & (y >> 8);
        } else {
            if constexpr (K > 4) {
                x &= x >> (8 * (K - 4));
#if defined(__aarch64__)
                asm("" : "+r"(x));
#endif
            }
            auto y = std::uint32_t(x);
            y &= y >> 16;
            return y & (y >> 8);
        }
#endif
    }
};
} // namespace ikea::seriespack::author
