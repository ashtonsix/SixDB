#pragma once
#include "scan.h"

namespace ikea::integers {
#if defined(__AVX512BW__)
// An execution grain over two existing32B stripes. The stored chunks and
// logical value order do not change. Applicable to1/2/4-bit ScanPack tiles.
template<unsigned K,unsigned N> IP_INLINE void scan_pairs_decode(const uint8_t* __restrict p,uint8_t* __restrict out) {
    static_assert(K==1||K==2||K==4);
    using F=ScanPack<K>;
    static_assert(N%(2*F::values)==0);
    for(unsigned i=0;i<N;i+=2*F::values) {
        const auto raw=Bytes<64>::load(p+i/F::values*32);
        std::array<Bytes<64>,F::groups> values;
        unroll<F::groups>([&](auto g) {values[g]=raw.template shift<-int(g*K)>().template mask<(1u<<K)-1>();});
        unroll<F::groups/2>([&](auto pair) {
            constexpr unsigned g=decltype(pair)::value*2;
            _mm512_storeu_si512(out+i+g*32,_mm512_shuffle_i64x2(values[g].v,values[g+1].v,0x44));
            _mm512_storeu_si512(out+i+F::values+g*32,_mm512_shuffle_i64x2(values[g].v,values[g+1].v,0xee));
        });
    }
}
template<unsigned K,unsigned N> IP_INLINE void scan_pairs_encode(const uint8_t* __restrict in,uint8_t* __restrict p) {
    static_assert(K==1||K==2||K==4);
    using F=ScanPack<K>;
    static_assert(N%(2*F::values)==0);
    for(unsigned i=0;i<N;i+=2*F::values) {
        std::array<Bytes<64>,F::groups> values;
        unroll<F::groups/2>([&](auto pair) {
            constexpr unsigned g=decltype(pair)::value*2;
            const auto a=_mm512_loadu_si512(in+i+g*32),b=_mm512_loadu_si512(in+i+F::values+g*32);
            values[g]={_mm512_shuffle_i64x2(a,b,0x44)};
            values[g+1]={_mm512_shuffle_i64x2(a,b,0xee)};
        });
        pack_power<K,0,F::groups>(values).store(p+i/F::values*32);
    }
}
#endif
} // namespace ikea::integers
