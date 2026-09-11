#include <ikea/seriespack/native_avx2.h>
#if !defined(__AVX2__) || defined(__GFNI__) || defined(__AVX512F__)
#error This witness is restricted to the AVX2-only feature ceiling.
#endif
#define WITNESS(Name, U) \
extern "C" void Name(const U* __restrict input, std::uint8_t* __restrict output, std::size_t n) { \
    ikea::seriespack::avx2::encode_low_tiles<4,ikea::seriespack::geometry::local8>(input,output,n/8); \
}
WITNESS(local4_u8,std::uint8_t)
WITNESS(local4_u16,std::uint16_t)
WITNESS(local4_u32,std::uint32_t)
WITNESS(local4_u64,std::uint64_t)
