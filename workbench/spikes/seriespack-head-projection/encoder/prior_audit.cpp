// Additional whole-operation control; external pinned Calico source only.
#include <algorithm>
#include <bit>
#include <bytepack.h>
#pragma clang attribute push(__attribute__((always_inline)), apply_to=function)
#include <planes.h>
#pragma clang attribute pop

namespace bp = bytepack;
#if defined(__AVX512VL__) && defined(__AVX512BW__)
inline constexpr auto prior_target = bp::Target::avx512;
#else
inline constexpr auto prior_target = bp::Target::avx2;
#endif
template<unsigned K,bp::Layout Layout>
[[gnu::always_inline]] inline void prior(const std::uint64_t* __restrict in,
    std::uint8_t* __restrict out,std::size_t n) {
    static constexpr bp::planes::Shape shape{K};
    bp::assume(n%256 == 0);
    for (;n!=0;n-=256,in+=256,out+=shape.bytes())
        bp::planes::Codec<Layout,prior_target>::set256({out,out+shape.body_bytes,&shape},in);
}
extern "C" [[gnu::noinline]] void calico_encode17_scan(const std::uint64_t* in,std::uint8_t* out,std::size_t n) {
    prior<17,bp::Layout::interleaved>(in,out,n);
}
extern "C" [[gnu::noinline]] void calico_encode23_scan(const std::uint64_t* in,std::uint8_t* out,std::size_t n) {
    prior<23,bp::Layout::interleaved>(in,out,n);
}
extern "C" [[gnu::noinline]] void calico_encode56_local(const std::uint64_t* in,std::uint8_t* out,std::size_t n) {
    prior<56,bp::Layout::bitplanes>(in,out,n);
}
