#pragma once
#include "early-bench.h"
namespace early {
template<unsigned K,int Op> [[gnu::always_inline]] unsigned vector_zero(const std::uint8_t* p,const std::uint8_t* h,word q) {
    if constexpr(K<64) if(q>>K) return Op==1?65535:0;
    auto head=load(h); unsigned y=q>>(K-8);
    bool empty;
#if defined(__aarch64__)
    auto equal=vceqq_u8(head,vdupq_n_u8(y));
    empty=vmaxvq_u8(equal)==0;
#elif defined(__AVX512BW__) && defined(__AVX512VL__)
    empty=_mm_cmpeq_epi8_mask(head,_mm_set1_epi8(y))==0;
#else
    auto equal=_mm_cmpeq_epi8(head,_mm_set1_epi8(y));
    empty=_mm_testz_si128(equal,equal);
#endif
    unsigned lt=0;
    if constexpr(Op==1) lt=compare<1>(head,y);
    if(empty) return lt;
    auto eq=compare<0>(head,y);
    constexpr auto mask=~word{0}>>(64-(K-8));
    return lt | (eq&current<K-8,Op>(p,q&mask,65535));
}
}
