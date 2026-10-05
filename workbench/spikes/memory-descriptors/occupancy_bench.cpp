#include "occupancy.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
struct Options {
  unsigned ms = 20;
  unsigned repetitions = 3;
  bool reverse = false;
};
void require(bool condition) {
  if (!condition) throw std::runtime_error("occupied-set benchmark check failed");
}
template <class T> void escape(const T &value) {
  asm volatile("" : : "g"(&value) : "memory");
}
struct Case {
  unsigned live;
  bool one_class;
  bool enumerate;
};

template <class Policy>
void run(const char *name, Case test, const Options &options, unsigned repetition) {
  if (test.live > Policy::prepared_capacity()) return;
  Policy policy;
  std::vector<unsigned> coordinates(test.live);
  for (unsigned i = 0; i < test.live; ++i) {
    const auto cls = test.one_class ? 0 : i % descriptors::occupancy_classes;
    const auto coordinate = policy.claim(cls);
    require(coordinate >= 0);
    coordinates[i] = static_cast<unsigned>(coordinate);
  }
  std::vector<unsigned> order(test.live);
  std::iota(order.begin(), order.end(), 0);
  std::mt19937 generator(9241 + test.live);
  std::shuffle(order.begin(), order.end(), generator);
  const auto before = policy.snapshot();
  std::uint64_t coordinate_sum = 0;
  for (auto coordinate : before) coordinate_sum += coordinate + 1;
  std::uint64_t checksum = 0;
  std::uint64_t operations = 0;
  const auto begin = Clock::now();
  auto end = begin;
  do {
    for (unsigned batch = 0; batch < 64; ++batch) {
      if (test.enumerate) {
        policy.for_each_occupied([&](unsigned coordinate) {
          checksum += (coordinate + 1) * 3;
        });
        ++operations;
      } else {
        for (auto index : order) {
          const auto previous = coordinates[index];
          policy.release(previous);
          const auto next = policy.claim(previous & 15);
          require(next >= 0);
          coordinates[index] = static_cast<unsigned>(next);
          checksum += static_cast<unsigned>(next) + 1;
          ++operations;
        }
      }
      escape(policy);
      escape(checksum);
    }
    end = Clock::now();
  } while (end - begin < std::chrono::milliseconds(options.ms));
  require(policy.occupancy() == test.live);
  auto after = policy.snapshot();
  auto expected = test.enumerate ? before : std::vector<std::uint8_t>{};
  if (!test.enumerate) {
    for (auto coordinate : coordinates)
      expected.push_back(static_cast<std::uint8_t>(coordinate));
  }
  std::sort(after.begin(), after.end());
  std::sort(expected.begin(), expected.end());
  require(after == expected);
  const auto expected_checksum = test.enumerate
      ? coordinate_sum * 3 * operations
      : coordinate_sum * (operations / test.live);
  require(checksum == expected_checksum);
  const auto ns = std::chrono::duration<double, std::nano>(end - begin).count();
  std::cout << name << ',' << test.live << ','
            << (test.one_class ? "one_class" : "mixed_classes") << ','
            << (test.enumerate ? "enumerate" : "same_class_reuse") << ','
            << Policy::metadata_bytes() << ',' << Policy::prepared_capacity()
            << ',' << repetition << ',' << operations << ','
            << ns / static_cast<double>(operations) << ','
            << ns / static_cast<double>(operations * (test.enumerate ? test.live : 1))
            << ',' << checksum << '\n';
}
void cases(const Options &options, Case test, unsigned repetition) {
  if (!options.reverse) {
    run<descriptors::occupied_bitmap>("occupied_bitmap", test, options, repetition);
    run<descriptors::occupied_sorted<>>("occupied_sorted_240", test, options, repetition);
    run<descriptors::occupied_unordered<>>("occupied_unordered_240", test, options, repetition);
    run<descriptors::occupied_sorted<16>>("occupied_sorted_16", test, options, repetition);
    run<descriptors::occupied_unordered<16>>("occupied_unordered_16", test, options, repetition);
    run<descriptors::occupied_sorted<64>>("occupied_sorted_64", test, options, repetition);
    run<descriptors::occupied_unordered<64>>("occupied_unordered_64", test, options, repetition);
  } else {
    run<descriptors::occupied_unordered<64>>("occupied_unordered_64", test, options, repetition);
    run<descriptors::occupied_sorted<64>>("occupied_sorted_64", test, options, repetition);
    run<descriptors::occupied_unordered<16>>("occupied_unordered_16", test, options, repetition);
    run<descriptors::occupied_sorted<16>>("occupied_sorted_16", test, options, repetition);
    run<descriptors::occupied_unordered<>>("occupied_unordered_240", test, options, repetition);
    run<descriptors::occupied_sorted<>>("occupied_sorted_240", test, options, repetition);
    run<descriptors::occupied_bitmap>("occupied_bitmap", test, options, repetition);
  }
}
} // namespace

int main(int argc, char **argv) {
  try {
    Options options;
    for (int i = 1; i < argc; ++i) {
      const std::string_view argument = argv[i];
      if (argument == "--quick") { options.ms = 1; options.repetitions = 1; }
      else if (argument == "--reverse") options.reverse = true;
      else if (argument == "--ms" && i + 1 < argc) options.ms = std::stoul(argv[++i]);
      else if (argument == "--repetitions" && i + 1 < argc) options.repetitions = std::stoul(argv[++i]);
      else throw std::runtime_error("unknown occupancy benchmark option");
    }
    require(options.ms > 0 && options.repetitions > 0);
    std::vector<Case> tests;
    for (unsigned live : {1u, 4u, 16u, 64u, 128u, 240u}) {
      tests.push_back({live, false, false});
      tests.push_back({live, false, true});
    }
    tests.push_back({16, true, false});
    tests.push_back({16, true, true});
    if (options.reverse) std::reverse(tests.begin(), tests.end());
    std::cout << "policy,live,distribution,workload,metadata_bytes,prepared_capacity,rep,operations,ns_per_operation,ns_per_item,checksum\n";
    for (unsigned repetition = 0; repetition < options.repetitions; ++repetition)
      for (auto test : tests) cases(options, test, repetition);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
