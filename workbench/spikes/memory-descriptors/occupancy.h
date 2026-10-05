#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace descriptors {

inline constexpr unsigned occupancy_classes = 15;
inline constexpr unsigned homes_per_class = 16;
inline constexpr unsigned occupancy_coordinates =
    occupancy_classes * homes_per_class;

/// One serialized owner. Occupancy includes live, retained and pending homes;
/// release is legal only after the physical home's final use has ended.
class occupied_bitmap {
public:
  int claim(unsigned cls) {
    assert(cls < occupancy_classes);
    const auto available = static_cast<std::uint16_t>(~masks_[cls]);
    if (!available) return -1;
    const auto home = std::countr_zero(available);
    masks_[cls] |= static_cast<std::uint16_t>(1u << home);
    return (home << 4) | cls;
  }
  void release(unsigned coordinate) {
    assert(coordinate < 256 && (coordinate & 15) < occupancy_classes);
    auto &mask = masks_[coordinate & 15];
    const auto bit = static_cast<std::uint16_t>(1u << (coordinate >> 4));
    assert(mask & bit);
    mask &= static_cast<std::uint16_t>(~bit);
  }
  std::size_t occupancy() const;
  static constexpr std::size_t metadata_bytes() { return sizeof(occupied_bitmap); }
  static constexpr std::size_t prepared_capacity() {
    return occupancy_coordinates;
  }
  template <class F> void for_each_occupied(F &&visit) const {
    for (unsigned cls = 0; cls < occupancy_classes; ++cls) {
      auto remaining = masks_[cls];
      while (remaining) {
        const auto home = std::countr_zero(remaining);
        visit(static_cast<std::uint8_t>((home << 4) | cls));
        remaining &= static_cast<std::uint16_t>(remaining - 1);
      }
    }
  }
  std::vector<std::uint8_t> snapshot() const;

private:
  std::array<std::uint16_t, occupancy_classes> masks_{};
};

namespace occupancy_detail {
inline unsigned rank(unsigned coordinate) {
  return ((coordinate & 15) << 4) | (coordinate >> 4);
}
inline bool valid(unsigned coordinate) {
  return coordinate < 256 && (coordinate & 15) < occupancy_classes;
}
} // namespace occupancy_detail

/// Prepared capacity is a real occupancy limit, not the number of logical
/// descriptors. A sparse instance cannot accommodate unbounded old homes.
template <std::size_t Capacity = occupancy_coordinates> class occupied_sorted {
  static_assert(Capacity > 0 && Capacity <= occupancy_coordinates);

public:
  int claim(unsigned cls) {
    assert(cls < occupancy_classes);
    if (count_ == Capacity) return -1;
    const auto begin = coordinates_.begin();
    const auto end = begin + count_;
    auto position = std::lower_bound(
        begin, end, cls << 4,
        [](std::uint8_t coordinate, unsigned wanted_rank) {
          return occupancy_detail::rank(coordinate) < wanted_rank;
        });
    unsigned home = 0;
    while (position != end && (*position & 15) == cls) {
      if ((*position >> 4) != home) break;
      ++home;
      ++position;
    }
    if (home == homes_per_class) return -1;
    std::move_backward(position, end, end + 1);
    const auto coordinate = static_cast<std::uint8_t>((home << 4) | cls);
    *position = coordinate;
    ++count_;
    return coordinate;
  }
  void release(unsigned coordinate) {
    assert(occupancy_detail::valid(coordinate));
    const auto end = coordinates_.begin() + count_;
    auto position = std::lower_bound(
        coordinates_.begin(), end, occupancy_detail::rank(coordinate),
        [](std::uint8_t current, unsigned wanted_rank) {
          return occupancy_detail::rank(current) < wanted_rank;
        });
    assert(position != end && *position == coordinate);
    std::move(position + 1, end, position);
    --count_;
  }
  std::size_t occupancy() const { return count_; }
  static constexpr std::size_t metadata_bytes() { return sizeof(occupied_sorted); }
  static constexpr std::size_t prepared_capacity() { return Capacity; }
  template <class F> void for_each_occupied(F &&visit) const {
    for (unsigned i = 0; i < count_; ++i) visit(coordinates_[i]);
  }
  std::vector<std::uint8_t> snapshot() const {
    return {coordinates_.begin(), coordinates_.begin() + count_};
  }

private:
  std::array<std::uint8_t, Capacity> coordinates_{};
  std::uint8_t count_{};
};

template <std::size_t Capacity = occupancy_coordinates>
class occupied_unordered {
  static_assert(Capacity > 0 && Capacity <= occupancy_coordinates);

public:
  int claim(unsigned cls) {
    assert(cls < occupancy_classes);
    if (count_ == Capacity) return -1;
    std::uint16_t occupied = 0;
    for (unsigned i = 0; i < count_; ++i) {
      const auto coordinate = coordinates_[i];
      if ((coordinate & 15) == cls) occupied |= 1u << (coordinate >> 4);
    }
    const auto available = static_cast<std::uint16_t>(~occupied);
    if (!available) return -1;
    const auto coordinate = static_cast<std::uint8_t>(
        (std::countr_zero(available) << 4) | cls);
    coordinates_[count_++] = coordinate;
    return coordinate;
  }
  void release(unsigned coordinate) {
    assert(occupancy_detail::valid(coordinate));
    unsigned i = 0;
    while (i < count_ && coordinates_[i] != coordinate) ++i;
    assert(i < count_);
    coordinates_[i] = coordinates_[--count_];
  }
  std::size_t occupancy() const { return count_; }
  static constexpr std::size_t metadata_bytes() {
    return sizeof(occupied_unordered);
  }
  static constexpr std::size_t prepared_capacity() { return Capacity; }
  template <class F> void for_each_occupied(F &&visit) const {
    for (unsigned i = 0; i < count_; ++i) visit(coordinates_[i]);
  }
  std::vector<std::uint8_t> snapshot() const {
    return {coordinates_.begin(), coordinates_.begin() + count_};
  }

private:
  std::array<std::uint8_t, Capacity> coordinates_{};
  std::uint8_t count_{};
};

} // namespace descriptors
