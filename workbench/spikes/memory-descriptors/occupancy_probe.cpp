#include "occupancy.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

using Expected = std::array<bool, 256>;
std::size_t assertions = 0;

template <class Policy> void compare(const Policy &policy, const Expected &want) {
  Expected seen{};
  std::size_t count = 0;
  policy.for_each_occupied([&](unsigned coordinate) {
    require(coordinate < seen.size() && (coordinate & 15) < 15,
            "enumeration returned invalid coordinate");
    require(!seen[coordinate], "enumeration duplicated coordinate");
    seen[coordinate] = true;
    ++count;
  });
  require(seen == want, "enumeration differs from independent oracle");
  require(count == policy.occupancy(), "occupancy differs from enumeration");
  require(policy.metadata_bytes() == sizeof(policy), "metadata omits storage");
  Expected snap{};
  for (auto coordinate : policy.snapshot()) {
    require(!snap[coordinate], "snapshot duplicated coordinate");
    snap[coordinate] = true;
  }
  require(snap == want, "snapshot differs from independent oracle");
  ++assertions;
}

template <class Policy>
int claim(Policy &policy, Expected &want, unsigned cls) {
  int expected = -1;
  const auto occupied = std::count(want.begin(), want.end(), true);
  if (static_cast<std::size_t>(occupied) < policy.prepared_capacity()) {
    for (unsigned home = 0; home < 16; ++home) {
      const auto coordinate = 16 * home + cls;
      if (!want[coordinate]) {
        expected = static_cast<int>(coordinate);
        break;
      }
    }
  }
  const auto actual = policy.claim(cls);
  require(actual == expected, "claim did not choose first free home");
  if (actual >= 0) want[actual] = true;
  compare(policy, want);
  return actual;
}

template <class Policy>
void release(Policy &policy, Expected &want, unsigned coordinate) {
  require(want[coordinate], "bad test release");
  policy.release(coordinate);
  want[coordinate] = false;
  compare(policy, want);
}

template <class Policy> void test(const std::string &name) {
  Policy policy;
  Expected expected{};
  compare(policy, expected);
  for (unsigned cls = 0; cls < 15; ++cls) {
    std::vector<unsigned> homes;
    for (unsigned home = 0; home < 16; ++home)
      homes.push_back(static_cast<unsigned>(claim(policy, expected, cls)));
    require(claim(policy, expected, cls) == -1, "full class accepted home");
    // Arbitrary removal, including ends and holes, then lowest-home reuse.
    for (auto home : {7u, 0u, 15u, 3u}) release(policy, expected, homes[home]);
    for (auto home : {0u, 3u, 7u, 15u})
      require(claim(policy, expected, cls) == static_cast<int>(homes[home]),
              "hole selection order changed");
    for (unsigned home : {11u, 1u, 14u, 2u, 13u, 4u, 12u, 5u,
                          10u, 6u, 9u, 7u, 8u, 3u, 0u, 15u})
      release(policy, expected, homes[home]);
  }
  // Physical retention/pending pressure may exceed all sixteen logical heads.
  for (unsigned home = 0; home < 16; ++home)
    for (unsigned cls = 0; cls < 15; ++cls) claim(policy, expected, cls);
  require(policy.occupancy() == 240, "physical pressure was capped at16");
  for (unsigned cls = 0; cls < 15; ++cls) claim(policy, expected, cls);
  std::vector<unsigned> occupied;
  for (unsigned coordinate = 0; coordinate < 256; ++coordinate)
    if (expected[coordinate]) occupied.push_back(coordinate);
  std::mt19937 random(0x51db);
  std::shuffle(occupied.begin(), occupied.end(), random);
  for (auto coordinate : occupied) release(policy, expected, coordinate);
  // Long deterministic mixed pressure, including sparse K=0/1/4/16 and K>16.
  for (unsigned step = 0; step < 20000; ++step) {
    if ((random() % 100) < 53 || policy.occupancy() == 0) {
      claim(policy, expected, random() % 15);
    } else {
      occupied.clear();
      for (unsigned coordinate = 0; coordinate < 256; ++coordinate)
        if (expected[coordinate]) occupied.push_back(coordinate);
      release(policy, expected, occupied[random() % occupied.size()]);
    }
  }
  std::cout << name << ",metadata_bytes=" << policy.metadata_bytes()
            << ",prepared_capacity=" << policy.prepared_capacity() << '\n';
}

template <class Policy> void bounded(const char *name) {
  Policy policy;
  Expected expected{};
  for (std::size_t i = 0; i < policy.prepared_capacity(); ++i)
    require(claim(policy, expected, i % 15) >= 0, "bounded preparation failed");
  require(claim(policy, expected, 14) == -1,
          "sparse list silently exceeded prepared storage");
  const auto old = policy.snapshot().front();
  release(policy, expected, old);
  require(claim(policy, expected, 14) >= 0, "bounded list did not recover");
  std::mt19937 random(0xb01d + policy.prepared_capacity());
  std::vector<unsigned> occupied;
  // Exercise capacity overflow, class exhaustion, gaps and arbitrary removals
  // independently for each sparse representation, not only its dense sibling.
  for (unsigned step = 0; step < 20000; ++step) {
    if ((random() % 100) < 57 || policy.occupancy() == 0) {
      claim(policy, expected, random() % 15);
    } else {
      occupied.clear();
      for (unsigned coordinate = 0; coordinate < 256; ++coordinate)
        if (expected[coordinate]) occupied.push_back(coordinate);
      release(policy, expected, occupied[random() % occupied.size()]);
    }
  }
  occupied.clear();
  for (unsigned coordinate = 0; coordinate < 256; ++coordinate)
    if (expected[coordinate]) occupied.push_back(coordinate);
  std::shuffle(occupied.begin(), occupied.end(), random);
  for (auto coordinate : occupied) release(policy, expected, coordinate);
  for (unsigned home = 0; home < 16; ++home)
    require(claim(policy, expected, 0) == static_cast<int>(home << 4),
            "bounded single-class fill failed");
  require(claim(policy, expected, 0) == -1, "bounded full class accepted home");
  if (policy.prepared_capacity() > 16)
    require(claim(policy, expected, 1) == 1,
            "class exhaustion incorrectly blocked another class");
  for (unsigned home : {8u, 0u, 15u, 3u}) release(policy, expected, home << 4);
  for (unsigned home : {0u, 3u, 8u, 15u})
    require(claim(policy, expected, 0) == static_cast<int>(home << 4),
            "bounded arbitrary holes selected incorrectly");
  std::cout << name << ",metadata_bytes=" << policy.metadata_bytes()
            << ",prepared_capacity=" << policy.prepared_capacity() << '\n';
}
} // namespace

int main() {
  try {
    test<descriptors::occupied_bitmap>("bitmap");
    test<descriptors::occupied_sorted<>>("sorted240");
    test<descriptors::occupied_unordered<>>("unordered240");
    bounded<descriptors::occupied_sorted<16>>("sorted16");
    bounded<descriptors::occupied_unordered<16>>("unordered16");
    bounded<descriptors::occupied_sorted<64>>("sorted64");
    bounded<descriptors::occupied_unordered<64>>("unordered64");
    std::cout << "independent_state_checks=" << assertions << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
