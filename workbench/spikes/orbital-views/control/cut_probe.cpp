#include "cut.h"

#include <array>
#include <cassert>
#include <iostream>
#include <random>
#include <thread>

int main() {
  // Independently scan actual completed IDs, not the counter/min formula.
  constexpr unsigned producers = 4;
  constexpr std::size_t rounds = 67;
  control::Cuts bitmap(control::CutKind::bitmap, producers, rounds);
  control::Cuts local(control::CutKind::producers, producers, rounds);
  std::array<std::size_t, producers> issued{};
  std::array<bool, producers * rounds> installed{};
  std::mt19937 random(317);
  for (std::size_t n = 0; n < installed.size(); ++n) {
    unsigned p;
    do { p = random() % producers; } while (issued[p] == rounds);
    const auto epoch = issued[p] * producers + p + 1;
    installed[epoch - 1] = true;
    assert(bitmap.complete(p, ++issued[p])); assert(local.complete(p, issued[p]));
    std::size_t expected = 0;
    while (expected < installed.size() && installed[expected]) ++expected;
    assert(bitmap.query() == expected && local.query() == expected);
  }
  assert(local.query() == installed.size());

  control::Cuts holes(control::CutKind::producers, 2, 3);
  assert(holes.complete(1, 1) && holes.complete(1, 2));
  assert(holes.query() == 0);  // Other producer has failed/pending epoch 1.
  assert(!holes.complete(0, 2) && holes.query() == 0);
  assert(holes.complete(0, 1) && holes.query() == 2);
  assert(!holes.complete(0, 3) && holes.query() == 2);
  assert(holes.complete(0, 2) && holes.query() == 4);
  // A cached cutoff really lags completed work between queries. No concurrent
  // oracle or clock is needed to establish this deterministic cadence behavior.
  const auto cached = holes.query();
  assert(holes.complete(0, 3) && holes.complete(1, 3));
  assert(cached == 4 && holes.query() == 6);

  auto cpus = control::available_cpus();
  for (unsigned p : {1U, 2U, 4U}) if (p <= cpus.size())
    for (auto kind : {control::CutKind::bitmap, control::CutKind::producers})
      for (std::size_t cadence : {0U, 1U, 16U}) {
        const auto result = control::cut_updates(kind, p, cadence, 257, cpus);
        assert(result.publications == p * 257 && result.final_cut == p * 257);
        assert(result.queries == 1 + (cadence ? p * (257 / cadence) : 0));
        assert(result.max_unqueried_updates == (cadence ? cadence : 257));
      }
  for (unsigned p : {1U, 2U, 4U}) for (auto kind : {control::CutKind::bitmap, control::CutKind::producers})
    control::cut_queries(kind, p, 1009);
  std::cout << "{\"status\":\"passed\",\"cases\":\"independent-prefix-oracle,gaps,rejected-local-reordering,query-lag,immutable-bytes,monotone-concurrent-cut\"}\n";
}
