#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>
#include <type_traits>

namespace loom {

/// A borrowed known-key loop, bounded for one native continuation visit.
/// The caller owns scheduling, reader/sink bodies and physical qualification.
/// Cursor, keys, reader and sinks stay stable through native use and cleanup;
/// confirm retirement at a returned boundary before ending their borrow.
/// Accepted overflow frames have the same lifetime obligations as active work.
template<class Key, std::size_t Quantum, std::size_t Lookahead = 32>
class IndependentStream {
  static_assert(Quantum >= 1);
  std::span<const Key> keys_;
  std::size_t next_{};
public:
  static constexpr auto quantum = Quantum;
  static constexpr auto lookahead = Lookahead;

  /// Bind/rebind before admission or after returned-boundary retirement/cleanup.
  /// Empty input is valid. Construction performs no hints or useful work.
  explicit constexpr IndependentStream(std::span<const Key> keys = {}) noexcept : keys_(keys) {}
  constexpr std::size_t processed() const noexcept { return next_; }
  constexpr std::size_t remaining() const noexcept { return keys_.size() - next_; }
  constexpr bool empty() const noexcept { return next_ == keys_.size(); }

  /// Consume at most Quantum keys; true means every request index completed.
  /// First visit also issues min(Lookahead,N) bootstrap hints. Across the drain,
  /// each index invokes prefetch/consume once, including duplicate-valued keys.
  /// Lookahead zero means an immediate hint before consume, without lead time.
  /// Bodies must be synchronous, nonthrowing and nonrecursive; no reentry or
  /// mutation of this cursor/input, and no references retained to its locals.
  /// Keep reductions local and publish them before the caller's tail transfer.
  /// Budgets bound work, not time or cache readiness; no partial-visit recovery.
  [[nodiscard, gnu::always_inline]] bool advance(auto&& prefetch, auto&& consume) noexcept {
    static_assert(std::is_nothrow_invocable_v<decltype(prefetch)&, const Key&>);
    static_assert(std::is_nothrow_invocable_v<decltype(consume)&, const Key&>);
    const auto size = keys_.size();
    const auto begin = next_;
    if (begin == size) return true;
    const auto* keys = keys_.data();
    if (!begin) {
      const auto primed = std::min(Lookahead, size);
      for (std::size_t i = 0; i < primed; ++i) {
        [[clang::always_inline]] prefetch(keys[i]);
      }
    }
    const auto remaining = size - begin;
    const auto work = std::min(Quantum, remaining);
    const auto hinted = std::min(work, remaining > Lookahead ? remaining - Lookahead : 0);
    auto* current = keys + begin;
    for (auto left = hinted; left; --left, ++current) {
      [[clang::always_inline]] prefetch(current[Lookahead]);
      [[clang::always_inline]] consume(*current);
    }
    for (auto left = work - hinted; left; --left, ++current) {
      [[clang::always_inline]] consume(*current);
    }
    // Implicit hinted frontier: mathematically min(N,next+Lookahead). It holds
    // even when Quantum < Lookahead, so reentry needs no separate prime state.
    next_ = begin + work;
    return work == remaining;
  }
};

/// Complete the same typed lookahead loop synchronously at an owner boundary.
/// The input and bodies obey IndependentStream's borrow/noexcept contract.
/// No Ring admission or dispatch; caller-owned reductions stay in the body.
template<std::size_t Lookahead = 32, class Key>
[[gnu::always_inline]] inline void prefetch_for_each(std::span<const Key> keys,
    auto&& prefetch, auto&& consume) noexcept {
  IndependentStream<Key, std::numeric_limits<std::size_t>::max(), Lookahead> stream(keys);
  (void)stream.advance(prefetch, consume);
}
} // namespace loom
