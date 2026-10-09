// NDEBUG-active falsifiers for the shared routing/packed counter candidates.
#include "../../shape/native_fixture.h"
#include "internal.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <thread>

namespace oa = orbital::allocation;
namespace d = oa::detail;

template<class T> void check(const T& value,
    std::source_location location = std::source_location::current()) {
  if (value) return;
  std::fprintf(stderr, "route check failed at %s:%u\n", location.file_name(), location.line());
  std::abort();
}

template<class T> T take(orbital::Result<T> result) {
  check(result); return std::move(*result);
}

unsigned identity(const oa::Storage& use) {
#if ROUTE_STORAGE == 4
  return use.identity();
#else
  return use.identity;
#endif
}

void drain(compact_shape::probe::NativeFixture& fixture) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (orbital::worker::pending(fixture.lane)) {
    check(std::chrono::steady_clock::now() < deadline);
    orbital::worker::poll(fixture.lane, 64);
  }
}

void saturation(oa::Allocation& allocation) {
  auto held = take(oa::hold(allocation, {0, 1}));
  auto& use = *allocation.storage;
  const auto index = identity(use);
  const auto normal = use.users.load();
  check(normal >= 2);
  // Synthetic count boundary: no millions of real owners are invented.
  use.users.store(d::returning - 1);
  auto refused_copy = oa::retain(held);
  check(!refused_copy && refused_copy.error() == orbital::Error::capacity);
  auto refused_capture = oa::hold(allocation, {0, 1});
  check(!refused_capture && refused_capture.error() == orbital::Error::capacity);
  check(use.users.load() == d::returning - 1 && identity(use) == index);
  use.users.store(d::returning - 2);
  auto last = take(oa::retain(held));
  check(use.users.load() == d::returning - 1 && identity(use) == index);
  oa::release(last);
  check(use.users.load() == d::returning - 2 && identity(use) == index);
  use.users.store(normal); // Restore all actual Allocation/Hold ownership.
  orbital::Error failure{};
  std::thread foreign([&] {
    auto copy = oa::retain(held);
    check(!copy); failure = copy.error();
  });
  foreign.join();
  check(failure == orbital::Error::conflict && use.users.load() == normal);
  oa::release(held);
  check(use.users.load() == normal - 1);
}

void routing_and_lifetimes() {
  compact_shape::probe::NativeFixture fixture; check(fixture.open());
  std::array<oa::Allocation, 256> values;
  std::array<oa::Hold, 256> retained;
  std::array<oa::Storage*, 256> records{};
  std::array<bool, 256> seen{};
  d::Domain* domain{};
  for (unsigned i = 0; i < values.size(); ++i) {
    values[i] = take(oa::allocate(*fixture.pool, {.bytes = 64, .alignment = 1}));
    records[i] = values[i].storage;
    const auto index = identity(*records[i]);
    check(index < 256 && !seen[index]); seen[index] = true;
    auto* found = d::route(*records[i]);
    if (!domain) domain = found;
    check(found == domain && found->finite && found->pool == fixture.pool);
    values[i].address[0] = std::byte(i);
    retained[i] = take(oa::hold(values[i], {0, 64}));
  }
  auto& group = *static_cast<d::Group*>(domain->context);
  check(group.active_identities == 256);
  // Include identity 255: its high byte must survive every packed count update.
  for (auto& value : values) if (identity(*value.storage) == 255) saturation(value);
  auto window = take(oa::allocate(*fixture.pool, {.bytes = 8192, .alignment = 1}));
  auto* window_route = d::route(*window.storage);
  check(window_route != domain && !window_route->finite && window_route->pool == fixture.pool);
  saturation(window);
  auto window_hold = take(oa::hold(window, {0, 8192}));
  auto window_extra = take(oa::retain(window_hold));
  std::thread window_advice([&] { oa::release(window_extra, {oa::Temperature::hot}); });
  window_advice.join();
  oa::release(window);
  for (auto& value : values) oa::release(value);
  check(group.active_identities == 256 && group.active_lifetime);
  std::array<std::thread, 8> publishers;
  for (unsigned worker = 0; worker < publishers.size(); ++worker) {
    publishers[worker] = std::thread([&, worker] {
      for (unsigned i = worker; i < retained.size(); i += publishers.size()) {
        check(retained[i].address[0] == std::byte(i));
        oa::release(retained[i], {oa::Temperature::warm});
      }
    });
  }
  for (auto& publisher : publishers) publisher.join();
  std::thread window_final([&] { oa::release(window_hold); }); window_final.join();
  check(domain->queued.load() && domain->lifetime && group.active_identities == 256);
  for (auto* record : records) check(record->users.load() == d::returning);
  drain(fixture);
  check(!domain->queued.load() && !domain->lifetime && !group.active_lifetime && group.active_identities == 0);
  check(fixture.statistics().foreign_returns == 257);
  for (unsigned i = 0; i < values.size(); ++i) {
    values[i] = take(oa::allocate(*fixture.pool, {.bytes = 64, .alignment = 1}));
    check(values[i].storage == records[i] && d::route(*records[i]) == domain);
  }
  for (auto& value : values) oa::release(value);
  auto arena = orbital::Ref<oa::Arena>::retain(*fixture.arena);
  check(fixture.close()); check(arena->charged_bytes.load() == 0); arena.reset();
}

void held_growth_preserves_bytes() {
  compact_shape::probe::NativeFixture fixture; check(fixture.open());
  auto allocation = take(oa::allocate(*fixture.pool,
      {.bytes = 63, .reserved_bytes = 96, .alignment = 1, .readable_padding = 1}));
  allocation.address[63] = std::byte{0xd3};
  auto held = take(oa::hold(allocation, {63, 1}));
  check(oa::grow(allocation, {.bytes = 63, .alignment = 1}));
  check(oa::grow(allocation, {.bytes = 80, .alignment = 1, .readable_padding = 1}));
  check(held.address[0] == std::byte{0xd3});
  auto refused = oa::grow(allocation, {.bytes = 1536, .alignment = 1});
  check(!refused && refused.error() == orbital::Error::conflict);
  oa::release(held);
  check(oa::grow(allocation, {.bytes = 1536, .alignment = 1}));
  check(allocation.address[63] == std::byte{0xd3});
  oa::release(allocation); check(fixture.close());
}

int main() {
  routing_and_lifetimes(); held_growth_preserves_bytes();
  std::printf("{\"storage_bytes\":%zu,\"group_bytes\":%zu,\"window_bytes\":%zu,"
      "\"max_live_uses\":%u,\"route_lifetime_overflow_growth\":true}\n",
      sizeof(oa::Storage), sizeof(d::Group), sizeof(d::Window), d::returning - 1);
}
