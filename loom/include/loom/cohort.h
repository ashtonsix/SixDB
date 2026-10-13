#pragma once

#include <orbital/types.h>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <type_traits>

namespace loom {

/// A small group of independent states visited as one native continuation.
/// Borrowed states remain qualified and stable through every visit and cleanup.
/// Maximum bounds states visited per callback; callers bound each state's work.
/// Choose admission from active states across all cohorts. Ring capacity counts
/// cohort frames and spin budgets count cohort callbacks, so grouping must also
/// preserve useful overlap and time between owner service boundaries.
template<class State, std::size_t Maximum> class Cohort {
  static_assert(Maximum >= 1 && Maximum <= 64);
  State* states_{};
  std::uint64_t active_{};

  static constexpr std::uint64_t mask(std::size_t count) noexcept {
    return count == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << count) - 1;
  }
  Cohort(std::span<State> states, std::uint64_t active) noexcept
      : states_(states.data()), active_(active) {}

  // A std::invoke wrapper can remain outlined after inlining a large kernel
  // into it. Call ordinary functions/functors directly in this hot path.
  template<class Function>
  [[gnu::always_inline]] static decltype(auto) invoke(Function& function, State& state) noexcept {
    if constexpr (std::is_member_pointer_v<std::remove_cvref_t<Function>>)
      return std::invoke(function, state);
    else
      return function(state);
  }

public:
  Cohort() = default;

  /// Cold binding: every selected bit names an actual borrowed State. Empty
  /// spans and zero masks are valid; no inactive or absent state is visited.
  [[nodiscard]] static orbital::Result<Cohort> bind(std::span<State> states,
      std::uint64_t active) noexcept {
    if (states.size() > Maximum || (active & ~mask(states.size())))
      return std::unexpected(orbital::Error::invalid_argument);
    return Cohort(states, active);
  }
  [[nodiscard]] static orbital::Result<Cohort> bind(std::span<State> states) noexcept {
    if (states.size() > Maximum)
      return std::unexpected(orbital::Error::invalid_argument);
    return Cohort(states, mask(states.size()));
  }

  /// Callbacks may update their State, but must not reenter or mutate this
  /// Cohort. Its pending/empty observations describe returned boundaries.
  template<class Function>
  [[gnu::always_inline]] void visit(Function&& function) const noexcept {
    static_assert(std::is_nothrow_invocable_v<Function&, State&>);
    const auto active = active_;
    auto* const states = states_;
    for (std::size_t i = 0; i < Maximum; ++i)
      if (active & (std::uint64_t{1} << i)) invoke(function, states[i]);
  }

  /// True completes a state; false preserves it for the next native visit.
  template<class Function>
  [[gnu::always_inline]] void advance(Function&& function) noexcept {
    static_assert(std::is_nothrow_invocable_v<Function&, State&>);
    static_assert(std::is_same_v<std::invoke_result_t<Function&, State&>, bool>);
    const auto active = active_;
    auto* const states = states_;
    auto remaining = active;
    for (std::size_t i = 0; i < Maximum; ++i) {
      const auto bit = std::uint64_t{1} << i;
      if ((active & bit) && invoke(function, states[i])) remaining &= ~bit;
    }
    active_ = remaining;
  }

  [[gnu::always_inline]] std::size_t pending() const noexcept { return std::popcount(active_); }
  [[gnu::always_inline]] bool empty() const noexcept { return active_ == 0; }
};
} // namespace loom
