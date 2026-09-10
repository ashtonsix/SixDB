#pragma once
#include "formats.h"
#include "native_bytes.h"
#include "local_neon.h"

namespace ikea::integers {
template<unsigned K> IP_INLINE __attribute__((aligned(64))) uint8_t local_point(const uint8_t* p,unsigned i) {
    constexpr uint64_t mask=0x0101010101010101ULL >> (8*(8-K));
#if defined(__aarch64__)
    if constexpr(K==2||K==3) {
        unsigned result=0;
        unroll<K>([&](auto b) {result|=((p[i/8*K+b]>>(i%8))&1u)<<b;});
        return uint8_t(result);
    }
#if defined(__ARM_FEATURE_SVE2_BITPERM)
    if constexpr(K==7) {
        const auto bytes=svld1_u8(svwhilelt_b8(uint64_t(0),uint64_t(K)),p+i/8*K);
        const auto result=svbext_u64(svreinterpret_u64_u8(bytes),svdup_n_u64(mask<<(i%8)));
        return uint8_t(vgetq_lane_u64(svget_neonq_u64(result),0));
    }
#endif
#endif
    const auto x=read_bytes<K>(p+i/8*K) >> (i%8);
#if defined(__BMI2__)
    return uint8_t(_pext_u64(x,mask));
#else
    return uint8_t(((x&mask)*0x0102040810204080ULL)>>56);
#endif
}

template<unsigned K> IP_INLINE Bytes<16> local_pair(const uint8_t* p,const uint8_t* second) {
#if defined(__aarch64__)
    if constexpr(K<=2) {
        const auto bit=vld1q_u8(reinterpret_cast<const uint8_t*>("\x01\x02\x04\x08\x10\x20\x40\x80\x01\x02\x04\x08\x10\x20\x40\x80"));
        auto result=vdupq_n_u8(0);
        unroll<K>([&](auto b) {
            const auto planes=vcombine_u8(vdup_n_u8(p[b]),vdup_n_u8(second[b]));
            const auto values=vandq_u8(vtstq_u8(planes,bit),vdupq_n_u8(1u<<b));
            result=vorrq_u8(result,values);
        });
        return {result};
    }
    auto x=vcombine_u64(vcreate_u64(read_bytes<K>(p)),vcreate_u64(read_bytes<K>(second)));
#if defined(__ARM_FEATURE_SVE2_BITPERM)
    auto z=svset_neonq_u64(svundef_u64(),x);
    const auto m=svdup_n_u64(0x5555555555555555ULL);
    z=svbgrp_u64(z,m); z=svbgrp_u64(z,m); z=svbgrp_u64(z,m);
    return {vreinterpretq_u8_u64(svget_neonq_u64(z))};
#else
    auto exchange=[&]<unsigned S,uint64_t Mask>() {
        const auto t=vandq_u64(veorq_u64(x,vshrq_n_u64(x,S)),vdupq_n_u64(Mask));
        x=veorq_u64(x,veorq_u64(t,vshlq_n_u64(t,S)));
    };
    exchange.template operator()<7,0x00aa00aa00aa00aaULL>();
    exchange.template operator()<14,0x0000cccc0000ccccULL>();
    exchange.template operator()<28,0x00000000f0f0f0f0ULL>();
    return {vreinterpretq_u8_u64(x)};
#endif
#elif defined(__GFNI__)
    auto x=_mm_set_epi64x(read_bytes<K>(second),read_bytes<K>(p));
    x=_mm_shuffle_epi8(x,_mm_setr_epi8(7,6,5,4,3,2,1,0,15,14,13,12,11,10,9,8));
    return {_mm_gf2p8affine_epi64_epi8(_mm_set1_epi64x(0x8040201008040201ULL),x,0)};
#elif defined(__AVX2__)
    return {_mm_set_epi64x(transpose8(read_bytes<K>(second)),transpose8(read_bytes<K>(p)))};
#endif
}
template<unsigned K> IP_INLINE Bytes<16> local_read16(const uint8_t* p,unsigned i) {
    p+=i/8*K;
#if defined(__ARM_FEATURE_SVE2_BITPERM)
    if constexpr(K==2) {
        constexpr auto indices=[] {
            std::array<uint8_t,16> a{};a.fill(128);
            for(unsigned g=0;g<2;++g) for(unsigned b=0;b<K;++b) a[g*8+b]=g*K+b;
            return a;
        }();
        // One exact four-byte read supplies both neighbouring transposes.
        const auto x=vqtbl1q_u8(neon_prefix<2*K>(p),vld1q_u8(indices.data()));
        return {neon_transpose<true>(x)};
    }
    return local_pair<K>(p,p+K);
#elif defined(__GFNI__) && defined(__AVX512BW__)
    constexpr auto shuffle=[] {
        std::array<uint8_t,16> a{}; a.fill(128);
        for(unsigned g=0;g<2;++g) for(unsigned b=0;b<K;++b) a[g*8+7-b]=g*K+b;
        return a;
    }();
    const auto load=[&] {
        // Native scalar-width loads avoid mask setup and masked-load costs
        // when the exact two-primitive extent is itself a native width.
        if constexpr(K==1||K==2||K==4) return _mm_cvtsi64_si128(read_bytes<2*K>(p));
        else return _mm_maskz_loadu_epi8((1u<<(2*K))-1,p);
    };
    auto x=load();
    x=_mm_shuffle_epi8(x,_mm_loadu_si128(reinterpret_cast<const __m128i*>(shuffle.data())));
    return {_mm_gf2p8affine_epi64_epi8(_mm_set1_epi64x(0x8040201008040201ULL),x,0)};
#else
    return local_pair<K>(p,p+K);
#endif
}

template<unsigned K,unsigned N=256> IP_INLINE void local_decode(const uint8_t* __restrict p,uint8_t* __restrict out) {
    static_assert(N%16==0);
    if constexpr(N%64!=0) {
        for(unsigned i=0;i<N;i+=16) local_read16<K>(p,i).store(out+i);
    } else {
#if defined(__aarch64__)
    local_neon_decode<K,N>(p,out);
#elif defined(__AVX512VBMI__) && defined(__GFNI__)
    static_assert(N%64==0);
    constexpr auto shuffle=[] {
        std::array<uint8_t,64> a{};
        for(unsigned g=0;g<8;++g) for(unsigned b=0;b<K;++b) a[g*8+7-b]=g*K+b;
        return a;
    }();
    constexpr uint64_t valid=0x0101010101010101ULL*((1u<<K)-1)<<(8-K);
    const auto perm=_mm512_loadu_si512(shuffle.data());
    for(unsigned i=0;i<N;i+=64) {
        const auto packed=_mm512_maskz_loadu_epi8((1ULL<<(8*K))-1,p+i/8*K);
        const auto matrix=_mm512_maskz_permutexvar_epi8(valid,perm,packed);
        const auto values=_mm512_gf2p8affine_epi64_epi8(_mm512_set1_epi64(0x8040201008040201ULL),matrix,0);
        _mm512_storeu_si512(out+i,values);
    }
#else
    for(unsigned i=0;i<N;i+=16) local_read16<K>(p,i).store(out+i);
#endif
    }
}

template<unsigned K,unsigned N=256> IP_INLINE void local_encode(const uint8_t* __restrict in,uint8_t* __restrict p) {
    static_assert(N%8==0);
    if constexpr(N%64!=0) {
        for(unsigned i=0;i<N;i+=8) write_bytes<K>(p+i/8*K,transpose8(read_bytes<8>(in+i)));
    } else {
#if defined(__aarch64__)
    local_neon_encode<K,N>(in,p);
#elif defined(__AVX512VBMI2__) && defined(__GFNI__)
    static_assert(N%64==0);
    constexpr auto reverse=[] {
        std::array<uint8_t,64> a{}; for(unsigned i=0;i<64;++i) a[i]=(i&~7u)+(7-i%8); return a;
    }();
    const auto perm=_mm512_loadu_si512(reverse.data());
    for(unsigned i=0;i<N;i+=64) {
        auto x=_mm512_loadu_si512(in+i);
        if constexpr(K==1) write_bytes<8>(p+i/8,_mm512_movepi8_mask(_mm512_slli_epi64(x,7)));
        else {
            x=_mm512_shuffle_epi8(x,perm);
            x=_mm512_gf2p8affine_epi64_epi8(_mm512_set1_epi64(0x8040201008040201ULL),x,0);
            x=_mm512_maskz_compress_epi8(0x0101010101010101ULL*((1u<<K)-1),x);
            _mm512_mask_storeu_epi8(p+i/8*K,(1ULL<<(8*K))-1,x);
        }
    }
#else
    for(unsigned i=0;i<N;i+=8) write_bytes<K>(p+i/8*K,transpose8(read_bytes<8>(in+i)));
#endif
    }
}
} // namespace ikea::integers
