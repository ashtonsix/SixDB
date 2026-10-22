#pragma once
#include "input.h"
#include <array>
#include <bit>
#include <span>

namespace frame_columns {
using frame_execution::Word;
struct Total { Word count{}, sum{}; };
struct Bits {
    std::array<Word, 4> words{};
    bool test(unsigned row) const { return words[row / 64] >> (row % 64) & 1; }
    void set(unsigned row) { words[row / 64] |= Word{1} << (row % 64); }
    unsigned count() const {
        unsigned n = 0; for (auto w : words) n += std::popcount(w); return n;
    }
};
struct NativeCodes {
    std::vector<std::uint8_t> small;
    std::vector<std::uint16_t> large;
    std::size_t bytes() const { return small.capacity() + 2 * large.capacity(); }
};
struct NativeRows {
    NativeCodes category;
    std::vector<std::uint32_t> x;
    std::size_t bytes() const { return category.bytes() + 4 * x.capacity(); }
};
// Independent complete native loops: no Frame encoding/decoding or per-row
// erased callbacks. Category indices use u8/u16, X the admitted native u32 law.
void native_group(const NativeRows&, std::span<const Bits> selected,
                  std::span<const Bits> has_category, std::span<const std::uint8_t> allowed,
                  Word threshold, std::span<Total> totals, bool coalesce = false);
Word native_x_sum(const NativeRows&, std::span<const Bits> selected, Word threshold);
void native_histogram(const NativeCodes&, std::span<const Bits> selected,
                      std::span<std::uint32_t> counts, bool coalesce = false);
Word native_projected_sum(const NativeCodes&, std::span<const std::uint32_t> x,
                          std::span<const Bits> selected, Word threshold);
} // namespace frame_columns
