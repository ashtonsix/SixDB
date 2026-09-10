#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>

namespace ikea::integers {
enum class Layout { local, scan };

template<unsigned K> struct LocalPack {
    static_assert(K >= 1 && K <= 7);
    static constexpr unsigned width = K, values = 8, bytes = K;
};
template<unsigned K> struct ScanPack {
    static_assert(K >= 1 && K <= 7);
    static constexpr unsigned width = K;
    static constexpr unsigned groups = 8 / std::gcd(K, 8u);
    static constexpr unsigned stripes = K / std::gcd(K, 8u);
    static constexpr unsigned values = 32 * groups, bytes = 32 * stripes;
};
template<class Child, unsigned N> struct Tiles {
    static_assert(N > 0);
    using child = Child;
    static constexpr unsigned repetitions = N;
    static constexpr unsigned values = N * Child::values;
    static constexpr unsigned bytes = N * Child::bytes;
};
struct BitAddress { unsigned byte, bit; };

// A bit's physical location, independent of ISA and operation grain. Widths
// 5 and 7 use a continuous high-fragment-first assignment: a crossing value's
// high bits fill the current stripe, and its low bits begin the next stripe.
template<unsigned K> constexpr BitAddress scan_bit(unsigned i, unsigned bit) {
    using F = ScanPack<K>;
    const unsigned tile = i / F::values, lane = i % 32;
    const unsigned group = (i % F::values) / 32;
    unsigned byte = 0, place = 0;
    if constexpr(K == 3) {
        constexpr std::array<std::array<unsigned, 3>, 8> map{{
            {{0,1,2}}, {{3,4,5}}, {{6,7,14}}, {{8,9,10}},
            {{11,12,13}}, {{22,23,15}}, {{16,17,18}}, {{19,20,21}}
        }};
        byte = map[group][bit] / 8; place = map[group][bit] % 8;
    } else if constexpr(K == 6) {
        constexpr std::array<std::array<unsigned, 6>, 4> map{{
            {{0,1,2,3,4,5}}, {{8,9,10,11,6,7}},
            {{12,13,14,15,22,23}}, {{16,17,18,19,20,21}}
        }};
        byte = map[group][bit] / 8; place = map[group][bit] % 8;
    } else {
        const unsigned start = group*K, room = 8-start%8;
        byte = start/8;
        if(K <= room) place = start%8 + bit;
        else if(bit < K-room) { ++byte; place = bit; }
        else place = start%8 + bit-(K-room);
    }
    return {tile*F::bytes + byte*32 + lane, place};
}
template<unsigned K> constexpr BitAddress local_bit(unsigned i, unsigned bit) {
    return {i/8*K+bit, i%8};
}
} // namespace ikea::integers
