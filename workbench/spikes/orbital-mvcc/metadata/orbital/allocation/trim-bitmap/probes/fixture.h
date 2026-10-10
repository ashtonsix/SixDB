#pragma once

#include <orbital/allocation.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace compact_shape::probe {

// Probe-only owner for one real allocator Pool. Declare before Store/exports so
// all physical uses end before cleanup. No per-body wrapper or controlblock.
struct NativeFixture {
  orbital::worker::Lane lane;
  orbital::Ref<orbital::allocation::Arena> arena;
  orbital::allocation::Pool* pool{};

  NativeFixture() = default;
  NativeFixture(const NativeFixture&) = delete;
  NativeFixture& operator=(const NativeFixture&) = delete;

  [[nodiscard]] orbital::Result<void> open(std::uint64_t byte_limit = 1ULL << 30,
      int lane_cpu = -1, int reclaimer_cpu = -1) {
    if (adopted_ || arena || pool) return std::unexpected(orbital::Error::conflict);
    if (auto adopted = orbital::worker::adopt(lane, lane_cpu); !adopted) return adopted;
    adopted_ = true;
    auto opened = orbital::allocation::open_arena(byte_limit, reclaimer_cpu);
    if (!opened) {
      orbital::worker::relinquish(lane); adopted_ = false;
      return std::unexpected(opened.error());
    }
    arena = std::move(*opened);
    auto added = orbital::allocation::add_pool(*arena, lane);
    if (!added) {
      arena.reset(); orbital::worker::relinquish(lane); adopted_ = false;
      return std::unexpected(added.error());
    }
    pool = *added;
    return {};
  }

  // Snapshot after open for the empty-Pool phase. These are public Pool counters;
  // Arena/Pool setup heap, vector capacity and thread stacks need separate census.
  orbital::allocation::Statistics statistics() const noexcept {
    return pool ? orbital::allocation::statistics(*pool) : orbital::allocation::Statistics{};
  }

  [[nodiscard]] orbital::Result<void> close(std::chrono::milliseconds timeout = std::chrono::seconds(20)) {
    if (pool) {
      const auto deadline = std::chrono::steady_clock::now() + timeout;
      for (;;) {
        const auto progress = orbital::allocation::close(*pool);
        if (progress.kind() == orbital::Progress::Kind::failed) return std::unexpected(progress.failure());
        if (progress.kind() == orbital::Progress::Kind::done) break;
        if (std::chrono::steady_clock::now() >= deadline) return std::unexpected(orbital::Error::unavailable);
        orbital::worker::poll(lane, 64);
        std::this_thread::yield();
      }
      if (auto removed = orbital::allocation::remove_pool(*pool); !removed) return removed;
      pool = nullptr;
    }
    arena.reset();
    if (adopted_) { orbital::worker::relinquish(lane); adopted_ = false; }
    return {};
  }

  ~NativeFixture() {
    if (auto closed = close(); !closed) {
      std::fputs("native allocation fixture did not close; physical uses or reclamation remain\n", stderr);
      std::abort();
    }
  }

private:
  bool adopted_{};
};

} // namespace compact_shape::probe
