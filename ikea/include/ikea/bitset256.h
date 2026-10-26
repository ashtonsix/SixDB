#pragma once
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace ikea {
/// A value containing 256 position bits, independent of rows, ranks or blocks.
/// Position i is numeric bit i%64 of words[i/64]. Default construction is empty.
/// Words have their ordinary host representation; load/store use the portable
/// byte law: position 8*j+b is bit b of byte j, on every endian target.
/// Natural alignment is sufficient; this is not a native register carrier.
struct bitset256 {
    std::array<std::uint64_t, 4> words{};

    /// Positions [0,count), with 0 <= count <= 256.
    static constexpr bitset256 first_n(unsigned count) noexcept {
        assert(count <= 256);
        bitset256 result;
        for (unsigned w = 0; w < 4; ++w) {
            const unsigned remaining = count > w * 64 ? count - w * 64 : 0;
            result.words[w] = remaining >= 64 ? ~std::uint64_t{0}
                             : remaining ? (std::uint64_t{1} << remaining) - 1 : 0;
        }
        return result;
    }
    /// Visit set positions in increasing order. The callback receives unsigned.
    template<class F> constexpr void for_each(F&& f) const {
        for (unsigned w = 0; w < 4; ++w)
            for (auto bits = words[w]; bits; bits &= bits - 1)
                f(w * 64 + static_cast<unsigned>(std::countr_zero(bits)));
    }
    /// Visit maximal runs of consecutive set positions in increasing order,
    /// as f(first, count).
    template<class F> constexpr void for_each_run(F&& f) const {
        for (unsigned first = next(0, true); first < 256;) {
            const unsigned end = next(first, false);
            f(first, end - first);
            first = next(end, true);
        }
    }
    friend constexpr bitset256 operator&(const bitset256& a, const bitset256& b) noexcept {
        return {{a.words[0] & b.words[0], a.words[1] & b.words[1],
                 a.words[2] & b.words[2], a.words[3] & b.words[3]}};
    }
    friend constexpr bitset256 operator|(const bitset256& a, const bitset256& b) noexcept {
        return {{a.words[0] | b.words[0], a.words[1] | b.words[1],
                 a.words[2] | b.words[2], a.words[3] | b.words[3]}};
    }
    /// The complement over all 256 positions.
    friend constexpr bitset256 operator~(const bitset256& a) noexcept {
        return {{~a.words[0], ~a.words[1], ~a.words[2], ~a.words[3]}};
    }
    /// The first position at or after from (0..256) whose bit is value, or 256.
    constexpr unsigned next(unsigned from, bool value) const noexcept {
        assert(from <= 256);
        while (from < 256) {
            const auto word = (value ? words[from / 64] : ~words[from / 64]) & (~std::uint64_t{0} << (from % 64));
            if (word)
                return from / 64 * 64 + static_cast<unsigned>(std::countr_zero(word));
            from = (from / 64 + 1) * 64;
        }
        return 256;
    }

    constexpr bool contains(unsigned position) const noexcept {
        assert(position < 256);
        return words[position / 64] >> (position % 64) & 1;
    }
    constexpr void set(unsigned position, bool value = true) noexcept {
        assert(position < 256);
        const auto bit = std::uint64_t{1} << (position % 64);
        auto& word = words[position / 64];
        word = value ? word | bit : word & ~bit;
    }
    constexpr unsigned count() const noexcept {
        unsigned result = 0;
        for (auto word : words)
            result += std::popcount(word);
        return result;
    }
    constexpr bool empty() const noexcept {
        return !(words[0] | words[1] | words[2] | words[3]);
    }
    constexpr bool full() const noexcept {
        return (words[0] & words[1] & words[2] & words[3]) == ~std::uint64_t{0};
    }
    /// Number of set positions strictly before position; rank(256) is count().
    constexpr unsigned rank(unsigned position) const noexcept {
        assert(position <= 256);
        unsigned result = 0;
        for (unsigned i = 0; i < position / 64; ++i)
            result += std::popcount(words[i]);
        if (position % 64)
            result += std::popcount(words[position / 64] &
                                    ((std::uint64_t{1} << (position % 64)) - 1));
        return result;
    }
    /// Trusted bounded window of 0..64 positions. Bit r names first+r.
    /// mask(256,0) is valid and reads no word; no shift by 64 is performed.
    constexpr std::uint64_t mask(unsigned first, unsigned count) const noexcept {
        assert(count <= 64 && first <= 256 && count <= 256 - first);
        if (!count)
            return 0;
        const auto shift = first % 64;
        auto result = words[first / 64] >> shift;
        if (shift && count > 64 - shift)
            result |= words[first / 64 + 1] << (64 - shift);
        return result & (~std::uint64_t{0} >> (64 - count));
    }
    static bitset256 load(std::span<const std::byte, 32> bytes) noexcept {
        bitset256 result;
        std::memcpy(result.words.data(), bytes.data(), 32);
        if constexpr (std::endian::native == std::endian::big)
            for (auto& word : result.words)
                word = std::byteswap(word);
        return result;
    }
    void store(std::span<std::byte, 32> bytes) const noexcept {
        if constexpr (std::endian::native == std::endian::big) {
            const std::array out{std::byteswap(words[0]), std::byteswap(words[1]),
                                 std::byteswap(words[2]), std::byteswap(words[3])};
            std::memcpy(bytes.data(), out.data(), 32);
        } else
            std::memcpy(bytes.data(), words.data(), 32);
    }
    bool operator==(const bitset256&) const = default;
};
static_assert(sizeof(bitset256) == 32 && alignof(bitset256) == alignof(std::uint64_t));
// Also make the hidden friends available to qualified composition adapters.
constexpr bitset256 operator&(const bitset256&, const bitset256&) noexcept;
constexpr bitset256 operator|(const bitset256&, const bitset256&) noexcept;
constexpr bitset256 operator~(const bitset256&) noexcept;
} // namespace ikea
