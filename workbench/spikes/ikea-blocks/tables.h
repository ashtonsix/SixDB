#pragma once
#include <array>
#include <bit>
#include <cstdint>

namespace ikea_probe::bec {
inline constexpr std::array<std::uint8_t, 16> byte_width{
    0, 3, 5, 6, 7, 6, 5, 3, 0};
inline constexpr std::array<std::uint8_t, 16> byte_base{
    0, 1, 9, 37, 93, 163, 219, 247, 255};
inline constexpr std::array<unsigned, 9> choices{1, 8, 28, 56, 70, 56, 28, 8, 1};
struct ByteCodes {
    alignas(64) std::array<std::uint8_t, 256> rank{};
    alignas(64) std::array<std::uint8_t, 256> value{};
    consteval ByteCodes() {
        unsigned slot = 0;
        for (unsigned count = 0; count != 9; ++count) {
            unsigned r = 0;
            for (unsigned b = 0; b != 256; ++b) {
                if (std::popcount(b) != count) continue;
                rank[b] = r++;
                value[slot++] = b;
            }
        }
    }
};
inline constexpr ByteCodes codes;
alignas(64) inline constexpr auto log_width = [] {
    std::array<std::uint8_t, 128> result{};
    for (unsigned i = 0; i != result.size(); ++i) result[i] = std::bit_width(i);
    return result;
}();
alignas(64) inline constexpr auto log_width16 = [] {
    std::array<std::uint16_t,64> result{};
    for(unsigned i=0;i<result.size();++i) result[i]=std::bit_width(i);
    return result;
}();
}
