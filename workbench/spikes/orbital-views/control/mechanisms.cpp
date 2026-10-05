#include "mechanisms.h"

#include <algorithm>
#include <array>
#include <barrier>
#include <bit>
#include <cassert>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <sched.h>

namespace control {
namespace {
using Clock = std::chrono::steady_clock;
double ns(Clock::time_point start) {
  return std::chrono::duration<double, std::nano>(Clock::now() - start).count();
}
struct alignas(128) Object {
  std::mutex owner;
  std::array<std::uint64_t, 8> values{};
  std::uint64_t sum{};
  std::atomic<std::uint64_t> installed{0};
};
struct alignas(128) Latch { std::mutex owner; };

struct Ordered {
  std::set<std::size_t> ready;
  explicit Ordered(std::size_t) {}
  void clear() { ready.clear(); }
  bool has(std::size_t p) const { return ready.contains(p); }
  void put(std::size_t p) { ready.insert(p); }
};
struct Bitmap {
  std::vector<std::uint64_t> ready;
  explicit Bitmap(std::size_t pages) : ready((pages + 63) / 64) {}
  void clear() { std::fill(ready.begin(), ready.end(), 0); }
  bool has(std::size_t p) const { return ready[p / 64] & (1ULL << (p % 64)); }
  void put(std::size_t p) { ready[p / 64] |= 1ULL << (p % 64); }
};
using Page = std::array<std::uint64_t, 512>;
struct Backing { explicit Backing(std::size_t n) : pages(n) {} std::vector<Page> pages; };
std::uint64_t seed(Backing& image) {
  std::uint64_t sum = 0;
  for (std::size_t p = 0; p < image.pages.size(); ++p)
    for (std::size_t w = 0; w < image.pages[p].size(); ++w) {
      image.pages[p][w] = (p + 1) * 101 + w * 7;
      sum += image.pages[p][w];
    }
  return sum;
}
std::uint64_t verify(const Backing& image, std::span<const std::size_t> selected,
                     std::size_t iterations) {
  std::vector<bool> touched(image.pages.size());
  for (auto p : selected) { assert(p < touched.size() && !touched[p]); touched[p] = true; }
  std::uint64_t actual = 0;
  for (std::size_t p = 0; p < image.pages.size(); ++p)
    for (std::size_t w = 0; w < image.pages[p].size(); ++w) {
      const auto expected = (p + 1) * 101 + w * 7 + (touched[p] && w < 8 ? iterations : 0);
      assert(image.pages[p][w] == expected); actual += image.pages[p][w];
    }
  return actual;
}

template<class Ready>
Receipt ready_work(bool cached, std::size_t pages, std::span<const std::size_t> selected,
                   std::size_t iterations) {
  Ready ready(pages);
  Backing image(pages);
  if (cached) for (auto p : selected) ready.put(p);
  auto sum = seed(image);
  auto start = Clock::now();
  for (std::size_t i = 0; i < iterations; ++i) {
    if (!cached) ready.clear();
    for (auto p : selected) if (!ready.has(p)) ready.put(p);
    // A second consumer/stage asks the same readiness question. Both policies
    // perform identical native stores and maintain the same exact summary.
    for (auto p : selected) {
      if (!ready.has(p)) throw std::runtime_error("unready page");
      for (unsigned w = 0; w < 8; ++w) { ++image.pages[p][w]; ++sum; }
    }
    std::atomic_signal_fence(std::memory_order_seq_cst);
  }
  const auto elapsed = ns(start);
  const auto actual = verify(image, selected, iterations);
  assert(actual == sum);
  return {iterations, iterations * selected.size(), actual, 0, elapsed};
}
}  // namespace

Prefix::Prefix(std::size_t epochs) : size_(epochs),
    bits_(std::make_unique<std::atomic<std::uint64_t>[]>((epochs + 63) / 64)) {
  for (std::size_t w = 0; w < (epochs + 63) / 64; ++w) bits_[w].store(0);
}
void Prefix::complete(std::size_t epoch) {
  assert(epoch && epoch <= size_);
  const auto index = epoch - 1;
  bits_[index / 64].fetch_or(1ULL << (index % 64), std::memory_order_release);
  auto current = frontier_.load(std::memory_order_acquire);
  while (current < size_) {
    const auto word = bits_[current / 64].load(std::memory_order_acquire) >> (current % 64);
    const auto count = std::min<std::size_t>(std::countr_one(word), 64 - current % 64);
    if (!count) return;
    const auto next = std::min(current + count, size_);
    if (frontier_.compare_exchange_weak(current, next, std::memory_order_acq_rel)) current = next;
  }
}
std::size_t Prefix::load() const { return frontier_.load(std::memory_order_acquire); }

std::vector<int> available_cpus() {
  cpu_set_t set;
  CPU_ZERO(&set);
  if (sched_getaffinity(0, sizeof(set), &set)) throw std::runtime_error("sched_getaffinity");
  std::vector<int> cpus;
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) if (CPU_ISSET(cpu, &set)) cpus.push_back(cpu);
  return cpus;
}
void pin_thread(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set); CPU_SET(cpu, &set);
  if (sched_setaffinity(0, sizeof(set), &set)) throw std::runtime_error("sched_setaffinity");
}

Receipt ownership(Owners policy, unsigned threads, bool shared_group, bool cut,
                  std::size_t iterations, std::span<const int> cpus) {
  assert(threads && cpus.size() >= threads);
  std::vector<std::unique_ptr<Object>> objects;
  for (unsigned n = 0; n < threads * 8; ++n) objects.push_back(std::make_unique<Object>());
  Latch global;
  std::array<Latch, 4> groups;
  Prefix prefix(threads * iterations);
  std::barrier gate(threads + 1);
  std::vector<std::thread> workers;
  for (unsigned t = 0; t < threads; ++t) workers.emplace_back([&, t] {
    pin_thread(cpus[t]);
    gate.arrive_and_wait();
    for (std::size_t i = 0; i < iterations; ++i) {
      auto& object = *objects[t * 8 + i % 8];
      const auto group = shared_group ? i % 4 : t % 4;
      auto& mutex = policy == Owners::global ? global.owner :
          policy == Owners::group ? groups[group].owner : object.owner;
      {
        std::lock_guard hold(mutex);
        for (auto& value : object.values) value += t + 1;
        object.sum += 8 * (t + 1);
        const auto epoch = i * threads + t + 1;
        // Reader metadata is one release publication. Payload remains protected
        // by its owner here; this benchmark has no concurrent payload readers.
        object.installed.store((epoch << 32) | object.sum, std::memory_order_release);
        if (cut) prefix.complete(epoch);
      }
    }
    gate.arrive_and_wait();
  });
  auto start = Clock::now();
  gate.arrive_and_wait();
  gate.arrive_and_wait();
  const auto elapsed = ns(start);
  for (auto& worker : workers) worker.join();
  std::uint64_t actual = 0, expected = 0;
  for (unsigned t = 0; t < threads; ++t) {
    expected += iterations * 8 * (t + 1);
    for (unsigned j = 0; j < 8; ++j) {
      const auto count = iterations / 8 + (j < iterations % 8);
      const auto& object = *objects[t * 8 + j];
      for (auto value : object.values) assert(value == count * (t + 1));
      assert(object.sum == count * (t + 1) * 8);
      assert((object.installed.load() & 0xffffffffULL) == object.sum);
      actual += object.sum;
    }
  }
  assert(actual == expected && (!cut || prefix.load() == threads * iterations));
  return {iterations * threads, iterations * threads, actual, 0, elapsed};
}

Receipt readiness(Metadata policy, bool cached, std::size_t pages,
                  std::span<const std::size_t> selected, std::size_t iterations) {
  for (auto p : selected) assert(p < pages);
  return policy == Metadata::ordered ? ready_work<Ordered>(cached, pages, selected, iterations) :
                                      ready_work<Bitmap>(cached, pages, selected, iterations);
}

Receipt lifetime(Lifetime policy, std::size_t pages,
                 std::span<const std::size_t> selected, std::size_t iterations) {
  auto owner = std::make_shared<Backing>(pages);
  std::vector<std::shared_ptr<Backing>> borrows;
  borrows.reserve(selected.size());
  auto sum = seed(*owner);
  std::uint64_t holds = 0;
  auto start = Clock::now();
  for (std::size_t i = 0; i < iterations; ++i) {
    if (policy == Lifetime::window) {
      auto window = owner; ++holds;
      auto* data = window->pages.data();
      for (auto p : selected) for (unsigned w = 0; w < 8; ++w) { ++data[p][w]; ++sum; }
    } else if (policy == Lifetime::page) {
      for (auto p : selected) { (void)p; borrows.push_back(owner); ++holds; }
      for (std::size_t n = 0; n < selected.size(); ++n) {
        auto* data = borrows[n]->pages.data();
        for (unsigned w = 0; w < 8; ++w) { ++data[selected[n]][w]; ++sum; }
      }
      borrows.clear();
    } else {
      for (auto p : selected) for (unsigned w = 0; w < 8; ++w) {
        auto grant = owner; ++holds;
        ++grant->pages[p][w]; ++sum;
      }
    }
    std::atomic_signal_fence(std::memory_order_seq_cst);
  }
  const auto elapsed = ns(start);
  const auto actual = verify(*owner, selected, iterations);
  assert(actual == sum);
  assert(owner.use_count() == 1);
  return {iterations, iterations * selected.size(), actual, holds, elapsed};
}

Receipt history(History policy, std::size_t revisions, std::size_t iterations) {
  struct Revision { std::uint64_t epoch, value; };
  std::map<std::uint64_t, Revision> ordered;
  std::vector<Revision> contiguous;
  contiguous.reserve(revisions);
  for (std::size_t i = 0; i < revisions; ++i) {
    Revision r{i * 3 + 7, i * 17 + 11};
    ordered.emplace(r.epoch, r); contiguous.push_back(r);
  }
  std::uint64_t actual = 0, expected = 0;
  std::vector<std::size_t> indexes(iterations);
  for (std::size_t i = 0; i < iterations; ++i) {
    indexes[i] = (i * 73 + 19) % revisions;
    expected += indexes[i] * 17 + 11;
  }
  auto start = Clock::now();
  for (auto index : indexes) {
    const auto epoch = index * 3 + 7;
    if (policy == History::ordered) {
      auto at = ordered.upper_bound(epoch); assert(at != ordered.begin());
      actual += std::prev(at)->second.value;
    } else if (policy == History::contiguous) {
      auto at = std::upper_bound(contiguous.begin(), contiguous.end(), epoch,
          [](auto e, const auto& r) { return e < r.epoch; });
      actual += std::prev(at)->value;
    } else {
      const auto& captured = contiguous[index];
      assert(captured.epoch == epoch);
      actual += captured.value;
    }
    std::atomic_signal_fence(std::memory_order_seq_cst);
  }
  auto elapsed = ns(start);
  assert(actual == expected);
  return {iterations, iterations, actual, 0, elapsed};
}

Receipt combined(bool compact, std::size_t blocks,
                 std::span<const std::size_t> selected, std::size_t iterations) {
  // These are 64-byte damage blocks, not OS pages or a permission mechanism.
  using Block = std::array<std::uint64_t, 8>;
  struct Memory { explicit Memory(std::size_t n) : data(n) {} std::vector<Block> data; };
  struct Snapshot { std::uint64_t epoch, sum; std::vector<Block> values; };
  auto home = std::make_shared<Memory>(blocks);
  Ordered ordered_ready(blocks), ordered_damage(blocks);
  Bitmap bit_ready(blocks), bit_damage(blocks);
  std::vector<std::shared_ptr<Memory>> borrows;
  borrows.reserve(selected.size());
  std::vector<std::size_t> damaged;
  damaged.reserve(selected.size());
  std::atomic<std::shared_ptr<const Snapshot>> head;
  Prefix prefix(iterations);
  std::uint64_t sum = 0, holds = 0;
  for (std::size_t b = 0; b < blocks; ++b) for (unsigned w = 0; w < 8; ++w) {
    home->data[b][w] = (b + 1) * 101 + w * 7; sum += home->data[b][w];
  }
  auto start = Clock::now();
  for (std::size_t i = 0; i < iterations; ++i) {
    ordered_ready.clear(); ordered_damage.clear(); bit_ready.clear(); bit_damage.clear();
    for (auto b : selected) {
      if (compact) { bit_ready.put(b); bit_damage.put(b); }
      else { ordered_ready.put(b); ordered_damage.put(b); }
    }
    auto window = compact ? home : std::shared_ptr<Memory>{};
    if (compact) ++holds;
    else for (auto b : selected) { (void)b; borrows.push_back(home); ++holds; }
    // The same kernel and store granularity in both cases.
    auto* data = home->data.data();
    for (auto b : selected) {
      assert(compact ? bit_ready.has(b) : ordered_ready.has(b));
      for (auto& value : data[b]) ++value;
      sum += 8;
    }
    borrows.clear(); window.reset();
    damaged.clear();
    if (compact) {
      for (std::size_t w = 0; w < bit_damage.ready.size(); ++w) {
        auto bits = bit_damage.ready[w];
        while (bits) {
          damaged.push_back(w * 64 + std::countr_zero(bits)); bits &= bits - 1;
        }
      }
    } else for (auto b : ordered_damage.ready) damaged.push_back(b);
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->epoch = i + 1; snapshot->sum = sum; snapshot->values.resize(damaged.size());
    for (std::size_t n = 0; n < damaged.size(); ++n) snapshot->values[n] = data[damaged[n]];
    head.store(std::move(snapshot), std::memory_order_release);
    prefix.complete(i + 1);
  }
  const auto elapsed = ns(start);
  std::vector<bool> touched(blocks);
  for (auto b : selected) { assert(b < blocks && !touched[b]); touched[b] = true; }
  std::uint64_t actual = 0;
  for (std::size_t b = 0; b < blocks; ++b) for (unsigned w = 0; w < 8; ++w) {
    const auto value = home->data[b][w];
    assert(value == (b + 1) * 101 + w * 7 + (touched[b] ? iterations : 0)); actual += value;
  }
  auto last = head.load();
  assert(last && last->epoch == iterations && last->sum == actual && prefix.load() == iterations);
  assert(last->values.size() == selected.size());
  std::vector<std::size_t> expected_layout(selected.begin(), selected.end());
  std::sort(expected_layout.begin(), expected_layout.end());
  for (std::size_t n = 0; n < expected_layout.size(); ++n) for (unsigned w = 0; w < 8; ++w)
    assert(last->values[n][w] == (expected_layout[n] + 1) * 101 + w * 7 + iterations);
  return {iterations, iterations * selected.size(), actual, holds, elapsed};
}
}  // namespace control
