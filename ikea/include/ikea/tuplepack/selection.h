#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace ikea::tuplepack {
/// Original-row selection, independent of code slots and evidence meaning.
/// A null/default selection means all rows. Explicit words use bit zero at first.
/// Storage stays stable and disjoint from mutable outputs through the whole call.
struct selection {
    std::span<const std::uint64_t> words{};
    std::size_t first = 0;
    bool explicit_mask = false;
    static selection all() noexcept {
        return {};
    }
    static selection bits(std::size_t first, std::span<const std::uint64_t> words) noexcept {
        return {words, first, true};
    }
    bool covers(std::size_t begin, std::size_t count) const noexcept {
        if (!explicit_mask || !count)
            return true;
        if (begin < first || count - 1 > ~std::size_t(0) - begin)
            return false;
        return (begin + count - 1 - first) / 64 < words.size();
    }
    /// Trusted covered window of 0..64 rows. Bit r names begin+r, including
    /// when the selection origin or window crosses a backing word boundary.
    std::uint64_t mask(std::size_t begin, unsigned count) const noexcept {
        if (!count)
            return 0;
        const auto limit = ~std::uint64_t(0) >> (64 - count);
        if (!explicit_mask)
            return limit;
        const auto offset = begin - first;
        const unsigned shift = offset % 64;
        auto value = words[offset / 64] >> shift;
        if (shift && count > 64 - shift)
            value |= words[offset / 64 + 1] << (64 - shift);
        return value & limit;
    }
    bool contains(std::size_t row) const noexcept {
        return !explicit_mask || ((words[(row - first) / 64] >> ((row - first) % 64)) & 1);
    }
};
} // namespace ikea::tuplepack
