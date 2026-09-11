#pragma once
#include <ikea/seriespack/native_avx512.h>

#if !defined(__AVX512BW__) || !defined(__AVX512VBMI__) || !defined(__GFNI__)
#error The Local6 diagnostic requires the full AVX512/GFNI profile.
#endif
namespace local6_experiment {
namespace sp = ikea::seriespack;

// These controls retain the current leaf's exact 48-byte permission, shuffle,
// GFNI transform and 64-value store. Only enclosing loop grain changes.
template<unsigned RegionValues>
[[gnu::always_inline]] inline void dense(const std::uint8_t* __restrict input,
    std::uint8_t* __restrict output, std::size_t tiles) {
    static_assert(RegionValues==0 || RegionValues==64 || RegionValues==256 || RegionValues==512);
    if constexpr (RegionValues==0)
        sp::avx512::decode_tiles<6,sp::geometry::local8>(input,output,tiles);
    else {
        constexpr unsigned Regions=RegionValues/64,Tiles=RegionValues/8;
        std::size_t tile=0;
#pragma clang loop unroll(disable)
        for (; tiles-tile>=Tiles;tile+=Tiles) {
            sp::detail::static_for<Regions>([&](auto part) {
                _mm512_storeu_si512(output+(tile+part*8)*8,
                    sp::avx512::read_local_region64<6>(input+(tile+part*8)*6));
            });
        }
#pragma clang loop unroll(disable)
        for (;tiles-tile>=8;tile+=8)
            _mm512_storeu_si512(output+tile*8,sp::avx512::read_local_region64<6>(input+tile*6));
        for (;tile<tiles;++tile)
            sp::avx512::decode_tile<6,sp::geometry::local8>(input+tile*6,output+tile*8);
    }
}

template<unsigned RegionValues>
[[gnu::always_inline]] inline void decode(const std::uint8_t* __restrict input,
    std::uint8_t* __restrict output,std::size_t n,std::size_t stride=6) {
    const auto full=n/8;
    if (stride==6) dense<RegionValues>(input,output,full);
    else for (std::size_t tile=0;tile<full;++tile)
        sp::avx512::decode_tile<6,sp::geometry::local8>(input+tile*stride,output+tile*8);
    if (const auto left=n%8;left!=0) {
        std::array<std::uint8_t,8> boundary{};
        sp::avx512::decode_tile<6,sp::geometry::local8>(input+full*stride,boundary.data());
        std::memcpy(output+full*8,boundary.data(),left);
    }
}
}
