#pragma once
#include "packed-construction.h"
#include <loom/preparation.h>
#include <orbital/preparation.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <time.h>

namespace orbital_bench::packed {
// Private driver policy. No ordinary lookup calls any method on this object.
class Inventory {
  struct Helper {
    orbital::worker::Lane lane;
    orbital::worker::Thread thread;
    std::atomic<unsigned> startup{};
    std::atomic<bool> stopping{};
    std::atomic<std::uint64_t> epoch{};
    int requested_cpu{-1}, observed_cpu{-1}, owner_cpu{-1};
    clockid_t cpu_clock{};
    std::uint64_t final_cpu{};
    void start(int cpu);
    static void entry(void*) noexcept;
    static void notify(void*) noexcept;
    void stop();
    std::uint64_t cpu() const;
    ~Helper();
  } helper_;
  struct Slot {
    orbital::allocation::Preparation preparation;
    Inventory* inventory{};
    std::unique_ptr<Page> page;
    ConstructionDelta delta;
    bool issued{}, completed{};
  };
  struct Completed {
    mutable std::mutex mutex;
    PreparationStats totals;
    void success(const ConstructionDelta&) noexcept;
    void failure() noexcept;
    PreparationStats snapshot() const noexcept;
  } completed_;
  loom::Worker& owner_;
  const Schema& schema_;
  bool check_effects_{}, accepting_{}, started_{}, stopped_{};
  // Membership is fixed before helper startup; accepted work borrows Slots.
  std::unique_ptr<Slot[]> slots_;
  std::size_t slot_count_{};
  PreparationStats stats_;
  std::span<Slot> slots() noexcept { return {slots_.get(),slot_count_}; }
  std::span<const Slot> slots() const noexcept { return {slots_.get(),slot_count_}; }
  static orbital::Result<void> construct(orbital::allocation::PreparedBytes, void*) noexcept;
  bool offer(Slot&);
  void harvest(bool refill);
  void observe_peak();
public:
  Inventory(loom::Worker&, const Schema&, bool check_effects);
  ~Inventory();
  Inventory(const Inventory&) = delete;
  void start(std::size_t count, int cpu, bool prefill);
  // At Page creation only: harvest physical outcomes, take once, replace once.
  std::unique_ptr<Page> take_ready(PreparationDemands&, ConstructionDelta&);
  // End refills, cancel stock, settle every accepted record, then join helper.
  void drain();
  PreparationStats snapshot(bool include_storage = false) const;
};
} // namespace orbital_bench::packed
