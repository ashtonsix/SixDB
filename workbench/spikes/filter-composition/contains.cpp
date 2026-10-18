#include <immintrin.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>

// Eight readable bytes. The first R contain Local bitplanes; the other bytes
// can contain arbitrary owner data. Prepared operands are charged separately.
extern "C" [[gnu::noinline]] bool contains_local_prepared(
    const std::uint8_t* p,__m128i basis,__m128i reversed_query,__m128i significant) {
    std::uint64_t raw; __builtin_memcpy(&raw,p,8);
    const auto values=_mm_gf2p8affine_epi64_epi8(basis,_mm_set1_epi64x(raw),0);
    return _mm_testn_epi8_mask(_mm_xor_si128(values,reversed_query),significant)!=0;
}
extern "C" [[gnu::noinline]] bool contains6_runtime(const std::uint8_t* p,unsigned query) {
    if(query>=64) return false;
    const auto basis=_mm_set1_epi64x(0x8040201008040201ULL);
    const auto reversed=_mm_gf2p8affine_epi64_epi8(_mm_set1_epi8(query),basis,0);
    std::uint64_t raw; __builtin_memcpy(&raw,p,8);
    const auto values=_mm_gf2p8affine_epi64_epi8(basis,_mm_set1_epi64x(raw),0);
    return _mm_testn_epi8_mask(_mm_xor_si128(values,reversed),_mm_set1_epi8(0xfc))!=0;
}
int main() {
    std::mt19937_64 rng(0xa190fe);
    const auto basis=_mm_set1_epi64x(0x8040201008040201ULL);
    for(unsigned r=1;r<=7;++r) for(unsigned trial=0;trial<4096;++trial) {
        std::uint8_t planes[8]; for(auto& p:planes) p=rng();
        for(unsigned query=0;query<(1u<<r);++query) {
            unsigned expected=0;
            for(unsigned row=0;row<8;++row) {
                unsigned value=0;
                for(unsigned bit=0;bit<r;++bit) value|=((planes[bit]>>row)&1)<<bit;
                expected|=unsigned(value==query)<<row;
            }
            const auto reversed=_mm_gf2p8affine_epi64_epi8(_mm_set1_epi8(query),basis,0);
            assert(contains_local_prepared(planes,basis,reversed,_mm_set1_epi8(255u<<(8-r)))==bool(expected));
            if(r==6) assert(contains6_runtime(planes,query)==bool(expected));
        }
    }
    std::uint8_t zero[8]{};
    assert(!contains6_runtime(zero,64));
    std::puts("GFNI Local contains: R1..7, every in-domain query, 4096 randomized tiles per width, arbitrary extra bytes passed");
}
