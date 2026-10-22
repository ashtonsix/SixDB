#pragma once
#include <ikea/intervals/detail/prefix.h>
#include <ikea/intervals/detail/search.h>

namespace ikea::intervals::native_detail {
/// Low-latency exact-prefix path retained independently of the batch kernel.
template<unsigned Spacing,class W>
[[gnu::always_inline]] unsigned prefix_serial(const W *p,unsigned stride,unsigned take) {
    unsigned sum=0;
    for(unsigned g=0;g<take/16;++g) sum+=intervals::detail::prefix16_native(p+std::size_t(g)*stride,16);
    if(take%16==0) return sum;
    return sum+intervals::detail::prefix16_native(p+std::size_t(take/16)*stride,take%16);
}

#if defined(__AVX2__)
template<class W>
[[gnu::always_inline]] unsigned prefix64(const W *p,unsigned stride,unsigned take) {
    if constexpr(sizeof(W)==1) {
        const auto a=_mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        const auto b=_mm_loadu_si128(reinterpret_cast<const __m128i*>(p+stride));
        const auto c=_mm_loadu_si128(reinterpret_cast<const __m128i*>(p+2*stride));
        const auto d=_mm_loadu_si128(reinterpret_cast<const __m128i*>(p+3*stride));
#if defined(__AVX512F__) && defined(__AVX512BW__)
        auto all=_mm512_castsi128_si512(a);
        all=_mm512_inserti32x4(all,b,1);all=_mm512_inserti32x4(all,c,2);all=_mm512_inserti32x4(all,d,3);
#if defined(__BMI2__)
        const auto mask=_bzhi_u64(~std::uint64_t{},take);
#else
        const auto mask=intervals::detail::low_bits(take);
#endif
        const auto sums=_mm512_sad_epu8(_mm512_maskz_mov_epi8(mask,all),_mm512_setzero_si512());
        return unsigned(_mm512_reduce_add_epi64(sums));
#else
        const auto ab=_mm256_inserti128_si256(_mm256_castsi128_si256(a),b,1);
        const auto cd=_mm256_inserti128_si256(_mm256_castsi128_si256(c),d,1);
        const auto lanes=_mm256_setr_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
                                         16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31);
        const auto bound=_mm256_set1_epi8(take);
        const auto left=_mm256_and_si256(ab,_mm256_cmpgt_epi8(bound,lanes));
        const auto right=_mm256_and_si256(cd,_mm256_cmpgt_epi8(bound,_mm256_add_epi8(lanes,_mm256_set1_epi8(32))));
        const auto sum=_mm256_add_epi64(_mm256_sad_epu8(left,_mm256_setzero_si256()),_mm256_sad_epu8(right,_mm256_setzero_si256()));
        const auto pair=_mm_add_epi64(_mm256_castsi256_si128(sum),_mm256_extracti128_si256(sum,1));
        return unsigned(_mm_cvtsi128_si64(_mm_add_epi64(pair,_mm_srli_si128(pair,8))));
#endif
    } else return prefix_serial<64>(p,stride,take);
}
#endif

#if defined(__aarch64__)
/// Four initialized native windows, only after the enclosing source proves all
/// four exist (possibly a padded final native tail). No extra permanent sidecar.
template<class W>
[[gnu::always_inline]] std::uint32_t prefix64(const W *p,unsigned stride,unsigned take) {
    if constexpr(sizeof(W)==1) {
        const uint8x16_t lanes{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
        const auto bound=vdupq_n_u8(take);
        const auto a=vandq_u8(vld1q_u8(p),vcltq_u8(lanes,bound));
        const auto b=vandq_u8(vld1q_u8(p+stride),vcltq_u8(vaddq_u8(lanes,vdupq_n_u8(16)),bound));
        const auto c=vandq_u8(vld1q_u8(p+2*stride),vcltq_u8(vaddq_u8(lanes,vdupq_n_u8(32)),bound));
        const auto d=vandq_u8(vld1q_u8(p+3*stride),vcltq_u8(vaddq_u8(lanes,vdupq_n_u8(48)),bound));
        auto lo=vaddl_u8(vget_low_u8(a),vget_low_u8(b));
        auto hi=vaddl_u8(vget_high_u8(a),vget_high_u8(b));
        lo=vaddw_u8(lo,vget_low_u8(c)); hi=vaddw_u8(hi,vget_high_u8(c));
        lo=vaddw_u8(lo,vget_low_u8(d)); hi=vaddw_u8(hi,vget_high_u8(d));
        return vaddvq_u16(vaddq_u16(lo,hi));
    } else {
        const uint16x8_t lo{0,1,2,3,4,5,6,7},hi{8,9,10,11,12,13,14,15};
        const auto bound=vdupq_n_u16(take);
        const auto a=vandq_u16(vld1q_u16(p),vcltq_u16(lo,bound));
        const auto b=vandq_u16(vld1q_u16(p+8),vcltq_u16(hi,bound));
        auto sum0=vpaddlq_u16(a);sum0=vpadalq_u16(sum0,b);
        const auto c=vandq_u16(vld1q_u16(p+stride),vcltq_u16(vaddq_u16(lo,vdupq_n_u16(16)),bound));
        const auto d=vandq_u16(vld1q_u16(p+stride+8),vcltq_u16(vaddq_u16(hi,vdupq_n_u16(16)),bound));
        auto sum1=vpaddlq_u16(c);sum1=vpadalq_u16(sum1,d);
        const auto e=vandq_u16(vld1q_u16(p+2*stride),vcltq_u16(vaddq_u16(lo,vdupq_n_u16(32)),bound));
        const auto f=vandq_u16(vld1q_u16(p+2*stride+8),vcltq_u16(vaddq_u16(hi,vdupq_n_u16(32)),bound));
        auto sum2=vpaddlq_u16(e);sum2=vpadalq_u16(sum2,f);
        const auto g=vandq_u16(vld1q_u16(p+3*stride),vcltq_u16(vaddq_u16(lo,vdupq_n_u16(48)),bound));
        const auto h=vandq_u16(vld1q_u16(p+3*stride+8),vcltq_u16(vaddq_u16(hi,vdupq_n_u16(48)),bound));
        auto sum3=vpaddlq_u16(g);sum3=vpadalq_u16(sum3,h);
        return vaddvq_u32(vaddq_u32(vaddq_u32(sum0,sum1),vaddq_u32(sum2,sum3)));
    }
}

#endif
} // namespace ikea::intervals::native_detail
