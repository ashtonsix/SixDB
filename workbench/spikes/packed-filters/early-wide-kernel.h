#pragma once
#include "early-bench.h"
#if defined(__AVX512BW__) && defined(__AVX512DQ__)
namespace early {
template<unsigned K,int Op,unsigned H,bool Stop> [[gnu::always_inline]]
unsigned zmm(const std::uint8_t* p,const std::uint8_t* h,word q) {
    if constexpr(K<64) if(q>>K) return (Op==1||Op==2)?65535:0;
    constexpr int imm=Op==0?0:Op==1?1:Op==2?2:Op==3?6:5;
    unsigned eq=65535,lt=0;
    if constexpr(H) {
        auto heads=load(h);
        eq=compare<0>(heads,q>>(K-8));
        if constexpr(Op!=0) lt=compare<1>(heads,q>>(K-8));
        if constexpr(Stop) if(!eq) return finish<Op>(0,lt,65535);
        q&=0xffffffff;
    }
    unsigned low;
    if constexpr(K-H==32)
        low=_mm512_cmp_epu32_mask(_mm512_loadu_si512(p),_mm512_set1_epi32(q),imm);
    else low=_mm512_cmp_epu64_mask(_mm512_loadu_si512(p),_mm512_set1_epi64(q),imm) |
        (unsigned(_mm512_cmp_epu64_mask(_mm512_loadu_si512(p+64),_mm512_set1_epi64(q),imm))<<8);
    if constexpr(H) return (finish<Op>(0,lt,65535)&~eq)|(low&eq);
    else return low;
}
template<unsigned K,int Op,unsigned H,bool Stop> void wide_variant(Fixture& f) {
    auto call=[&](unsigned w) __attribute__((always_inline)) {
        asm("" : "+r"(w));
        return zmm<K,Op,H,Stop>(f.bytes.data()+w*2*(K-H),f.heads.data()+w*16,f.queries[w]);
    };
    constexpr auto limit=~word{0}>>(64-K);
    for(unsigned w=0;w<windows;++w) for(word q:{f.queries[w],word{0},limit,limit+1,~word{0}}) {
        unsigned expected=0;
        for(unsigned lane=0;lane<16;++lane) expected|=unsigned(oracle(Op,f.truth[w*16+lane],q))<<lane;
        if(zmm<K,Op,H,Stop>(f.bytes.data()+w*2*(K-H),f.heads.data()+w*16,q)!=expected) std::abort();
    }
    measure(f,Op,H?"head8":"local",Stop?"fused_zmm_early":"fused_zmm",call);
}
template<unsigned K,int Op,unsigned H> void wide_case() {
    for(const char* pattern:{"uniform","reject_first","shared_prefix","all_equal","one_match","half_match","mixed10","mixed50","ordered1","ordered99"}) {
        Fixture f(K,H,pattern);describe(f,Op);
        run_variant<K,Op,H,0>(f);run_variant<K,Op,H,1>(f);
        run_variant<K,Op,H,2>(f);run_variant<K,Op,H,3>(f);run_variant<K,Op,H,4>(f);
        wide_variant<K,Op,H,false>(f);
        if constexpr(H) wide_variant<K,Op,H,true>(f);
    }
}
}
#endif
