#pragma once
#include "early-bench.h"
#include <ikea/tuplepack.h>
namespace early {
namespace tp=ikea::tuplepack;
template<unsigned Stride> [[gnu::always_inline]] inline Vec tuple_bytes(const std::uint8_t* p) {
    if constexpr(Stride==1) return load(p);
    else {
        auto lo=column8<Stride,0>(p),hi=column8<Stride,0>(p+8*Stride);
#if defined(__aarch64__)
        return vcombine_u8(vget_low_u8(lo),vget_low_u8(hi));
#else
        return _mm_unpacklo_epi64(lo,hi);
#endif
    }
}
template<unsigned K,int Op,unsigned Stride,int Mode,class Filter>
void tuple_variant(Fixture& f,const std::vector<std::uint8_t>& a,const std::vector<std::uint8_t>& b,
                   const Filter& first,const Filter& second) {
    constexpr unsigned shift=8-K,mask=((1u<<K)-1)<<shift;
    auto call=[&](unsigned w) __attribute__((always_inline)) {
        asm("" : "+r"(w));
        unsigned selected;
        if constexpr(Mode<3) selected=first.template filter_unchecked<C(Op)>(w*16,f.queries[w]);
        else selected=compare<Op>(masked(tuple_bytes<Stride>(a.data()+w*16*Stride),mask),f.queries[w]<<shift);
        if constexpr(Mode==1||Mode==4) if(!selected) return 0u;
        unsigned rest;
        if constexpr(Mode==2) rest=second.template filter_unchecked<C::less>(w*16,128,selected);
        else if constexpr(Mode<3) rest=second.template filter_unchecked<C::less>(w*16,128);
        else rest=compare<1>(tuple_bytes<Stride>(b.data()+w*16*Stride),128);
        return selected&rest;
    };
    for(unsigned w=0;w<windows;++w) {
        unsigned expected=0;
        for(unsigned lane=0;lane<16;++lane)
            if(oracle(Op,f.truth[w*16+lane],f.queries[w])&&b[(w*16+lane)*Stride]<128) expected|=1u<<lane;
        if(call(w)!=expected) {std::fprintf(stderr,"Tuple oracle failed K%u op%d stride%u mode%d\n",K,Op,Stride,Mode);std::abort();}
    }
    measure(f,Op,Stride==1?"tuple_pair":"tuple_pair_stride4",
            Mode==0?"current":Mode==1?"early":Mode==2?"mask_forward":Mode==3?"fused":"fused_early",call);
}
template<unsigned K,int Op,unsigned Stride> void tuple_case() {
    if(only_op>=0 && only_op!=Op) return;
    if(!only_module.empty()&&only_module!=(Stride==1?"tuple_pair":"tuple_pair_stride4")) return;
    auto layout1=*tp::layout::make(1,std::array<tp::code,1>{{{0,8-K,K}}});
    auto layout2=*tp::layout::make(1,std::array<tp::code,1>{{{0,0,8}}});
    auto plan1=*tp::reader<64,16>::make(layout1,std::array<tp::byte,1>{0});
    auto plan2=*tp::reader<64,16>::make(layout2,std::array<tp::byte,1>{0});
    for(const char* pattern:{"uniform","reject_first","shared_prefix","all_equal","one_match","half_match",
                            "mixed10","mixed50","ordered1","ordered99"}) {
        if(!only_pattern.empty()&&only_pattern!=pattern) continue;
        Fixture f(K,0,pattern); describe(f,Op);
        std::vector<std::uint8_t> a(windows*16*Stride),b(a.size());
        for(unsigned i=0;i<windows*16;++i) {
            a[i*Stride]=(f.truth[i]<<(8-K)) | ((i*17)&((1u<<(8-K))-1));
            b[i*Stride]=(i*71+i/16*13)&255;
        }
        // Report the complete pair's selectivity and actual first-predicate
        // empty frequency along its result-dependent trace, not prefix ties.
        unsigned cursor=0,selected_count=0,empty_count=0;
        for(unsigned i=0;i<32768;++i) {
            unsigned first_mask=0,result=0;
            for(unsigned lane=0;lane<16;++lane) {
                bool pass=oracle(Op,f.truth[cursor*16+lane],f.queries[cursor]);
                first_mask|=unsigned(pass)<<lane;
                result|=unsigned(pass&&b[(cursor*16+lane)*Stride]<128)<<lane;
            }
            selected_count+=std::popcount(result);empty_count+=first_mask==0;
            cursor=next(cursor,result,i);
        }
        f.selected=double(selected_count)/(32768*16);f.resolved=double(empty_count)/32768;
        auto view1=*tp::view::bind(layout1,a,windows*16,Stride);
        auto view2=*tp::view::bind(layout2,b,windows*16,Stride);
        auto read1=*tp::bind_reader(plan1,view1),read2=*tp::bind_reader(plan2,view2);
        auto first=*tp::bind_filter(read1),second=*tp::bind_filter(read2);
        sp::detail::each<5>([&](auto mode){tuple_variant<K,Op,Stride,mode>(f,a,b,first,second);});
    }
}
template<unsigned K> void tuple_width() {
    if(only_width&&only_width!=K) return;
    sp::detail::each<5>([](auto op){tuple_case<K,op,1>();tuple_case<K,op,4>();});
}
}
