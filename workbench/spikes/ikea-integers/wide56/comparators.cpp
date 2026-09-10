#include "comparators.h"
#include <planes.h>
#include <array>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#else
#include <immintrin.h>
#endif

namespace ikea::integers::wide56::study {
#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE2_BITPERM)
constexpr auto target=bytepack::Target::sve2;
#elif defined(__aarch64__)
constexpr auto target=bytepack::Target::neon;
#elif defined(__AVX512F__)
constexpr auto target=bytepack::Target::avx512;
#else
constexpr auto target=bytepack::Target::avx2;
#endif
using Prior=bytepack::planes::Codec<bytepack::Layout::bitplanes,target>;
constexpr bytepack::planes::Shape shape(56);
static_assert(shape.count==4 && shape.bytes()==1792 && shape.residual==0);
extern "C" std::uint64_t wide56_prior_get1(const std::uint8_t* p,unsigned i) {
    return Prior::get1({p,p+1792,&shape},i);
}
extern "C" void wide56_prior_get16(const std::uint8_t* __restrict p,unsigned i,std::uint64_t* __restrict out) {
    Prior::get16({p,p+1792,&shape},i,out);
}
extern "C" void wide56_prior_decode256(const std::uint8_t* __restrict p,std::uint64_t* __restrict out) {
    Prior::get256({p,p+1792,&shape},out);
}
extern "C" void wide56_prior_encode256(const std::uint64_t* __restrict in,std::uint8_t* __restrict p) {
    Prior::set256({p,p+1792,&shape},in);
}
extern "C" std::uint64_t wide56_prior_sum256(const std::uint8_t* p) {
    alignas(64) std::uint64_t values[256];
    Prior::get256({p,p+1792,&shape},values);
    std::uint64_t result=0; for(auto v:values) result+=v; return result;
}

// Fresh fixed-width control over the prior's exact wire. This removes its
// Shape walk and repeated output reconstruction; it is not lifted prior code.
inline std::uint64_t plane_value(const std::uint8_t* p,unsigned i) {
    std::uint32_t low; std::memcpy(&low,p+768+4*i,4);
    return (std::uint64_t(p[i])<<48) | (std::uint64_t(p[256+i])<<40) |
        (std::uint64_t(p[512+i])<<32) | low;
}
namespace {
#if defined(__AVX512F__)
using PlaneFragment=__m512i;
constexpr unsigned plane_fragment_values=8;
inline PlaneFragment plane_fragment(const std::uint8_t* p,unsigned i) {
    const auto a=_mm512_cvtepu8_epi64(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p+i)));
    const auto b=_mm512_cvtepu8_epi64(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p+256+i)));
    const auto c=_mm512_cvtepu8_epi64(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p+512+i)));
    const auto low=_mm512_cvtepu32_epi64(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p+768+4*i)));
    return _mm512_or_si512(_mm512_or_si512(_mm512_slli_epi64(a,48),_mm512_slli_epi64(b,40)),
        _mm512_or_si512(_mm512_slli_epi64(c,32),low));
}
inline void store_plane_fragment(std::uint64_t* p,PlaneFragment v) {_mm512_storeu_si512(p,v);}
#elif defined(__AVX2__)
using PlaneFragment=__m256i;
constexpr unsigned plane_fragment_values=4;
inline PlaneFragment plane_byte_fragment(const std::uint8_t* p) {
    std::uint32_t bytes;std::memcpy(&bytes,p,4);
    return _mm256_cvtepu8_epi64(_mm_cvtsi32_si128(int(bytes)));
}
inline PlaneFragment plane_fragment(const std::uint8_t* p,unsigned i) {
    const auto a=plane_byte_fragment(p+i),b=plane_byte_fragment(p+256+i),c=plane_byte_fragment(p+512+i);
    const auto low=_mm256_cvtepu32_epi64(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p+768+4*i)));
    return _mm256_or_si256(_mm256_or_si256(_mm256_slli_epi64(a,48),_mm256_slli_epi64(b,40)),
        _mm256_or_si256(_mm256_slli_epi64(c,32),low));
}
inline void store_plane_fragment(std::uint64_t* p,PlaneFragment v) {_mm256_storeu_si256(reinterpret_cast<__m256i*>(p),v);}
#else
using PlaneFragment=uint64x2_t;
constexpr unsigned plane_fragment_values=2;
template<unsigned Byte> inline uint8x16_t plane_byte_fragment(const std::uint8_t* p) {
    constexpr auto indices=[] {
        std::array<std::uint8_t,16> r;r.fill(255);r[Byte]=0;r[Byte+8]=1;return r;
    }();
    const auto pair=vreinterpretq_u8_u16(vld1q_dup_u16(reinterpret_cast<const std::uint16_t*>(p)));
    return vqtbl1q_u8(pair,vld1q_u8(indices.data()));
}
inline PlaneFragment plane_fragment(const std::uint8_t* p,unsigned i) {
    const auto a=plane_byte_fragment<6>(p+i),b=plane_byte_fragment<5>(p+256+i),c=plane_byte_fragment<4>(p+512+i);
    const auto low=vreinterpretq_u8_u64(vmovl_u32(vld1_u32(reinterpret_cast<const std::uint32_t*>(p+768+4*i))));
    return vreinterpretq_u64_u8(vorrq_u8(vorrq_u8(a,b),vorrq_u8(c,low)));
}
inline void store_plane_fragment(std::uint64_t* p,PlaneFragment v) {vst1q_u64(p,v);}
#endif
}
extern "C" std::uint64_t wide56_planes_get1(const std::uint8_t* p,unsigned i) {
    __builtin_assume(i<256); return plane_value(p,i);
}
extern "C" void wide56_planes_get16(const std::uint8_t* __restrict p,unsigned i,std::uint64_t* __restrict out) {
    __builtin_assume(i<256 && i%16==0);
    for(unsigned j=0;j<16;j+=plane_fragment_values)
        store_plane_fragment(out+j,plane_fragment(p,i+j));
}
extern "C" void wide56_planes_decode256(const std::uint8_t* __restrict p,std::uint64_t* __restrict out) {
    for(unsigned i=0;i<256;++i) out[i]=plane_value(p,i);
}
extern "C" void wide56_planes_encode256(const std::uint64_t* __restrict in,std::uint8_t* __restrict p) {
    for(unsigned i=0;i<256;++i) {
        const auto v=in[i]; p[i]=std::uint8_t(v>>48); p[256+i]=std::uint8_t(v>>40);
        p[512+i]=std::uint8_t(v>>32); const auto low=std::uint32_t(v);
        std::memcpy(p+768+4*i,&low,4);
    }
}
extern "C" std::uint64_t wide56_planes_sum256(const std::uint8_t* p) {
    std::uint64_t result=0; for(unsigned i=0;i<256;++i) result+=plane_value(p,i); return result;
}
extern "C" std::uint64_t wide56_plain_get1(const std::uint8_t* p,unsigned i) {
    __builtin_assume(i<256); std::uint64_t v;std::memcpy(&v,p+8*i,8);return v;
}
extern "C" void wide56_plain_get16(const std::uint8_t* p,unsigned i,std::uint64_t* out) {
    __builtin_assume(i<256 && i%16==0);std::memcpy(out,p+8*i,128);
}
extern "C" void wide56_plain_decode256(const std::uint8_t* p,std::uint64_t* out) {std::memcpy(out,p,2048);}
extern "C" void wide56_plain_encode256(const std::uint64_t* in,std::uint8_t* p) {std::memcpy(p,in,2048);}
extern "C" std::uint64_t wide56_plain_sum256(const std::uint8_t* p) {
    std::uint64_t result=0;for(unsigned i=0;i<256;++i) result+=wide56_plain_get1(p,i);return result;
}
const Arm prior{"prior_shape",{wide56_prior_get1,wide56_prior_get16,wide56_prior_decode256,wide56_prior_encode256,wide56_prior_sum256},Wire::planes,1792};
const Arm fixed_planes{"fixed_planes",{wide56_planes_get1,wide56_planes_get16,wide56_planes_decode256,wide56_planes_encode256,wide56_planes_sum256},Wire::planes,1792};
const Arm plain{"plain_u64",{wide56_plain_get1,wide56_plain_get16,wide56_plain_decode256,wide56_plain_encode256,wide56_plain_sum256},Wire::plain,2048};
}
