#pragma once
#include <cstdint>
#include <bit>
#include <cstring>
namespace ikea_probe::bec {
// A singleton follows one branch at each level: five reversed direction bits
// and its three-bit rank within a byte. A single hole complements that code.
inline unsigned encode_singleton(std::uint64_t a,std::uint64_t b,
                                 std::uint64_t c,std::uint64_t d,
                                 bool hole,std::uint8_t* out) {
    unsigned word=(a==0)+((a|b)==0)+((a|b|c)==0);
    unsigned position=64*word+std::countr_zero(a|b|c|d);
    auto path=__builtin_bitreverse8(std::uint8_t(~(position>>3)))>>3;
    out[0]=std::uint8_t(((position&7)<<5)|path) ^ (hole?255:0);
    return 1;
}

// Eight independent groups, each at most 56 bits. Overlapping 8-byte stores
// stitch the bit stream; only the carry byte links groups. Output has 64 bytes.
// Zero-width padding may occur between groups without changing the format.
inline __attribute__((always_inline)) unsigned stitch(const std::uint64_t* value,
                                                      const std::uint64_t* width,
                                                      std::uint8_t* out) {
    auto* cursor = out;
    unsigned residual = 0;
    std::uint64_t carry = 0;
    for (unsigned i = 0; i != 8; ++i) {
        auto word = (value[i] << residual) | carry;
        std::memcpy(cursor, &word, 8);
        auto used = residual + unsigned(width[i]);
        cursor += used / 8;
        residual = used % 8;
        carry = word >> (used & ~7u); // used <= 63, including zero-width groups
    }
    return unsigned(cursor - out) + (residual != 0);
}
}
