#pragma once
#include "algebra.h"
#include <bit>

namespace ikea::heterogeneous {
// Forward traversal retains original ordinals. peek() does not consume an
// inactive partner to satisfy a decoder's physical two-input grain.
class SelectedSlices {
  const SliceMask &mask_;
  unsigned word_ = 0;
  std::uint64_t remaining_;
public:
  explicit SelectedSlices(const SliceMask &mask) : mask_(mask), remaining_(mask[0]) {}
  inline __attribute__((always_inline)) unsigned peek() {
    while (!remaining_ && word_ < 3) remaining_ = mask_[++word_];
    return remaining_ ? word_ * 64 + std::countr_zero(remaining_) : 256;
  }
  inline __attribute__((always_inline)) unsigned next() {
    const auto ordinal = peek();
    remaining_ &= remaining_ - 1;
    return ordinal;
  }
};
} // namespace ikea::heterogeneous
