#pragma once
#include "common.h"

// Source-level controls for the diagnosed partial-register BZHI dependency.
// Zero is the ordinary expression. This is a spike experiment, not a policy.
#ifndef HT_HASH_MASK
#define HT_HASH_MASK 0
#endif
static_assert(HT_HASH_MASK>=0 && HT_HASH_MASK<=3);
namespace ht {
template<unsigned Width>
[[gnu::always_inline]] constexpr word hash_mask(word x) {
    if constexpr(Width==0)return 0;
    else if constexpr(Width>=64)return x;
    else if constexpr(Width<=32 || HT_HASH_MASK==0)return x&mask(Width);
    else {
        if(__builtin_is_constant_evaluated() || __builtin_constant_p(x))return x&mask(Width);
#if defined(__x86_64__) && defined(__BMI2__)
        word result;
#if HT_HASH_MASK == 1
        // Separate pure operations permit hoisting the invariant full definition.
        // The opaque 64-bit input prevents a later low-byte-only count copy.
        word count;
        asm("movl %1, %k0" : "=r"(count) : "i"(Width));
        asm("bzhiq %2, %1, %0" : "=r"(result) : "r"(x), "r"(count) : "cc");
#elif HT_HASH_MASK == 2
        // Early clobber: MOV overwrites count before BZHI reads x.
        word count;
        asm("movl %3, %k1\n\tbzhiq %1, %2, %0"
            : "=r"(result), "=&r"(count) : "r"(x), "i"(Width) : "cc");
#elif HT_HASH_MASK == 3
        // Independent lowering control. A full mask register replaces the count;
        // its register pressure and lost algebraic simplification are charged.
        result=x;
        asm("andq %1, %0" : "+r"(result) : "r"(mask(Width)) : "cc");
#endif
        constexpr word upper=~mask(Width);
        __builtin_assume((result&upper)==0);
        return result;
#else
        return x&mask(Width);
#endif
    }
}
} // namespace ht
