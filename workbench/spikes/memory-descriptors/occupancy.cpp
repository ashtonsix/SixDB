#include "occupancy.h"

namespace descriptors {

std::size_t occupied_bitmap::occupancy() const {
  std::size_t count = 0;
  for (const auto mask : masks_) count += std::popcount(mask);
  return count;
}

std::vector<std::uint8_t> occupied_bitmap::snapshot() const {
  std::vector<std::uint8_t> result;
  result.reserve(occupancy());
  for_each_occupied([&](auto coordinate) { result.push_back(coordinate); });
  return result;
}

} // namespace descriptors
