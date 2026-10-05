#include "recycling.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <sched.h>
#include <string_view>
#include <sys/mman.h>
#include <vector>

using namespace recycling;
namespace {
void check(bool ok) { if (!ok) std::abort(); }
void checks() {
  Pool pool; const auto cpu = ::sched_getcpu();
  auto native = pool.acquire(Temperature::hot, cpu); auto* held = native.data(); auto backend = native;
  const auto last_access = std::chrono::steady_clock::now() - std::chrono::milliseconds(20);
  held[0] = std::byte{71}; native.release(Hint{Temperature::hot, cpu, last_access});
  auto other = pool.acquire(Temperature::hot, cpu); check(other.data() != held);
  check(backend.data()[0] == std::byte{71}); backend.close();
  auto reused = pool.acquire(Temperature::hot, cpu); check(reused.data() == held);
  check(reused.hint().cpu == cpu && reused.hint().temperature == Temperature::hot);
  check(reused.hint().last_access == last_access);
  reused.release(Temperature::cold, cpu); other.release(Temperature::cold, cpu);
  pool.trim_cold(0); pool.wait_idle(); check(pool.counts().unmaps == 0);
  auto fallback = pool.acquire(Temperature::hot, cpu); check(pool.counts().fallback == 1);
  fallback.release(Temperature::cold, cpu);
  std::promise<void> entered, proceed; auto waiting = proceed.get_future().share();
  pool.reclaim_hook([&] { entered.set_value(); waiting.wait(); });
  auto warm = pool.acquire(Temperature::warm, cpu);
  warm.release(Temperature::warm, cpu); entered.get_future().wait();
  // The background OS operation is paused. These foreground operations still finish.
  auto fast = pool.acquire(Temperature::warm, cpu); fast.release(Temperature::warm, cpu);
  auto spare = pool.acquire(Temperature::cold, cpu); spare.release(Temperature::cold, cpu);
  pool.reclaim_hook({}); proceed.set_value(); pool.wait_idle(); check(pool.counts().unmaps == 1);
  check(pool.counts().held == 0);
  std::printf("{\"result\":\"pass\",\"page_bytes\":%zu,\"borrowers\":\"captured\",\"cold_reclaim\":\"background\"}\n", page_size());
}
const char* name(Temperature t) { return t == Temperature::hot ? "hot" : t == Temperature::warm ? "warm" : "cold"; }
void benchmark(bool reverse) {
  constexpr int iterations = 10000;
  const int cpu = ::sched_getcpu();
  std::vector<std::pair<Temperature, Temperature>> cases{{Temperature::hot,Temperature::hot},
      {Temperature::warm,Temperature::warm},{Temperature::cold,Temperature::cold},{Temperature::hot,Temperature::cold}};
  if (reverse) std::reverse(cases.begin(), cases.end());
  std::printf("request,release,iterations,mean_ns,release_ns,new_maps,unmaps,reused,fallback\n");
  for (auto [request, release] : cases) {
    Pool pool; { auto seed = pool.acquire(request, cpu); std::memset(seed.data(), 19, page_size()); seed.release(release, cpu); }
    double release_ns{}; const auto before = pool.counts();
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
      auto page = pool.acquire(request, cpu); std::memset(page.data(), static_cast<unsigned char>(i), 64);
      check(page.data()[0] == std::byte(static_cast<unsigned char>(i)));
      const auto a = std::chrono::steady_clock::now(); page.release(release, cpu);
      const auto b = std::chrono::steady_clock::now(); release_ns += std::chrono::duration<double,std::nano>(b-a).count();
    }
    const auto end = std::chrono::steady_clock::now(); const auto after = pool.counts();
    check(after.maps == before.maps && after.unmaps == 0 && after.held == 0);
    std::printf("%s,%s,%d,%.3f,%.3f,%zu,%zu,%zu,%zu\n", name(request),name(release),iterations,
        std::chrono::duration<double,std::nano>(end-start).count()/iterations,release_ns/iterations,
        after.maps-before.maps,after.unmaps,after.reused-before.reused,after.fallback-before.fallback);
  }
}
}
int main(int argc, char** argv) {
  if (argc > 1) benchmark(std::string_view(argv[1]) == "--reverse"); else checks();
}
