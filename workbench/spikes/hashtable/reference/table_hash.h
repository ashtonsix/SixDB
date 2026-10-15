#pragma once
#include "map.h"

namespace hashtable_spike::tiered {
// One whole-container alternative to the strong permutation control. This is
// not a cryptographic PRP claim. Growth receives fresh caller entropy; neither
// constructor nor the inverse invents an entropy source or updates an old seed.
template<unsigned K,unsigned A,bool MixSelector=true> struct quotient_hash {
    static_assert(A<K);
    static constexpr unsigned R=K-A,F=std::min(8u,R),C=R-F;
    word input_seed,bucket_seed,selector_seed,exact_selector;
    static constexpr word multiplier=0x79d5f9e0de1e8cf5ULL;
    [[gnu::always_inline]] static word fold_product(word x) {
        const __uint128_t p=__uint128_t(x)*multiplier;
        return word(p)^word(p>>64);
    }
    explicit quotient_hash(word entropy):input_seed(entropy&mask(K)),
        bucket_seed(mix(entropy^0x6275636b65742d31ULL)),
        selector_seed(mix(entropy^0x73656c6563742d32ULL)),
        exact_selector(fold_product(selector_seed)&mask(F)) {}
    [[gnu::always_inline]] word selector_mix(word suffix)const {
        if constexpr(!MixSelector)return 0;
        else if constexpr(C==0)return exact_selector;
        else return fold_product(suffix^selector_seed)&mask(F);
    }
    [[gnu::always_inline]] word operator()(word key)const {
        const word x=(key^input_seed)&mask(K),r=x>>A,suffix=r>>F;
        const word b=(x&mask(A))^(fold_product(r^bucket_seed)&mask(A));
        const word fp=(r&mask(F))^selector_mix(suffix);
        if constexpr(A==0)return (fp<<C)|suffix;
        else return (b<<R)|(fp<<C)|suffix;
    }
    word invert(word h)const {
        const word suffix=h&mask(C),fp=(h>>C)&mask(F);
        const word r=(suffix<<F)|(fp^selector_mix(suffix));
        if constexpr(A==0)return r^input_seed;
        else {
            const word lo=(h>>R)^(fold_product(r^bucket_seed)&mask(A));
            return ((r<<A)|lo)^input_seed;
        }
    }
};
} // namespace hashtable_spike::tiered
