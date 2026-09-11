#include <ikea/seriespack/native_avx512.h>
extern "C" void local6_dense(const std::uint8_t* __restrict in,std::uint8_t* __restrict out,std::size_t n) {
    ikea::seriespack::avx512::decode_tiles<6,ikea::seriespack::geometry::local8>(in,out,n/8);
}
