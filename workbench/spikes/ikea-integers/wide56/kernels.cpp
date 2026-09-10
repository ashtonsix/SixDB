#include "api.h"
#include "kernels.h"
#if defined(W56_ENCODE_REGION32)
#include "region_encode.h"
#endif

using namespace ikea::integers::wide56;

extern "C" std::uint64_t wide56_local_get1(const std::uint8_t* p,unsigned i) noexcept {
    __builtin_assume(i<block_values);
    return point(p+7*i);
}
extern "C" void wide56_local_get16(const std::uint8_t* __restrict p,unsigned i,std::uint64_t* __restrict out) noexcept {
    __builtin_assume(i<block_values && i%16==0);
    p+=7*i;
    unroll<2>([&](auto packet) {
        unroll<fragments_per_packet>([&](auto part) {
            store_values(out+packet*packet_values+part*fragment_values,
                decode_fragment<part>(p+packet*packet_bytes));
        });
    });
}
extern "C" void wide56_local_decode256(const std::uint8_t* __restrict p,std::uint64_t* __restrict out) noexcept {
    for(unsigned packet=0;packet<block_values/packet_values;++packet) {
        unroll<fragments_per_packet>([&](auto part) {
            store_values(out+packet*packet_values+part*fragment_values,
                decode_fragment<part>(p+packet*packet_bytes));
        });
    }
}
extern "C" void wide56_local_encode256(const std::uint64_t* __restrict values,std::uint8_t* __restrict p) noexcept {
#if defined(W56_ENCODE_REGION32) && defined(__AVX512BW__) && defined(__AVX512VBMI__)
    for(unsigned i=0;i<block_values;i+=32) encode_region32(values+i,p+7*i);
#else
    for(unsigned packet=0;packet<block_values/packet_values;++packet) {
        std::array<Fragment,fragments_per_packet> fragments;
        unroll<fragments_per_packet>([&](auto part) {
            fragments[part]=load_values(values+packet*packet_values+part*fragment_values);
        });
        encode_packet(fragments,p+packet*packet_bytes);
    }
#endif
}
extern "C" std::uint64_t wide56_local_sum256(const std::uint8_t* p) noexcept {
    auto first=zero(),second=zero();
    for(unsigned packet=0;packet<block_values/packet_values;packet+=2) {
        unroll<fragments_per_packet>([&](auto part) {
            first=add(first,decode_fragment<part>(p+packet*packet_bytes));
            second=add(second,decode_fragment<part>(p+(packet+1)*packet_bytes));
        });
    }
    return horizontal_sum(add(first,second));
}
