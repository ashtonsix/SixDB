#include "checks.h"
#include "retention.h"
#include "../candidates/common.h"

#include <array>
#include <cstdio>
#include <limits>
#include <optional>
#include <random>
#include <span>

using compact_retention::DistinctRetention;
using compact_retention::FloorRetention;
using compact_version_probe::Memory;
using Cut = std::uint64_t;
using Cuts = std::vector<Cut>;

std::optional<Cut> selected(std::span<const Cut> history, Cut reader) {
  for (auto version : history) if (version <= reader) return version;
  return std::nullopt;
}

Cuts exact_oracle(std::span<const Cut> history, std::span<const Cut> readers) {
  if (history.empty()) return {};
  Cuts result{history.front()};
  for (auto reader : readers)
    if (auto version = selected(history, reader)) result.push_back(*version);
  std::sort(result.begin(), result.end(), std::greater<Cut>{});
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

Cuts floor_oracle(std::span<const Cut> history, Cut floor) {
  if (history.empty()) return {};
  const auto predecessor = selected(history, floor);
  Cuts result;
  for (auto version : history)
    if (version == history.front() || version > floor || predecessor == version)
      result.push_back(version);
  return result;
}

template<class Plan> Cuts kept(const Plan& plan, std::span<const Cut> history) {
  if (history.empty()) return {};
  Cuts result{history.front()};
  auto cursor = plan.cursor(history.front());
  for (auto version : history.subspan(1)) if (cursor.keep(version)) result.push_back(version);
  return result;
}

void compare(std::span<const Cut> history, std::span<const Cut> readers) {
  CHECK(std::is_sorted(readers.begin(), readers.end()));
  CHECK(std::is_sorted(history.begin(), history.end(), std::greater<Cut>{}));
  const auto floor = readers.empty() ? std::numeric_limits<Cut>::max() : readers.front();
  Memory memory;
  {
    auto visit = [&](auto consume) { for (auto reader : readers) consume(reader); };
    DistinctRetention::Plan exact(memory, readers.size(), floor, visit);
    const auto actual = kept(exact, history);
    CHECK(actual == exact_oracle(history, readers));
    for (auto reader : readers) CHECK(selected(actual, reader) == selected(history, reader));
    CHECK(exact.requested_bytes() == memory.live);
    CHECK(exact.requested_bytes() >= readers.size() * sizeof(Cut));
    std::size_t distinct = 0;
    for (std::size_t i = 0; i < readers.size(); ++i)
      if (!i || readers[i] != readers[i - 1]) ++distinct;
    CHECK(exact.cut_count() == distinct);
    FloorRetention::Plan baseline(memory, readers.size(), floor, visit);
    CHECK(kept(baseline, history) == floor_oracle(history, floor));
    CHECK(baseline.requested_bytes() == 0 && baseline.cut_count() == 0);
  }
  CHECK(memory.live == 0);
}

void boundary_cases() {
  constexpr auto maximum = std::numeric_limits<Cut>::max();
  const std::array histories{
      Cuts{}, Cuts{0}, Cuts{1}, Cuts{maximum}, Cuts{maximum, maximum - 1, 7, 1, 0},
      Cuts{50, 20, 10, 1}, Cuts{10, 8, 6, 4, 2}};
  const std::array readers{
      Cuts{}, Cuts{0}, Cuts{0, 0}, Cuts{maximum}, Cuts{0, 1, maximum - 1, maximum},
      Cuts{0, 3, 7, 7, 10, maximum}, Cuts{1, 2, 9, 10, 19, 20, 49}};
  for (const auto& history : histories) for (const auto& scopes : readers) compare(history, scopes);
}

void exhaustive_small() {
  for (unsigned versions = 0; versions < 256; ++versions) {
    Cuts history;
    for (unsigned cut = 8; cut--;) if (versions & (1u << cut)) history.push_back(cut);
    for (unsigned scopes = 0; scopes < 256; ++scopes) {
      Cuts readers;
      for (unsigned cut = 0; cut < 8; ++cut) if (scopes & (1u << cut)) readers.push_back(cut);
      compare(history, readers);
    }
  }
}

void wide_and_duplicate() {
  Cuts history, readers;
  for (Cut cut = 8192; cut; cut -= 16) history.push_back(cut);
  for (Cut cut = 0; cut <= 8192; ++cut) {
    readers.push_back(cut);
    if (cut % 4 == 0) readers.push_back(cut);
  }
  compare(history, readers);
  std::mt19937_64 random(0x6db9c);
  for (unsigned trial = 0; trial < 256; ++trial) {
    history.clear(); readers.clear();
    for (unsigned i = 0; i < 128; ++i) history.push_back(random());
    std::sort(history.begin(), history.end(), std::greater<Cut>{});
    history.erase(std::unique(history.begin(), history.end()), history.end());
    for (unsigned i = 0; i < 256; ++i) readers.push_back(random());
    readers.insert(readers.end(), {0, 0, std::numeric_limits<Cut>::max()});
    std::sort(readers.begin(), readers.end());
    compare(history, readers);
  }
}

void refusal_and_baseline() {
  Memory memory;
  memory.limit = 0;
  bool visited = false;
  auto visit = [&](auto consume) { visited = true; consume(Cut{0}); };
  FloorRetention::Plan baseline(memory, 1, 0, visit);
  CHECK(!visited && memory.live == 0);
  CHECK(kept(baseline, Cuts{10, 5, 0}) == Cuts({10, 5, 0}));
  try {
    DistinctRetention::Plan refused(memory, 1, 0, visit);
    CHECK(false);
  } catch (const std::bad_alloc&) {}
  CHECK(!visited && memory.live == 0);
  try {
    DistinctRetention::Plan overflow(memory, std::numeric_limits<std::size_t>::max(), 0, visit);
    CHECK(false);
  } catch (const std::bad_alloc&) {}
  CHECK(!visited && memory.live == 0);
  DistinctRetention::Plan empty(memory, 0, 0, [](auto) {});
  CHECK(empty.requested_bytes() == 0 && empty.cut_count() == 0);
  CHECK(kept(empty, Cuts{10, 5, 0}) == Cuts({10}));
}

int main() {
  boundary_cases();
  exhaustive_small();
  wide_and_duplicate();
  refusal_and_baseline();
  std::puts("retention: independent predecessor oracle, exhaustive cuts, wide gaps, duplicates, refusal passed");
}
