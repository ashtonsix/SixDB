#include "pack.h"
namespace sp = ikea::seriespack;
namespace experiment = local1_experiment;
extern "C" {
__m256i candidate_region(const std::uint8_t* p) { return experiment::read_region32(p); }
__m256i current_region(const std::uint8_t* p) { return sp::avx2::read_local_region32<1>(p); }
#define FRAGMENT(L, BEGIN, COUNT) \
__m256i candidate_fragment_##L##_##BEGIN(const std::uint8_t* p) { return experiment::read_fragment<L, BEGIN, COUNT>(p); } \
__m256i current_fragment_##L##_##BEGIN(const std::uint8_t* p) { return sp::avx2::read_fragment<1, sp::geometry::local8, L, BEGIN>(p); }
FRAGMENT(1, 0, 8)
FRAGMENT(2, 0, 8)
FRAGMENT(4, 0, 8)
FRAGMENT(8, 0, 4)
FRAGMENT(8, 4, 4)
#undef FRAGMENT
#define DENSE(BITS) \
void candidate_dense_u##BITS(const std::uint8_t* p, std::uint##BITS##_t* out, std::size_t n) { experiment::decode_tiles(p, out, n); } \
void candidate_group4_u##BITS(const std::uint8_t* p, std::uint##BITS##_t* out, std::size_t n) { experiment::decode_tiles<std::uint##BITS##_t, 4>(p, out, n); } \
void current_dense_u##BITS(const std::uint8_t* p, std::uint##BITS##_t* out, std::size_t n) { sp::avx2::decode_tiles<1, sp::geometry::local8>(p, out, n); }
DENSE(8)
DENSE(16)
DENSE(32)
DENSE(64)
#undef DENSE
}
