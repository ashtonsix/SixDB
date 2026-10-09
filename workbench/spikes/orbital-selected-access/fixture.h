#pragma once
#include "access.h"
#include <orbital/allocation.h>
#include <cassert>
#include <chrono>
#include <thread>

namespace selected::fixture {
struct Memory {
  orbital::Ref<orbital::allocation::Arena> arena;
  orbital::worker::Lane lane;
  orbital::allocation::Pool* pool{};
  explicit Memory(std::uint64_t limit = 8 * 1024 * 1024) {
    auto value = orbital::allocation::open_arena(limit); assert(value);
    arena = std::move(*value);
    assert(orbital::worker::adopt(lane));
    auto added = orbital::allocation::add_pool(*arena, lane); assert(added); pool = *added;
  }
  ~Memory() {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (orbital::allocation::close(*pool).kind() != orbital::Progress::Kind::done) {
      assert(std::chrono::steady_clock::now() < deadline);
      orbital::worker::poll(lane, 256); std::this_thread::yield();
    }
    assert(arena->charged_bytes.load() == 0);
    assert(orbital::allocation::remove_pool(*pool));
    orbital::worker::relinquish(lane);
  }
};

inline Rep representation(Memory& memory, Key key, Id realization,
    Id format, Bytes bytes, bool native = true) {
  const PartInput part{0, bytes, native};
  auto builder = Builder::make(*memory.pool, key, realization, format, {}, {&part, 1}); assert(builder);
  auto rep = builder->freeze(); assert(rep); return std::move(*rep);
}

inline void publish(Store& store, Rep rep, Id epoch) {
  auto context = store.read(); Draft draft(context);
  assert(draft.replace(std::move(rep), {}));
  CommitOwner owner(store); auto prepared = owner.prepare(draft, epoch); assert(prepared);
  assert(owner.install(*prepared)); prepared->retire();
}
} // namespace selected::fixture
