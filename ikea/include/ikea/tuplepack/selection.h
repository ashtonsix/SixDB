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
    bool contains(std::size_t row) const noexcept {
        return !explicit_mask || ((words[(row - first) / 64] >> ((row - first) % 64)) & 1);
    }
};
} // namespace ikea::tuplepack
