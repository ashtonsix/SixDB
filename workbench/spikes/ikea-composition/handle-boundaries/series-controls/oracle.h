#pragma once
#include "controls.h"
#include <vector>
namespace series_controls {
std::vector<std::byte> encode_reference(shape, std::span<const word>);
inline word domain_mask(unsigned width) { return width == 64 ? ~word{0} : (word{1} << width) - 1; }
inline std::vector<word> values(unsigned width, std::size_t count, word seed) {
    std::vector<word> result(count);
    for (auto& x : result) {
        seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
        x = seed & domain_mask(width);
    }
    return result;
}
}
