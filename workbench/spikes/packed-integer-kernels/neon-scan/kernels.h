#pragma once

#include <ikea/seriespack/native_neon.h>
#include "../../ikea-composition/probes/ikea-integers/scan.h"

namespace scan_grain {
namespace sp = ikea::seriespack;

template<unsigned W> inline constexpr unsigned tile_values = W == 4 ? 64 : 128;
template<unsigned W> inline constexpr unsigned tile_bytes = tile_values<W> * W / 8;

// No call-site constant count is permitted to turn the array control into a
// fixed8192-value specialization. Outer-loop grain remains the explicit arm.
template<unsigned W, unsigned Region, bool Encode, class UInt = std::uint8_t>
[[gnu::noinline]] void native_region(const std::uint8_t* __restrict input,
                                     std::uint8_t* __restrict output, std::size_t count) {
    static_assert(W == 4 || W == 6);
    constexpr auto G = sp::geometry::striped;
    constexpr unsigned T = tile_values<W>, B = tile_bytes<W>;
    static_assert(Region == 0 || (Region >= T && Region % T == 0));
    asm("" : "+r"(count));
    if (count == 0) return;
    __builtin_assume(count % T == 0);
    const auto encode = [&](std::size_t i, std::size_t tiles) {
        sp::neon::encode_low_tiles<W, G>(reinterpret_cast<const UInt*>(input) + i,
                                        output + i / T * B, tiles);
    };
    const auto decode = [&](std::size_t i, std::size_t tiles) {
        sp::neon::decode_tiles<W, G>(input + i / T * B,
                                    reinterpret_cast<UInt*>(output) + i, tiles);
    };
    if constexpr (Region == 0) {
        if constexpr (Encode) encode(0, count / T);
        else decode(0, count / T);
    } else {
        std::size_t i = 0;
#pragma clang loop unroll(disable)
        for (; count - i >= Region; i += Region) {
            if constexpr (Encode) encode(i, Region / T);
            else decode(i, Region / T);
        }
        if constexpr (Encode) encode(i, (count - i) / T);
        else decode(i, (count - i) / T);
    }
}

template<unsigned W, unsigned Region, bool Encode>
[[gnu::noinline]] void predecessor(const std::uint8_t* __restrict input,
                                   std::uint8_t* __restrict output, std::size_t count) {
    static_assert((W == 4 || W == 6) && Region % tile_values<W> == 0);
    asm("" : "+r"(count));
    __builtin_assume(count % Region == 0);
#pragma clang loop unroll(disable)
    for (std::size_t i = 0; i < count; i += Region) {
        if constexpr (Encode) ikea::integers::scan_encode<W, Region>(input + i, output + i * W / 8);
        else ikea::integers::scan_decode<W, Region>(input + i * W / 8, output + i);
    }
}

// Independent wire oracle: four-bit groups occupy the low/high nibbles;
// six-bit groups use the repaired shared middle stripe from the specification.
template<unsigned W>
unsigned position(unsigned group, unsigned bit) {
    if constexpr (W == 4) return group * 4 + bit;
    else {
        constexpr unsigned positions[4][6] = {
            {0,1,2,3,4,5}, {8,9,10,11,6,7},
            {12,13,14,15,22,23}, {16,17,18,19,20,21}};
        return positions[group][bit];
    }
}

template<unsigned W, class UInt>
void oracle(const UInt* input, std::uint8_t* wire, std::size_t count) {
    std::fill_n(wire, count * W / 8, 0);
    for (std::size_t i = 0; i < count; ++i)
        for (unsigned bit = 0; bit < W; ++bit) {
            const auto p = position<W>((i % tile_values<W>) / 32, bit);
            wire[i / tile_values<W> * tile_bytes<W> + p / 8 * 32 + i % 32] |=
                std::uint8_t(((input[i] >> bit) & 1) << (p % 8));
        }
}
} // namespace scan_grain
