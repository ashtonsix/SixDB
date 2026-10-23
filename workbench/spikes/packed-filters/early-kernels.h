#pragma once
#include "baseline.h"
#include <ikea/seriespack.h>
#include <bit>

namespace early {
namespace sp = ikea::seriespack;
using C = ikea::comparison;
using word = std::uint64_t;
#if defined(__aarch64__)
using Vec = uint8x16_t;
[[gnu::always_inline]] inline Vec load(const std::uint8_t* p) { return vld1q_u8(p); }
[[gnu::always_inline]] inline Vec masked(Vec x, unsigned m) { return vandq_u8(x, vdupq_n_u8(m)); }
template<int Op> [[gnu::always_inline]] inline unsigned compare(Vec x, unsigned q) {
    auto y = vdupq_n_u8(q);
    uint8x16_t z;
    if constexpr(Op == 0) z = vceqq_u8(x,y);
    if constexpr(Op == 1) z = vcltq_u8(x,y);
    if constexpr(Op == 2) z = vcleq_u8(x,y);
    if constexpr(Op == 3) z = vcgtq_u8(x,y);
    if constexpr(Op == 4) z = vcgeq_u8(x,y);
    auto w = vreinterpretq_u8_u64(vdupq_n_u64(0x8040201008040201ULL));
    z = vandq_u8(z,w);
    return vaddv_u8(vget_low_u8(z)) | (unsigned(vaddv_u8(vget_high_u8(z))) << 8);
}
#else
using Vec = __m128i;
[[gnu::always_inline]] inline Vec load(const std::uint8_t* p) { return _mm_loadu_si128(reinterpret_cast<const Vec*>(p)); }
[[gnu::always_inline]] inline Vec masked(Vec x, unsigned m) { return _mm_and_si128(x,_mm_set1_epi8(m)); }
template<int Op> [[gnu::always_inline]] inline unsigned compare(Vec x, unsigned q) {
    auto y = _mm_set1_epi8(q);
#if defined(__AVX512BW__) && defined(__AVX512VL__)
    constexpr int imm = Op==0?0:Op==1?1:Op==2?2:Op==3?6:5;
    return _mm_cmp_epu8_mask(x,y,imm);
#else
    Vec z;
    if constexpr(Op == 0) z = _mm_cmpeq_epi8(x,y);
    else {
        x = _mm_xor_si128(x,_mm_set1_epi8(-128));
        y = _mm_xor_si128(y,_mm_set1_epi8(-128));
        if constexpr(Op == 1 || Op == 4) z = _mm_cmpgt_epi8(y,x);
        else z = _mm_cmpgt_epi8(x,y);
        if constexpr(Op == 2 || Op == 4) z = _mm_xor_si128(z,_mm_set1_epi8(-1));
    }
    return unsigned(_mm_movemask_epi8(z));
#endif
}
#endif

// Independent bounded SIMD byte-column extraction from eight Local rows.
// Each load is at most the occupied body extent, including odd byte widths.
template<unsigned B, unsigned Byte> [[gnu::always_inline]] inline Vec column8(const std::uint8_t* p) {
#if defined(__aarch64__)
    auto result = vdupq_n_u8(0);
#else
    auto result = _mm_setzero_si128();
#endif
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ([&] {
            static constexpr auto map = [] {
                std::array<std::uint8_t,16> a{}; a.fill(255);
                for(unsigned row=0;row<8;++row) {
                    unsigned offset=row*B+Byte;
                    if(offset/16==I) a[row]=offset%16;
                }
                return a;
            }();
            constexpr bool used = [] { for(auto x:map) if(x!=255) return true; return false; }();
            if constexpr(used) {
                const auto* address=p+I*16;
#if defined(__aarch64__)
                auto x = [&] { if constexpr(I*16+16<=B*8) return vld1q_u8(address);
                              else return vcombine_u8(vld1_u8(address),vdup_n_u8(0)); }();
                result=vorrq_u8(result,vqtbl1q_u8(x,vld1q_u8(map.data())));
#else
                auto x = [&] { if constexpr(I*16+16<=B*8) return load(p+I*16);
                              else return _mm_loadl_epi64(reinterpret_cast<const Vec*>(p+I*16)); }();
                result=_mm_or_si128(result,_mm_shuffle_epi8(x,load(map.data())));
#endif
            }
        }(), ...);
    }(std::make_index_sequence<(B+1)/2>{});
    return result;
}
template<unsigned K, unsigned Byte> [[gnu::always_inline]] inline Vec column(const std::uint8_t* p) {
    auto lo=column8<K/8,Byte>(p), hi=column8<K/8,Byte>(p+K);
#if defined(__aarch64__)
    return vcombine_u8(vget_low_u8(lo),vget_low_u8(hi));
#else
    return _mm_unpacklo_epi64(lo,hi);
#endif
}
template<int Op> [[gnu::always_inline]] inline unsigned finish(unsigned equal,unsigned less,unsigned active) {
    if constexpr(Op==0) return equal;
    if constexpr(Op==1) return less;
    if constexpr(Op==2) return less|equal;
    if constexpr(Op==3) return active&~(less|equal);
    if constexpr(Op==4) return active&~less;
}
// Descend from most significant byte, then most significant residual plane.
// Early=0 is the identical progressive algorithm with no stopping branches.
template<unsigned K,int Op,bool Early,int Part=int(K/8+K%8)-1>
[[gnu::always_inline]] inline unsigned descend(const std::uint8_t* p,word q,unsigned eq,unsigned lt,unsigned active) {
    if constexpr(Part<0) return finish<Op>(eq,lt,active);
    else {
        if constexpr(Early) if(!eq) return finish<Op>(0,lt,active);
        unsigned e,l=0;
        if constexpr(Part>=int(K%8)) {
            constexpr unsigned byte=Part-K%8, shift=K%8+byte*8;
            auto x=column<K,byte>(p); auto y=(q>>shift)&255;
            e=compare<0>(x,y);
            if constexpr(Op!=0) l=compare<1>(x,y);
        } else {
            unsigned bits=p[8*(K/8)+Part] | (unsigned(p[K+8*(K/8)+Part])<<8);
            e=bits ^ ((q>>Part)&1 ? 0 : 65535);
            if constexpr(Op!=0) l=((q>>Part)&1) ? (~bits&65535) : 0;
        }
        if constexpr(Op!=0) lt|=eq&l;
        return descend<K,Op,Early,Part-1>(p,q,eq&e,lt,active);
    }
}
template<unsigned K,int Op,bool Early> [[gnu::always_inline]] inline unsigned progressive(const std::uint8_t* p,word q,unsigned a) {
    if(!a) return 0;
    if constexpr(K<64) if(q>>K) return (Op==1||Op==2)?a:0;
    return descend<K,Op,Early>(p,q,a,0,a);
}
template<unsigned K,int Op> [[gnu::always_inline]] inline unsigned current(const std::uint8_t* p,word q,unsigned a) {
    sp::dense_source<sp::layout<K>> source{reinterpret_cast<const std::byte*>(p),16};
    return sp::author::filter16_unchecked<C(Op)>(sp::author::expression(source),0,q,a);
}
template<unsigned K,int Op> [[gnu::always_inline]] inline unsigned fused(const std::uint8_t* p,word q,unsigned a) {
    if(!a) return 0;
    if constexpr(K<8 || K==8 || K==16 || K==32 || K==64)
        return control::run<K,Op>(p,q)&a;
    else return progressive<K,Op,false>(p,q,a);
}
template<unsigned K,int Op> [[gnu::always_inline]] inline unsigned head_current(const std::uint8_t* p,const std::uint8_t* h,word q,unsigned a,unsigned w) {
    using F=sp::layout<K,sp::geometry::local,8>;
    // Reconstruct the admitted whole-source origin, which is 64B aligned;
    // an individual window need not start on that alignment.
    auto source=sp::const_view<F>::attach_unchecked(256*16, {{{std::as_bytes(std::span{p-w*2*(K-8),256*2*(K-8)}),K-8},{std::as_bytes(std::span{h-w*16,256*16}),8},{std::as_bytes(std::span{std::span<const std::byte>{}}),0}}});
    return sp::author::filter16_unchecked<C(Op)>(sp::author::expression(source),w*16,q,a);
}
template<unsigned K,int Op,bool Early,bool Fused> [[gnu::always_inline]] inline unsigned head_split(const std::uint8_t* p,const std::uint8_t* h,word q,unsigned a) {
    if(!a) return 0;
    if constexpr(K<64) if(q>>K) return (Op==1||Op==2)?a:0;
    auto x=load(h); auto y=q>>(K-8);
    unsigned eq=compare<0>(x,y)&a;
    unsigned lt=0;
    if constexpr(Op!=0) lt=compare<1>(x,y)&a;
    if constexpr(Early) if(!eq) return finish<Op>(0,lt,a);
    constexpr word mask=~word{0}>>(64-(K-8));
    // Branchless control forces the suffix comparison even with zero survivors.
    unsigned low;
    if constexpr(Fused) low=fused<K-8,Op>(p,q&mask,65535);
    else low=current<K-8,Op>(p,q&mask,65535);
    return (finish<Op>(0,lt,a)&~eq) | (low&eq);
}
template<unsigned K,int Op> [[gnu::always_inline]] inline unsigned gate(const std::uint8_t* p,word q,unsigned a) {
    if(!a) return 0;
    if constexpr(K<64) if(q>>K) return (Op==1||Op==2)?a:0;
    unsigned eq,lt=0;
    if constexpr(K>=8) {
        auto x=column<K,K/8-1>(p); auto y=q>>(K-8);
        eq=compare<0>(x,y)&a;
        if constexpr(Op!=0) lt=compare<1>(x,y)&a;
    } else {
        auto bits=unsigned(p[K-1])|(unsigned(p[2*K-1])<<8);
        eq=(bits^((q>>(K-1))?0:65535))&a;
        if constexpr(Op!=0) lt=(q>>(K-1))?(~bits&a):0;
    }
    if(!eq) return finish<Op>(0,lt,a);
    return current<K,Op>(p,q,a);
}
}
