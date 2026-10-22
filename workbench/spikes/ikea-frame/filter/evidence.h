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
        return sizeof(*this) + parts_.capacity() * sizeof(part) +
               head0_.capacity() + head1_.capacity();
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
    std::size_t bytes() const { return sizeof(*this) + blocks_.capacity() * sizeof(block); }
};

// Strong inexpensive control for finer evidence: logical bounds alone can
// already skip many prediction blocks. Do not credit window summaries for it.
class block_bounds {
    struct bounds { u64 lower, upper; };
    const frame_encoding::column &original_;
    std::vector<bounds> blocks_;
  public:
    explicit block_bounds(const frame_encoding::column &);
    u64 count_ge(const frame_encoding::block_mask &, u64 rhs) const;
    std::size_t bytes() const { return sizeof(*this) + blocks_.capacity() * sizeof(bounds); }
};
} // namespace frame_filter
