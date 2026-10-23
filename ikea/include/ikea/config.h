#pragma once
// Current execution profiles. Ordinary contracts use scalar/byte types; adding
// another backend does not require changing those signatures or wire laws.
#if defined(__aarch64__)
# if !defined(__ARM_NEON)
#  error "Ikea's current ARM profile requires armv8-a+simd"
# endif
#elif defined(__x86_64__)
# if !defined(__AVX2__)
#  error "Ikea's current x86 profile requires x86-64-v3 or newer"
# endif
#else
# error "Ikea needs an execution backend for this target (currently x86-64-v3 / armv8-a+simd)"
#endif
