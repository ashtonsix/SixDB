#include "codec.h"
#if defined(__aarch64__)
#include "native_neon.h"
#else
#include "native_avx512.h"
#endif
namespace ikea_probe {
unsigned encode_native(const std::uint8_t* plain, unsigned cardinality, std::uint8_t* out) {
#if defined(__aarch64__)
    return neon::encode(neon::load256(plain),cardinality,out);
#else
    return avx512::encode(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(plain)),cardinality,out);
#endif
}
unsigned decode_native(const std::uint8_t* body, unsigned cardinality, std::uint8_t* plain) {
    unsigned bits;
#if defined(__aarch64__)
    neon::store256(plain,neon::decode(body,cardinality,bits));
#else
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(plain),avx512::decode(body,cardinality,bits));
#endif
    return bits;
}
void decode_native2(const std::uint8_t* a,unsigned pop_a,const std::uint8_t* b,unsigned pop_b,std::uint8_t* plain) {
    unsigned bits_a,bits_b;
#if defined(__aarch64__)
    neon::store256(plain,neon::decode(a,pop_a,bits_a));
    neon::store256(plain+32,neon::decode(b,pop_b,bits_b));
#else
    _mm512_storeu_si512(plain,avx512::decode2(a,pop_a,b,pop_b,bits_a,bits_b));
#endif
}
}
