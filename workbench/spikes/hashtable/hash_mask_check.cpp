#include "hash.h"
#include <cassert>
#include <cstdio>

namespace {
using namespace ht;
// Deliberately ordinary arithmetic oracle: neither intrinsic nor experimental
// helper supplies the expected masked key or hash parts.
template<unsigned K,unsigned A> void check(word key,word seed) {
    permutation<K,A> hash(seed);
    const word bounded=(key^hash.input_seed)&mask(K);
    const word quotient=bounded/(word(1)<<A);
    const word suffix=quotient/(word(1)<<hash.F);
    const auto folded=[](word x) {
        const __uint128_t product=__uint128_t(x)*0x79d5f9e0de1e8cf5ULL;
        return word(product)^word(product>>64);
    };
    const word bucket=(bounded% (word(1)<<A))^(folded(quotient^hash.bucket_seed)&mask(A));
    const word selector=[&] {
        if constexpr(HT_SELECTOR_HASH && K-A>8) {
            constexpr unsigned width=K-A<=33?32:64;
            return (word(__uint128_t(suffix)*hash.selector_seed+hash.selector_bias)&mask(width))>>(width-hash.F);
        }else return folded(suffix^hash.selector_seed)&mask(hash.F);
    }();
    const byte fingerprint=byte((quotient&mask(hash.F))^selector);
    const auto actual=hash.split(key);
    assert(actual.bucket==bucket && actual.suffix==suffix && actual.fingerprint==fingerprint);
    assert(hash.invert(actual)==(key&mask(K)));
    assert(hash_mask<K>(key)==(key&mask(K)));
}
template<unsigned K> void domain() {
    for(word seed : {word(0),~word(0),word(0x5a178923cafed012ULL),word(0xa70d936cf4751028ULL)}) {
        const auto run=[&](word key) {
            check<K,1>(key,seed);
            check<K,K-1>(key,seed);
            check<K,(K<16?3:9)>(key,seed);
        };
        for(unsigned bit=0;bit<64;++bit) {
            run(word(1)<<bit);run(~(word(1)<<bit));
        }
        run(0);run(~word(0));run(mask(K));run(mask(K)-1);
        word state=seed^0x190bdde293fb7cb9ULL;
        // Exhaust narrow domains; retain unmasked high bits in wider controls.
        for(word i=0;i<(K<=16?(word(1)<<K):32768);++i) {
            state^=state<<13;state^=state>>7;state^=state<<17;
            run(K<=16?i:state);
        }
    }
}
}
int main() {
    static_assert(hash_mask<0>(~word(0))==0);
    static_assert(hash_mask<40>(~word(0))==0xffffffffffULL);
    static_assert(hash_mask<64>(~word(0))==~word(0));
    static_assert(hash_mask<256>(~word(0))==~word(0));
    domain<8>();domain<16>();domain<24>();domain<32>();
    domain<40>();domain<48>();domain<56>();domain<64>();
    std::printf("hash mask %d: all eight domains, boundary/high bits, seeds and geometries passed\n",HT_HASH_MASK);
}
