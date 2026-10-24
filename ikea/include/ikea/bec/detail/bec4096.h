#pragma once
// Bec4096 framing shared by every execution profile. See docs/bec/bec4096.md.
#include <ikea/bec/detail/tables.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>

namespace ikea::bec4096::detail {
// Exact fewest and most Bec256 stream bits for each block population. A
// node's field width depends only on its own count, so the extremes follow
// from a DP over halves; the check recomputes them independently.
alignas(64) inline constexpr std::array<std::uint16_t, 257> min_bits{
    0, 8, 15, 16, 22, 21, 20, 18, 20, 22, 24, 25, 26, 24, 23, 20,
    20, 23, 26, 27, 29, 28, 27, 25, 23, 24, 26, 27, 28, 25, 24, 20,
    18, 22, 26, 27, 30, 29, 28, 26, 25, 27, 29, 30, 31, 29, 28, 25,
    22, 24, 27, 28, 30, 29, 28, 26, 24, 24, 26, 27, 28, 24, 23, 18,
    14, 19, 24, 25, 29, 28, 27, 25, 25, 27, 29, 30, 31, 29, 28, 25,
    23, 26, 29, 30, 32, 31, 30, 28, 26, 27, 29, 30, 31, 28, 27, 23,
    19, 22, 26, 27, 30, 29, 28, 26, 25, 27, 29, 30, 31, 29, 28, 25,
    22, 23, 26, 27, 29, 28, 27, 25, 23, 22, 24, 25, 26, 21, 20, 14,
    8, 14, 20, 21, 26, 25, 24, 22, 23, 25, 27, 28, 29, 27, 26, 23,
    22, 25, 28, 29, 31, 30, 29, 27, 25, 26, 28, 29, 30, 27, 26, 22,
    19, 23, 27, 28, 31, 30, 29, 27, 26, 28, 30, 31, 32, 30, 29, 26,
    23, 25, 28, 29, 31, 30, 29, 27, 25, 25, 27, 28, 29, 25, 24, 19,
    14, 18, 23, 24, 28, 27, 26, 24, 24, 26, 28, 29, 30, 28, 27, 24,
    22, 25, 28, 29, 31, 30, 29, 27, 25, 26, 28, 29, 30, 27, 26, 22,
    18, 20, 24, 25, 28, 27, 26, 24, 23, 25, 27, 28, 29, 27, 26, 23,
    20, 20, 23, 24, 26, 25, 24, 22, 20, 18, 20, 21, 22, 16, 15, 8,
    0};
alignas(64) inline constexpr std::array<std::uint16_t, 257> max_bits{
    0, 8, 16, 23, 31, 37, 44, 50, 58, 63, 69, 74, 81, 86, 92, 97,
    105, 109, 114, 118, 124, 128, 133, 137, 144, 148, 153, 157, 163, 167, 172, 176,
    184, 186, 189, 191, 195, 197, 200, 202, 207, 209, 212, 214, 218, 220, 223, 225,
    231, 233, 236, 238, 242, 244, 247, 249, 254, 256, 259, 261, 265, 267, 270, 272,
    279, 280, 281, 282, 284, 285, 286, 287, 290, 291, 292, 293, 295, 296, 297, 298,
    302, 303, 304, 305, 307, 308, 309, 310, 313, 314, 315, 316, 318, 319, 320, 321,
    326, 327, 328, 329, 331, 332, 333, 334, 337, 338, 339, 340, 342, 343, 344, 345,
    349, 350, 351, 352, 354, 355, 356, 357, 360, 361, 362, 363, 365, 366, 367, 368,
    374, 368, 367, 366, 365, 363, 362, 361, 360, 357, 356, 355, 354, 352, 351, 350,
    349, 345, 344, 343, 342, 340, 339, 338, 337, 334, 333, 332, 331, 329, 328, 327,
    326, 321, 320, 319, 318, 316, 315, 314, 313, 310, 309, 308, 307, 305, 304, 303,
    302, 298, 297, 296, 295, 293, 292, 291, 290, 287, 286, 285, 284, 282, 281, 280,
    279, 272, 270, 267, 265, 261, 259, 256, 254, 249, 247, 244, 242, 238, 236, 233,
    231, 225, 223, 220, 218, 214, 212, 209, 207, 202, 200, 197, 195, 191, 189, 186,
    184, 176, 172, 167, 163, 157, 153, 148, 144, 137, 133, 128, 124, 118, 114, 109,
    105, 97, 92, 86, 81, 74, 69, 63, 58, 50, 44, 37, 31, 23, 16, 8,
    0};
static_assert(max_bits[128] == 374 && min_bits[1] == 8 && max_bits[1] == 8);
// A block whose stream would exceed 256 bits is stored raw instead, so coded
// streams are at most 256 bits and their lengths range over min..min(max, 256).
inline constexpr unsigned raw_bits = 256;
// Rank-width sum from which encoding sizes a block before packing it.
inline constexpr unsigned raw_gate_bits = 144;
// Length field width for a block of population c; zero for 0, 1, 255 and 256.
alignas(64) inline constexpr auto length_width = [] {
    std::array<std::uint8_t, 257> result{};
    for (unsigned c = 0; c <= 256; ++c)
        result[c] = std::uint8_t(std::bit_width(
            unsigned(std::min<unsigned>(max_bits[c], raw_bits) - min_bits[c])));
    return result;
}();
inline constexpr unsigned max_length_width =
    *std::max_element(length_width.begin(), length_width.end());
// The raw mask, then the head over regions of 4096, 2048, 1024 and 512
// positions, the length table and sixteen streams or raw blocks.
inline constexpr unsigned mask_bits = 16;
inline constexpr unsigned head_max_bits = 12 + 2 * 11 + 4 * 10 + 8 * 9;
inline constexpr unsigned body_max_bits =
    mask_bits + head_max_bits + 15 * max_length_width + 16 * raw_bits;
[[gnu::always_inline]] inline unsigned lower(unsigned parent, unsigned half) {
    return parent > half ? parent - half : 0;
}
[[gnu::always_inline]] inline unsigned split_width(unsigned parent, unsigned half) {
    return std::bit_width(std::min(parent, 2 * half - parent));
}
[[gnu::always_inline]] inline bool terminal(unsigned population) {
    return population == 0 || population == 256;
}
} // namespace ikea::bec4096::detail
