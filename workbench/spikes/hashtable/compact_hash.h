#pragma once
#include "hash.h"
#include <type_traits>

namespace ht::state_experiment {

namespace detail {
struct empty_bucket { bool operator==(const empty_bucket&)const=default; };
struct bucket_seed { word value; bool operator==(const bucket_seed&)const=default; };
template<bool Address,bool Suffix> struct seeds {
    word input;
    [[no_unique_address]] std::conditional_t<Address,bucket_seed,empty_bucket> bucket;
    std::conditional_t<Suffix,word,byte> selector;
    bool operator==(const seeds&)const=default;
};
struct xor_byte { byte value; bool operator==(const xor_byte&)const=default; };
} // namespace detail

// The mapping is exactly permutation<K,A>, including its full-width seed
// derivation. This only removes state that the selected geometry cannot read.
// It is not a new hash family or a claim about adversarial resistance.
template<unsigned K,unsigned A> class compact_permutation {
    static_assert(A<K && K<=64);
  public:
    static constexpr unsigned R=K-A,F=std::min(8u,R),C=R-F;
    using parts=typename permutation<K,A>::parts;
  private:
    static constexpr bool byte_xor=A==0 && C==0;
    [[no_unique_address]] std::conditional_t<byte_xor,detail::xor_byte,
        detail::seeds<A!=0,C!=0>> state_;
    [[gnu::always_inline]] static word fold(word x) {return permutation<K,A>::fold(x);}
    [[gnu::always_inline]] word selector(word suffix)const requires(!byte_xor) {
        if constexpr(C==0)return state_.selector;
        else return fold(suffix^state_.selector)&mask(F);
    }
  public:
    explicit compact_permutation(word entropy) {
        const word selector_seed=mix(entropy^0x73656c6563742d32ULL);
        if constexpr(byte_xor) {
            // With no address or suffix, the two XORs compose into one byte.
            state_.value=byte((entropy&mask(K))^(fold(selector_seed)&mask(F)));
        } else {
            state_.input=entropy&mask(K);
            if constexpr(A!=0)state_.bucket.value=mix(entropy^0x6275636b65742d31ULL);
            if constexpr(C!=0)state_.selector=selector_seed;
            else state_.selector=byte(fold(selector_seed)&mask(F));
        }
    }
    bool operator==(const compact_permutation&)const=default;
    /// Transient facts for the existing SIMD packet lowering. Unused geometry
    /// fields are constant; callers inline this binding instead of storing it.
    [[gnu::always_inline]] permutation<K,A> expanded()const {
        permutation<K,A> result(0);
        if constexpr(byte_xor) {
            result.input_seed=state_.value;
            result.bucket_seed=0;result.selector_seed=0;result.selector_bias=0;
        } else {
            result.input_seed=state_.input;
            if constexpr(A!=0)result.bucket_seed=state_.bucket.value;
            else result.bucket_seed=0;
            if constexpr(C!=0) {
                result.selector_seed=state_.selector;
                result.selector_bias=fold(state_.selector)&mask(F);
            } else {
                result.selector_seed=0;result.selector_bias=state_.selector;
            }
        }
        return result;
    }
    [[gnu::always_inline]] parts split(word key)const {
        if constexpr(byte_xor)return {0,0,byte((key&mask(K))^state_.value)};
        else {
            const word x=(key^state_.input)&mask(K),r=x>>A,s=r>>F;
            const word bucket=[&] {
                if constexpr(A==0)return word(0);
                else return (x&mask(A))^(fold(r^state_.bucket.value)&mask(A));
            }();
            return {bucket,s,byte((r&mask(F))^selector(s))};
        }
    }
    [[gnu::always_inline]] word invert(parts p)const {
        if constexpr(byte_xor)return word(p.fingerprint^state_.value);
        else {
            const word r=(p.suffix<<F)|(p.fingerprint^selector(p.suffix));
            const word low=[&] {
                if constexpr(A==0)return p.bucket;
                else return p.bucket^(fold(r^state_.bucket.value)&mask(A));
            }();
            return ((r<<A)|low)^state_.input;
        }
    }
};

static_assert(sizeof(compact_permutation<8,0>)==1);
static_assert(sizeof(compact_permutation<16,0>)==16);
static_assert(sizeof(compact_permutation<64,0>)==16);
static_assert(sizeof(compact_permutation<64,32>)==24);
static_assert(sizeof(compact_permutation<64,56>)==24);

} // namespace ht::state_experiment
