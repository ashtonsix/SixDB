#pragma once
#include "authoring.h"
#include "../local.h"
#include "../scan.h"

namespace ikea::integers::composition {
#if defined(__aarch64__)
struct Values16 {uint16x8_t lo,hi;};
IP_INLINE Values16 join12(Bytes<16> high,Bytes<16> low) {
    return {vorrq_u16(vshlq_n_u16(vmovl_u8(vget_low_u8(high.v)),4),vmovl_u8(vget_low_u8(low.v))),
            vorrq_u16(vshlq_n_u16(vmovl_u8(vget_high_u8(high.v)),4),vmovl_u8(vget_high_u8(low.v)))};
}
IP_INLINE unsigned filter_native(Values16 values,unsigned cutoff) {
    constexpr std::uint16_t weights[8]={1,2,4,8,16,32,64,128};
    const auto w=vld1q_u16(weights),c=vdupq_n_u16(cutoff);
    return vaddvq_u16(vandq_u16(vcltq_u16(values.lo,c),w)) |
           (unsigned(vaddvq_u16(vandq_u16(vcltq_u16(values.hi,c),w)))<<8);
}
IP_INLINE std::uint64_t sum_native(Values16 values,unsigned mask) {
    constexpr std::uint16_t weights[8]={1,2,4,8,16,32,64,128};
    const auto w=vld1q_u16(weights);
    return std::uint64_t(vaddlvq_u16(vandq_u16(values.lo,vtstq_u16(vdupq_n_u16(mask),w))))+
           vaddlvq_u16(vandq_u16(values.hi,vtstq_u16(vdupq_n_u16(mask>>8),w)));
}
IP_INLINE void store_values(std::uint16_t* out,Values16 v) {vst1q_u16(out,v.lo);vst1q_u16(out+8,v.hi);}
#elif defined(__AVX2__)
using Values16=__m256i;
IP_INLINE Values16 join12(Bytes<16> high,Bytes<16> low) {
    return _mm256_or_si256(_mm256_slli_epi16(_mm256_cvtepu8_epi16(high.v),4),_mm256_cvtepu8_epi16(low.v));
}
IP_INLINE unsigned filter_native(Values16 values,unsigned cutoff) {
    auto m=unsigned(_mm256_movemask_epi8(_mm256_cmpgt_epi16(_mm256_set1_epi16(cutoff),values)));
#if defined(__BMI2__)
    return _pext_u32(m,0x55555555);
#else
    m&=0x55555555;m=(m|(m>>1))&0x33333333;m=(m|(m>>2))&0x0f0f0f0f;
    m=(m|(m>>4))&0x00ff00ff;return (m|(m>>8))&0xffff;
#endif
}
IP_INLINE std::uint64_t sum_native(Values16 values,unsigned mask) {
    const auto weights=_mm256_setr_epi16(1,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,-32768);
    const auto absent=_mm256_cmpeq_epi16(_mm256_and_si256(_mm256_set1_epi16(mask),weights),_mm256_setzero_si256());
    const auto sums=_mm256_madd_epi16(_mm256_andnot_si256(absent,values),_mm256_set1_epi16(1));
    auto x=_mm_add_epi32(_mm256_castsi256_si128(sums),_mm256_extracti128_si256(sums,1));
    x=_mm_add_epi32(x,_mm_shuffle_epi32(x,0x4e));x=_mm_add_epi32(x,_mm_shuffle_epi32(x,0xb1));
    return unsigned(_mm_cvtsi128_si32(x));
}
IP_INLINE void store_values(std::uint16_t* out,Values16 v) {_mm256_storeu_si256(reinterpret_cast<__m256i*>(out),v);}
#endif

template<class Tail,ParentKind Parent> struct NativeOps {
    const std::uint8_t* tile;
    IP_INLINE Bytes<16> read_body(Body8,unsigned group) const {
        if constexpr(Parent==ParentKind::packets8) {
            const auto* p=tile+(group/8)*12;
#if defined(__aarch64__)
            return {vcombine_u8(vld1_u8(p),vld1_u8(p+12))};
#else
            return {_mm_set_epi64x(read_bytes<8>(p+12),read_bytes<8>(p))};
#endif
        } else if constexpr(Parent==ParentKind::body64_tail32) return Bytes<16>::load(tile+group);
        else return Bytes<16>::load(tile+group+(group>=32?32:0));
    }
    IP_INLINE Bytes<16> read_tail(Tail,unsigned group) const {
        if constexpr(Parent==ParentKind::packets8) {
            static_assert(Tail::kind==TailKind::local4);
            const auto* p=tile+(group/8)*12+8;
            return local_pair<4>(p,p+12);
        } else {
            static_assert(Tail::kind==TailKind::scan4);
            return scan_read16<4>(tile+(Parent==ParentKind::body64_tail32?64:32),group);
        }
    }
    IP_INLINE Values16 join12(Bytes<16> high,Bytes<16> low) const {return composition::join12(high,low);}
    IP_INLINE unsigned less_than(Values16 values,unsigned cutoff) const {return filter_native(values,cutoff);}
    IP_INLINE std::uint64_t masked_sum(Values16 values,unsigned mask) const {return sum_native(values,mask);}
};
} // namespace ikea::integers::composition
