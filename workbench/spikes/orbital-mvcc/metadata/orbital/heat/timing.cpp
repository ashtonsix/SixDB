// Directional local screen, separate from Tooling's complete-operation comparison.
#include "heat.h"
#include "../shape/native_fixture.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <sched.h>
#include <thread>

namespace oa = orbital::allocation;
namespace d = oa::detail;
using Clock = std::chrono::steady_clock;
template<class T> T take(orbital::Result<T> r) { if (!r) std::abort(); return std::move(*r); }
int cpu() {
  cpu_set_t allowed; CPU_ZERO(&allowed);
  if (sched_getaffinity(0, sizeof(allowed), &allowed)) std::abort();
  for (int i = 0; i < CPU_SETSIZE; ++i) if (CPU_ISSET(i, &allowed)) return i;
  std::abort();
}
template<class F> double measure(F&& operation, unsigned n) {
  const auto start = Clock::now();
  for (unsigned i = 0; i < n; ++i) operation(i);
  return std::chrono::duration<double, std::nano>(Clock::now() - start).count() / n;
}
void point(unsigned bytes, bool hinted) {
  compact_shape::probe::NativeFixture native;
  if (!native.open(1ULL << 30, cpu())) std::abort();
  std::array<oa::Allocation, 256> warm;
  for (auto& item : warm) item = take(oa::allocate(*native.pool, {.bytes = bytes, .alignment = 1}));
  auto& group = *static_cast<d::Group*>(warm[0].storage->domain->context);
  const auto basis = Clock::time_point{Clock::duration{INT64_C(1) << 50}};
  const auto initial_metadata = native.statistics().metadata_bytes;
  for (auto& item : warm) oa::release(item);
  const auto ns = measure([&](unsigned i) {
    auto allocation = take(oa::allocate(*native.pool, {.bytes = bytes, .alignment = 1}));
    if (hinted) oa::release(allocation, {.temperature = oa::Temperature::warm, .last_access = basis + Clock::duration{i + 1}});
    else oa::release(allocation);
  }, 2000000);
  std::printf("{\"case\":\"actual-local-allocate-release\",\"bytes\":%u,\"hinted\":%s,\"group_size\":%zu,"
      "\"pool_metadata\":%llu,\"ns_per_cycle\":%.3f}\n", bytes, hinted ? "true" : "false", sizeof(group),
      static_cast<unsigned long long>(initial_metadata), ns);
}
void updates(unsigned bytes) {
  compact_shape::probe::NativeFixture native;
  if (!native.open(1ULL << 30, cpu())) std::abort();
  auto allocation = take(oa::allocate(*native.pool, {.bytes = bytes, .alignment = 1}));
  auto& group = *static_cast<d::Group*>(allocation.storage->domain->context);
  const auto home = take(group.geometry->bound_home({allocation.storage->identity}));
  const auto boundary = Clock::time_point{Clock::duration{INT64_C(1) << 50}};
  const auto period = Clock::duration{1000000};
  orbital_heat::GroupAdvice aggregate(group.domain.mutex, native.lane);
  orbital_heat::EpochBanks spatial(*group.geometry, group.palette, group.domain.mutex, boundary, period);
  orbital_heat::EpochBanks coarse(*group.geometry, group.palette, group.domain.mutex, boundary, period, true);
  const auto group_ns = measure([&](unsigned) { aggregate.local({oa::Temperature::hot}); }, 2000000);
  const auto coarse_ns = measure([&](unsigned) { coarse.local(home, {oa::Temperature::hot}); }, 2000000);
  const auto spatial_ns = measure([&](unsigned) { spatial.local(home, {oa::Temperature::hot}); }, 2000000);
  double group_foreign_ns{}, spatial_foreign_ns{};
  std::thread foreign_group([&] { group_foreign_ns = measure([&](unsigned) { aggregate.foreign({oa::Temperature::hot}); }, 200000); });
  foreign_group.join();
  while (orbital::worker::pending(native.lane)) orbital::worker::poll(native.lane, 32);
  std::thread foreign_spatial([&] { spatial_foreign_ns = measure([&](unsigned) { spatial.foreign(home, {oa::Temperature::hot}); }, 200000); });
  foreign_spatial.join();
  const auto rollover_ns = measure([&](unsigned i) { if (!spatial.rollover(boundary + period * (i + 1))) std::abort(); }, 10000);
  std::printf("{\"case\":\"prototype-updates\",\"bytes\":%u,\"group_ns\":%.3f,\"group_epoch_ns\":%.3f,"
      "\"spatial_ns\":%.3f,\"group_foreign_ns\":%.3f,\"spatial_foreign_ns\":%.3f,\"rollover_ns\":%.3f,\"bytes_cleared\":%llu,\"bank_request\":%zu,\"bank_usable\":%zu}\n",
      bytes, group_ns, coarse_ns, spatial_ns, group_foreign_ns, spatial_foreign_ns, rollover_ns, static_cast<unsigned long long>(spatial.bytes_cleared),
      spatial.bank_bytes(), spatial.heap_usable());
  oa::release(allocation);
}
int main() { for (auto bytes : {64u, 1536u}) for (auto hinted : {false, true}) point(bytes, hinted); updates(48); updates(1536); }
