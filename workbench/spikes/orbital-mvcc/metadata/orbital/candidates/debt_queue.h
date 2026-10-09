#pragma once

#include "common.h"

namespace compact_history::detail {

// Cold slots reuse their link as the free-list link. Live entries rotate through
// the queue; prepare never scans historical holes to reserve new enrollments.
class DebtQueue {
  struct Slot { std::uint32_t object{}, next{}; };
  static constexpr std::size_t maximum = UINT32_MAX >> 3;
  std::pmr::vector<Slot> slots_;
  std::uint32_t first_{}, last_{}, free_{}, live_{};
  std::size_t reservation_work_{};
public:
  explicit DebtQueue(compact_version_probe::Memory& memory) : slots_(&memory) {}
  void reserve(std::size_t additional) {
    const auto free_count = slots_.size() - live_;
    if (additional <= free_count) return;
    const auto added = additional - free_count;
    if (added > maximum - slots_.size()) throw std::bad_alloc();
    const auto needed = slots_.size() + added;
    if (needed > slots_.capacity()) {
      const auto grown = std::min(maximum, std::max(needed, slots_.capacity() * 2));
      const auto copied = slots_.size();
      slots_.reserve(grown);
      reservation_work_ += copied; // Occasional capacity growth is still work.
    }
    while (slots_.size() < needed) {
      slots_.push_back({0, free_}); free_ = static_cast<std::uint32_t>(slots_.size());
      ++reservation_work_;
    }
  }
  std::uint32_t add(std::uint32_t object) noexcept {
    assert(free_ && object);
    const auto slot = free_; free_ = slots_[slot - 1].next;
    slots_[slot - 1] = {object, 0};
    if (last_) slots_[last_ - 1].next = slot; else first_ = slot;
    last_ = slot; ++live_; return slot - 1;
  }
  std::uint32_t front() const noexcept { return first_ ? slots_[first_ - 1].object : 0; }
  void remove(std::uint32_t index) noexcept {
    // Protocol removal is always at the current queue head. Aborted private
    // placeholders have not been enrolled; accepted existing tips keep debt.
    assert(first_ == index + 1);
    first_ = slots_[index].next; if (!first_) last_ = 0;
    slots_[index] = {0, free_}; free_ = index + 1; --live_;
  }
  void rotate() noexcept {
    if (!first_ || first_ == last_) return;
    const auto old = first_; first_ = slots_[old - 1].next;
    slots_[last_ - 1].next = old; slots_[old - 1].next = 0; last_ = old;
  }
  std::size_t size() const noexcept { return live_; }
  std::size_t capacity_bytes() const noexcept { return slots_.capacity() * sizeof(Slot); }
  std::size_t reservation_work() const noexcept { return reservation_work_; }
};

static_assert(sizeof(std::uint32_t) * 2 == 8);

} // namespace compact_history::detail
