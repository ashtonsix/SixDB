#pragma once
#include <ikea/bitset256.h>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace ikea {
/// A value containing 4096 position bits: sixteen 256-position blocks.
/// Position i is numeric bit i%64 of words[i/64]; block k is words 4k..4k+3,
/// the law of bitset256. load/store use the portable byte law: position
/// 8*j+b is bit b of byte j, on every endian target.
struct bitset4096 {
    std::array<std::uint64_t, 64> words{};

    constexpr bool contains(unsigned position) const noexcept {
        assert(position < 4096);
        return words[position / 64] >> (position % 64) & 1;
    }
    constexpr void set(unsigned position, bool value = true) noexcept {
        assert(position < 4096);
        const auto bit = std::uint64_t{1} << (position % 64);
        auto &word = words[position / 64];
        word = value ? word | bit : word & ~bit;
    }
    constexpr unsigned count() const noexcept {
        unsigned result = 0;
        for (auto word : words)
            result += std::popcount(word);
        return result;
    }
    constexpr bitset256 block(unsigned index) const noexcept {
        assert(index < 16);
        bitset256 result;
        for (unsigned i = 0; i < 4; ++i)
            result.words[i] = words[4 * index + i];
        return result;
    }
    constexpr void assign(unsigned index, const bitset256 &value) noexcept {
        assert(index < 16);
        for (unsigned i = 0; i < 4; ++i)
            words[4 * index + i] = value.words[i];
    }
    static bitset4096 load(std::span<const std::byte, 512> bytes) noexcept {
        bitset4096 result;
        std::memcpy(result.words.data(), bytes.data(), 512);
        if constexpr (std::endian::native == std::endian::big)
            for (auto &word : result.words)
                word = std::byteswap(word);
        return result;
    }
    void store(std::span<std::byte, 512> bytes) const noexcept {
        if constexpr (std::endian::native == std::endian::big)
            for (unsigned i = 0; i < 64; ++i) {
                const auto word = std::byteswap(words[i]);
                std::memcpy(bytes.data() + 8 * i, &word, 8);
            }
        else
            std::memcpy(bytes.data(), words.data(), 512);
    }
    bool operator==(const bitset4096 &) const = default;
};
static_assert(sizeof(bitset4096) == 512 && alignof(bitset4096) == alignof(std::uint64_t));
} // namespace ikea
