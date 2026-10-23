#include <ikea/detail/filter/compare.h>
#include <ikea/tuplepack/author/filter.h>
#include <ikea/tuplepack/filter.h>
#include "../packed-filters/baseline.h"

// Keep runtime operands at the ABI boundary; inspect these and complete callers.
#define WITNESS(V, T, B, O) \
extern "C" [[gnu::noinline]] std::uint64_t mask_##V##_##B##_##O(T x, std::uint64_t q) { \
    return ikea::detail::filter::bits<B, ikea::comparison(O)>(x, q); \
}
#define OPS(V, T, B) WITNESS(V,T,B,0) WITNESS(V,T,B,1) WITNESS(V,T,B,2) WITNESS(V,T,B,3) WITNESS(V,T,B,4)
#define WIDTHS(V, T) OPS(V,T,1) OPS(V,T,2) OPS(V,T,4) OPS(V,T,8)
#if defined(__aarch64__)
WIDTHS(128, uint8x16_t)
#elif defined(__AVX2__)
WIDTHS(128, __m128i)
WIDTHS(256, __m256i)
#if defined(__AVX512BW__) && defined(__AVX512VL__)
WIDTHS(512, __m512i)
#endif
#endif

#define TUPLE(N, O) \
extern "C" [[gnu::noinline]] std::uint64_t tuple_##N##_##O(ikea::tuplepack::native::packet_for<N> x, std::uint64_t q, std::uint64_t active) { \
    return ikea::tuplepack::author::filter_values<ikea::comparison(O), N>(x, q, active); \
}
TUPLE(8,0) TUPLE(8,1) TUPLE(8,2) TUPLE(8,3) TUPLE(8,4)
TUPLE(64,0) TUPLE(64,1) TUPLE(64,2) TUPLE(64,3) TUPLE(64,4)

#define CONTROL(K, O) \
extern "C" [[gnu::noinline]] unsigned control_##K##_##O(const std::uint8_t* p, std::uint64_t q) { \
    return control::run<K, O>(p, q); \
}
#define CONTROL_OPS(K) CONTROL(K,0) CONTROL(K,1) CONTROL(K,2) CONTROL(K,3) CONTROL(K,4)
CONTROL_OPS(8) CONTROL_OPS(16) CONTROL_OPS(32) CONTROL_OPS(64)
