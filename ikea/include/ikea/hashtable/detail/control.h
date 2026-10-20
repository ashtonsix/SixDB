#pragma once
#include "bits.h"
#include <limits>
namespace ikea::hashtable::detail {
inline constexpr byte control_empty = 0, control_tombstone = 1;
inline constexpr bool occupied_control(byte control) {
    return control >= 2;
}
inline constexpr byte unknown_overflow = 255;
inline constexpr auto no_slot = std::numeric_limits<std::size_t>::max();
} // namespace ikea::hashtable::detail
