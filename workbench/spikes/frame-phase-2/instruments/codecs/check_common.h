#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

inline std::uint64_t codec_mix(std::uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
inline std::uint64_t codec_mask(unsigned bits) {
    return bits == 64 ? ~std::uint64_t(0) : (std::uint64_t(1) << bits) - 1;
}
inline void require_codec(bool law, const std::string& message) {
    if (!law) throw std::runtime_error(message);
}
