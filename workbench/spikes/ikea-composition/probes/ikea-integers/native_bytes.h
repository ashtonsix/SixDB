#pragma once
#include "bytes.h"
#include <array>
#include <utility>
#if defined(__aarch64__)
#include <arm_neon.h>
#if defined(__ARM_FEATURE_SVE2_BITPERM)
#include <arm_sve.h>
#include <arm_neon_sve_bridge.h>
#endif
#elif defined(__x86_64__)
#include <immintrin.h>
#endif

namespace ikea::integers {
// ISA operands. These wrappers disappear inside a selected body; their class
// layout does not prescribe an opaque-call return convention.
template<unsigned N> struct Bytes;
#if defined(__aarch64__)
template<> struct Bytes<16> {
    uint8x16_t v;
    static IP_INLINE Bytes load(const uint8_t* p) { return {vld1q_u8(p)}; }
    IP_INLINE void store(uint8_t* p) const { vst1q_u8(p,v); }
    static IP_INLINE Bytes zero() { return {vdupq_n_u8(0)}; }
    template<unsigned M> IP_INLINE Bytes mask() const { return {vandq_u8(v,vdupq_n_u8(M))}; }
    template<int S> IP_INLINE Bytes shift() const {
        if constexpr(S>0) return {vshlq_n_u8(v,S)};
        else if constexpr(S<0) return {vshrq_n_u8(v,-S)};
        else return *this;
    }
    IP_INLINE Bytes operator|(Bytes b) const { return {vorrq_u8(v,b.v)}; }
    template<unsigned M> IP_INLINE Bytes replace(Bytes b) const { return {vbslq_u8(vdupq_n_u8(M),b.v,v)}; }
    IP_INLINE Bytes replace(unsigned m, Bytes b) const { return {vbslq_u8(vdupq_n_u8(m),b.v,v)}; }
    template<unsigned S> IP_INLINE Bytes high(Bytes b) const { return {vsliq_n_u8(v,b.v,S)}; }
    template<unsigned S> IP_INLINE Bytes low(Bytes b) const { return {vsriq_n_u8(v,b.v,S)}; }
    IP_INLINE Bytes right(unsigned s) const { return {vshlq_u8(v,vdupq_n_s8(-int(s)))}; }
};
#elif defined(__AVX2__)
template<> struct Bytes<16> {
    __m128i v;
    static IP_INLINE Bytes load(const uint8_t* p) { return {_mm_loadu_si128(reinterpret_cast<const __m128i*>(p))}; }
    IP_INLINE void store(uint8_t* p) const { _mm_storeu_si128(reinterpret_cast<__m128i*>(p),v); }
    static IP_INLINE Bytes zero() { return {_mm_setzero_si128()}; }
    template<unsigned M> IP_INLINE Bytes mask() const { return {_mm_and_si128(v,_mm_set1_epi8(char(M)))}; }
    template<int S> IP_INLINE Bytes shift() const {
        if constexpr(S>0) return Bytes{_mm_slli_epi16(v,S)}.template mask<(255u<<S)&255>();
        else if constexpr(S<0) return Bytes{_mm_srli_epi16(v,-S)}.template mask<(255u>>-S)>();
        else return *this;
    }
    IP_INLINE Bytes operator|(Bytes b) const { return {_mm_or_si128(v,b.v)}; }
    template<unsigned M> IP_INLINE Bytes replace(Bytes b) const {
        const auto m=_mm_set1_epi8(char(M));
#if defined(__AVX512VL__)
        return {_mm_ternarylogic_epi32(m,b.v,v,0xca)};
#else
        return {_mm_or_si128(_mm_and_si128(m,b.v),_mm_andnot_si128(m,v))};
#endif
    }
    IP_INLINE Bytes replace(unsigned bits, Bytes b) const {
        const auto m=_mm_set1_epi8(char(bits));
#if defined(__AVX512VL__)
        return {_mm_ternarylogic_epi32(m,b.v,v,0xca)};
#else
        return {_mm_or_si128(_mm_and_si128(m,b.v),_mm_andnot_si128(m,v))};
#endif
    }
    // A field merge supplies the byte mask already. A word shift can leave
    // cross-byte bits outside M without a separate byte-cleanup instruction.
    template<unsigned M, int S> IP_INLINE Bytes replace_shifted(Bytes b) const {
        static_assert(S > -8 && S < 8);
        constexpr unsigned valid=S>=0 ? (255u<<S)&255u : 255u>>-S;
        static_assert((M & ~valid)==0);
        if constexpr(S>0) return replace<M>({_mm_slli_epi16(b.v,S)});
        else if constexpr(S<0) return replace<M>({_mm_srli_epi16(b.v,-S)});
        else return replace<M>(b);
    }
    template<unsigned M, int S> IP_INLINE Bytes extract_shifted() const {
        static_assert(S > -8 && S < 8);
        constexpr unsigned valid=S>=0 ? (255u<<S)&255u : 255u>>-S;
        static_assert((M & ~valid)==0);
        if constexpr(S>0) return Bytes{_mm_slli_epi16(v,S)}.template mask<M>();
        else if constexpr(S<0) return Bytes{_mm_srli_epi16(v,-S)}.template mask<M>();
        else return mask<M>();
    }
    template<unsigned S> IP_INLINE Bytes high(Bytes b) const {return replace<(255u<<S)&255>(b.template shift<S>());}
    template<unsigned S> IP_INLINE Bytes low(Bytes b) const {return replace<(255u>>S)>(b.template shift<-int(S)>());}
    IP_INLINE Bytes right(unsigned s) const {return {_mm_srl_epi16(v,_mm_cvtsi32_si128(int(s)))};}
};
template<> struct Bytes<32> {
    __m256i v;
    static IP_INLINE Bytes load(const uint8_t* p) { return {_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p))}; }
    IP_INLINE void store(uint8_t* p) const { _mm256_storeu_si256(reinterpret_cast<__m256i*>(p),v); }
    static IP_INLINE Bytes zero() { return {_mm256_setzero_si256()}; }
    template<unsigned M> IP_INLINE Bytes mask() const { return {_mm256_and_si256(v,_mm256_set1_epi8(char(M)))}; }
    template<int S> IP_INLINE Bytes shift() const {
        if constexpr(S>0) return Bytes{_mm256_slli_epi16(v,S)}.template mask<(255u<<S)&255>();
        else if constexpr(S<0) return Bytes{_mm256_srli_epi16(v,-S)}.template mask<(255u>>-S)>();
        else return *this;
    }
    IP_INLINE Bytes operator|(Bytes b) const { return {_mm256_or_si256(v,b.v)}; }
    template<unsigned M> IP_INLINE Bytes replace(Bytes b) const {
        const auto m=_mm256_set1_epi8(char(M));
#if defined(__AVX512VL__)
        return {_mm256_ternarylogic_epi32(m,b.v,v,0xca)};
#else
        return {_mm256_or_si256(_mm256_and_si256(m,b.v),_mm256_andnot_si256(m,v))};
#endif
    }
    template<unsigned M, int S> IP_INLINE Bytes replace_shifted(Bytes b) const {
        static_assert(S > -8 && S < 8);
        constexpr unsigned valid=S>=0 ? (255u<<S)&255u : 255u>>-S;
        static_assert((M & ~valid)==0);
        if constexpr(S>0) return replace<M>({_mm256_slli_epi16(b.v,S)});
        else if constexpr(S<0) return replace<M>({_mm256_srli_epi16(b.v,-S)});
        else return replace<M>(b);
    }
    template<unsigned M,int S> IP_INLINE Bytes replace_shifted_register_mask(Bytes b) const {
        static_assert(S>-8 && S<8);
        constexpr unsigned valid=S>=0 ? (255u<<S)&255u : 255u>>-S;
        static_assert((M&~valid)==0);
        if constexpr(S>0) b={_mm256_slli_epi16(b.v,S)};
        else if constexpr(S<0) b={_mm256_srli_epi16(b.v,-S)};
        // This optional lowering trades constant-memory operands for GPR
        // immediates and broadcasts; the barrier prevents constant refolding.
        uint32_t scalar=M*0x01010101u;
        asm volatile("" : "+r"(scalar));
        const auto mask=_mm256_set1_epi32(int(scalar));
#if defined(__AVX512VL__)
        return {_mm256_ternarylogic_epi32(mask,b.v,v,0xca)};
#else
        return {_mm256_or_si256(_mm256_and_si256(mask,b.v),_mm256_andnot_si256(mask,v))};
#endif
    }
    template<unsigned M, int S> IP_INLINE Bytes extract_shifted() const {
        static_assert(S > -8 && S < 8);
        constexpr unsigned valid=S>=0 ? (255u<<S)&255u : 255u>>-S;
        static_assert((M & ~valid)==0);
        if constexpr(S>0) return Bytes{_mm256_slli_epi16(v,S)}.template mask<M>();
        else if constexpr(S<0) return Bytes{_mm256_srli_epi16(v,-S)}.template mask<M>();
        else return mask<M>();
    }
    template<unsigned S> IP_INLINE Bytes high(Bytes b) const {return replace<(255u<<S)&255>(b.template shift<S>());}
    template<unsigned S> IP_INLINE Bytes low(Bytes b) const {return replace<(255u>>S)>(b.template shift<-int(S)>());}
};
#if defined(__AVX512BW__)
template<> struct Bytes<64> {
    __m512i v;
    static IP_INLINE Bytes load(const uint8_t* p) {return {_mm512_loadu_si512(p)};}
    IP_INLINE void store(uint8_t* p) const {_mm512_storeu_si512(p,v);}
    static IP_INLINE Bytes zero() {return {_mm512_setzero_si512()};}
    template<unsigned M> IP_INLINE Bytes mask() const {return {_mm512_and_si512(v,_mm512_set1_epi8(char(M)))};}
    template<int S> IP_INLINE Bytes shift() const {
        if constexpr(S>0) return Bytes{_mm512_slli_epi16(v,S)}.template mask<(255u<<S)&255>();
        else if constexpr(S<0) return Bytes{_mm512_srli_epi16(v,-S)}.template mask<(255u>>-S)>();
        else return *this;
    }
    template<unsigned M> IP_INLINE Bytes replace(Bytes b) const {return {_mm512_ternarylogic_epi32(_mm512_set1_epi8(char(M)),b.v,v,0xca)};}
    template<unsigned S> IP_INLINE Bytes high(Bytes b) const {return replace<(255u<<S)&255>(b.template shift<S>());}
};
#endif
#endif
template<unsigned N, class F> constexpr IP_INLINE void unroll(F&& f) {
    [&]<size_t... I>(std::index_sequence<I...>) { (f(std::integral_constant<unsigned,I>{}),...); }(std::make_index_sequence<N>{});
}
template<unsigned N, class F> IP_INLINE auto group_dispatch(unsigned g, F&& f) {
    if constexpr(N == 1) return f(std::integral_constant<unsigned,0>{});
    else {
        if(g == N-1) return f(std::integral_constant<unsigned,N-1>{});
        return group_dispatch<N-1>(g,std::forward<F>(f));
    }
}
} // namespace ikea::integers
