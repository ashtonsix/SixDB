#include "columns_domains.h"
#include <algorithm>
#include <cstring>
#include <type_traits>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace frame_columns {
NativeX::NativeX(std::span<const Word> values, std::span<const Bits> has_x, bool use_base) {
    bool any = false;
    for (unsigned i=0;i<values.size();++i) if (has_x[i/256].test(i%256)) {
        minimum = any ? std::min(minimum, values[i]) : values[i];
        maximum = any ? std::max(maximum, values[i]) : values[i]; any = true;
    }
    base = use_base ? minimum : 0;
    lane_bits = maximum-base<=UINT8_MAX ? 8 : maximum-base<=UINT16_MAX ? 16 : 32;
    auto fill = [&](auto& target) {
        target.reserve(values.size());
        for (unsigned i=0;i<values.size();++i)
            target.push_back(has_x[i/256].test(i%256) ? values[i]-base : 0);
    };
    if (lane_bits==8) fill(bytes8);
    else if (lane_bits==16) fill(bytes16);
    else fill(bytes32);
}
namespace {
#if defined(__aarch64__)
uint8x16_t mask8(unsigned bits) {
    const uint8x8_t weights{1,2,4,8,16,32,64,128};
    return vcombine_u8(vtst_u8(vdup_n_u8(std::uint8_t(bits)), weights),
                       vtst_u8(vdup_n_u8(std::uint8_t(bits>>8)), weights));
}
uint16x8_t mask16(unsigned bits) {
    const uint16x8_t weights{1,2,4,8,16,32,64,128};
    return vtstq_u16(vdupq_n_u16(std::uint16_t(bits)), weights);
}
uint32x4_t mask32(unsigned bits) {
    const uint32x4_t weights{1,2,4,8};
    return vtstq_u32(vdupq_n_u32(bits), weights);
}
#endif
template<class T> unsigned ge_bits(const T* values, Word threshold) {
    constexpr unsigned lanes=16/sizeof(T);
    if (!threshold) return (1u<<lanes)-1;
#if defined(__aarch64__)
    if constexpr(sizeof(T)==1) {
        const auto cmp=vcgeq_u8(vld1q_u8(values),vdupq_n_u8(std::uint8_t(threshold)));
        const uint8x8_t weights{1,2,4,8,16,32,64,128};
        return vaddv_u8(vand_u8(vget_low_u8(cmp),weights)) |
               (unsigned(vaddv_u8(vand_u8(vget_high_u8(cmp),weights)))<<8);
    } else if constexpr(sizeof(T)==2) {
        const uint16x8_t weights{1,2,4,8,16,32,64,128};
        return vaddvq_u16(vandq_u16(vcgeq_u16(vld1q_u16(values),vdupq_n_u16(std::uint16_t(threshold))),weights));
    } else {
        const uint32x4_t weights{1,2,4,8};
        return vaddvq_u32(vandq_u32(vcgeq_u32(vld1q_u32(values),vdupq_n_u32(std::uint32_t(threshold))),weights));
    }
#else
    unsigned bits=0;for(unsigned i=0;i<lanes;++i)bits|=unsigned(values[i]>=threshold)<<i;return bits;
#endif
}
template<class T> bool same_four(const T* values) {
    if constexpr(sizeof(T)==1) {
        std::uint32_t word;std::memcpy(&word,values,4);
        return word==std::uint32_t(values[0])*UINT32_C(0x01010101);
    } else if constexpr(sizeof(T)==2) {
        std::uint64_t word;std::memcpy(&word,values,8);
        return word==std::uint64_t(values[0])*UINT64_C(0x0001000100010001);
    } else {
#if defined(__aarch64__)
        return vminvq_u32(vceqq_u32(vld1q_u32(values),vdupq_n_u32(values[0])))==UINT32_MAX;
#else
        return values[0]==values[1]&&values[0]==values[2]&&values[0]==values[3];
#endif
    }
}
template<class C,class X,bool Coalesce> void group(const C* cs,const X* xs,Word base,
        std::span<const Bits> selected,std::span<const Bits> has_c,
        std::span<const std::uint8_t> allowed,Word threshold,std::span<Total> totals) {
    constexpr unsigned lanes=16/sizeof(X),lowmask=(1u<<lanes)-1;
    const unsigned null_code=unsigned(totals.size()-1);
    for(unsigned b=0;b<selected.size();++b) {
        const auto& s=selected[b].words;if(!(s[0]|s[1]|s[2]|s[3]))continue;
        for(unsigned local=0;local<256;local+=lanes) {
            unsigned bits=unsigned(s[local/64]>>(local%64))&lowmask;
            if(!bits)continue;
            const unsigned first=b*256+local;
            bits&=ge_bits(xs+first,threshold);if(!bits)continue;
            if constexpr(Coalesce) {
                for(unsigned sub=0;sub<lanes;sub+=4) {
                    const unsigned keep=(bits>>sub)&15;if(!keep)continue;
                    const unsigned valid=unsigned(has_c[b].words[(local+sub)/64]>>((local+sub)%64))&15;
                    if((valid==0||valid==15)&&same_four(cs+first+sub)&&same_four(xs+first+sub)) {
                        const auto c=valid?unsigned(cs[first+sub]):null_code;
                        const auto n=std::popcount(keep);
                        if(allowed[c]){totals[c].count+=n;totals[c].sum+=Word(n)*(base+xs[first+sub]);}
                        bits&=~(15u<<sub);
                    }
                }
            }
            while(bits) {
                const unsigned lane=std::countr_zero(bits);bits&=bits-1;
                const unsigned c=has_c[b].test(local+lane)?unsigned(cs[first+lane]):null_code;
                if(allowed[c]){++totals[c].count;totals[c].sum+=base+xs[first+lane];}
            }
        }
    }
}
template<class X,bool Base,bool Selective> Word sum(const X* xs,Word base,std::span<const Bits> selected,Word threshold) {
    constexpr unsigned lanes=16/sizeof(X),lowmask=(1u<<lanes)-1;
    Word total=0,rows=0;
#if defined(__aarch64__)
    uint64x2_t accumulator=vdupq_n_u64(0);
#endif
    for(unsigned b=0;b<selected.size();++b) {
        const auto& s=selected[b].words;if(!(s[0]|s[1]|s[2]|s[3]))continue;
        if constexpr(Base&&!Selective)rows+=selected[b].count();
        for(unsigned local=0;local<256;local+=lanes) {
            const unsigned bits=unsigned(s[local/64]>>(local%64))&lowmask;
            if(!bits)continue;
            const unsigned first=b*256+local;
#if defined(__aarch64__)
            if constexpr(sizeof(X)==1) {
                const auto values=vld1q_u8(xs+first);
                auto keep=mask8(bits);
                if constexpr(Selective)keep=vandq_u8(keep,vcgeq_u8(values,vdupq_n_u8(std::uint8_t(threshold))));
                const auto kept=vandq_u8(values,keep);
                accumulator=vaddq_u64(accumulator,vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(kept))));
                if constexpr(Base&&Selective)rows+=vaddvq_u8(vshrq_n_u8(keep,7));
            } else if constexpr(sizeof(X)==2) {
                const auto values=vld1q_u16(xs+first);
                auto keep=mask16(bits);
                if constexpr(Selective)keep=vandq_u16(keep,vcgeq_u16(values,vdupq_n_u16(std::uint16_t(threshold))));
                accumulator=vaddq_u64(accumulator,vpaddlq_u32(vpaddlq_u16(vandq_u16(values,keep))));
                if constexpr(Base&&Selective)rows+=vaddvq_u16(vshrq_n_u16(keep,15));
            } else {
                const auto values=vld1q_u32(xs+first);
                auto keep=mask32(bits);
                if constexpr(Selective)keep=vandq_u32(keep,vcgeq_u32(values,vdupq_n_u32(std::uint32_t(threshold))));
                accumulator=vaddq_u64(accumulator,vpaddlq_u32(vandq_u32(values,keep)));
                if constexpr(Base&&Selective)rows+=vaddvq_u32(vshrq_n_u32(keep,31));
            }
#else
            unsigned rest=bits;
            while(rest){const unsigned lane=std::countr_zero(rest);rest&=rest-1;if(!Selective||xs[first+lane]>=threshold){total+=xs[first+lane];if constexpr(Base&&Selective)++rows;}}
#endif
        }
    }
#if defined(__aarch64__)
    total=vaddvq_u64(accumulator);
#endif
    return total+(Base?rows*base:0);
}
template<class F> void with_x(const NativeX& x,F&& f) {
    if(x.lane_bits==8)f(x.bytes8.data());else if(x.lane_bits==16)f(x.bytes16.data());else f(x.bytes32.data());
}
bool translated(const NativeX& x,Word threshold,Word& relative) {
    if(threshold>UINT32_MAX)return false;
    relative=threshold<=x.base?0:threshold-x.base;
    return relative<=(x.lane_bits==32?Word(UINT32_MAX):(Word{1}<<x.lane_bits)-1);
}
} // namespace
void domain_group(const NativeCodes& categories,const NativeX& x,std::span<const Bits> selected,
        std::span<const Bits> has_c,std::span<const std::uint8_t> allowed,
        Word threshold,std::span<Total> totals,bool coalesce) {
    Word relative;if(!translated(x,threshold,relative))return;
    auto with_c=[&](const auto* cs){with_x(x,[&](const auto* xs){
        using C=std::remove_cv_t<std::remove_pointer_t<decltype(cs)>>;
        using X=std::remove_cv_t<std::remove_pointer_t<decltype(xs)>>;
        if(coalesce)group<C,X,true>(cs,xs,x.base,selected,has_c,allowed,relative,totals);
        else group<C,X,false>(cs,xs,x.base,selected,has_c,allowed,relative,totals);
    });};
    if(!categories.small.empty())with_c(categories.small.data());else with_c(categories.large.data());
}
Word domain_x_sum(const NativeX& x,std::span<const Bits> selected,Word threshold) {
    Word relative;if(!translated(x,threshold,relative))return 0;
    Word result=0;with_x(x,[&](const auto* xs){
        using X=std::remove_cv_t<std::remove_pointer_t<decltype(xs)>>;
        if(x.base)result=relative?sum<X,true,true>(xs,x.base,selected,relative):sum<X,true,false>(xs,x.base,selected,0);
        else result=relative?sum<X,false,true>(xs,0,selected,relative):sum<X,false,false>(xs,0,selected,0);
    });return result;
}
} // namespace frame_columns
