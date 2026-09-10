#pragma once
#include "kernels.h"

namespace ikea::integers::wide56 {
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
// An optional inline region covers four unchanged storage packets. Output
// windows cross packet boundaries, using three full ZMM stores and one YMM
// store instead of four unaligned masked stores. Input remains four native
// eight-value fragments; no fixed get16 interchange is involved.
template<unsigned Window> inline constexpr auto region_encode_indices=[] {
    static_assert(Window<4);
    std::array<std::uint8_t,64> out{};
    for(unsigned i=0;i<(Window==3?32u:64u);++i) {
        const unsigned packed=Window*64+i;
        out[i]=std::uint8_t((packed/7)*8+packed%7-Window*64);
    }
    return out;
}();
W56_INLINE void encode_region32(const std::uint64_t* values,std::uint8_t* packed) {
    const auto a=load_values(values),b=load_values(values+8);
    const auto c=load_values(values+16),d=load_values(values+24);
    const auto first=_mm512_permutex2var_epi8(a,_mm512_loadu_si512(region_encode_indices<0>.data()),b);
    const auto second=_mm512_permutex2var_epi8(b,_mm512_loadu_si512(region_encode_indices<1>.data()),c);
    const auto third=_mm512_permutex2var_epi8(c,_mm512_loadu_si512(region_encode_indices<2>.data()),d);
    const auto last=_mm512_permutexvar_epi8(_mm512_loadu_si512(region_encode_indices<3>.data()),d);
    _mm512_storeu_si512(packed,first);
    _mm512_storeu_si512(packed+64,second);
    _mm512_storeu_si512(packed+128,third);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(packed+192),_mm512_castsi512_si256(last));
}
#endif
}
