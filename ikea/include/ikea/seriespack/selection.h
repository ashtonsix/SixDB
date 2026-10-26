#pragma once
#include <ikea/bitset256.h>
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <limits>
#include <span>

namespace ikea::seriespack {
/// Borrowed selection of original rows. Each numeric u16 word holds sixteen
/// consecutive row bits; origin is divisible by 16. A bitset256 holds 256 bits
/// under the same coordinate law without reinterpreting its u64 objects as u16.
/// Keep backing stable through admission/execution; selection is no read grant.
class selection {
    enum class kind { all, none, bits, block, blocks };
    kind kind_ = kind::none;
    std::size_t first_ = 0;
    const void* data_ = nullptr;
    std::size_t words_ = 0;
    selection(kind type, std::size_t first, const void* data, std::size_t words) noexcept
        : kind_(type), first_(first), data_(data), words_(words) {}
    std::uint16_t word(std::size_t index) const noexcept {
        std::uint16_t value;
        std::memcpy(&value, static_cast<const std::byte*>(data_) + index * 2, 2);
        return value;
    }

  public:
    /// Sixteen-row region words with the selection's kind resolved once. Native
    /// writers read one word per complete region without re-dispatching.
    class regions {
        const selection* selection_;
        const std::byte* words_;
        std::size_t base_;
        friend class selection;
        regions(const selection& s, const std::byte* words, std::size_t base) noexcept
            : selection_(&s), words_(words), base_(base) {}

      public:
        /// Trusted covered origin divisible by sixteen.
        std::uint16_t operator()(std::size_t origin) const noexcept {
            if (words_) {
                std::uint16_t value;
                std::memcpy(&value, words_ + (origin / 16 - base_) * 2, 2);
                return value;
            }
            return std::uint16_t(selection_->mask(origin, 16));
        }
    };
    /// Borrows this selection and its backing for the region reader's use.
    regions region_words() const noexcept {
        if (kind_ == kind::bits)
            return {*this, static_cast<const std::byte*>(data_), first_ / 16};
        return {*this, nullptr, 0};
    }
    selection() = default;
    static selection all() noexcept { return {kind::all, 0, nullptr, 0}; }
    static selection none() noexcept { return {kind::none, 0, nullptr, 0}; }
    /// Checked commands reject a misaligned origin or insufficient coverage.
    static selection bits(std::size_t first, std::span<const std::uint16_t> words) noexcept {
        return {kind::bits, first, words.data(), words.size()};
    }
    static selection bits(std::size_t first, const bitset256& block) noexcept {
        if constexpr (std::endian::native == std::endian::little)
            return {kind::bits, first, block.words.data(), 16};
        else
            return {kind::block, first, &block, 16};
    }
    static selection bits(std::size_t, const bitset256&&) = delete;
    /// Borrows consecutive original-row blocks without flattening their u64
    /// subobjects or copying a filter's output. Origin is divisible by sixteen.
    static selection bits(std::size_t first, std::span<const bitset256> blocks) noexcept {
        return {kind::blocks, first, blocks.data(), blocks.size()};
    }
    bool is_all() const noexcept { return kind_ == kind::all; }
    bool is_none() const noexcept { return kind_ == kind::none; }
    /// The borrowed block holding rows [first, first + 256), when this
    /// selection borrows consecutive blocks and first is one of their
    /// origins; otherwise null. Lets a caller use a block without masks.
    const bitset256* block_at(std::size_t first) const noexcept {
        if (kind_ != kind::blocks || first < first_ || (first - first_) % 256 || (first - first_) / 256 >= words_)
            return nullptr;
        return static_cast<const bitset256*>(data_) + (first - first_) / 256;
    }
    bool covers(std::size_t first, std::size_t count) const noexcept {
        if ((kind_ == kind::bits || kind_ == kind::block || kind_ == kind::blocks) && first_ % 16)
            return false;
        if (!count)
            return true;
        if (count - 1 > std::numeric_limits<std::size_t>::max() - first)
            return false;
        if (is_all() || is_none())
            return true;
        return first >= first_ && (first + (count - 1) - first_) /
            (kind_ == kind::blocks ? 256 : 16) < words_;
    }
    /// Trusted covered original row, after covers(row,1).
    bool contains(std::size_t row) const noexcept {
        if (is_all() || is_none())
            return is_all();
        const auto offset = row - first_;
        if (kind_ == kind::blocks)
            return static_cast<const bitset256*>(data_)[offset / 256].contains(unsigned(offset % 256));
        if constexpr (std::endian::native == std::endian::big)
            if (kind_ == kind::block)
                return static_cast<const bitset256*>(data_)->contains(unsigned(offset));
        return word(offset / 16) >> (offset % 16) & 1;
    }
    /// Trusted covered window of 0..64 rows. Bit r names first+r. This does
    /// not change the writer's native traversal or admit inactive input lanes.
    std::uint64_t mask(std::size_t first, unsigned count) const noexcept {
        assert(count <= 64);
        if (!count || is_none())
            return 0;
        const auto limit = ~std::uint64_t{0} >> (64 - count);
        if (is_all())
            return limit;
        const auto offset = first - first_;
        if (kind_ == kind::blocks) {
            const auto* blocks = static_cast<const bitset256*>(data_);
            const auto local = unsigned(offset % 256);
            const auto head = std::min(count, 256u - local);
            auto result = blocks[offset / 256].mask(local, head);
            if (head < count)
                result |= blocks[offset / 256 + 1].mask(0, count - head) << head;
            return result;
        }
        if constexpr (std::endian::native == std::endian::big)
            if (kind_ == kind::block)
                return static_cast<const bitset256*>(data_)->mask(unsigned(offset), count);
        // Existing native writers request complete aligned sixteen-row words.
        if (count == 16 && first % 16 == 0)
            return word(first / 16 - first_ / 16);
        const unsigned shift = offset % 16;
        std::uint64_t result = word(offset / 16) >> shift;
        for (unsigned done = 16 - shift; done < count; done += 16)
            result |= std::uint64_t(word((offset + done) / 16)) << done;
        return result & limit;
    }
};
} // namespace ikea::seriespack
