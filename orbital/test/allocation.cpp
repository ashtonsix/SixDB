#include <orbital/allocation.h>
#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
#include <unistd.h>
using namespace orbital;
using namespace orbital::allocation;
namespace {
void drain(Pool& pool) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (close(pool).kind() != Progress::Kind::done) {
    assert(std::chrono::steady_clock::now() < deadline);
    worker::poll(*pool.lane, 1024);
    std::this_thread::yield();
  }
}

void trim(Pool& pool) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (trim_cold(pool, 0).kind() != Progress::Kind::done) {
    assert(std::chrono::steady_clock::now() < deadline);
    worker::poll(*pool.lane, 1024);
    std::this_thread::yield();
  }
}

void exercise(std::size_t amount, std::size_t alignment) {
  auto arena = open_arena(128 * 1024 * 1024); assert(arena);
  worker::Lane lane; assert(worker::adopt(lane));
  auto pool = add_pool(**arena, lane); assert(pool);
  auto allocation = allocate(**pool, {amount, amount * 4, alignment, 32, Initialization::zero});
  // Reserve includes padding; exact tiny bodies use a compatible finite class.
  if (!allocation && amount * 4 < amount + 32) {
    allocation = allocate(**pool, {amount, amount + 32, alignment, 32, Initialization::zero});
  }
  assert(allocation);
  assert(reinterpret_cast<std::uintptr_t>(allocation->address) % alignment == 0);
  for (auto value : bytes(*allocation)) assert(value == std::byte{0});
  std::memset(allocation->address, 0x5a, amount);
  auto captured = hold(*allocation, {0, amount}); assert(captured);
  const auto* original = captured->address;
  assert(grow(*allocation, {amount + 1, 0, alignment, 0, Initialization::zero}));
  assert(allocation->address == original);
  assert(allocation->address[amount] == std::byte{0});
  assert(original[0] == std::byte{0x5a});
  release(*allocation, {Temperature::hot});
  auto future = allocate(**pool, {amount, amount * 4, alignment, 32});
  assert(future && future->address != original);
  std::thread foreign([owned = std::move(*captured), original]() mutable {
    assert(owned.address == original && owned.address[0] == std::byte{0x5a});
    release(owned);
  }); foreign.join();
  assert(worker::pending(lane));
  worker::poll(lane, 1024);
  release(*future);
  trim(**pool);
  drain(**pool);
  assert((*arena)->charged_bytes.load() == 0);
  worker::relinquish(lane);
}

void padding_and_rollback() {
  auto arena = open_arena(16 * 1024 * 1024); assert(arena);
  worker::Lane lane; assert(worker::adopt(lane));
  auto pool = add_pool(**arena, lane); assert(pool);
  auto value = allocate(**pool, {63, 256, 64, 1, Initialization::zero}); assert(value);
  value->address[63] = std::byte{0x73};
  auto wide = hold(*value, {0, 64}); assert(wide);
  auto denied = grow(*value, {64, 256, 64, 0, Initialization::zero});
  assert(!denied && denied.error() == Error::conflict);
  assert(value->size == 63 && wide->address[63] == std::byte{0x73});
  release(*wide);
  assert(grow(*value, {64, 256, 64, 0, Initialization::zero}));
  assert(value->address[63] == std::byte{0});
  const auto* address = value->address;
  const auto charged = (*arena)->charged_bytes.load();
  assert(!grow(*value, {32 * 1024 * 1024, 0, 64}));
  assert(value->address == address && value->size == 64);
  // Failed cold allocation may keep reusable metadata/VA, never charge phantom body.
  assert((*arena)->charged_bytes.load() == charged);
  assert(!allocate(**pool, {std::numeric_limits<std::size_t>::max(), 0, 8, 1}));
  assert(!allocate(**pool, {16, 0, 3}));
  assert(!hold(*value, {64, 1}));
  release(*value); drain(**pool); worker::relinquish(lane);
}

void concurrent_returns() {
  auto arena = open_arena(32 * 1024 * 1024); assert(arena);
  worker::Lane lane; assert(worker::adopt(lane));
  auto pool = add_pool(**arena, lane); assert(pool);
  for (unsigned iteration = 0; iteration < 50; ++iteration) {
    std::vector<Allocation> values;
    std::array<std::vector<Hold>, 4> uses;
    for (unsigned i = 0; i < 64; ++i) {
      auto value = allocate(**pool, {64, 0, 16, 0, Initialization::zero}); assert(value);
      value->address[0] = std::byte(i);
      auto held = hold(*value, {0, 64}); assert(held);
      uses[i % 4].push_back(std::move(*held)); values.push_back(std::move(*value));
    }
    values.clear();
    std::array<std::thread, 4> threads;
    for (unsigned n = 0; n < threads.size(); ++n)
      threads[n] = std::thread([&uses, n] { uses[n].clear(); });
    for (unsigned n = 0; n < 16; ++n) { worker::poll(lane, 1); std::this_thread::yield(); }
    for (auto& thread : threads) thread.join();
    while (worker::pending(lane)) worker::poll(lane, 1);
  }
  drain(**pool); assert((*arena)->charged_bytes.load() == 0); worker::relinquish(lane);
}

void finite_inventory() {
  auto arena = open_arena(32 * 1024 * 1024); assert(arena);
  worker::Lane lane; assert(worker::adopt(lane));
  auto pool = add_pool(**arena, lane); assert(pool);
  std::vector<Allocation> values;
  for (unsigned index = 0; index < 8 * 256; ++index) {
    auto value = allocate(**pool, {64, 0, 16}); assert(value);
    value->address[0] = std::byte(index % 251);
    values.push_back(std::move(*value));
  }
  // All eight groups are full. Recycling an old identity must restore its
  // owner's eligibility rather than force a new group or overwrite live bytes.
  auto* released = values[123].address;
  const auto retained = statistics(**pool);
  release(values[123]);
  auto replacement = allocate(**pool, {64, 0, 16}); assert(replacement);
  assert(replacement->address == released);
  assert(statistics(**pool).reserved_bytes == retained.reserved_bytes);
  assert(statistics(**pool).metadata_bytes == retained.metadata_bytes);
  for (unsigned index = 0; index < values.size(); ++index)
    if (index != 123) assert(values[index].address[0] == std::byte(index % 251));
  release(*replacement); values.clear();
  drain(**pool); worker::relinquish(lane);
}

void finite_alignment_growth() {
  auto arena = open_arena(16 * 1024 * 1024); assert(arena);
  worker::Lane lane; assert(worker::adopt(lane));
  auto pool = add_pool(**arena, lane); assert(pool);
  std::vector<Allocation> values;
  for (unsigned index = 0; index < 16; ++index) {
    auto value = allocate(**pool, {300, 320, 64, 16, Initialization::zero}); assert(value);
    std::memset(value->address, 0x57 + index, value->size);
    values.push_back(std::move(*value));
  }
  auto& source = values[1];
  auto* original = source.address;
  assert(reinterpret_cast<std::uintptr_t>(original) % 128 == 64);
  auto held = hold(source, {0, 316}); assert(held);
  const AllocationRequest target{305, 320, 128, 15, Initialization::zero};
  const auto charged = (*arena)->charged_bytes.load();
  const auto refused = grow(source, target);
  assert(!refused && refused.error() == Error::conflict);
  assert(source.address == original && source.size == 300 && (*arena)->charged_bytes.load() == charged);
  release(*held);
  assert(grow(source, target));
  assert(source.address != original && reinterpret_cast<std::uintptr_t>(source.address) % 128 == 0);
  for (unsigned byte = 0; byte < 300; ++byte) assert(source.address[byte] == std::byte{0x58});
  for (unsigned byte = 300; byte < 320; ++byte) assert(source.address[byte] == std::byte{0});
  auto reused = allocate(**pool, {300, 320, 64, 16}); assert(reused);
  assert(reused->address == original);
  release(*reused); values.clear();
  drain(**pool); worker::relinquish(lane);
}

void finite_metadata_budget() {
  const auto page = static_cast<std::uint64_t>(::sysconf(_SC_PAGESIZE));
  // A small aligned allocation should not require a 60 KiB controller. This
  // public cap leaves room for three native page floors without pricing private fields.
  auto arena = open_arena(32 * 1024 + 3 * page); assert(arena);
  worker::Lane lane; assert(worker::adopt(lane));
  auto pool = add_pool(**arena, lane); assert(pool);
  for (unsigned round = 0; round < 4; ++round) {
    auto value = allocate(**pool, {64, 0, 64, 16, Initialization::zero}); assert(value);
    std::memset(value->address, 0x61 + round, value->size);
    auto captured = hold(*value, {0, 80}); assert(captured);
    const auto charged = (*arena)->charged_bytes.load();
    assert(charged <= 32 * 1024 + 3 * page);
    release(*value);
    const auto progress = trim_cold(**pool, 0);
    assert(progress.kind() == Progress::Kind::done && (*arena)->charged_bytes.load() == charged);
    std::thread foreign([held = std::move(*captured), round]() mutable {
      for (unsigned byte = 0; byte < 64; ++byte)
        assert(held.address[byte] == std::byte(0x61 + round));
      for (unsigned byte = 64; byte < 80; ++byte) assert(held.address[byte] == std::byte{0});
      release(held);
    });
    foreign.join();
    while (worker::pending(lane)) worker::poll(lane, 16);
    trim(**pool);
    assert((*arena)->charged_bytes.load() == 0 && statistics(**pool).metadata_bytes == 0);
  }
  drain(**pool); worker::relinquish(lane);
}

void finite_prepared_before_hint() {
  auto history = [](std::uint64_t limit, bool check_reuse) {
    auto arena = open_arena(limit); assert(arena);
    worker::Lane lane; assert(worker::adopt(lane));
    auto pool = add_pool(**arena, lane); assert(pool);
    std::vector<Allocation> live;
    for (unsigned index = 0; index < 256; ++index) {
      auto value = allocate(**pool, {64, 256, 64, 16, Initialization::zero}); assert(value);
      value->address[0] = std::byte(index % 251);
      live.push_back(std::move(*value));
    }
    const AllocationRequest request{1000, 1024, 64, 24, Initialization::zero};
    auto cold = allocate(**pool, request); assert(cold);
    auto* prepared = cold->address;
    release(live[0], {Temperature::warm});
    release(*cold, {Temperature::cold});
    const auto charged = (*arena)->charged_bytes.load();
    if (check_reuse) {
      assert(charged == limit);
      auto reused = allocate(**pool, request); assert(reused);
      assert(reused->address == prepared && (*arena)->charged_bytes.load() == charged);
      for (unsigned byte = 0; byte < 1024; ++byte) assert(reused->address[byte] == std::byte{0});
      for (unsigned index = 1; index < live.size(); ++index)
        assert(live[index].address[0] == std::byte(index % 251));
      release(*reused);
    }
    live.clear(); drain(**pool);
    assert((*arena)->charged_bytes.load() == 0);
    worker::relinquish(lane);
    return charged;
  };
  // Observe the actual setup cost, then repeat under that exact public budget.
  // This avoids baking private Group/controller sizes into the regression.
  history(history(16 * 1024 * 1024, false), true);
}

void finite_growth_prepared_fallback() {
  auto history = [](std::uint64_t limit, bool grow_at_limit) {
    auto arena = open_arena(limit); assert(arena);
    worker::Lane lane; assert(worker::adopt(lane));
    auto pool = add_pool(**arena, lane); assert(pool);
    std::array<Allocation, 256> live;
    for (unsigned index = 0; index < live.size(); ++index) {
      auto value = allocate(**pool, {64, 256, 64, 16, Initialization::zero}); assert(value);
      std::memset(value->address, 0x40 + index % 32, value->size);
      live[index] = std::move(*value);
    }
    const AllocationRequest target{1000, 1024, 64, 24, Initialization::zero};
    auto spare = allocate(**pool, target); assert(spare);
    auto* prepared = spare->address;
    release(live[0], {Temperature::warm});
    release(*spare, {Temperature::cold});
    const auto charged = (*arena)->charged_bytes.load();
    if (grow_at_limit) {
      assert(charged == limit);
      // Establish that the same public request has a zero-charge destination.
      auto ordinary = allocate(**pool, target); assert(ordinary);
      assert(ordinary->address == prepared && (*arena)->charged_bytes.load() == charged);
      std::memset(ordinary->address, 0xdd, 1024);
      release(*ordinary, {Temperature::cold});
      auto& source = live[1];
      auto* original = source.address;
      auto retained = hold(source, {0, 64}); assert(retained);
      auto refused = grow(source, target);
      assert(!refused && refused.error() == Error::conflict);
      assert(source.address == original && source.size == 64 &&
          (*arena)->charged_bytes.load() == charged);
      release(*retained);
      assert(grow(source, target));
      assert(source.address == prepared && source.size == target.bytes &&
          source.readable_padding == target.readable_padding &&
          (*arena)->charged_bytes.load() == charged);
      for (unsigned byte = 0; byte < 64; ++byte) assert(source.address[byte] == std::byte{0x41});
      for (unsigned byte = 64; byte < 1024; ++byte) assert(source.address[byte] == std::byte{0});
      for (unsigned index = 2; index < live.size(); ++index)
        for (const auto byte : bytes(live[index])) assert(byte == std::byte(0x40 + index % 32));
    }
    for (auto& value : live) release(value);
    drain(**pool);
    assert((*arena)->charged_bytes.load() == 0);
    assert(remove_pool(**pool));
    worker::relinquish(lane);
    return charged;
  };
  history(history(16 * 1024 * 1024, false), true);
}
}

int main() {
  exercise(256, 16); exercise(4096, 64); exercise(256 * 1024, 4096);
  padding_and_rollback(); concurrent_returns(); finite_inventory(); finite_alignment_growth();
  finite_prepared_before_hint(); finite_growth_prepared_fallback(); finite_metadata_budget();
  std::cout << "allocation ownership, growth, padding, pressure and foreign retirement pass\n";
}
