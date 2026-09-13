#pragma once
#include <cstdint>
#include <bit>
#include <cstring>
namespace ikea::bec256::detail {
// A singleton follows one branch at each level: five reversed direction bits
// and its three-bit rank within a byte. A single hole complements that code.
inline unsigned encode_singleton(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d,
                                 bool hole, std::uint8_t *out) {
    unsigned word = (a == 0) + ((a | b) == 0) + ((a | b | c) == 0);
    unsigned position = 64 * word + std::countr_zero(a | b | c | d);
    auto path = __builtin_bitreverse8(std::uint8_t(~(position >> 3))) >> 3;
    out[0] = std::uint8_t(((position & 7) << 5) | path) ^ (hole ? 255 : 0);
    return 1;
}

// Eight independent groups, each at most 56 bits. Overlapping 8-byte stores
// stitch the bit stream; only the carry byte links groups. Output has 64 bytes.
// Zero-width padding may occur between groups without changing the format.
inline __attribute__((always_inline)) unsigned
stitch(const std::uint64_t *value, const std::uint64_t *width, std::uint8_t *out) {
    auto *cursor = out;
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
inline unsigned encoded_bytes(const std::uint64_t *widths) {
    unsigned bits = 0;
    for (unsigned i = 0; i < 8; ++i)
        bits += widths[i];
    return (bits + 7) / 8;
}
// Emit a prefix of complete, possibly overlapping words, then assemble all
// remaining groups into one tail word. Once fewer than eight bytes remain,
// all remaining bits fit in 56 bits: there is no per-group tail-store cascade.
// Zero-width groups are harmless in the prefix and need no separate branch.
inline __attribute__((always_inline)) void stitch_exact(const std::uint64_t *value,
                                                        const std::uint64_t *width,
                                                        std::uint8_t *out, unsigned bytes) {
    unsigned offset = 0, residual = 0;
    std::uint64_t carry = 0;
    for (unsigned i = 0; i < 8; ++i) {
        auto word = (value[i] << residual) | carry;
        const unsigned remaining = bytes - offset;
        auto *cursor = out + offset;
        if (remaining < 8) {
            unsigned used = residual + unsigned(width[i]);
            for (unsigned j = i + 1; j < 8; ++j) {
                word |= value[j] << used;
                used += unsigned(width[j]);
            }
            if (remaining >= 4) {
                const auto first = std::uint32_t(word),
                           last = std::uint32_t(word >> ((remaining - 4) * 8));
                std::memcpy(cursor, &first, 4);
                std::memcpy(cursor + remaining - 4, &last, 4);
            } else if (remaining >= 2) {
                const auto first = std::uint16_t(word),
                           last = std::uint16_t(word >> ((remaining - 2) * 8));
                std::memcpy(cursor, &first, 2);
                std::memcpy(cursor + remaining - 2, &last, 2);
            } else if (remaining)
                *cursor = std::uint8_t(word);
            return;
        }
        std::memcpy(cursor, &word, 8);
        const auto used = residual + unsigned(width[i]);
        offset += used / 8;
        residual = used % 8;
        carry = word >> (used & ~7u);
    }
}
struct wide_sink {
    std::uint8_t *output;
    unsigned operator()(const std::uint64_t *values, const std::uint64_t *widths) const {
        return stitch(values, widths, output);
    }
    unsigned singleton(std::uint8_t value) const {
        *output = value;
        return 1;
    }
};
} // namespace ikea::bec256::detail
