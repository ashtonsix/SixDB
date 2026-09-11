#pragma once
#include <ikea/seriespack/native_neon.h>

namespace candidate {
namespace sp=ikea::seriespack;
namespace b=sp::neon::native_detail::bytes;
namespace nd=sp::neon::native_detail;

template<unsigned W>
[[gnu::always_inline]] inline void encode64(const std::uint8_t* __restrict in,
                                           std::uint8_t* __restrict out) {
    static_assert(W>=3 && W<=7);
    std::array<uint8x16_t,4> matrix;
    sp::detail::static_for<4>([&](auto part){
        matrix[part]=nd::transpose_pair(vld1q_u8(in+16*part));
    });
    sp::detail::static_for<(8*W+15)/16>([&](auto chunk){
        constexpr unsigned begin=16*chunk;
        constexpr unsigned count=std::min<unsigned>(16,8*W-begin);
        constexpr unsigned first=begin/W/2, last=(begin+count-1)/W/2;
        static constexpr auto indices=[] {
            std::array<std::uint8_t,16> a{};a.fill(255);
            for(unsigned byte=0;byte<count;++byte)
                a[byte]=(begin+byte)/W*8+(begin+byte)%W-first*16;
            return a;
        }();
        b::store_bytes<count>(out+begin,b::table<first,last-first+1>(matrix,vld1q_u8(indices.data())));
    });
}

template<unsigned W>
[[gnu::always_inline]] inline void decode64(const std::uint8_t* __restrict in,
                                           std::uint8_t* __restrict out) {
    static_assert(W>=3 && W<=7);
    std::array<uint8x16_t,(8*W+15)/16> encoded;
    sp::detail::static_for<(8*W+15)/16>([&](auto chunk){
        constexpr unsigned count=std::min<unsigned>(16,8*W-16*chunk);
        encoded[chunk]=b::load_bytes<count>(in+16*chunk);
    });
    sp::detail::static_for<4>([&](auto part){
        constexpr unsigned first=part*2*W/16,last=((part+1)*2*W-1)/16;
        static constexpr auto indices=[] {
            std::array<std::uint8_t,16> a{};a.fill(255);
            for(unsigned byte=0;byte<16;++byte)
                if(byte%8<W)a[byte]=(decltype(part)::value*2+byte/8)*W+byte%8-first*16;
            return a;
        }();
        const auto matrix=b::table<first,last-first+1>(encoded,vld1q_u8(indices.data()));
        vst1q_u8(out+part*16,nd::transpose_pair(matrix));
    });
}
}
