#pragma once

#include "memory.h"

namespace orbital::object_detail {

// Changed chains are active; settled history waits for a reader to retire.
// Both queues share stable cold slots, reserved before publication.
class DebtQueue {
  enum class State : std::uint32_t { active, waiting };
  struct Slot {
    std::uint32_t object{}, next{}, previous{};
    State state{};
    std::uint64_t parked_at{};
  };
  struct Queue { std::uint32_t first{}, last{}, count{}; };
  static constexpr std::size_t maximum = UINT32_MAX >> 3;
  std::pmr::vector<Slot> slots_;
  Queue active_, waiting_;
  std::uint32_t free_{}, live_{};
  std::size_t reservation_work_{};
  void append(Queue& queue, std::uint32_t slot) noexcept {
    auto& value = slots_[slot - 1];
    value.previous = queue.last; value.next = 0;
    if (queue.last) slots_[queue.last - 1].next = slot;
    else queue.first = slot;
    queue.last = slot; ++queue.count;
  }
  void unlink(Queue& queue, std::uint32_t slot) noexcept {
    auto& value = slots_[slot - 1];
    if (value.previous) slots_[value.previous - 1].next = value.next;
    else queue.first = value.next;
    if (value.next) slots_[value.next - 1].previous = value.previous;
    else queue.last = value.previous;
    --queue.count;
  }
public:
  explicit DebtQueue(std::pmr::memory_resource& memory) : slots_(&memory) {}
  void reserve(std::size_t additional);
  std::uint32_t add(std::uint32_t object) noexcept {
    assert(free_ && object);
    const auto slot = free_; free_ = slots_[slot - 1].next;
    auto& value = slots_[slot - 1];
    value.object = object; value.state = State::active;
    append(active_, slot); ++live_; return slot - 1;
  }
  std::uint32_t front() const noexcept { return active_.first ? slots_[active_.first - 1].object : 0; }
  void remove(std::uint32_t index) noexcept {
    auto& value = slots_[index];
    unlink(value.state == State::active ? active_ : waiting_, index + 1);
    value.object = 0; value.next = free_; free_ = index + 1; --live_;
  }
  void rotate() noexcept {
    if (active_.count < 2) return;
    const auto slot = active_.first;
    unlink(active_, slot); append(active_, slot);
  }
  void activate(std::uint32_t index) noexcept {
    auto& value = slots_[index];
    if (value.state == State::active) return;
    unlink(waiting_, index + 1); value.state = State::active;
    append(active_, index + 1);
  }
  void park(std::uint64_t cut) noexcept {
    const auto slot = active_.first; assert(slot);
    assert(!waiting_.last || slots_[waiting_.last - 1].parked_at <= cut);
    unlink(active_, slot);
    auto& value = slots_[slot - 1]; value.state = State::waiting; value.parked_at = cut;
    append(waiting_, slot);
  }
  bool waiting_after(std::uint64_t cut) const noexcept {
    return waiting_.last && slots_[waiting_.last - 1].parked_at > cut;
  }
  bool wake_after(std::uint64_t cut) noexcept {
    if (!waiting_after(cut)) return false;
    activate(waiting_.last - 1); return true;
  }
  std::size_t size() const noexcept { return live_; }
  std::size_t active_size() const noexcept { return active_.count; }
  std::size_t capacity_bytes() const noexcept { return slots_.capacity() * sizeof(Slot); }
  std::size_t reservation_work() const noexcept { return reservation_work_; }
};

} // namespace orbital::object_detail
