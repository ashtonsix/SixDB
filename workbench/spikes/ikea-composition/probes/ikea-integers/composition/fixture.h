#pragma once
#include "prepared.h"
#include <algorithm>
#include <cassert>
#include <span>

namespace ikea::integers::composition::fixture {
// Scalar test/benchmark fixture writer, deliberately outside the kernel library.
inline void encode(ParentKind parent,std::span<const std::uint16_t,64> values,std::uint8_t* out) {
    std::fill_n(out,96,0);
    for(unsigned i=0;i<64;++i) {
        assert(values[i]<4096);
        if(parent==ParentKind::packets8) {
            out[(i/8)*12+i%8]=values[i]>>4;
            for(unsigned b=0;b<4;++b) out[(i/8)*12+8+b]|=((values[i]>>b)&1)<<(i%8);
        } else {
            const bool middle=parent==ParentKind::body32_tail32_body32;
            out[i+(middle && i>=32?32:0)]=values[i]>>4;
            out[(middle?32:64)+i%32]|=(values[i]&15)<<(4*(i/32));
        }
    }
}
inline std::uint64_t sum(std::span<const std::uint16_t> values,unsigned cutoff) {
    std::uint64_t result=0;for(auto value:values)if(value<cutoff)result+=value;return result;
}
inline std::vector<unsigned> footprint(ParentKind parent,unsigned first,unsigned count) {
    std::array<bool,96> used{};
    for(unsigned i=first;i<first+count;++i) {
        if(parent==ParentKind::packets8) {
            used[(i/8)*12+i%8]=true;
            for(unsigned b=0;b<4;++b)used[(i/8)*12+8+b]=true;
        } else {
            const bool middle=parent==ParentKind::body32_tail32_body32;
            used[i+(middle && i>=32?32:0)]=true;used[(middle?32:64)+i%32]=true;
        }
    }
    std::vector<unsigned> result;
    for(unsigned i=0;i<96;++i)if(used[i])result.push_back(i);
    return result;
}
} // namespace ikea::integers::composition::fixture
