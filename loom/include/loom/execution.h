#pragma once

#include <orbital/types.h>
#include <algorithm>
#include <cstdint>
#include <limits>

namespace loom::execution {

using Epoch = std::uint64_t;
inline constexpr auto unknown_domain = std::numeric_limits<std::uint32_t>::max();

/// One caller cohort, including the complete direct/Ring procedure pair and its
/// access distribution. Publication, owner reuse and placement changes update
/// the corresponding identity. These tags grant no read or retention rights.
struct Identity {
  std::uint64_t owner_incarnation{}, generation{}, population{};
  std::uint32_t worker{unknown_domain}, cache_domain{unknown_domain};
  [[gnu::always_inline]] friend bool operator==(const Identity&, const Identity&) = default;
  [[gnu::always_inline]] bool qualified() const noexcept {
    return owner_incarnation && population && worker != unknown_domain && cache_domain != unknown_domain;
  }
};

/// Completed useful requests executed serially, including result-dependent
/// work. Reciprocal throughput from interleaved requests is not a serial sample.
/// The caller chooses representative work and charges all sampling to its operation.
struct SerialSample { std::uint64_t elapsed_ns{}, completed_requests{}; };

/// Epoch and max_age use the caller's full monotonic counter in one consistent unit.
/// SerialSample and the cutoff always use nanoseconds.
/// The cutoff must be qualified for this consumer's complete direct/Ring paths;
/// no hardware-independent serial-cost threshold establishes their crossover.
struct Options {
  std::uint32_t cheap_ns_per_request{};
  Epoch max_age{};
  std::uint16_t minimum_samples{2};
  std::uint32_t minimum_independent{2};
  std::uint32_t sample_interval{32}; ///< Eligible operation boundaries, not time or query count.
};

enum class Path : std::uint8_t { unavailable, direct, ring };
enum class Prior : std::uint8_t { unknown, direct };
enum class Reason : std::uint8_t {
  no_legal_path, only_direct, only_ring, caller_known, insufficient_independence,
  cheap_serial_sample, no_cheap_sample
};
struct Decision { Path path; Reason reason; };

/// Legal complete executions and useful independence are caller facts. Width
/// names the actual configured bank, not a hardware miss-resource count.
struct Paths {
  bool direct_legal{}, ring_legal{};
  std::uint32_t independent_requests{}, ring_width{};
};

/// Owner-local ordinary-boundary policy; no allocation, clock, atomic, hardware
/// query or native callback wrapper. Replace the Policy to change its options.
/// Samples predict this cohort's serial cost, never cache residence or latency.
class Policy {
  enum class SampleState : std::uint8_t { empty, cheap, expensive };
  Options options_;
  Identity identity_{};
  Epoch sample_epoch_{}, last_epoch_{};
  std::uint32_t remaining_{};
  std::uint16_t cheap_samples_{};
  std::uint8_t expensive_samples_{};
  SampleState state_{SampleState::empty};

  explicit Policy(Options options) noexcept : options_(options) {}
  [[gnu::always_inline]] void forget_sample() noexcept {
    remaining_ = 0;
    cheap_samples_ = 0;
    expensive_samples_ = 0;
    state_ = SampleState::empty;
  }
  [[gnu::always_inline]] bool touch(const Identity& identity, Epoch now) noexcept {
    // Stored identities are qualified. Validate only when entering a new cohort.
    if (!identity_.owner_incarnation || identity_ != identity) [[unlikely]] {
      if (!identity.qualified()) {
        forget_sample();
        identity_ = {};
        return false;
      }
      forget_sample();
      identity_ = identity;
    }
    if (now < last_epoch_) forget_sample();
    last_epoch_ = now;
    if (state_ != SampleState::empty && now - sample_epoch_ > options_.max_age)
      forget_sample();
    return true;
  }
  [[gnu::always_inline]] bool confirmed() const noexcept {
    return state_ == SampleState::cheap && cheap_samples_ >= options_.minimum_samples;
  }
  [[gnu::always_inline]] bool eligible(Paths paths, Prior prior) const noexcept {
    return prior == Prior::unknown && paths.direct_legal && paths.ring_legal &&
        std::min(paths.independent_requests, paths.ring_width) >= options_.minimum_independent;
  }

public:
  /// Zero cutoff, age, confirmations, independence or interval is refused.
  [[nodiscard]] static orbital::Result<Policy> bind(Options) noexcept;
  const Options& options() const noexcept { return options_; }
  void reset() noexcept;

  /// Call once per operation that may choose its path. An eligible call consumes
  /// one cadence boundary; calls with no useful choice do not. A skipped or
  /// rejected requested sample remains due. The caller bounds sample work.
  /// Expiry can request a probe every operation; the interval is no rate limit.
  [[nodiscard, gnu::always_inline]] bool sample_due(const Identity& identity, Epoch now,
      Paths paths, Prior prior = Prior::unknown) noexcept {
    if (!touch(identity, now) || !eligible(paths, prior)) return false;
    if (state_ == SampleState::empty || (state_ == SampleState::cheap && !confirmed())) return true;
    if (remaining_) --remaining_;
    return !remaining_;
  }

  /// Invalid identity or zero work/time leaves all state unchanged. Accepted
  /// evidence is rounded upward without overflow. A first expensive sample gets
  /// a next-boundary retry; a second backs off. Cheap confirmations also retry
  /// immediately until qualified. No hints or interleaved timings enter here.
  [[nodiscard]] bool observe(const Identity&, Epoch, SerialSample) noexcept;

  /// Select after an optional completed sample, using its completion epoch.
  /// Caller-known direct is a prior, not fabricated evidence. Legal paths win.
  /// Still-fresh evidence can cover a batch after unreported interference, and
  /// small samples can miss heterogeneity. This supplies no latency guarantee.
  [[nodiscard, gnu::always_inline]] Decision select(const Identity& identity, Epoch now,
      Paths paths, Prior prior = Prior::unknown) noexcept {
    const bool qualified = touch(identity, now);
    if (!paths.ring_legal || !paths.ring_width)
      return paths.direct_legal ? Decision{Path::direct, Reason::only_direct} :
          Decision{Path::unavailable, Reason::no_legal_path};
    if (!paths.direct_legal) return {Path::ring, Reason::only_ring};
    if (prior == Prior::direct) return {Path::direct, Reason::caller_known};
    if (std::min(paths.independent_requests, paths.ring_width) < options_.minimum_independent)
      return {Path::direct, Reason::insufficient_independence};
    if (qualified && confirmed()) return {Path::direct, Reason::cheap_serial_sample};
    return {Path::ring, Reason::no_cheap_sample};
  }
};

} // namespace loom::execution
