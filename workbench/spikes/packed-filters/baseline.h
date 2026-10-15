#pragma once
#include <array>
#include <cstdint>
#include <cstring>
// Independent fused controls: no Ikea loading, reconstruction or predicate body.
// Identical wire bytes and result coordinates; explicit SIMD, no vectorizer.
#if defined(__aarch64__)
#include <arm_neon.h>
#else
#include <immintrin.h>
#endif
namespace control {
template<unsigned K, int Op>
[[gnu::always_inline]] inline unsigned run(const std::uint8_t* p,std::uint64_t q) {
    if constexpr(K<64)if(q>=(std::uint64_t{1}<<K))return (Op==1||Op==2)?65535:0;
    constexpr unsigned B=K<=8?1:K/8;
    unsigned result=0;
#if defined(__aarch64__)
    for(unsigned part=0;part<B;++part) {
        uint8x16_t bytes;
        if constexpr(K<8) {
            bytes=vdupq_n_u8(0);
            const auto weight=vreinterpretq_u8_u64(vdupq_n_u64(0x8040201008040201ULL));
            for(unsigned b=0;b<K;++b) {
                const auto plane=vcombine_u8(vdup_n_u8(p[b]),vdup_n_u8(p[K+b]));
                bytes=vorrq_u8(bytes,vandq_u8(vtstq_u8(plane,weight),vdupq_n_u8(1u<<b)));
            }
        } else bytes=vld1q_u8(p+part*16);
        uint8x16_t keep;
#define ARM_COMPARE(W, A) \
        const auto a=A; const auto b=vdupq_n_u##W(q); \
        if constexpr(Op==0) keep=vreinterpretq_u8_u##W(vceqq_u##W(a,b)); \
        if constexpr(Op==1) keep=vreinterpretq_u8_u##W(vcltq_u##W(a,b)); \
        if constexpr(Op==2) keep=vreinterpretq_u8_u##W(vcleq_u##W(a,b)); \
        if constexpr(Op==3) keep=vreinterpretq_u8_u##W(vcgtq_u##W(a,b)); \
        if constexpr(Op==4) keep=vreinterpretq_u8_u##W(vcgeq_u##W(a,b));
        if constexpr(B==1) {
            auto b=vdupq_n_u8(q);
            if constexpr(Op==0)keep=vceqq_u8(bytes,b);
            if constexpr(Op==1)keep=vcltq_u8(bytes,b);
            if constexpr(Op==2)keep=vcleq_u8(bytes,b);
            if constexpr(Op==3)keep=vcgtq_u8(bytes,b);
            if constexpr(Op==4)keep=vcgeq_u8(bytes,b);
        } else if constexpr(B==2) { ARM_COMPARE(16,vreinterpretq_u16_u8(bytes)) }
        else if constexpr(B==4) { ARM_COMPARE(32,vreinterpretq_u32_u8(bytes)) }
        else { ARM_COMPARE(64,vreinterpretq_u64_u8(bytes)) }
#undef ARM_COMPARE
        unsigned bits;
        if constexpr(B==1) {
            auto weights=vreinterpretq_u8_u64(vdupq_n_u64(0x8040201008040201ULL));
            auto x=vandq_u8(keep,weights);
            bits=vaddv_u8(vget_low_u8(x))|(unsigned(vaddv_u8(vget_high_u8(x)))<<8);
        } else if constexpr(B==2) {
            auto x=vmovn_u16(vreinterpretq_u16_u8(keep));
            bits=vaddv_u8(vand_u8(x,vreinterpret_u8_u64(vcreate_u64(0x8040201008040201ULL))));
        } else if constexpr(B==4) {
            auto x=vmovn_u16(vcombine_u16(vmovn_u32(vreinterpretq_u32_u8(keep)),vdup_n_u16(0)));
            bits=vaddv_u8(vand_u8(x,vreinterpret_u8_u64(vcreate_u64(0x08040201ULL))));
        } else bits=(vgetq_lane_u64(vreinterpretq_u64_u8(keep),0)&1)|(vgetq_lane_u64(vreinterpretq_u64_u8(keep),1)&2);
        result|=bits<<(part*(16/B));
    }
#else
    [[maybe_unused]] constexpr int imm=Op==0?_MM_CMPINT_EQ:Op==1?_MM_CMPINT_LT:Op==2?_MM_CMPINT_LE:Op==3?_MM_CMPINT_NLE:_MM_CMPINT_NLT;
    if constexpr(B==1) {
        __m128i x;
        if constexpr(K<8) {
#if defined(__GFNI__)
            std::uint64_t lo=0,hi=0;
            std::memcpy(&lo,p,K);std::memcpy(&hi,p+K,K);
            const auto matrix=_mm_set_epi64x(hi,lo);
            static constexpr auto map=[] {
                std::array<std::uint8_t,16> a{};a.fill(128);
                for(unsigned row=0;row<2;++row)
                    for(unsigned b=0;b<K;++b)a[row*8+7-b]=row*8+b;
                return a;
            }();
            x=_mm_gf2p8affine_epi64_epi8(_mm_set1_epi64x(0x8040201008040201ULL),
                _mm_shuffle_epi8(matrix,_mm_loadu_si128(reinterpret_cast<const __m128i*>(map.data()))),0);
#else
            x=_mm_setzero_si128();
            auto weights=_mm_set_epi64x(0x8040201008040201ULL,0x8040201008040201ULL);
            for(unsigned b=0;b<K;++b) {
                auto plane=_mm_unpacklo_epi64(_mm_set1_epi8(p[b]),_mm_set1_epi8(p[K+b]));
                auto has=_mm_cmpeq_epi8(_mm_and_si128(plane,weights),weights);
                x=_mm_or_si128(x,_mm_and_si128(has,_mm_set1_epi8(1u<<b)));
            }
#endif
        } else x=_mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
#if defined(__AVX512BW__) && defined(__AVX512VL__)
        result=_mm_cmp_epu8_mask(x,_mm_set1_epi8(q),imm);
#else
        auto y=_mm_set1_epi8(q);__m128i keep;
        if constexpr(Op==0)keep=_mm_cmpeq_epi8(x,y);
        else {
            auto a=_mm_xor_si128(x,_mm_set1_epi8(-128)),b=_mm_xor_si128(y,_mm_set1_epi8(-128));
            if constexpr(Op==1||Op==4)keep=_mm_cmpgt_epi8(b,a);else keep=_mm_cmpgt_epi8(a,b);
            if constexpr(Op==2||Op==4)keep=_mm_xor_si128(keep,_mm_set1_epi8(-1));
        }
        result=unsigned(_mm_movemask_epi8(keep));
#endif
    } else for(unsigned part=0;part<B/2;++part) {
        auto x=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p+part*32));
#if defined(__AVX512BW__) && defined(__AVX512VL__)
        unsigned bits;
        if constexpr(B==2)bits=_mm256_cmp_epu16_mask(x,_mm256_set1_epi16(q),imm);
        if constexpr(B==4)bits=_mm256_cmp_epu32_mask(x,_mm256_set1_epi32(q),imm);
        if constexpr(B==8)bits=_mm256_cmp_epu64_mask(x,_mm256_set1_epi64x(q),imm);
#else
        __m256i keep;
#define X86_COMPARE(W,SET) \
        const auto y=SET(q); \
        if constexpr(Op==0)keep=_mm256_cmpeq_epi##W(x,y); \
        else { \
            auto a=_mm256_xor_si256(x,SET(std::uint64_t{1}<<(W-1))),b=_mm256_xor_si256(y,SET(std::uint64_t{1}<<(W-1))); \
            if constexpr(Op==1||Op==4)keep=_mm256_cmpgt_epi##W(b,a);else keep=_mm256_cmpgt_epi##W(a,b); \
            if constexpr(Op==2||Op==4)keep=_mm256_xor_si256(keep,_mm256_set1_epi8(-1)); \
        }
        if constexpr(B==2){X86_COMPARE(16,_mm256_set1_epi16)}
        if constexpr(B==4){X86_COMPARE(32,_mm256_set1_epi32)}
        if constexpr(B==8){X86_COMPARE(64,_mm256_set1_epi64x)}
#undef X86_COMPARE
        unsigned bits;
        if constexpr(B==2)bits=unsigned(_mm_movemask_epi8(_mm_packs_epi16(_mm256_castsi256_si128(keep),_mm256_extracti128_si256(keep,1))));
        if constexpr(B==4)bits=unsigned(_mm256_movemask_ps(_mm256_castsi256_ps(keep)));
        if constexpr(B==8)bits=unsigned(_mm256_movemask_pd(_mm256_castsi256_pd(keep)));
#endif
        result|=bits<<(part*(32/B));
    }
#endif
    return result;
}
}
