#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#if defined(__aarch64__)
#include "native_neon.h"
#else
#include "native_avx512.h"
#endif

namespace ikea_probe {
// Driver policy: expand only the small repetitions, keep larger extents as
// one ordinary loop. A new storage extent does not create a new kernel TU.
template<std::size_t Count, class Body>
inline void repeat_tiles(Body body) {
    if constexpr(Count <= 4) {
        [&]<std::size_t... I>(std::index_sequence<I...>) { (body(I),...); }(std::make_index_sequence<Count>{});
    } else {
        for(std::size_t i=0;i<Count;++i) body(i);
    }
}
enum class Bitwise { intersection, set_union };
// A contiguous placement adapter for either flat or tiled physical definitions.
// The caller establishes the same ordered bit coordinates on a/b/output.
// Exact in-place output is supported; arbitrary partial overlap is not.
template<Bitwise Operation,std::size_t Positions>
inline void bitwise_contiguous(const void* a,const void* b,void* output) {
    static_assert(Positions > 0 && Positions%64 == 0);
    auto* x=static_cast<const std::uint8_t*>(a);
    auto* y=static_cast<const std::uint8_t*>(b);
    auto* out=static_cast<std::uint8_t*>(output);
#if defined(__aarch64__)
    constexpr std::size_t grain=256;
    repeat_tiles<Positions/grain>([&](auto i) {
        auto left=neon::load256(x+i*32),right=neon::load256(y+i*32);
        if constexpr(Operation==Bitwise::intersection) neon::store256(out+i*32,neon::intersection(left,right));
        else neon::store256(out+i*32,neon::set_union(left,right));
    });
#else
    constexpr std::size_t grain=512;
    repeat_tiles<Positions/grain>([&](auto i) {
        auto left=avx512::load512(x+i*64),right=avx512::load512(y+i*64);
        if constexpr(Operation==Bitwise::intersection) avx512::store512(out+i*64,avx512::intersection512(left,right));
        else avx512::store512(out+i*64,avx512::union512(left,right));
    });
#endif
    // The short remainder is physical traversal, not another logical operator.
    for(std::size_t i=(Positions/grain)*(grain/64);i<Positions/64;++i) {
        std::uint64_t left,right,value;
        std::memcpy(&left,x+i*8,8); std::memcpy(&right,y+i*8,8);
        if constexpr(Operation==Bitwise::intersection) value=left&right;
        else value=left|right;
        std::memcpy(out+i*8,&value,8);
    }
}
}
