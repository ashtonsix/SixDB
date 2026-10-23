#pragma once
#include "bits.h"
#include <limits>
namespace ikea::hashtable::detail {
inline constexpr std::uint8_t control_empty = 0, control_tombstone = 1;
inline constexpr bool occupied_control(std::uint8_t control) {
    return control >= 2;
}
inline constexpr std::uint8_t unknown_overflow = 255;
inline constexpr auto no_slot = std::numeric_limits<std::size_t>::max();
} // namespace ikea::hashtable::detail
