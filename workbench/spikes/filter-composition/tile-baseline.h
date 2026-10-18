#pragma once
#include "../packed-filters/early-kernels.h"

// Independent fused eight-row controls. No Ikea read/compare/mask helper is
// called. Standard words use native compares; odd widths compare byte columns
// and residual planes directly, with exact eight-row bounds.
namespace tile_control {
template<unsigned B, int Op>
[[gnu::always_inline]] unsigned words(const std::uint8_t* p, std::uint64_t q) {
    unsigned result = 0;
#if defined(__aarch64__)
    for (unsigned part = 0; part < (B + 1) / 2; ++part) {
        auto x = [&] { if constexpr(B == 1) return vcombine_u8(vld1_u8(p),vdup_n_u8(0));
                      else return vld1q_u8(p+part*16); }();
        uint8x16_t mask;
#define TC_ARM(W, X) \
        const auto a=X; const auto b=vdupq_n_u##W(q); \
        if constexpr(Op==0) mask=vreinterpretq_u8_u##W(vceqq_u##W(a,b)); \
        if constexpr(Op==1) mask=vreinterpretq_u8_u##W(vcltq_u##W(a,b)); \
        if constexpr(Op==2) mask=vreinterpretq_u8_u##W(vcleq_u##W(a,b)); \
        if constexpr(Op==3) mask=vreinterpretq_u8_u##W(vcgtq_u##W(a,b)); \
        if constexpr(Op==4) mask=vreinterpretq_u8_u##W(vcgeq_u##W(a,b));
        if constexpr(B==1) return early::compare<Op>(x,q)&255;
        else if constexpr(B==2) { TC_ARM(16,vreinterpretq_u16_u8(x)) }
        else if constexpr(B==4) { TC_ARM(32,vreinterpretq_u32_u8(x)) }
        else { TC_ARM(64,vreinterpretq_u64_u8(x)) }
#undef TC_ARM
        unsigned bits;
        if constexpr(B==2) bits=vaddv_u8(vand_u8(vmovn_u16(vreinterpretq_u16_u8(mask)),
                    vreinterpret_u8_u64(vcreate_u64(0x8040201008040201ULL))));
        if constexpr(B==4) bits=vaddvq_u32(vandq_u32(vreinterpretq_u32_u8(mask),uint32x4_t{1,2,4,8}));
        if constexpr(B==8) bits=(vgetq_lane_u64(vreinterpretq_u64_u8(mask),0)&1)|
                              (vgetq_lane_u64(vreinterpretq_u64_u8(mask),1)&2);
        result |= bits << (part*(16/B));
    }
#else
    constexpr int imm=Op==0?0:Op==1?1:Op==2?2:Op==3?6:5;
    if constexpr(B==1) return early::compare<Op>(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)),q)&255;
    else if constexpr(B==2) {
        auto x=_mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        auto y=_mm_set1_epi16(q);
#if defined(__AVX512BW__) && defined(__AVX512VL__)
        return _mm_cmp_epu16_mask(x,y,imm);
#else
        __m128i z;
        if constexpr(Op==0) z=_mm_cmpeq_epi16(x,y);
        else {
            x=_mm_xor_si128(x,_mm_set1_epi16(-32768)); y=_mm_xor_si128(y,_mm_set1_epi16(-32768));
            if constexpr(Op==1 || Op==4) z=_mm_cmpgt_epi16(y,x); else z=_mm_cmpgt_epi16(x,y);
            if constexpr(Op==2 || Op==4) z=_mm_xor_si128(z,_mm_set1_epi8(-1));
        }
        return unsigned(_mm_movemask_epi8(_mm_packs_epi16(z,z)))&255;
#endif
    } else {
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
        if constexpr(B==8) return _mm512_cmp_epu64_mask(_mm512_loadu_si512(p),_mm512_set1_epi64(q),imm);
#endif
        for(unsigned part=0;part<B/4;++part) {
            auto x=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p+part*32));
#if defined(__AVX512BW__) && defined(__AVX512VL__)
            unsigned bits;
            if constexpr(B==4) bits=_mm256_cmp_epu32_mask(x,_mm256_set1_epi32(q),imm);
            else bits=_mm256_cmp_epu64_mask(x,_mm256_set1_epi64x(q),imm);
#else
            __m256i z;
#define TC_X86(W, SET) \
            auto y=SET(q); \
            if constexpr(Op==0) z=_mm256_cmpeq_epi##W(x,y); \
            else { \
                x=_mm256_xor_si256(x,SET(std::uint64_t{1}<<(W-1))); \
                y=_mm256_xor_si256(y,SET(std::uint64_t{1}<<(W-1))); \
                if constexpr(Op==1 || Op==4) z=_mm256_cmpgt_epi##W(y,x); else z=_mm256_cmpgt_epi##W(x,y); \
                if constexpr(Op==2 || Op==4) z=_mm256_xor_si256(z,_mm256_set1_epi8(-1)); \
            }
            if constexpr(B==4) { TC_X86(32,_mm256_set1_epi32) }
            else { TC_X86(64,_mm256_set1_epi64x) }
#undef TC_X86
            unsigned bits;
            if constexpr(B==4) bits=_mm256_movemask_ps(_mm256_castsi256_ps(z));
            else bits=_mm256_movemask_pd(_mm256_castsi256_pd(z));
#endif
            result|=bits<<(part*(32/B));
        }
    }
#endif
    return result&255;
}
template<unsigned K,int Op,int Part=int(K/8+K%8)-1>
[[gnu::always_inline]] unsigned descend(const std::uint8_t* p,std::uint64_t q,unsigned eq,unsigned lt) {
    if constexpr(Part<0) return early::finish<Op>(eq,lt,255)&255;
    else {
        unsigned e,l=0;
        if constexpr(Part>=int(K%8)) {
            constexpr unsigned byte=Part-K%8;
            const auto x=early::column8<K/8,byte>(p);
            const auto v=(q>>(K%8+8*byte))&255;
            e=early::compare<0>(x,v)&255;
            if constexpr(Op!=0) l=early::compare<1>(x,v)&255;
        } else {
            const unsigned bits=p[8*(K/8)+Part];
            e=bits ^ (((q>>Part)&1)?0:255);
            if constexpr(Op!=0) l=((q>>Part)&1)?(~bits&255):0;
        }
        return descend<K,Op,Part-1>(p,q,eq&e,lt|(eq&l));
    }
}
template<unsigned K,int Op> [[gnu::always_inline]] unsigned run(const std::uint8_t* p,std::uint64_t q) {
    if constexpr(K<64) if(q>>K) return (Op==1||Op==2)?255:0;
    if constexpr(K==8||K==16||K==32||K==64) return words<K/8,Op>(p,q);
    else return descend<K,Op>(p,q,255,0);
}
// Fuse the complete consumer, including wider contiguous standard-word loads.
// Masked AVX-512 loads cover only the logical payload, never bucket padding.
template<unsigned K,unsigned N,int Op>
[[gnu::always_inline]] std::uint64_t window(const std::uint8_t* p,std::uint64_t q) {
    constexpr auto live=~std::uint64_t{0}>>(64-N);
    if constexpr(K<64) if(q>>K) return (Op==1||Op==2)?live:0;
    std::uint64_t out=0;
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
    if constexpr((K==8||K==16||K==32||K==64) && N*K/8>32) {
        constexpr int imm=Op==0?0:Op==1?1:Op==2?2:Op==3?6:5;
        constexpr unsigned lanes=512/K;
        for(unsigned start=0;start<N;start+=lanes) {
            const unsigned count=std::min(lanes,N-start);
            const auto mask=~std::uint64_t{0}>>(64-count);
            __m512i data;
            const auto* at=p+start*(K/8);
            if constexpr(K==8) data=_mm512_maskz_loadu_epi8(mask,at);
            if constexpr(K==16) data=_mm512_maskz_loadu_epi16(mask,at);
            if constexpr(K==32) data=_mm512_maskz_loadu_epi32(mask,at);
            if constexpr(K==64) data=_mm512_maskz_loadu_epi64(mask,at);
            std::uint64_t found;
            if constexpr(K==8) found=_mm512_cmp_epu8_mask(data,_mm512_set1_epi8(q),imm);
            if constexpr(K==16) found=_mm512_cmp_epu16_mask(data,_mm512_set1_epi16(q),imm);
            if constexpr(K==32) found=_mm512_cmp_epu32_mask(data,_mm512_set1_epi32(q),imm);
            if constexpr(K==64) found=_mm512_cmp_epu64_mask(data,_mm512_set1_epi64(q),imm);
            out|=(found&mask)<<start;
        }
    } else
#endif
    if constexpr(K==8||K==16) {
        for(unsigned start=0;start<N/16*16;start+=16)
            out|=std::uint64_t(control::run<K,Op>(p+start*(K/8),q))<<start;
        if constexpr(N%16) out|=std::uint64_t(run<K,Op>(p+(N/16*16)*(K/8),q))<<(N/16*16);
    } else
        for(unsigned tile=0;tile<N/8;++tile)
            out|=std::uint64_t(run<K,Op>(p+tile*K,q))<<(8*tile);
    return out&live;
}
} // namespace tile_control
