#pragma once
#include "native_bytes.h"
#include <algorithm>
#if defined(__aarch64__)
namespace ikea::integers {
template<unsigned N> IP_INLINE uint8x16_t neon_prefix(const uint8_t* p) {
    static_assert(N>=1&&N<=16);
    if constexpr(N==16) return vld1q_u8(p);
    else if constexpr(N<=8) return vreinterpretq_u8_u64(vcombine_u64(vcreate_u64(read_bytes<N>(p)),vdup_n_u64(0)));
    else return vreinterpretq_u8_u64(vcombine_u64(vcreate_u64(read_bytes<8>(p)),vcreate_u64(read_bytes<N-8>(p+8))));
}
template<unsigned N> IP_INLINE void neon_store_prefix(uint8_t* p,uint8x16_t x) {
    if constexpr(N==16) vst1q_u8(p,x);
    else if constexpr(N<=8) write_bytes<N>(p,vgetq_lane_u64(vreinterpretq_u64_u8(x),0));
    else {write_bytes<8>(p,vgetq_lane_u64(vreinterpretq_u64_u8(x),0));write_bytes<N-8>(p+8,vgetq_lane_u64(vreinterpretq_u64_u8(x),1));}
}
template<bool BitGroup> IP_INLINE uint8x16_t neon_transpose(uint8x16_t in) {
#if defined(__ARM_FEATURE_SVE2_BITPERM)
    if constexpr(BitGroup) {
        const auto mask=svdup_n_u64(0x5555555555555555ULL);
        auto x=svset_neonq_u64(svundef_u64(),vreinterpretq_u64_u8(in));
        x=svbgrp_u64(x,mask);x=svbgrp_u64(x,mask);x=svbgrp_u64(x,mask);
        return vreinterpretq_u8_u64(svget_neonq_u64(x));
    }
#endif
    auto x=vreinterpretq_u64_u8(in);
    auto exchange=[&]<unsigned S,uint64_t Mask>() {
        auto delta=vandq_u64(veorq_u64(x,vshrq_n_u64(x,S)),vdupq_n_u64(Mask));
        // Preserve the exchange form instead of turning narrow known-zero
        // terms into widening multiplications. No instructions are emitted.
        asm("" : "+w"(delta));
        x=veorq_u64(x,veorq_u64(delta,vshlq_n_u64(delta,S)));
    };
    exchange.template operator()<7,0x00aa00aa00aa00aaULL>();
    exchange.template operator()<14,0x0000cccc0000ccccULL>();
    exchange.template operator()<28,0x00000000f0f0f0f0ULL>();
    return vreinterpretq_u8_u64(x);
}
template<unsigned First,unsigned Count,size_t Size> IP_INLINE uint8x16_t neon_table(const std::array<uint8x16_t,Size>& x,uint8x16_t index) {
    if constexpr(Count==1) return vqtbl1q_u8(x[First],index);
    else if constexpr(Count==2) return vqtbl2q_u8({{x[First],x[First+1]}},index);
    else if constexpr(Count==3) return vqtbl3q_u8({{x[First],x[First+1],x[First+2]}},index);
    else return vqtbl4q_u8({{x[First],x[First+1],x[First+2],x[First+3]}},index);
}
template<unsigned K,unsigned N> IP_INLINE void local_neon_decode(const uint8_t* __restrict p,uint8_t* __restrict out) {
    static_assert(N%64==0);
    constexpr unsigned ChunkCount=(8*K+15)/16;
    for(unsigned tile=0;tile<N/64;++tile) {
        std::array<uint8x16_t,ChunkCount> raw;
        unroll<ChunkCount>([&](auto c) { constexpr unsigned S=std::min(16u,8*K-c*16); raw[c]=neon_prefix<S>(p+tile*8*K+c*16); });
        unroll<4>([&](auto pair) {
            constexpr unsigned P=decltype(pair)::value;
            constexpr unsigned First=P*2*K/16,Last=((P+1)*2*K-1)/16;
            constexpr auto indices=[] {
                std::array<uint8_t,16> a{};a.fill(128);
                for(unsigned g=0;g<2;++g) for(unsigned b=0;b<K;++b) a[g*8+b]=(P*2+g)*K+b-First*16;
                return a;
            }();
            auto x=neon_table<First,Last-First+1>(raw,vld1q_u8(indices.data()));
            if constexpr(K<=2) {
                constexpr auto weights=[] {std::array<uint8_t,16>a{};for(unsigned i=0;i<16;++i)a[i]=1u<<(i%8);return a;}();
                const auto w=vld1q_u8(weights.data());
                auto result=vdupq_n_u8(0);
                unroll<K>([&](auto bit) {
                    constexpr auto splat=[] {std::array<uint8_t,16>a{};for(unsigned i=0;i<16;++i)a[i]=(P*2+i/8)*K+decltype(bit)::value-First*16;return a;}();
                    const auto plane=neon_table<First,Last-First+1>(raw,vld1q_u8(splat.data()));
                    result=vorrq_u8(result,vandq_u8(vtstq_u8(plane,w),vdupq_n_u8(1u<<bit)));
                });
                x=result;
            } else x=neon_transpose<(P!=3)>(x);
            vst1q_u8(out+tile*64+P*16,x);
        });
    }
}
template<unsigned K,unsigned N> IP_INLINE void local_neon_encode(const uint8_t* __restrict in,uint8_t* __restrict p) {
    static_assert(N%64==0);
    for(unsigned tile=0;tile<N/64;++tile) {
        std::array<uint8x16_t,4> x;
        unroll<4>([&](auto q) {x[q]=vld1q_u8(in+tile*64+q*16);});
        if constexpr(K==1) {
            constexpr auto weights=[] {std::array<uint8_t,16>a{};for(unsigned i=0;i<16;++i)a[i]=1u<<(i%8);return a;}();
            const auto w=vld1q_u8(weights.data());
            for(auto& v:x) v=vmulq_u8(v,w);
            const auto a=vpaddq_u8(x[0],x[1]),b=vpaddq_u8(x[2],x[3]);
            const auto c=vpaddq_u8(a,b);
            vst1_u8(p+tile*8,vget_low_u8(vpaddq_u8(c,c)));
        } else {
            unroll<4>([&](auto q) {x[q]=neon_transpose<(q!=3)>(x[q]);});
            unroll<(8*K+15)/16>([&](auto chunk) {
                constexpr unsigned Begin=decltype(chunk)::value*16;
                constexpr unsigned Count=std::min(16u,8*K-Begin);
                constexpr unsigned First=Begin/K/2,Last=(Begin+Count-1)/K/2;
                constexpr auto indices=[] {
                    std::array<uint8_t,16>a{};a.fill(128);
                    for(unsigned i=0;i<Count;++i) a[i]=(Begin+i)/K*8+(Begin+i)%K-First*16;
                    return a;
                }();
                const auto encoded=neon_table<First,Last-First+1>(x,vld1q_u8(indices.data()));
                neon_store_prefix<Count>(p+tile*8*K+Begin,encoded);
            });
        }
    }
}
} // namespace ikea::integers
#endif
