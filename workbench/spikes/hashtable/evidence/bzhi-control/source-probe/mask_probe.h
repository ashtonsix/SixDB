#pragma once
// Isolated source/codegen experiment. Not included by the spike implementation.
namespace source_probe {
using word = unsigned long long;
static_assert(sizeof(word) == 8);
constexpr word width_mask(unsigned width) {
    return width >= 64 ? ~word(0) : width == 0 ? 0 : (word(1) << width) - 1;
}
template<unsigned Width>
[[gnu::always_inline]] constexpr word ordinary(word x) {
    return x & width_mask(Width);
}
// Guaranteed count definition, with a separately schedulable invariant MOV.
// The two asm operations are pure: no volatile or memory clobber.
template<unsigned Width>
[[gnu::always_inline]] constexpr word explicit_split(word x) {
    if constexpr (Width == 0) return 0;
    else if constexpr (Width >= 64) return x;
    else if constexpr (Width <= 32) return ordinary<Width>(x);
    else {
        if (__builtin_is_constant_evaluated() || __builtin_constant_p(x))
            return ordinary<Width>(x);
#if defined(__x86_64__) && defined(__BMI2__)
        word count, result;
        // MOVL defines all64 bits by architectural zero extension. Output is
        // 64-bit so the next asm's register input retains full-register liveness.
        asm("movl %1, %k0" : "=r"(count) : "i"(Width));
        // One instruction: result may overlap either input (read-before-write).
        asm("bzhiq %2, %1, %0" : "=r"(result) : "r"(x), "r"(count) : "cc");
        // Restore the result's proven upper-zero fact for subsequent C++.
        constexpr word upper = ~width_mask(Width);
        __builtin_assume((result & upper) == 0);
        return result;
#else
        return ordinary<Width>(x);
#endif
    }
}
// Strict adjacency control: guarantees MOVL immediately before BZHI, but
// cannot hoist the MOV independently when x changes on every iteration.
template<unsigned Width>
[[gnu::always_inline]] constexpr word explicit_pair(word x) {
    if constexpr (Width == 0) return 0;
    else if constexpr (Width >= 64) return x;
    else if constexpr (Width <= 32) return ordinary<Width>(x);
    else {
        if (__builtin_is_constant_evaluated() || __builtin_constant_p(x))
            return ordinary<Width>(x);
#if defined(__x86_64__) && defined(__BMI2__)
        word count, result;
        // count is overwritten before x is consumed: early-clobber is required.
        // result is written only by the final instruction; ordinary =r is enough.
        asm("movl %3, %k1\n\tbzhiq %1, %2, %0"
            : "=r"(result), "=&r"(count) : "r"(x), "i"(Width) : "cc");
        constexpr word upper = ~width_mask(Width);
        __builtin_assume((result & upper) == 0);
        return result;
#else
        return ordinary<Width>(x);
#endif
    }
}
// Earlier proposal/control: only count production is explicit. The intrinsic's
// eventual operand selection/copies still belong to the compiler.
template<unsigned Width>
[[gnu::always_inline]] constexpr word count_hint(word x) {
    if constexpr (Width == 0) return 0;
    else if constexpr (Width >= 64) return x;
    else if constexpr (Width <= 32) return ordinary<Width>(x);
    else {
        if (__builtin_is_constant_evaluated() || __builtin_constant_p(x))
            return ordinary<Width>(x);
#if defined(__x86_64__) && defined(__BMI2__)
        unsigned count;
        asm("movl %1, %0" : "=r"(count) : "i"(Width));
        return __builtin_ia32_bzhi_di(x, count);
#else
        return ordinary<Width>(x);
#endif
    }
}
// Dynamic-width semantic control, not a proposed new hashtable API. Guard the
// full unsigned width before BZHI's low8 count behavior can truncate it.
[[gnu::always_inline]] inline word runtime_width(word x, unsigned width) {
    if (width >= 64) return x;
    if (width == 0) return 0;
#if defined(__x86_64__) && defined(__BMI2__)
    word count, result;
    asm("movl %k3, %k1\n\tbzhiq %1, %2, %0"
        : "=r"(result), "=&r"(count) : "r"(x), "r"(width) : "cc");
    return result;
#else
    return x & width_mask(width);
#endif
}
static_assert(explicit_split<0>(~word(0)) == 0);
static_assert(explicit_split<40>(~word(0)) == 0xffffffffffULL);
static_assert(explicit_split<64>(~word(0)) == ~word(0));
static_assert(explicit_split<256>(~word(0)) == ~word(0));
static_assert(explicit_pair<40>(0x123456789abcdef0ULL) == 0x789abcdef0ULL);
} // namespace source_probe
