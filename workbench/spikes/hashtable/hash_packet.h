#pragma once
#include "hash.h"
#include <span>

#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#else
#error "hash_packet requires NEON or AVX2 (with optional AVX-512)"
#endif

namespace ht {

// Experimental packet widths, not a public batch-size decision. Word selectors
// keep the result in SIMD-friendly SoA form; their values still fit in one byte.
template<unsigned N> struct hash_packet {
    static_assert(N==4 || N==8);
    std::array<word,N> bucket, suffix, fingerprint;
};

// The quotient permutation already contains exactly K useful bits. Keep the
// compact control in registers while encoding, with no intermediate word planes.
template<unsigned N,unsigned K,unsigned A> struct compact_hash_packet {
    static_assert(N==4 || N==8);
    std::array<word,N> encoded;
};
template<unsigned K,unsigned A,unsigned N>
[[gnu::always_inline]] inline auto packet_parts(const hash_packet<N>& p,unsigned lane) {
    return typename permutation<K,A>::parts{p.bucket[lane],p.suffix[lane],byte(p.fingerprint[lane])};
}
template<unsigned K,unsigned A,unsigned N>
[[gnu::always_inline]] inline auto packet_parts(const compact_hash_packet<N,K,A>& p,unsigned lane) {
    constexpr unsigned F=permutation<K,A>::F,C=permutation<K,A>::C;
    const word encoded=p.encoded[lane];
    const auto suffix=[&] {if constexpr(C==0)return word(0);else return encoded>>(A+F);}();
    return typename permutation<K,A>::parts{encoded&mask(A),suffix,byte((encoded>>A)&mask(F))};
}

namespace hash_packet_detail {
inline constexpr word multiplier=0x79d5f9e0de1e8cf5ULL;
inline constexpr std::uint32_t multiplier_low=std::uint32_t(multiplier);
inline constexpr std::uint32_t multiplier_high=std::uint32_t(multiplier>>32);

// Base-2^32 multiplication, independently in each 64-bit SIMD lane:
//   middle = (p00 >> 32) + low32(p01) + low32(p10)
//   low    = low32(p00) | (middle << 32)
//   high   = p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32)
// The 34-bit middle is retained until its carry enters high. Adding the two
// cross-products as 64-bit words first would silently discard an essential carry.
// Every multiply below is an explicit widening unsigned 32x32 -> 64 operation.
#if defined(__aarch64__)
using vector2=uint64x2_t;
[[gnu::always_inline]] inline vector2 fold(vector2 x) {
    const auto lo=vmovn_u64(x),hi=vshrn_n_u64(x,32);
    const auto p00=vmull_n_u32(lo,multiplier_low);
    const auto p01=vmull_n_u32(lo,multiplier_high);
    const auto p10=vmull_n_u32(hi,multiplier_low);
    const auto p11=vmull_n_u32(hi,multiplier_high);
    const auto low_mask=vdupq_n_u64(0xffffffffULL);
    const auto middle=vaddq_u64(vshrq_n_u64(p00,32),
        vaddq_u64(vandq_u64(p01,low_mask),vandq_u64(p10,low_mask)));
    const auto low=vorrq_u64(vandq_u64(p00,low_mask),vshlq_n_u64(middle,32));
    const auto high=vaddq_u64(vaddq_u64(p11,vshrq_n_u64(p01,32)),
        vaddq_u64(vshrq_n_u64(p10,32),vshrq_n_u64(middle,32)));
    return veorq_u64(low,high);
}
[[gnu::always_inline]] inline vector2 multiply_low(vector2 x,word a) {
    const auto lo=vmovn_u64(x),hi=vshrn_n_u64(x,32);
    const auto cross=vaddq_u64(vmull_n_u32(lo,std::uint32_t(a>>32)),vmull_n_u32(hi,std::uint32_t(a)));
    return vaddq_u64(vmull_n_u32(lo,std::uint32_t(a)),vshlq_n_u64(cross,32));
}
template<unsigned K,unsigned A,bool Compact=false> [[gnu::always_inline]] inline void split_vector(
    const permutation<K,A>& p,const word* keys,word* bucket,word* suffix,word* fingerprint) {
    constexpr unsigned F=permutation<K,A>::F,C=permutation<K,A>::C;
    const auto x=vandq_u64(veorq_u64(vld1q_u64(keys),vdupq_n_u64(p.input_seed)),vdupq_n_u64(mask(K)));
    const auto r=[&] {if constexpr(A==0)return x;else return vshrq_n_u64(x,A);}();
    const auto s=vshrq_n_u64(r,F);
    const auto b=[&] {
        if constexpr(A==0)return vdupq_n_u64(0);
        else return vandq_u64(veorq_u64(x,fold(veorq_u64(r,vdupq_n_u64(p.bucket_seed)))),vdupq_n_u64(mask(A)));
    }();
    const auto selector=[&] {
        if constexpr(C==0)return vdupq_n_u64(p.selector_zero());
        else if constexpr(HT_SELECTOR_HASH) {
            if constexpr(permutation<K,A>::selector_word_bits==32) {
                const auto a=vreinterpretq_u32_u64(vdupq_n_u64(std::uint32_t(p.selector_seed)));
                const auto b=vreinterpretq_u32_u64(vdupq_n_u64(std::uint32_t(p.selector_bias)));
                return vshrq_n_u64(vreinterpretq_u64_u32(vaddq_u32(vmulq_u32(vreinterpretq_u32_u64(s),a),b)),32-F);
            }else return vshrq_n_u64(vaddq_u64(multiply_low(s,p.selector_seed),vdupq_n_u64(p.selector_bias)),64-F);
        }
        else return vandq_u64(fold(veorq_u64(s,vdupq_n_u64(p.selector_seed))),vdupq_n_u64(mask(F)));
    }();
    auto fp=veorq_u64(vandq_u64(r,vdupq_n_u64(mask(F))),selector);
    if constexpr(C==0)fp=vandq_u64(fp,vdupq_n_u64(255)); // Match scalar's byte conversion.
    if constexpr(Compact) {
        const auto shift=[&] {if constexpr(A==0)return selector;else return vshlq_n_u64(selector,A);}();
        vst1q_u64(bucket,veorq_u64(vorrq_u64(vbicq_u64(x,vdupq_n_u64(mask(A))),b),shift));
    }else {vst1q_u64(bucket,b);vst1q_u64(suffix,s);vst1q_u64(fingerprint,fp);}
}
#else
using vector4=__m256i;
[[gnu::always_inline]] inline vector4 fold(vector4 x) {
    const auto hi=_mm256_srli_epi64(x,32);
    const auto c0=_mm256_set1_epi64x(multiplier_low),c1=_mm256_set1_epi64x(multiplier_high);
    const auto p00=_mm256_mul_epu32(x,c0),p01=_mm256_mul_epu32(x,c1);
    const auto p10=_mm256_mul_epu32(hi,c0),p11=_mm256_mul_epu32(hi,c1);
    const auto low_mask=_mm256_set1_epi64x(0xffffffffULL);
    const auto middle=_mm256_add_epi64(_mm256_srli_epi64(p00,32),
        _mm256_add_epi64(_mm256_and_si256(p01,low_mask),_mm256_and_si256(p10,low_mask)));
    const auto low=_mm256_or_si256(_mm256_and_si256(p00,low_mask),_mm256_slli_epi64(middle,32));
    const auto high=_mm256_add_epi64(_mm256_add_epi64(p11,_mm256_srli_epi64(p01,32)),
        _mm256_add_epi64(_mm256_srli_epi64(p10,32),_mm256_srli_epi64(middle,32)));
    return _mm256_xor_si256(low,high);
}
[[gnu::always_inline]] inline vector4 multiply_low(vector4 x,word a) {
    const auto lo=_mm256_set1_epi64x(std::uint32_t(a)),hi=_mm256_set1_epi64x(a>>32);
    const auto cross=_mm256_add_epi64(_mm256_mul_epu32(x,hi),_mm256_mul_epu32(_mm256_srli_epi64(x,32),lo));
    return _mm256_add_epi64(_mm256_mul_epu32(x,lo),_mm256_slli_epi64(cross,32));
}
template<unsigned K,unsigned A,bool Compact=false> [[gnu::always_inline]] inline void split_vector(
    const permutation<K,A>& p,const word* keys,word* bucket,word* suffix,word* fingerprint) {
    constexpr unsigned F=permutation<K,A>::F,C=permutation<K,A>::C;
    const auto x=_mm256_and_si256(_mm256_xor_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(keys)),
        _mm256_set1_epi64x(p.input_seed)),_mm256_set1_epi64x(mask(K)));
    const auto r=_mm256_srli_epi64(x,A),s=_mm256_srli_epi64(r,F);
    const auto b=[&] {
        if constexpr(A==0)return _mm256_setzero_si256();
        else return _mm256_and_si256(_mm256_xor_si256(x,fold(_mm256_xor_si256(r,_mm256_set1_epi64x(p.bucket_seed)))),
            _mm256_set1_epi64x(mask(A)));
    }();
    const auto selector=[&] {
        if constexpr(C==0)return _mm256_set1_epi64x(p.selector_zero());
        else if constexpr(HT_SELECTOR_HASH) {
            if constexpr(permutation<K,A>::selector_word_bits==32)
                return _mm256_srli_epi64(_mm256_add_epi32(
                    _mm256_mullo_epi32(s,_mm256_set1_epi64x(std::uint32_t(p.selector_seed))),
                    _mm256_set1_epi64x(std::uint32_t(p.selector_bias))),32-F);
            else return _mm256_srli_epi64(_mm256_add_epi64(multiply_low(s,p.selector_seed),_mm256_set1_epi64x(p.selector_bias)),64-F);
        }
        else return _mm256_and_si256(fold(_mm256_xor_si256(s,_mm256_set1_epi64x(p.selector_seed))),_mm256_set1_epi64x(mask(F)));
    }();
    auto fp=_mm256_xor_si256(_mm256_and_si256(r,_mm256_set1_epi64x(mask(F))),selector);
    if constexpr(C==0)fp=_mm256_and_si256(fp,_mm256_set1_epi64x(255));
    if constexpr(Compact) {
        const auto encoded=_mm256_xor_si256(_mm256_or_si256(_mm256_andnot_si256(_mm256_set1_epi64x(mask(A)),x),b),
            _mm256_slli_epi64(selector,A));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(bucket),encoded);
    }else {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(bucket),b);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(suffix),s);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(fingerprint),fp);
    }
}
#if defined(__AVX512F__)
using vector8=__m512i;
[[gnu::always_inline]] inline vector8 fold(vector8 x) {
    const auto hi=_mm512_srli_epi64(x,32);
    const auto c0=_mm512_set1_epi64(multiplier_low),c1=_mm512_set1_epi64(multiplier_high);
    const auto p00=_mm512_mul_epu32(x,c0),p01=_mm512_mul_epu32(x,c1);
    const auto p10=_mm512_mul_epu32(hi,c0),p11=_mm512_mul_epu32(hi,c1);
    const auto low_mask=_mm512_set1_epi64(0xffffffffULL);
    const auto middle=_mm512_add_epi64(_mm512_srli_epi64(p00,32),
        _mm512_add_epi64(_mm512_and_si512(p01,low_mask),_mm512_and_si512(p10,low_mask)));
    const auto low=_mm512_or_si512(_mm512_and_si512(p00,low_mask),_mm512_slli_epi64(middle,32));
    const auto high=_mm512_add_epi64(_mm512_add_epi64(p11,_mm512_srli_epi64(p01,32)),
        _mm512_add_epi64(_mm512_srli_epi64(p10,32),_mm512_srli_epi64(middle,32)));
    return _mm512_xor_si512(low,high);
}
[[gnu::always_inline]] inline vector8 multiply_low(vector8 x,word a) {
#if defined(__AVX512DQ__)
    return _mm512_mullo_epi64(x,_mm512_set1_epi64(a));
#else
    const auto lo=_mm512_set1_epi64(std::uint32_t(a)),hi=_mm512_set1_epi64(a>>32);
    const auto cross=_mm512_add_epi64(_mm512_mul_epu32(x,hi),_mm512_mul_epu32(_mm512_srli_epi64(x,32),lo));
    return _mm512_add_epi64(_mm512_mul_epu32(x,lo),_mm512_slli_epi64(cross,32));
#endif
}
template<unsigned K,unsigned A,bool Compact=false> [[gnu::always_inline]] inline void split_vector8(
    const permutation<K,A>& p,const word* keys,word* bucket,word* suffix,word* fingerprint) {
    constexpr unsigned F=permutation<K,A>::F,C=permutation<K,A>::C;
    const auto x=_mm512_and_si512(_mm512_xor_si512(_mm512_loadu_si512(keys),_mm512_set1_epi64(p.input_seed)),
        _mm512_set1_epi64(mask(K)));
    const auto r=_mm512_srli_epi64(x,A),s=_mm512_srli_epi64(r,F);
    const auto b=[&] {
        if constexpr(A==0)return _mm512_setzero_si512();
        else return _mm512_and_si512(_mm512_xor_si512(x,fold(_mm512_xor_si512(r,_mm512_set1_epi64(p.bucket_seed)))),
            _mm512_set1_epi64(mask(A)));
    }();
    const auto selector=[&] {
        if constexpr(C==0)return _mm512_set1_epi64(p.selector_zero());
        else if constexpr(HT_SELECTOR_HASH) {
            if constexpr(permutation<K,A>::selector_word_bits==32)
                return _mm512_srli_epi64(_mm512_add_epi32(
                    _mm512_mullo_epi32(s,_mm512_set1_epi64(std::uint32_t(p.selector_seed))),
                    _mm512_set1_epi64(std::uint32_t(p.selector_bias))),32-F);
            else return _mm512_srli_epi64(_mm512_add_epi64(multiply_low(s,p.selector_seed),_mm512_set1_epi64(p.selector_bias)),64-F);
        }
        else return _mm512_and_si512(fold(_mm512_xor_si512(s,_mm512_set1_epi64(p.selector_seed))),_mm512_set1_epi64(mask(F)));
    }();
    auto fp=_mm512_xor_si512(_mm512_and_si512(r,_mm512_set1_epi64(mask(F))),selector);
    if constexpr(C==0)fp=_mm512_and_si512(fp,_mm512_set1_epi64(255));
    if constexpr(Compact) {
        const auto encoded=_mm512_xor_si512(_mm512_or_si512(_mm512_andnot_si512(_mm512_set1_epi64(mask(A)),x),b),
            _mm512_slli_epi64(selector,A));
        _mm512_storeu_si512(bucket,encoded);
    }else {_mm512_storeu_si512(bucket,b);_mm512_storeu_si512(suffix,s);_mm512_storeu_si512(fingerprint,fp);}
}
#endif
#endif
} // namespace hash_packet_detail

template<unsigned N,bool Compact=false,unsigned K,unsigned A> requires(N==4 || N==8)
[[gnu::always_inline]] inline auto split_packet(
    const permutation<K,A>& p,std::span<const word,N> keys) {
    static_assert(K>=8 && K<=64 && K%8==0);
    std::conditional_t<Compact,compact_hash_packet<N,K,A>,hash_packet<N>> result;
    auto bucket=[&] {if constexpr(Compact)return result.encoded.data();else return result.bucket.data();}();
    auto suffix=[&]() -> word* {if constexpr(Compact)return nullptr;else return result.suffix.data();}();
    auto fingerprint=[&]() -> word* {if constexpr(Compact)return nullptr;else return result.fingerprint.data();}();
    // Never perform pointer arithmetic on unused null planes.
    auto offset=[](word* p,unsigned i) {return p?p+i:nullptr;};
#if defined(__aarch64__)
    lanes<N/2>([&](auto i) {
        hash_packet_detail::split_vector<K,A,Compact>(p,keys.data()+2*i,bucket+2*i,
            offset(suffix,2*i),offset(fingerprint,2*i));
    });
#else
#if defined(__AVX512F__)
    if constexpr(N==8) {
        hash_packet_detail::split_vector8<K,A,Compact>(p,keys.data(),bucket,suffix,fingerprint);
    } else
#endif
    {
        lanes<N/4>([&](auto i) {
            hash_packet_detail::split_vector<K,A,Compact>(p,keys.data()+4*i,bucket+4*i,
                offset(suffix,4*i),offset(fingerprint,4*i));
        });
    }
#endif
    return result;
}
} // namespace ht
