#pragma once
#include "bits.h"
#include <limits>
namespace ikea::hashtable::detail {
inline constexpr byte control_empty = 0x80, control_tombstone = 0xfe;
inline constexpr byte unknown_overflow = 255;
inline constexpr auto no_slot = std::numeric_limits<std::size_t>::max();
} // namespace ikea::hashtable::detail
