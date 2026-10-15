#pragma once
#include "common.h"
#include "hash_mask.h"
#ifndef HT_SELECTOR_HASH
#define HT_SELECTOR_HASH 0
#endif
static_assert(HT_SELECTOR_HASH==0 || HT_SELECTOR_HASH==1);
namespace ht {
// Reviewed quotient permutation retained as the attribution control only.
// Fresh independent entropy for each tier is supplied by the caller. This is
// reversible but carries no cryptographic PRP / seed-hiding claim.
template<unsigned K,unsigned A,bool MultiplySelector=HT_SELECTOR_HASH> struct permutation {
    static_assert(A<K && K<=64);
    static constexpr unsigned R=K-A, F=std::min(8u,R), C=R-F;
    // Selector-only control. The multiply-add family needs W>=C+F-1;
    // 32-bit arithmetic therefore suffices for many current growth levels.
    // Expanding a single entropy word here does not establish independently
    // uniform parameters or an adaptive-adversary guarantee.
    static constexpr unsigned selector_word_bits=C+F<=33?32:64;
    word input_seed,bucket_seed,selector_seed,selector_bias;
    bool operator==(const permutation&)const=default;
    [[gnu::always_inline]] static word fold(word x) {
        __uint128_t p=__uint128_t(x)*0x79d5f9e0de1e8cf5ULL;
        return word(p)^word(p>>64);
    }
    explicit permutation(word entropy):input_seed(entropy&mask(K)),
      bucket_seed(mix(entropy^0x6275636b65742d31ULL)),
      selector_seed(mix(entropy^0x73656c6563742d32ULL)),
      selector_bias(MultiplySelector && C!=0?mix(entropy^0x626961732d73656cULL):fold(selector_seed)&mask(F)) {}
    [[gnu::always_inline]] word selector_zero()const {
        if constexpr(!MultiplySelector || C==0)return selector_bias;
        else return (selector_bias&mask(selector_word_bits))>>(selector_word_bits-F);
    }
    [[gnu::always_inline]] word selector(word suffix)const {
        if constexpr(C==0)return selector_zero();
        else if constexpr(MultiplySelector) {
            if constexpr(selector_word_bits==32)
                return std::uint32_t(std::uint32_t(suffix)*std::uint32_t(selector_seed)+std::uint32_t(selector_bias))>>(32-F);
            else return (suffix*selector_seed+selector_bias)>>(64-F);
        }
        else return fold(suffix^selector_seed)&mask(F);
    }
    struct parts { word bucket,suffix; byte fingerprint; };
    [[gnu::always_inline]] parts split(word key)const {
        word x=hash_mask<K>(key^input_seed), r=x>>A, s=r>>F;
        return {(x&mask(A))^(fold(r^bucket_seed)&mask(A)),s,
                byte((r&mask(F))^selector(s))};
    }
    word invert(parts p)const {
        word r=(p.suffix<<F)|(p.fingerprint^selector(p.suffix));
        word low=p.bucket^(fold(r^bucket_seed)&mask(A));
        return ((r<<A)|low)^input_seed;
    }
};
} // namespace ht
