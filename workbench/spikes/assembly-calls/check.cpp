#include "calls.h"
#include "table.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>

int main() {
  constexpr auto invalid = std::numeric_limits<std::uint64_t>::max();
  struct Case { std::size_t bytes, minimum; unsigned pattern; std::uint64_t code; };
  // Independent expectations at every class transition and both invalid dimensions.
  constexpr std::array<std::size_t, 7> transitions{128, 256, 512, 1024, 8192, 32768, 65536};
  std::size_t checks = 0;
  auto check = [&](Case c) {
    for (auto salt : {std::uint64_t(0), std::uint64_t(0x123456789abcdef0), invalid}) {
      const auto expected = c.code ^ salt;
      assert(prototype_handwritten(c.bytes, c.minimum, c.pattern, salt) == expected);
      assert(prototype_seed(c.bytes, c.minimum, c.pattern, salt) == expected);
      ++checks;
    }
  };
  for (unsigned pattern = 0; pattern < 2; ++pattern) {
    check({1, 1, pattern, pattern * 8});
    for (unsigned i = 0; i < transitions.size(); ++i) {
      const auto boundary = transitions[i];
      check({boundary - 1, boundary, pattern, pattern * 8 + i});
      check({boundary, boundary, pattern, pattern * 8 + i + 1});
      check({boundary, boundary - 1, pattern, pattern * 8 + i});
    }
    check({invalid, invalid, pattern, pattern * 8 + 7});
    check({0, 1, pattern, invalid});
    check({1, 0, pattern, invalid});
  }
  check({1, 1, 2, invalid});
  check({1, 1, ~0u, invalid});
  PrototypeTable table(713);
  for (std::uint64_t key = 0; key < 513; ++key) {
    assert(prototype_ikea_lookup(&table, key) == 0);
    assert(table.insert(key));
  }
  for (std::uint64_t key = 0; key < 1026; ++key) {
    assert(prototype_ikea_lookup(&table, key) == std::uint64_t(key < 513));
    ++checks;
  }
  std::printf("%zu assembly/module call cases passed\n", checks);
}
