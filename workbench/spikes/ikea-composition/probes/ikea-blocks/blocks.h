#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace ikea_probe {
// Physical definitions contain no compute or dispatch. Position i is bit i%64
// in word i/64. The persisted byte spelling is little endian.
template<std::size_t Positions> struct PlainBits {
    static_assert(Positions > 0 && Positions % 64 == 0);
    static constexpr auto positions = Positions;
    std::array<std::uint64_t, Positions / 64> words;
    bool operator==(const PlainBits&) const = default;
};
template<class Child, std::size_t Count> struct Tiles {
    static_assert(Count > 0);
    static constexpr auto positions = Child::positions * Count;
    std::array<Child, Count> children;
};
using Plain256 = PlainBits<256>;
static_assert(sizeof(Plain256) == 32);
static_assert(sizeof(Tiles<Plain256, 2>) == sizeof(PlainBits<512>));

// Format identity, not an owning allocation and not a native ABI carrier.
struct Bec256 {
    static constexpr unsigned positions = 256;
    static constexpr unsigned max_bits = 374;
    static constexpr unsigned max_bytes = 47;
};
}
