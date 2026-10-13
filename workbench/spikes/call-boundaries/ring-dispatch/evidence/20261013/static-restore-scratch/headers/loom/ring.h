#pragma once

// Typed owner-lane continuations for independent memory lookups. Active rotation
// and overflow are bounded; Rings are separate from the Worker's Task queue.

#include <loom/detail/ring_storage.h>
#include <loom/worker.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace loom {

struct RingCapacity { std::size_t active{}, overflow{}; };

struct SpinBudget { std::uint64_t visits{}; };

struct RingCounters {
  std::uint64_t accepted{}, refused{}, completed{}, abandoned{}, visits{};
  std::uint64_t promoted{}, budget_stops{};
};

#if defined(__x86_64__) || defined(__aarch64__)
#define LOOM_NATIVE_CC __attribute__((preserve_none))
#else
#error "The native Ring currently requires the pinned x86-64 or AArch64 compiler"
#endif

namespace detail {
template<class SharedSequence, class Sequence, class SharedArguments, class... Native>
class MemoryRing;

template<std::size_t... S, std::size_t... I, class... Shared, class... Native>
class alignas(64) MemoryRing<std::index_sequence<S...>, std::index_sequence<I...>,
    std::tuple<Shared...>, Native...> {
  // Native scalar/pointer/vector values only. Aggregate carriers and owning
  // closures belong to the operation, not to an implicit erased Ring ABI.
  static_assert(((!std::is_class_v<Native> && !std::is_union_v<Native> &&
                   !std::is_array_v<Native> && std::is_trivially_copyable_v<Native> &&
                   std::is_trivially_destructible_v<Native> &&
                   std::is_same_v<Native, std::remove_cv_t<Native>>) && ...));
  static_assert(((!std::is_class_v<Shared> && !std::is_union_v<Shared> &&
                   !std::is_array_v<Shared> && std::is_trivially_copyable_v<Shared> &&
                   std::is_trivially_destructible_v<Shared> &&
                   std::is_same_v<Shared, std::remove_cv_t<Shared>>) && ...));

public:
  struct Slot;
  // The scheduler's countdown stays in the native register transfer rather
  // than operation arguments or per-visit Ring memory. Authors carry it intact.
  class Cursor {
    friend class MemoryRing;
    MemoryRing* ring_;
    std::uint64_t remaining_;
    Cursor(MemoryRing* ring, std::uint64_t remaining) noexcept
        : ring_(ring), remaining_(remaining) {}
  public:
    [[gnu::always_inline]] MemoryRing* operator->() const noexcept { return ring_; }
  };
  static_assert(std::is_trivially_copyable_v<Cursor> &&
      sizeof(Cursor) == 2 * sizeof(std::uint64_t));
  using Arguments = std::tuple<Native...>;
  using SharedArguments = std::tuple<Shared...>;
  /// Same scheduler with a register-carried invariant prefix. Shared values
  /// apply to every admitted frame; operation-specific state stays in Arguments.
  /// Every callback forwards the received prefix unchanged through tail calls.
  template<class... Common> using WithShared = MemoryRing<std::index_sequence_for<Common...>,
      std::index_sequence_for<Native...>, std::tuple<Common...>, Native...>;
  // Native bodies must not throw. Pinned Clang rejects musttail inside C++
  // noexcept functions even for plain scalar calls, so the private tail ABI
  // expresses this as an author contract instead of an exception specification.
  typedef LOOM_NATIVE_CC void (*Function)(Cursor, Slot*, Shared..., Native...);

  struct Slot {
    // Keep the dispatch prefix together; two pointer arguments make a 64 B
    // Slot without padding an independent admission record to pointer alignment.
    Function function{};
    Slot* next{};
    Arguments arguments{};
    Slot* previous{};
    std::size_t inventory{};
    struct Dependency { const void* address{}; std::uint8_t selector{}; };
    const void* dependency_address{};
    std::uint8_t dependency_selector{};
    bool completed{};
  };

  MemoryRing() = default;
  ~MemoryRing() { assert(!worker_ && "close Ring storage before its owning Worker/Pool"); }
  MemoryRing(const MemoryRing&) = delete;
  MemoryRing& operator=(const MemoryRing&) = delete;
  MemoryRing(MemoryRing&&) = delete;
  MemoryRing& operator=(MemoryRing&&) = delete;

  /// Setup on the Worker lane. The private fixed Allocation owns the bank and
  /// remains at its address until owner close after drain or explicit discard.
  /// Failure leaves the Ring unconfigured.
  [[nodiscard]] orbital::Result<void> configure(Worker& worker, RingCapacity capacity,
      std::size_t page_bytes = 0) requires(sizeof...(Shared) == 0) {
    return configure_impl(worker, capacity, SharedArguments{}, page_bytes);
  }
  [[nodiscard]] orbital::Result<void> configure(Worker& worker, RingCapacity capacity,
      SharedArguments shared, std::size_t page_bytes = 0) requires(sizeof...(Shared) != 0) {
    return configure_impl(worker, capacity, shared, page_bytes);
  }

private:
  [[nodiscard]] orbital::Result<void> configure_impl(Worker& worker, RingCapacity capacity,
      SharedArguments shared, std::size_t page_bytes) {
    assert(orbital::worker::on_lane(worker.lane));
    const auto& policy = worker.hints;
    if (worker_ || !valid_hint_profile(policy.profile) || unsigned(policy.pressure) > 1 ||
        (page_bytes && page_bytes != policy.profile.geometry.page_bytes))
      return std::unexpected(orbital::Error::invalid_argument);
    auto storage = ring_storage(worker, capacity.active, capacity.overflow, sizeof(Slot), alignof(Slot));
    if (!storage) return std::unexpected(storage.error());
    storage_ = std::move(*storage);
    auto* base = orbital::allocation::bytes(storage_.allocation).data();
    slots_ = reinterpret_cast<Slot*>(base);
    free_ = reinterpret_cast<std::size_t*>(base + storage_.layout.free);
    overflow_ = reinterpret_cast<Slot**>(base + storage_.layout.overflow);
    capacity_ = capacity;
    hints_ = policy;
    shared_ = shared;
    const auto count = capacity.active + capacity.overflow;
    for (std::size_t i = 0; i < count; ++i) {
      std::construct_at(slots_ + i);
      slots_[i].inventory = i;
      std::construct_at(free_ + i, count - i - 1);
    }
    for (std::size_t i = 0; i < capacity.overflow; ++i)
      std::construct_at(overflow_ + i, nullptr);
    free_count_ = count;
    counters_ = {};
    worker_ = &worker;
    return {};
  }

public:

  /// Owner thread only. Accepted pointers/state stay qualified and stable
  /// through overflow and native use. Refusal consumes no inventory or work.
  [[nodiscard]] bool push(Function function, Arguments arguments,
      MemoryDependency dependency = {}) noexcept {
    const auto code = dependency.demand.code();
    return push_selected(function, arguments, {dependency.address,
        static_cast<std::uint8_t>(code == 16 ? 0 : code + 16)});
  }
  /// Explicit fixed controls for mechanism comparisons. The caller qualifies
  /// their 64-byte stride and this Ring's page geometry before admission.
  [[nodiscard]] bool push(Function function, Arguments arguments,
      BoundDependency dependency) noexcept {
    return push_selected(function, arguments,
        {dependency.address, static_cast<std::uint8_t>(dependency.hint)});
  }

  /// Copy at a returned boundary. Pending frames keep their admitted envelopes;
  /// geometry changes require closing/rebinding rather than enlarging rights.
  [[nodiscard]] orbital::Result<void> refresh_hints() noexcept {
    if (!worker_ || spinning_ || !orbital::worker::on_lane(worker_->lane))
      return std::unexpected(orbital::Error::conflict);
    const auto& source = worker_->hints;
    if (!valid_hint_profile(source.profile) || unsigned(source.pressure) > 1 ||
        source.profile.geometry != hints_.profile.geometry)
      return std::unexpected(orbital::Error::invalid_argument);
    hints_ = source;
    return {};
  }
  const HintPolicy& hint_policy() const noexcept { return hints_; }
  /// Returned boundary only. Values remain invariant through the next spin and
  /// must preserve the meaning and physical qualification of all pending frames.
  [[nodiscard]] orbital::Result<void> set_shared(SharedArguments shared) noexcept {
    if (!worker_ || spinning_ || !orbital::worker::on_lane(worker_->lane))
      return std::unexpected(orbital::Error::conflict);
    shared_ = shared;
    return {};
  }
  /// Address retains the class's contiguous minimum window and physical use.
  /// Activity, content stability and logical version remain the caller's proof.
  [[gnu::always_inline]] void prefetch(DemandClass demand, const void* address) const noexcept {
    loom::prefetch(hints_, demand, address);
  }

private:
  [[nodiscard]] bool push_selected(Function function, Arguments arguments,
      typename Slot::Dependency dependency) noexcept {
    if (!function || !free_count_) {
      ++counters_.refused;
      return false;
    }
    auto& slot = slots_[free_[--free_count_]];
    slot.function = function;
    slot.arguments = arguments;
    slot.dependency_address = dependency.address;
    slot.dependency_selector = dependency.selector;
    slot.completed = false;
    ++counters_.accepted;
    if (live_ < capacity_.active) {
      append(slot);
      ++live_;
      hint_admission(slot);
    } else {
      overflow_[overflow_tail_] = &slot;
      increment(overflow_tail_, capacity_.overflow);
      ++overflow_count_;
    }
    return true;
  }

public:

  /// One bounded native region per visit. Returns dispatched visits, never
  /// cache readiness; count budgeting adds no clock test/read to each switch.
  std::uint64_t spin(SpinBudget budget) noexcept {
    assert(worker_ && orbital::worker::on_lane(worker_->lane) && !spinning_);
    spin_left_ = budget.visits;
    if (!budget.visits || !live_) return 0;
    spinning_ = true;
    Arguments empty{};
    enter(resume, Cursor{this, budget.visits}, nullptr,
        std::get<S>(shared_)..., std::get<I>(empty)...);
    spinning_ = false;
    const auto visits = budget.visits - spin_left_;
    counters_.visits += visits;
    if (pending() && visits == budget.visits) ++counters_.budget_stops;
    return visits;
  }

  /// Marks the current slot for retirement on its next tail-call to yield.
  void complete(Slot& slot) noexcept { slot.completed = true; }

  /// Tail-call with the callback's current slot and next arguments. Continuation
  /// targets stay stable through later visits; never pass a callback's local state.
  [[gnu::always_inline]] static LOOM_NATIVE_CC void
  yield(Cursor ring, Slot* current, Shared... shared, Native... values) {
    if (current->completed) {
      [[clang::musttail]] return finish(ring, current, shared..., values...);
    }
    ((std::get<I>(current->arguments) = values), ...);
    [[clang::musttail]] return resume(ring, nullptr, shared..., values...);
  }

  /// Requeue with the already stored Arguments. Pass the callback's original
  /// values for musttail signature matching; this path does not save them.
  /// The current frame remains live. Use finish for its final visit.
  [[gnu::always_inline]] static LOOM_NATIVE_CC void
  yield_unchanged(Cursor ring, Slot*, Shared... shared, Native... values) {
    [[clang::musttail]] return resume(ring, nullptr, shared..., values...);
  }

  /// Final tail transfer. Result publication precedes this call; the Slot and
  /// its Arguments cease to be live before another callback is dispatched.
  [[gnu::noinline]] static LOOM_NATIVE_CC void
  finish(Cursor ring, Slot* slot, Shared... shared, Native... values) {
    ring->retire(*slot);
    if (!ring->live_) {
      ring->spin_left_ = ring.remaining_;
      return;
    }
    [[clang::musttail]] return resume(ring, nullptr, shared..., values...);
  }

  std::size_t pending() const noexcept { return live_ + overflow_count_; }

  bool configured() const noexcept { return worker_ != nullptr; }

  const RingCounters& counters() const noexcept { return counters_; }

  std::size_t storage_bytes() const noexcept { return sizeof(*this) + storage_.layout.bytes; }

  /// Spin-return boundary only. Removes physical frames, not parent outcomes
  /// or prior stores. Borrowed operation state stays alive through return.
  std::size_t discard() noexcept {
    assert(worker_ && orbital::worker::on_lane(worker_->lane) && !spinning_);
    const auto removed = pending();
    counters_.abandoned += removed;
    next_ = nullptr;
    live_ = overflow_head_ = overflow_tail_ = overflow_count_ = 0;
    free_count_ = capacity_.active + capacity_.overflow;
    for (std::size_t i = 0; i < free_count_; ++i) free_[i] = free_count_ - i - 1;
    return removed;
  }

  /// Optional shared-family cleanup at a returned boundary. The typed noexcept
  /// predicate only inspects Arguments. It must not mutate/progress this Ring.
  /// Survivors retain their state and overflow order; no owner test is added
  /// to yield. Removed borrowers remain alive until this call returns.
  template<class Predicate> std::size_t release_if(Predicate predicate) noexcept {
    static_assert(std::is_nothrow_invocable_r_v<bool, Predicate, const Arguments&>);
    assert(worker_ && orbital::worker::on_lane(worker_->lane) && !spinning_);
    spinning_ = true;
    std::size_t removed = 0;
    auto* slot = next_;
    const auto active_count = live_;
    for (std::size_t i = 0; i < active_count; ++i) {
      auto* following = slot->next;
      if (predicate(std::as_const(slot->arguments))) {
        unlink(*slot);
        --live_;
        free_[free_count_++] = slot->inventory;
        ++removed;
      }
      slot = following;
    }
    auto read = overflow_head_, write = overflow_head_;
    const auto previous_count = overflow_count_;
    std::size_t survivors = 0;
    // Logical write never overtakes unread entries, including a wrapped FIFO.
    for (std::size_t i = 0; i < previous_count; ++i) {
      auto* slot = overflow_[read];
      increment(read, capacity_.overflow);
      if (predicate(std::as_const(slot->arguments))) {
        free_[free_count_++] = slot->inventory;
        ++removed;
      } else {
        overflow_[write] = slot;
        increment(write, capacity_.overflow);
        ++survivors;
      }
    }
    overflow_count_ = survivors;
    overflow_tail_ = write;
    while (overflow_count_ && live_ < capacity_.active) promote();
    counters_.abandoned += removed;
    spinning_ = false;
    return removed;
  }

  /// Refuses live native frames. Releasing storage allows the actual Pool to
  /// close; the destructor requires this explicit owner-thread close first.
  [[nodiscard]] orbital::Result<void> close() noexcept {
    if (!worker_) return {};
    assert(orbital::worker::on_lane(worker_->lane));
    if (spinning_ || pending()) return std::unexpected(orbital::Error::conflict);
    std::destroy_n(slots_, capacity_.active + capacity_.overflow);
    const orbital::allocation::ReleaseHint hint{
      .temperature = orbital::allocation::Temperature::warm};
    orbital::allocation::release(storage_.allocation, hint);
    storage_.layout = {};
    worker_ = nullptr;
    slots_ = nullptr; free_ = nullptr; overflow_ = nullptr;
    capacity_ = {};
    next_ = nullptr;
    free_count_ = live_ = overflow_head_ = overflow_tail_ = overflow_count_ = 0;
    return {};
  }

private:
  Worker* worker_{};
  RingStorage storage_;
  RingCapacity capacity_;
  HintPolicy hints_;
  [[no_unique_address]] SharedArguments shared_;
  Slot* slots_{};
  std::size_t* free_{};
  Slot** overflow_{};
  Slot* next_{};
  std::size_t free_count_{}, live_{};
  std::size_t overflow_head_{}, overflow_tail_{}, overflow_count_{};
  RingCounters counters_;
  std::uint64_t spin_left_{};
  bool spinning_{};

  static void increment(std::size_t& cursor, std::size_t size) noexcept {
    if (++cursor == size) cursor = 0;
  }

  [[gnu::always_inline]] void hint_admission(const Slot& slot) noexcept {
    const auto selector = slot.dependency_selector;
    if (selector >= 16)
      prefetch_selected(hints_.codes[selector - 16], slot.dependency_address, hints_.profile.geometry);
    else
      prefetch_selected(static_cast<Hint>(selector), slot.dependency_address,
          {64, hints_.profile.geometry.page_bytes});
  }

  // Active frames form a circular list in their live rotation order. New work
  // follows existing frames behind the dispatch frontier. Selective cleanup
  // walks this same membership without changing surviving rotation order.
  void append(Slot& slot) noexcept {
    if (next_) {
      slot.next = next_;
      slot.previous = next_->previous;
      slot.previous->next = &slot;
      next_->previous = &slot;
    } else {
      slot.next = slot.previous = &slot;
      next_ = &slot;
    }
  }

  void unlink(Slot& slot) noexcept {
    if (slot.next == &slot) next_ = nullptr;
    else {
      slot.previous->next = slot.next;
      slot.next->previous = slot.previous;
      if (next_ == &slot) next_ = slot.next;
    }
  }

  void replace(Slot& current, Slot& incoming) noexcept {
    if (current.next == &current) incoming.next = incoming.previous = &incoming;
    else {
      incoming.next = current.next;
      incoming.previous = current.previous;
      incoming.previous->next = &incoming;
      incoming.next->previous = &incoming;
    }
    if (next_ == &current) next_ = &incoming;
  }

  void promote(Slot* replacing = nullptr) noexcept {
    auto* slot = overflow_[overflow_head_];
    increment(overflow_head_, capacity_.overflow);
    --overflow_count_;
    if (replacing) replace(*replacing, *slot);
    else append(*slot);
    ++live_;
    ++counters_.promoted;
    hint_admission(*slot);
  }

  void retire(Slot& slot) noexcept {
    --live_;
    ++counters_.completed;
    free_[free_count_++] = slot.inventory;
    if (overflow_count_) promote(&slot);
    else unlink(slot);
  }

  [[gnu::always_inline]] static LOOM_NATIVE_CC void
  resume(Cursor ring, Slot*, Shared... shared, Native...) {
    if (!ring.remaining_) {
      ring->spin_left_ = 0;
      return;
    }
    auto* slot = ring->next_;
    ring->next_ = slot->next;
    --ring.remaining_;
    [[clang::musttail]] return slot->function(ring, slot, shared...,
        std::get<I>(slot->arguments)...);
  }

  // Isolate the native call from a realigned ordinary caller's frame base:
  // AArch64 can retain x19 across it; x86 rejects a native invoke that clobbers
  // the ASan fake stack's RBX base. This fixed-frame ordinary bridge keeps that
  // borrow safe. Actual native state and tail transfers remain instrumented.
  __attribute__((noinline, no_sanitize("address", "undefined"))) static void
  enter(Function first, Cursor ring, Slot* slot, Shared... shared, Native... values) noexcept {
    first(ring, slot, shared..., values...);
  }
};
} // namespace detail

/// A reusable owner-lane bank for one scalar/pointer/vector callback signature.
/// Callbacks carry Cursor intact and transfer using [[clang::musttail]].
/// They must not throw or retain a Cursor from an earlier visit.
/// Argument targets remain qualified and stable until retired or discarded.
template<class... Native>
using Ring = detail::MemoryRing<std::index_sequence<>, std::index_sequence_for<Native...>,
    std::tuple<>, Native...>;

} // namespace loom
