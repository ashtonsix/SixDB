#pragma once
#include <ikea/tuplepack/description.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#define IKEA_TUPLE_CC
#elif defined(__AVX2__)
#include <immintrin.h>
#if defined(__AVX512VBMI__)
#define IKEA_TUPLE_CC
#else
#define IKEA_TUPLE_CC __attribute__((regcall))
#endif
#endif
#if defined(__aarch64__) || defined(__AVX2__)
namespace ikea::tuplepack::native {
#if defined(__aarch64__)
struct native_packet {
    uint8x16_t a, b, c, d;
};
using vector16 = uint8x16_t;
#elif defined(__AVX512VBMI__)
using native_packet = __m512i;
using vector16 = __m128i;
#else
struct native_packet {
    __m256i a, b;
};
using vector16 = __m128i;
#endif
using packet = native_packet;
} // namespace ikea::tuplepack::native
#endif
