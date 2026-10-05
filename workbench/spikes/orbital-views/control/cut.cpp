#include "cut.h"

#include <algorithm>
#include <array>
#include <barrier>
#include <cassert>
#include <chrono>
#include <thread>

namespace control {
namespace {
using Clock = std::chrono::steady_clock;
double ns(Clock::time_point start) {
  return std::chrono::duration<double, std::nano>(Clock::now() - start).count();
}
struct Version {
  std::size_t epoch{};
  std::uint64_t sum{};
  std::array<std::uint64_t, 8> bytes{};
};
struct alignas(128) Head { std::atomic<const Version*> value{nullptr}; };
struct alignas(128) Work { CutReceipt receipt; };

// Each version is written once. The fixture retains its source until all
// queries finish, independently of progress metadata and the latest local head.
class Published {
 public:
  Published(unsigned producers, std::size_t rounds) : producers_(producers),
      versions_(producers), heads_(std::make_unique<Head[]>(producers)) {
    for (auto& versions : versions_) versions.resize(rounds);
  }
  void install(unsigned producer, std::size_t round) {
    auto& v = versions_[producer][round - 1];
    v.epoch = (round - 1) * producers_ + producer + 1;
    for (unsigned w = 0; w < 8; ++w) { v.bytes[w] = v.epoch * 17 + w * 3; v.sum += v.bytes[w]; }
    heads_[producer].value.store(&v, std::memory_order_release);
  }
  std::uint64_t read(std::size_t epoch) const {
    if (!epoch) return 0;
    const auto producer = (epoch - 1) % producers_;
    const auto round = (epoch - 1) / producers_;
    const auto* latest = heads_[producer].value.load(std::memory_order_acquire);
    assert(latest && latest->epoch >= epoch);
    const auto& v = versions_[producer][round];
    assert(v.epoch == epoch);
    std::uint64_t sum = 0;
    for (unsigned w = 0; w < 8; ++w) {
      assert(v.bytes[w] == epoch * 17 + w * 3); sum += v.bytes[w];
    }
    assert(v.sum == sum);
    return sum;
  }
  std::uint64_t verify(std::size_t rounds) const {
    std::uint64_t actual = 0, expected = 0;
    for (std::size_t e = 1; e <= rounds * producers_; ++e) {
      actual += read(e); expected += e * 17 * 8 + 84;
    }
    assert(actual == expected); return actual;
  }
 private:
  unsigned producers_;
  std::vector<std::vector<Version>> versions_;
  std::unique_ptr<Head[]> heads_;
};
}  // namespace

Cuts::Cuts(CutKind kind, unsigned producers, std::size_t rounds)
    : kind_(kind), producers_(producers), rounds_(rounds) {
  assert(producers && rounds);
  if (kind == CutKind::bitmap) prefix_ = std::make_unique<Prefix>(producers * rounds);
  else progress_ = std::make_unique<Progress[]>(producers);
}
bool Cuts::complete(unsigned producer, std::size_t round) {
  assert(producer < producers_ && round && round <= rounds_);
  if (kind_ == CutKind::bitmap) {
    prefix_->complete((round - 1) * producers_ + producer + 1); return true;
  }
  auto& progress = progress_[producer].rounds;
  if (round != progress.load(std::memory_order_relaxed) + 1) return false;
  progress.store(round, std::memory_order_release); return true;
}
std::size_t Cuts::query() const {
  if (kind_ == CutKind::bitmap) return prefix_->load();
  auto cut = rounds_ * producers_;
  for (unsigned p = 0; p < producers_; ++p) {
    // next missing epoch = completed rounds * producer count + producer + 1.
    const auto completed = progress_[p].rounds.load(std::memory_order_acquire);
    cut = std::min(cut, completed * producers_ + p);
  }
  return cut;
}

CutReceipt cut_updates(CutKind kind, unsigned producers, std::size_t cadence,
                      std::size_t rounds, std::span<const int> cpus) {
  assert(cpus.size() >= producers);
  Cuts cuts(kind, producers, rounds);
  Published history(producers, rounds);
  auto work = std::make_unique<Work[]>(producers);
  std::barrier gate(producers + 1);
  std::vector<std::thread> workers;
  for (unsigned p = 0; p < producers; ++p) workers.emplace_back([&, p] {
    pin_thread(cpus[p]);
    CutReceipt local;
    std::size_t observed_cut = 0, unqueried = 0;
    gate.arrive_and_wait();
    for (std::size_t round = 1; round <= rounds; ++round) {
      history.install(p, round);
      assert(cuts.complete(p, round));
      const auto epoch = (round - 1) * producers + p + 1;
      ++unqueried;
      local.max_unqueried_updates = std::max(local.max_unqueried_updates, unqueried);
      local.max_head_distance = std::max(local.max_head_distance, epoch > observed_cut ? epoch - observed_cut : 0);
      if (cadence && round % cadence == 0) {
        const auto now = cuts.query(); assert(now >= observed_cut); observed_cut = now;
        local.query_checksum += history.read(now); ++local.queries; unqueried = 0;
      }
    }
    work[p].receipt = local;
    gate.arrive_and_wait();
  });
  auto start = Clock::now();
  gate.arrive_and_wait(); gate.arrive_and_wait();
  // Both policies also pay the same final reader query/source resolution.
  CutReceipt result;
  result.final_cut = cuts.query(); result.query_checksum = history.read(result.final_cut);
  result.elapsed_ns = ns(start);
  for (auto& worker : workers) worker.join();
  result.publications = producers * rounds; result.queries = 1;
  result.checksum = history.verify(rounds);
  assert(result.final_cut == result.publications);
  for (unsigned p = 0; p < producers; ++p) {
    const auto& local = work[p].receipt;
    result.queries += local.queries; result.query_checksum += local.query_checksum;
    result.max_head_distance = std::max(result.max_head_distance, local.max_head_distance);
    result.max_unqueried_updates = std::max(result.max_unqueried_updates, local.max_unqueried_updates);
  }
  return result;
}

CutReceipt cut_queries(CutKind kind, unsigned producers, std::size_t queries) {
  Cuts cuts(kind, producers, 1);
  Published history(producers, 1);
  for (unsigned p = 0; p < producers; ++p) { history.install(p, 1); assert(cuts.complete(p, 1)); }
  CutReceipt result;
  auto start = Clock::now();
  for (std::size_t n = 0; n < queries; ++n) {
    const auto cut = cuts.query(); assert(cut == producers);
    result.query_checksum += history.read(cut);
  }
  result.elapsed_ns = ns(start); result.queries = queries; result.final_cut = cuts.query();
  assert(result.query_checksum == queries * (producers * 17 * 8 + 84));
  return result;
}
}  // namespace control
