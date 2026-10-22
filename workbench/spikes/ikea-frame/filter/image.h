#pragma once
#include "kernels.h"
#include "../encoding/material.h"

namespace frame_filter {
enum class split_stage { after_for, working64 };
// Migration instrument: original image stays borrowed for unsupported kinds
// and the decode control. Its retained bytes must be charged separately.
struct split_block {
    std::uint8_t heads{}, width{};
    std::uint32_t payload_offset{}, head0_offset{}, head1_offset{};
};
static_assert(sizeof(split_block) == 16);
class image {
    const frame_encoding::column &original_;
    std::vector<split_block> split_;
    std::vector<std::uint8_t> payload_, head0_, head1_;
    split_stage stage_;
  public:
    image(const frame_encoding::column &, unsigned heads,
          split_stage = split_stage::after_for);
    u64 count_ge(const frame_encoding::block_mask &, u64 rhs, bool guard) const;
    std::size_t split_bytes() const;
    std::size_t split_metadata_bytes() const { return split_.size() * sizeof(split_block); }
};
} // namespace frame_filter
