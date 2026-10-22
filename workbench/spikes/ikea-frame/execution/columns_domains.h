#pragma once
#include "columns_native.h"

namespace frame_columns {
// Per-part native lane control. Domain is measured on live non-NULL X only;
// selected masks exclude the filler values in physical holes. FOR is the
// point study's minimum + byte-aligned lane, not exact-bit packing.
struct NativeX {
    Word minimum{}, maximum{}, base{};
    unsigned lane_bits = 8;
    std::vector<std::uint8_t> bytes8;
    std::vector<std::uint16_t> bytes16;
    std::vector<std::uint32_t> bytes32;
    NativeX(std::span<const Word> values, std::span<const Bits> has_x, bool use_base);
    std::size_t bytes() const {
        return sizeof(*this) + bytes8.capacity() + bytes16.capacity()*2 + bytes32.capacity()*4;
    }
};
void domain_group(const NativeCodes& categories, const NativeX&, std::span<const Bits> selected,
                  std::span<const Bits> has_category, std::span<const std::uint8_t> allowed,
                  Word threshold, std::span<Total> totals, bool coalesce);
Word domain_x_sum(const NativeX&, std::span<const Bits> selected, Word threshold);
} // namespace frame_columns
