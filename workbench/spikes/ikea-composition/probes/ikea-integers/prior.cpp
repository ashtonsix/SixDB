#include "codec.h"
#include <bytepack.h>

namespace ikea::integers {
#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE2_BITPERM)
constexpr auto target=bytepack::Target::sve2;
#elif defined(__aarch64__)
constexpr auto target=bytepack::Target::neon;
#elif defined(__AVX512F__)
constexpr auto target=bytepack::Target::avx512;
#else
constexpr auto target=bytepack::Target::avx2;
#endif
template<Layout L,unsigned K> constexpr Codec prior() {
    constexpr auto layout=L==Layout::local?bytepack::Layout::bitplanes:bytepack::Layout::interleaved;
    using F=bytepack::Kernel<layout,K,target>;
    return {F::get1,F::get16,F::get256,[](const uint8_t* in,uint8_t* out){F::set256(out,in);}};
}
template<Layout L> const std::array<Codec,7> prior_bank={prior<L,1>(),prior<L,2>(),prior<L,3>(),prior<L,4>(),prior<L,5>(),prior<L,6>(),prior<L,7>()};
const std::array<Codec,7>& prior_codecs(Layout l) {return l==Layout::local?prior_bank<Layout::local>:prior_bank<Layout::scan>;}
} // namespace ikea::integers
