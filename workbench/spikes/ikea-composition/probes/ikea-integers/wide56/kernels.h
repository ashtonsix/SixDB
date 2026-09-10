#pragma once
#include "api.h"
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>
#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#else
#error "The wide56 probe needs NEON or AVX2."
#endif

namespace ikea::integers::wide56 {
#define W56_INLINE inline __attribute__((always_inline))
static_assert(std::endian::native==std::endian::little);

template<unsigned N,class F> W56_INLINE void unroll(F&& f) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (f(std::integral_constant<unsigned,I>{}),...);
    }(std::make_index_sequence<N>{});
}

// A point read touches only its seven bytes. The overlapping words agree in
// their shared byte and avoid an eight-byte load beyond the selected value.
W56_INLINE std::uint64_t point(const std::uint8_t* p) {
    std::uint32_t low,high;
    std::memcpy(&low,p,4);std::memcpy(&high,p+3,4);
    return std::uint64_t(low)|(std::uint64_t(high)<<24);
}

#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
using Fragment=__m512i;
inline constexpr unsigned fragment_values=8;
inline constexpr unsigned fragments_per_packet=1;
inline constexpr auto decode_indices=[] {
    std::array<std::uint8_t,64> r{};
    for(unsigned lane=0;lane<8;++lane) {
        for(unsigned b=0;b<7;++b) r[8*lane+b]=7*lane+b;
        r[8*lane+7]=56; // The masked load supplies this zero byte for free.
    }
    return r;
}();
inline constexpr auto encode_indices=[] {
    std::array<std::uint8_t,64> r{};
    for(unsigned b=0;b<56;++b) r[b]=8*(b/7)+b%7;
    return r;
}();
W56_INLINE Fragment load_values(const std::uint64_t* p) {return _mm512_loadu_si512(p);}
W56_INLINE void store_values(std::uint64_t* p,Fragment v) {_mm512_storeu_si512(p,v);}
template<unsigned Part> W56_INLINE Fragment decode_fragment(const std::uint8_t* packet) {
    static_assert(Part==0);
    const auto bytes=_mm512_maskz_loadu_epi8(0x00ffffffffffffffULL,packet);
    return _mm512_permutexvar_epi8(_mm512_loadu_si512(decode_indices.data()),bytes);
}
template<unsigned Part> W56_INLINE void encode_fragment(std::uint8_t* packet,Fragment values) {
    static_assert(Part==0);
    const auto bytes=_mm512_permutexvar_epi8(_mm512_loadu_si512(encode_indices.data()),values);
    _mm512_mask_storeu_epi8(packet,0x00ffffffffffffffULL,bytes);
}
W56_INLINE Fragment zero() {return _mm512_setzero_si512();}
W56_INLINE Fragment add(Fragment a,Fragment b) {return _mm512_add_epi64(a,b);}
W56_INLINE std::uint64_t horizontal_sum(Fragment v) {return std::uint64_t(_mm512_reduce_add_epi64(v));}

#elif defined(__AVX2__)
using Fragment=__m256i;
inline constexpr unsigned fragment_values=4;
inline constexpr unsigned fragments_per_packet=2;
inline constexpr std::array<std::uint8_t,32> decode_indices={
    0,1,2,3,4,5,6,128,7,8,9,10,11,12,13,128,
    2,3,4,5,6,7,8,128,9,10,11,12,13,14,15,128};
inline constexpr std::array<std::uint8_t,32> encode_indices={
    0,1,2,3,4,5,6,8,9,10,11,12,13,14,128,128,
    0,1,2,3,4,5,6,8,9,10,11,12,13,14,128,128};
W56_INLINE Fragment load_values(const std::uint64_t* p) {return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));}
W56_INLINE void store_values(std::uint64_t* p,Fragment v) {_mm256_storeu_si256(reinterpret_cast<__m256i*>(p),v);}
template<unsigned Part> W56_INLINE Fragment decode_fragment(const std::uint8_t* packet) {
    static_assert(Part<2);
    const auto* p=packet+28*Part;
    const auto first=_mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
    const auto second=_mm_loadu_si128(reinterpret_cast<const __m128i*>(p+12));
    const auto joined=_mm256_inserti128_si256(_mm256_castsi128_si256(first),second,1);
    return _mm256_shuffle_epi8(joined,_mm256_loadu_si256(reinterpret_cast<const __m256i*>(decode_indices.data())));
}
template<unsigned Part> W56_INLINE void encode_fragment(std::uint8_t* packet,Fragment values) {
    static_assert(Part<2);
    auto* p=packet+28*Part;
    const auto compact=_mm256_shuffle_epi8(values,_mm256_loadu_si256(reinterpret_cast<const __m256i*>(encode_indices.data())));
    const auto first=_mm256_castsi256_si128(compact),second=_mm256_extracti128_si256(compact,1);
    const auto left=_mm_or_si128(first,_mm_slli_si128(second,14));
    const auto right=_mm_or_si128(_mm_srli_si128(first,12),_mm_slli_si128(second,2));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(p),left);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(p+12),right);
}
W56_INLINE Fragment zero() {return _mm256_setzero_si256();}
W56_INLINE Fragment add(Fragment a,Fragment b) {return _mm256_add_epi64(a,b);}
W56_INLINE std::uint64_t horizontal_sum(Fragment v) {
    const auto pair=_mm_add_epi64(_mm256_castsi256_si128(v),_mm256_extracti128_si256(v,1));
    return std::uint64_t(_mm_cvtsi128_si64(_mm_add_epi64(pair,_mm_unpackhi_epi64(pair,pair))));
}

#else
using Fragment=uint64x2_t;
inline constexpr unsigned fragment_values=2;
inline constexpr unsigned fragments_per_packet=4;
inline constexpr std::array<std::uint8_t,16> decode_indices={0,1,2,3,4,5,6,255,7,8,9,10,11,12,13,255};
inline constexpr std::array<std::uint8_t,16> decode_last_indices={2,3,4,5,6,7,8,255,9,10,11,12,13,14,15,255};
inline constexpr std::array<std::uint8_t,16> encode_indices={0,1,2,3,4,5,6,8,9,10,11,12,13,14,255,255};
W56_INLINE Fragment load_values(const std::uint64_t* p) {return vld1q_u64(p);}
W56_INLINE void store_values(std::uint64_t* p,Fragment v) {vst1q_u64(p,v);}
template<unsigned Part> W56_INLINE Fragment decode_fragment(const std::uint8_t* packet) {
    static_assert(Part<4);
    // The final pair uses the last 16 bytes of the packet, not a padded load.
    if constexpr(Part==3) return vreinterpretq_u64_u8(vqtbl1q_u8(vld1q_u8(packet+40),vld1q_u8(decode_last_indices.data())));
    else return vreinterpretq_u64_u8(vqtbl1q_u8(vld1q_u8(packet+14*Part),vld1q_u8(decode_indices.data())));
}
template<unsigned Part> W56_INLINE void encode_fragment(std::uint8_t* packet,Fragment values) {
    static_assert(Part<4);
    const auto compact=vqtbl1q_u8(vreinterpretq_u8_u64(values),vld1q_u8(encode_indices.data()));
    auto* p=packet+14*Part;
    vst1_u8(p,vget_low_u8(compact));
    vst1_u8(p+6,vget_low_u8(vextq_u8(compact,compact,6)));
}
W56_INLINE Fragment zero() {return vdupq_n_u64(0);}
W56_INLINE Fragment add(Fragment a,Fragment b) {return vaddq_u64(a,b);}
W56_INLINE std::uint64_t horizontal_sum(Fragment v) {return vaddvq_u64(v);}
#endif

// Selected inline parent region. NEON batches four native pairs to make three
// full stores plus one 8-byte store; consumers still receive one native pair.
W56_INLINE void encode_packet(const std::array<Fragment,fragments_per_packet>& values,std::uint8_t* packet) {
#if defined(__aarch64__)
    const uint8x16x4_t table={{vreinterpretq_u8_u64(values[0]),vreinterpretq_u8_u64(values[1]),
        vreinterpretq_u8_u64(values[2]),vreinterpretq_u8_u64(values[3])}};
    constexpr auto indices=[] {
        std::array<std::uint8_t,56> r{};
        for(unsigned b=0;b<56;++b) r[b]=8*(b/7)+b%7;
        return r;
    }();
    vst1q_u8(packet,vqtbl4q_u8(table,vld1q_u8(indices.data())));
    vst1q_u8(packet+16,vqtbl4q_u8(table,vld1q_u8(indices.data()+16)));
    vst1q_u8(packet+32,vqtbl4q_u8(table,vld1q_u8(indices.data()+32)));
    vst1_u8(packet+48,vqtbl4_u8(table,vld1_u8(indices.data()+48)));
#else
    unroll<fragments_per_packet>([&](auto part) {encode_fragment<part>(packet,values[part]);});
#endif
}
} // namespace ikea::integers::wide56
