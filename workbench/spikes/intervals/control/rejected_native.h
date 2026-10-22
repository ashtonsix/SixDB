#pragma once
#include <ikea/boundlist/detail/prefix.h>
#include <ikea/boundlist/detail/search.h>

// Rejected exploratory kernels, retained only as literal scheduling controls.
// First native worker sources own their measured complete callers.
namespace intervals_rejected {
namespace boundlist = ikea::boundlist;
/// Sorted checkpoint directory: count the assigned values before the query.
/// Capacity grants actual readable lanes; n alone still defines logical ranks.
template <boundlist::edge E, unsigned Capacity, std::unsigned_integral P>
[[gnu::always_inline]] unsigned checkpoint_boundary(const P *data,unsigned n,P point) {
#if defined(__aarch64__)
    if constexpr (Capacity == 4 && sizeof(P) == 2) {
        const auto values=vld1_u16(reinterpret_cast<const std::uint16_t*>(data));
        const uint16x4_t lanes{0,1,2,3};
        const auto active=vclt_u16(lanes,vdup_n_u16(n));
        const auto before=E==boundlist::edge::after ? vcle_u16(values,vdup_n_u16(point))
                                                  : vclt_u16(values,vdup_n_u16(point));
        return vaddv_u16(vshr_n_u16(vand_u16(active,before),15));
    } else if constexpr (Capacity == 4 && sizeof(P) == 4) {
        const auto values=vld1q_u32(reinterpret_cast<const std::uint32_t*>(data));
        const uint32x4_t lanes{0,1,2,3};
        const auto active=vcltq_u32(lanes,vdupq_n_u32(n));
        const auto before=E==boundlist::edge::after ? vcleq_u32(values,vdupq_n_u32(point))
                                                  : vcltq_u32(values,vdupq_n_u32(point));
        return vaddvq_u32(vshrq_n_u32(vandq_u32(active,before),31));
    } else
#elif defined(__AVX2__)
    if constexpr(Capacity==4 && (sizeof(P)==2 || sizeof(P)==4)) {
        constexpr unsigned bytes=sizeof(P);
        const auto values=[&] {
            if constexpr(bytes==2) return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(data));
            else return _mm_loadu_si128(reinterpret_cast<const __m128i*>(data));
        }();
        const auto sign=bytes==2?_mm_set1_epi16(INT16_MIN):_mm_set1_epi32(INT32_MIN);
        const auto q=bytes==2?_mm_set1_epi16(point):_mm_set1_epi32(point);
        const auto a=_mm_xor_si128(values,sign),b=_mm_xor_si128(q,sign);
        const auto greater=[&] {
            if constexpr(E==boundlist::edge::after) {
                if constexpr(bytes==2) return _mm_cmpgt_epi16(a,b);
                else return _mm_cmpgt_epi32(a,b);
            } else {
                if constexpr(bytes==2) return _mm_cmpgt_epi16(b,a);
                else return _mm_cmpgt_epi32(b,a);
            }
        }();
        unsigned mask=_mm_movemask_epi8(greater);
        if constexpr(E==boundlist::edge::at_or_after) mask=~mask;
        mask&=(1u<<(bytes*n))-1;
        return mask?std::countr_zero(mask)/bytes:n;
    } else
#endif
        return boundlist::detail::checkpoint_boundary<E,Capacity>(data,n,point);
}

/// Low-latency exact-prefix path retained independently of the batch kernel.
template<unsigned Spacing,class W>
[[gnu::always_inline]] unsigned prefix_serial(const W *p,unsigned stride,unsigned take) {
    unsigned sum=0;
    for(unsigned g=0;g<take/16;++g) sum+=boundlist::detail::prefix16_native(p+std::size_t(g)*stride,16);
    if(take==Spacing) return sum;
    return sum+boundlist::detail::prefix16_native(p+std::size_t(take/16)*stride,take%16);
}

/// Accumulate complete native groups in widened vector lanes, masking just the
/// final native group. No read beyond initialized native padding, including when
/// take == Spacing. Result is bounded by256*u16, independent of global origin.
template <unsigned Spacing,class W>
[[gnu::always_inline]] std::uint32_t prefix(const W *fields,unsigned stride,unsigned take) {
    if constexpr (Spacing == 16) return boundlist::detail::prefix16_native(fields,take);
#if defined(__aarch64__)
    if constexpr (Spacing > 16 && sizeof(W) == 1) {
        auto sum=vdupq_n_u16(0);
        for(unsigned group=0;group<take/16;++group)
            sum=vpadalq_u8(sum,vld1q_u8(fields+std::size_t(group)*stride));
        if(take%16) {
            const uint8x16_t lanes{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
            const auto selected=vandq_u8(vld1q_u8(fields+std::size_t(take/16)*stride),
                                         vcltq_u8(lanes,vdupq_n_u8(take%16)));
            sum=vpadalq_u8(sum,selected);
        }
        return vaddvq_u16(sum);
    } else if constexpr (Spacing > 16 && sizeof(W) == 2) {
        auto sum=vdupq_n_u32(0);
        for(unsigned group=0;group<take/16;++group) {
            const W *p=fields+std::size_t(group)*stride;
            sum=vpadalq_u16(sum,vld1q_u16(p));
            sum=vpadalq_u16(sum,vld1q_u16(p+8));
        }
        if(take%16) {
            const W *p=fields+std::size_t(take/16)*stride;
            const uint16x8_t lo{0,1,2,3,4,5,6,7},hi{8,9,10,11,12,13,14,15};
            const auto bound=vdupq_n_u16(take%16);
            sum=vpadalq_u16(sum,vandq_u16(vld1q_u16(p),vcltq_u16(lo,bound)));
            sum=vpadalq_u16(sum,vandq_u16(vld1q_u16(p+8),vcltq_u16(hi,bound)));
        }
        return vaddvq_u32(sum);
    } else
#endif
    {
        unsigned sum=0;
        for(unsigned group=0;group<take/16;++group)
            sum+=boundlist::detail::prefix16_native(fields+std::size_t(group)*stride,16);
        if(take%16)
            sum+=boundlist::detail::prefix16_native(fields+std::size_t(take/16)*stride,take%16);
        return sum;
    }
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
        const auto mask=boundlist::detail::low_bits(take);
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

/// Preloaded winning u8 native window; same widened prefix law as owner16.
[[gnu::always_inline]] uint16x8_t owner16_counts(uint8x16_t bytes,uint16x8_t q) {
    const auto zero=vdupq_n_u16(0);
    auto lo=vmovl_u8(vget_low_u8(bytes)),hi=vmovl_u8(vget_high_u8(bytes));
    lo=vaddq_u16(lo,vextq_u16(zero,lo,7));hi=vaddq_u16(hi,vextq_u16(zero,hi,7));
    lo=vaddq_u16(lo,vextq_u16(zero,lo,6));hi=vaddq_u16(hi,vextq_u16(zero,hi,6));
    lo=vaddq_u16(lo,vextq_u16(zero,lo,4));hi=vaddq_u16(hi,vextq_u16(zero,hi,4));
    hi=vaddq_u16(hi,vdupq_laneq_u16(lo,7));
    return vaddq_u16(vshrq_n_u16(vcleq_u16(lo,q),15),vshrq_n_u16(vcleq_u16(hi,q),15));
}
/// Parallel macrogroup routing: four totals, cumulative selection and a register
/// table-select of the winning16 values. Legal u8 totals fit u16 throughout.
[[gnu::always_inline]] unsigned owner64_parallel(const std::uint8_t *p,unsigned stride,unsigned offset) {
    const auto a=vld1q_u8(p),b=vld1q_u8(p+stride),c=vld1q_u8(p+2*stride),d=vld1q_u8(p+3*stride);
    uint16x4_t totals=vdup_n_u16(vaddlvq_u8(a));
    totals=vset_lane_u16(vaddlvq_u8(b),totals,1);
    totals=vset_lane_u16(vaddlvq_u8(c),totals,2);
    totals=vset_lane_u16(vaddlvq_u8(d),totals,3);
    const auto zero=vdup_n_u16(0);
    auto ends=vadd_u16(totals,vext_u16(zero,totals,3));
    ends=vadd_u16(ends,vext_u16(zero,ends,2));
    const auto group=vdup_n_u16(vaddv_u16(vshr_n_u16(vcle_u16(ends,vdup_n_u16(offset)),15)));
    const auto group_bytes=vdup_lane_u8(vreinterpret_u8_u16(group),0);
    const auto starts=vext_u16(zero,ends,3);
    const uint8x8_t byte_lanes{0,1,2,3,4,5,6,7};
    const auto selected_start=vtbl1_u8(vreinterpret_u8_u16(starts),vadd_u8(byte_lanes,vshl_n_u8(group_bytes,1)));
    const auto base=vdup_lane_u16(vreinterpret_u16_u8(selected_start),0);
    const uint8x16_t lanes{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    const uint8x16x4_t table{{a,b,c,d}};
    const auto group_indices=vcombine_u8(vshl_n_u8(group_bytes,4),vshl_n_u8(group_bytes,4));
    const auto values=vqtbl4q_u8(table,vorrq_u8(lanes,group_indices));
    const auto relative=vsub_u16(vdup_n_u16(offset),base);
    const auto local=vdup_n_u16(vaddvq_u16(owner16_counts(values,vcombine_u16(relative,relative))));
    return vget_lane_u16(vadd_u16(vshl_n_u16(group,4),local),0);
}

/// Two-level native tournament: one32-field total and one16-field total.
/// Conditional vector selects keep loaded widths in registers and avoid table
/// lookup/selected-checkpoint extraction. No data-dependent local branch.
[[gnu::always_inline]] unsigned owner64_tournament(const std::uint8_t *p,unsigned stride,unsigned offset) {
    const auto a=vld1q_u8(p),b=vld1q_u8(p+stride),c=vld1q_u8(p+2*stride),d=vld1q_u8(p+3*stride);
    const auto sums=vaddq_u16(vaddl_u8(vget_low_u8(a),vget_low_u8(b)),
                              vaddl_u8(vget_high_u8(a),vget_high_u8(b)));
    const unsigned total32=vaddvq_u16(sums);
    const unsigned half=offset>=total32;
    const auto half_mask=vdupq_n_u8(0u-half);
    const auto x=vbslq_u8(half_mask,c,a),y=vbslq_u8(half_mask,d,b);
    offset-=total32&(0u-half);
    const unsigned total16=vaddlvq_u8(x),upper=offset>=total16;
    const auto values=vbslq_u8(vdupq_n_u8(0u-upper),y,x);
    offset-=total16&(0u-upper);
    const auto relative=vdupq_n_u16(offset);
    return half*32+upper*16+vaddvq_u16(owner16_counts(values,relative));
}

[[gnu::always_inline]] uint16x8_t owner16_carry(uint8x16_t bytes,uint16x8_t q,uint16x8_t &carry) {
    const auto zero=vdupq_n_u16(0);
    auto lo=vmovl_u8(vget_low_u8(bytes)),hi=vmovl_u8(vget_high_u8(bytes));
    lo=vaddq_u16(lo,vextq_u16(zero,lo,7));hi=vaddq_u16(hi,vextq_u16(zero,hi,7));
    lo=vaddq_u16(lo,vextq_u16(zero,lo,6));hi=vaddq_u16(hi,vextq_u16(zero,hi,6));
    lo=vaddq_u16(lo,vextq_u16(zero,lo,4));hi=vaddq_u16(hi,vextq_u16(zero,hi,4));
    hi=vaddq_u16(hi,vdupq_laneq_u16(lo,7));
    lo=vaddq_u16(lo,carry);hi=vaddq_u16(hi,carry);
    carry=vdupq_laneq_u16(hi,7);
    return vaddq_u16(vshrq_n_u16(vcleq_u16(lo,q),15),vshrq_n_u16(vcleq_u16(hi,q),15));
}
/// Whole64 ordered prefixes with vector carries and one final scalar reduction.
[[gnu::always_inline]] unsigned owner64_whole(const std::uint8_t *p,unsigned stride,unsigned offset) {
    auto carry=vdupq_n_u16(0);const auto q=vdupq_n_u16(offset);
    const auto a=owner16_carry(vld1q_u8(p),q,carry);
    const auto b=owner16_carry(vld1q_u8(p+stride),q,carry);
    const auto c=owner16_carry(vld1q_u8(p+2*stride),q,carry);
    const auto d=owner16_carry(vld1q_u8(p+3*stride),q,carry);
    return vaddvq_u16(vaddq_u16(vaddq_u16(a,b),vaddq_u16(c,d)));
}
#endif
} // namespace ikea::intervals::native_detail
