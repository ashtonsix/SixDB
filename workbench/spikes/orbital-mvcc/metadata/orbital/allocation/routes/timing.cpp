// Small directional screen. Reuses actual Allocation/Hold operations; not a DB benchmark.
#include "../../shape/native_fixture.h"
#include "internal.h"
#include <array>
#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <sched.h>
#include <thread>
#include <type_traits>
#include <vector>

namespace oa = orbital::allocation;
namespace d = oa::detail;
using Clock = std::chrono::steady_clock;
template<class T> T take(orbital::Result<T> result) {
  if (!result) std::abort(); return std::move(*result);
}
template<class T> oa::Hold retained(T result) {
  if constexpr (std::is_same_v<T, oa::Hold>) return result;
  else return take(std::move(result));
}

std::vector<int> cpus() {
  cpu_set_t mask; CPU_ZERO(&mask);
  if (sched_getaffinity(0, sizeof(mask), &mask)) std::abort();
  std::vector<int> result;
  for (int i = 0; i < CPU_SETSIZE; ++i) if (CPU_ISSET(i, &mask)) result.push_back(i);
  return result;
}
std::vector<int> allowed_cpus;
void pin(int cpu) {
  cpu_set_t mask; CPU_ZERO(&mask); CPU_SET(cpu, &mask);
  if (pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask)) std::abort();
}

void owner(unsigned active) {
  compact_shape::probe::NativeFixture fixture; const auto& allowed = allowed_cpus;
  if (!fixture.open(1ULL << 30, allowed[0])) std::abort();
  std::array<oa::Allocation, 256> values;
  for (unsigned i = 0; i < active; ++i) {
    values[i] = take(oa::allocate(*fixture.pool, {.bytes = 64, .alignment = 1}));
    values[i].address[0] = std::byte(i);
  }
  const auto start = Clock::now();
  unsigned observed{};
  for (unsigned i = 0; i < 1000000; ++i) {
    const auto index = i % active;
    auto first = take(oa::hold(values[index], {0, 64}));
    auto second = retained(oa::retain(first));
    observed += std::to_integer<unsigned>(second.address[0]);
    oa::release(second); oa::release(first);
  }
  const auto ns = std::chrono::duration<double, std::nano>(Clock::now() - start).count() / 1000000;
  std::printf("{\"case\":\"owner-capture-copy-read-release\",\"storage_bytes\":%zu,\"active\":%u,"
      "\"ns\":%.3f,\"observed\":%u,\"pool_metadata\":%llu}\n", sizeof(oa::Storage), active, ns, observed,
      static_cast<unsigned long long>(fixture.statistics().metadata_bytes));
}

void foreign(bool shared_line) {
  const auto& allowed = allowed_cpus;
  if (allowed.size() < 3) return;
  compact_shape::probe::NativeFixture fixture;
  if (!fixture.open(1ULL << 30, allowed[0])) std::abort();
  std::array<oa::Allocation, 256> values;
  for (auto& value : values) value = take(oa::allocate(*fixture.pool, {.bytes = 64, .alignment = 1}));
  constexpr unsigned workers = 2, count = 65536;
  std::array<std::vector<oa::Hold>, workers> held;
  std::array<unsigned, workers> selected{0, 64};
  if (shared_line) {
    for (unsigned i = 0; i + 1 < values.size(); ++i) {
      const auto first = reinterpret_cast<std::uintptr_t>(&values[i].storage->users);
      const auto second = reinterpret_cast<std::uintptr_t>(&values[i + 1].storage->users);
      if (first / 64 == second / 64) { selected = {i, i + 1}; break; }
    }
    if (selected[1] != selected[0] + 1) std::abort();
  }
  for (unsigned worker = 0; worker < workers; ++worker) {
    auto& uses = held[worker]; uses.reserve(count);
    // Distinct identities: adjacent records share a counter cache line, spaced
    // records are separated by at least one line in every layout.
    const auto identity = selected[worker];
    for (unsigned i = 0; i < count; ++i) uses.push_back(take(oa::hold(values[identity], {0, 64})));
  }
  std::barrier ready(workers + 1);
  std::atomic<bool> go{};
  std::array<double, workers> elapsed{};
  std::array<std::thread, workers> threads;
  for (unsigned worker = 0; worker < workers; ++worker) {
    threads[worker] = std::thread([&, worker] {
      pin(allowed[worker + 1]); ready.arrive_and_wait();
      while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
      const auto start = Clock::now();
      for (auto& use : held[worker]) oa::release(use);
      elapsed[worker] = std::chrono::duration<double, std::nano>(Clock::now() - start).count() / count;
    });
  }
  ready.arrive_and_wait(); go.store(true, std::memory_order_release);
  for (auto& thread : threads) thread.join();
  std::printf("{\"case\":\"foreign-nonfinal-release\",\"storage_bytes\":%zu,\"shared_line\":%s,"
      "\"worker0_ns\":%.3f,\"worker1_ns\":%.3f,\"counter_uses\":%u,\"counter_byte_distance\":%llu}\n", sizeof(oa::Storage),
      shared_line ? "true" : "false", elapsed[0], elapsed[1], workers * count,
      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&values[selected[1]].storage->users) - reinterpret_cast<std::uintptr_t>(&values[selected[0]].storage->users)));
}

int main() { allowed_cpus = cpus(); owner(64); owner(256); foreign(false); foreign(true); }
