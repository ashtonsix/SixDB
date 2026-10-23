#pragma once
#include <ikea/bitset256.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ikea::tuplepack {
/// Borrowed original-row selection, independent of packet slots/evidence meaning.
/// Numeric u64 words use bit zero at first; any origin is valid. A bitset256 can
/// supply the same 256 positions without a storage conversion. Keep the backing
/// stable and disjoint from mutable outputs through admission and execution.
class selection {
    enum class kind { all, none, bits };
    kind kind_ = kind::none;
    std::size_t first_ = 0;
    const void* data_ = nullptr;
    std::size_t words_ = 0;
    selection(kind type, std::size_t first, const void* data, std::size_t words) noexcept
        : kind_(type), first_(first), data_(data), words_(words) {}

  public:
    selection() = default;
    static selection all() noexcept { return {kind::all, 0, nullptr, 0}; }
    static selection none() noexcept { return {kind::none, 0, nullptr, 0}; }
    static selection bits(std::size_t first, std::span<const std::uint64_t> words) noexcept {
        return {kind::bits, first, words.data(), words.size()};
    }
    static selection bits(std::size_t first, const bitset256& block) noexcept {
        return bits(first, std::span<const std::uint64_t>(block.words));
    }
    static selection bits(std::size_t, const bitset256&&) = delete;
    bool is_all() const noexcept { return kind_ == kind::all; }
    bool is_none() const noexcept { return kind_ == kind::none; }
    bool covers(std::size_t first, std::size_t count) const noexcept {
        if (!count)
            return true;
        if (count - 1 > std::numeric_limits<std::size_t>::max() - first)
            return false;
        if (is_all() || is_none())
            return true;
        return first >= first_ && (first + (count - 1) - first_) / 64 < words_;
    }
    /// Trusted covered window of 0..64 rows. Bit r names first+r, including
    /// when the origin or window crosses a backing word boundary.
    std::uint64_t mask(std::size_t first, unsigned count) const noexcept {
        assert(count <= 64);
        if (!count || is_none())
            return 0;
        const auto limit = ~std::uint64_t{0} >> (64 - count);
        if (is_all())
            return limit;
        const auto offset = first - first_;
        const auto* words = static_cast<const std::uint64_t*>(data_);
        const unsigned shift = offset % 64;
        auto value = words[offset / 64] >> shift;
        if (shift && count > 64 - shift)
            value |= words[offset / 64 + 1] << (64 - shift);
        return value & limit;
    }
    /// Trusted covered original row, after covers(row,1).
    bool contains(std::size_t row) const noexcept {
        if (is_all() || is_none())
            return is_all();
        const auto offset = row - first_;
        return static_cast<const std::uint64_t*>(data_)[offset / 64] >> (offset % 64) & 1;
    }
};
} // namespace ikea::tuplepack
