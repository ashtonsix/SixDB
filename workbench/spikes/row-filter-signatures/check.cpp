#include "model.h"
#include <iostream>
#include <random>
#include <stdexcept>

using namespace signatures;
int main(int argc, char **argv) {
  try {
    size_t checks = 0;
    std::vector<U> expected, actual;
    std::string text = argc > 1 ? argv[1] : "";
    for (auto c : cases()) {
      if (c.name == "text" && text.empty())
        continue;
      for (size_t n : {size_t(0), size_t(1), size_t(15), size_t(16), size_t(17),
                       size_t(63), size_t(64), size_t(65), size_t(255),
                       size_t(256), size_t(257)}) {
        if (c.name == "text" && !n)
          continue;
        for (U seed : {U(13), U(71)}) {
          auto f = fixture(c, n, 37, text);
          if (c.family == "range") {
            for (auto [lo, hi] :
                 {std::pair(0u, 0u), std::pair(0u, ~0u), std::pair(~0u, ~0u),
                  std::pair(200u, 100u), std::pair(0x1234567fu, 0x12345680u)}) {
              auto q = f.queries[0];
              q.lo = lo;
              q.hi = hi;
              f.queries.push_back(q);
            }
          }
          if (c.family == "text")
            for (std::string term : {"", "a", "ab", "Accident", "\xc3\xa9"}) {
              auto q = f.queries[0];
              q.term = term;
              f.queries.push_back(q);
            }
          for (auto m : methods(c)) {
            auto s = build(f, m, seed);
            for (auto &q : f.queries) {
              auto want = oracle(f, q, &expected);
              Work work;
              auto got = scan(f, s, q, seed, actual, &work);
              if (got != want || actual != expected)
                throw std::runtime_error("Mask/oracle mismatch: " + c.name +
                                         "/" + m + "/" + std::to_string(n));
              if (scan(f, s, q, seed, actual) != want || actual != expected)
                throw std::runtime_error("Uninstrumented mismatch");
              ++checks;
            }
          }
        }
      }
    }
    auto c = cases()[2];
    auto initial = fixture(c, 257, 19);
    for (std::string m :
         {"aos", "tag8", "tag16", "tag32", "shared8", "shared16", "shared32"}) {
      auto f = initial;
      auto s = build(f, m, 13);
      std::mt19937 rng(7);
      for (unsigned k = 0; k < 1024; ++k) {
        auto i = rng() % f.size();
        unsigned field = rng() % 8;
        replace(f.rows[i], s, i, field, rng(), 13);
        if (k % 31 == 0) {
          auto rebuilt = build(f, m, 13);
          if (s.words != rebuilt.words || s.halves != rebuilt.halves ||
              s.bytes != rebuilt.bytes)
            throw std::runtime_error("Replacement/rebuild mismatch");
          for (auto &q : f.queries) {
            auto want = oracle(f, q, &expected);
            if (scan(f, s, q, 13, actual) != want || actual != expected)
              throw std::runtime_error("Mutation scan mismatch");
            ++checks;
          }
        }
      }
    }
    std::cout << "PASS " << checks
              << " full-mask oracle, tail, range-boundary, text, and "
                 "replacement/rebuild checks\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
