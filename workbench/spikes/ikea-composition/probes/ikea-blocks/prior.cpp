// Test/measurement comparands only, loaded from a separately pinned input.
#include <cstddef>
#include "enum_fast.h"
#if defined(__AVX512VBMI__)
#include "p2_a5_enum.h"
#endif
extern "C" unsigned prior_encode(const unsigned char* in, unsigned count, unsigned char* out) {
#if defined(__aarch64__)
    return keyset::kern::neon_enum_encode_cell(in,count,out);
#else
    return keyset::kern::x86_enum_encode_cell(in,count,out);
#endif
}
extern "C" void prior_decode(const unsigned char* in, unsigned count, unsigned char* out) {
#if defined(__aarch64__)
    keyset::kern::neon_enum_decode_body(in,count,out);
#else
    keyset::kern::x86_enum_decode_body(in,count,out);
#endif
}
#if defined(__AVX512VBMI__)
extern "C" unsigned prior_encode512(const unsigned char* in, unsigned, unsigned char* out) {
    return p2::p2_avx512_enum_encode(in,out);
}
extern "C" void prior_decode512x2(const unsigned char* a, unsigned pc_a,
                                  const unsigned char* b, unsigned pc_b, unsigned char* out) {
    auto lp = keyset::kern::enum_a5::leftpop_pair(a,pc_a,b,pc_b);
    _mm512_storeu_si512(out,keyset::kern::enum_a5::enum_pair(a,b,lp));
}
#endif
