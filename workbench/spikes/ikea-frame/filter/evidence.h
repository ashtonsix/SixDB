// Logical predicate evidence over unchanged encoded bodies. These sidecars
// deliberately charge duplicated information; they are not a final Frame layout.
#pragma once
#include "image.h"
#include <array>

namespace frame_filter {
class logical_heads {
    struct part {
        u64 base, maximum;
        std::uint8_t shift, heads;
    };
    const frame_encoding::column &original_;
    std::vector<part> parts_;
    std::vector<std::uint8_t> head0_, head1_;
  public:
    logical_heads(const frame_encoding::column &, unsigned heads);
    u64 count_ge(const frame_encoding::block_mask &, u64 rhs) const;
    std::size_t bytes() const {
        return parts_.size() * sizeof(part) + head0_.size() + head1_.size();
    }
};

class window_bounds {
    struct block {
        u64 base, maximum;
        std::array<std::uint8_t, 16> lower, upper;
        std::uint8_t shift;
    };
    const frame_encoding::column &original_;
    std::vector<block> blocks_;
  public:
    explicit window_bounds(const frame_encoding::column &);
    u64 count_ge(const frame_encoding::block_mask &, u64 rhs) const;
    std::size_t bytes() const { return blocks_.size() * sizeof(block); }
};
} // namespace frame_filter
