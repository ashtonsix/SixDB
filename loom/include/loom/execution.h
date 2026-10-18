#pragma once

#include <orbital/types.h>
#include <algorithm>
#include <cstdint>
#include <limits>

namespace loom::execution {

using Epoch = std::uint64_t;

/// Completed useful requests executed serially, including result-dependent
/// work. Reciprocal throughput from interleaved requests is not a serial sample.
/// The caller chooses representative work and charges all sampling to its operation.
struct SerialSample { std::uint64_t elapsed_ns{}, completed_requests{}; };

/// One completed useful direct batch, including dependent result consumption.
/// Independent requests may interleave: this is complete-batch throughput
/// feedback, not a SerialSample or a cache-residence observation. Cold callers
/// charge the sample and clocks; useful requests are executed only once.
struct DirectBatchSample {
  std::uint64_t elapsed_ns{};
  std::uint32_t completed_requests{};
};

struct ColdPromotionOptions {
  std::uint32_t batch_requests{};
  std::uint32_t cheap_ns_per_request{}; ///< Qualified for the complete consumer procedures.
  std::uint32_t cheap_confirmations{2};
};

/// Owner-local cold-method confirmation. The caller owns its hot/cold method
/// pointers, physical protection, expiry and maintenance. Hot work never calls
/// this state or updates its counters. No clock, identity lookup, allocation or
/// service registration is hidden here; no age or cache-residence guarantee.
class ColdPromotion {
  ColdPromotionOptions options_;
  std::uint16_t cheap_{};
  bool promoted_{};

  explicit constexpr ColdPromotion(ColdPromotionOptions options) noexcept : options_(options) {}
public:
  /// Grain and cutoff are explicit. Zero options or >65535 confirmations refuse.
  [[nodiscard]] static constexpr orbital::Result<ColdPromotion> bind(ColdPromotionOptions options) noexcept {
    if (!options.batch_requests || !options.cheap_ns_per_request || !options.cheap_confirmations ||
        options.cheap_confirmations > std::numeric_limits<std::uint16_t>::max())
      return std::unexpected(orbital::Error::invalid_argument);
    return ColdPromotion(options);
  }
  constexpr const ColdPromotionOptions& options() const noexcept { return options_; }

  /// True requests one promotion after consecutive cheap direct observations.
  /// The caller installs hot and registers its demotion before leaving the owner
  /// boundary. False means no new promotion. Zero time/wrong grain changes no
  /// state; expensive valid work breaks confirmation. Once promoted, this stays
  /// latched until reset, so a second expiry is never requested by observation.
  [[nodiscard, gnu::always_inline]] constexpr bool observe(DirectBatchSample sample) noexcept {
    if (promoted_ || !sample.elapsed_ns || sample.completed_requests != options_.batch_requests) return false;
    // Both options are uint32: their product fits uint64 without truncation or
    // division of reciprocal throughput at the inclusive cutoff.
    const auto ceiling = std::uint64_t(options_.cheap_ns_per_request) * options_.batch_requests;
    if (sample.elapsed_ns > ceiling) {
      cheap_ = 0;
      return false;
    }
    ++cheap_;
    if (cheap_ != options_.cheap_confirmations) return false;
    promoted_ = true;
    return true;
  }

  /// Explicit demotion/rebind: retire any old expiry and restore the cold entry
  /// before reset. The caller decides when its qualified procedure or evidence
  /// cohort changes and resets the confirmation state explicitly.
  /// Options remain immutable; replace the state to change grain or cutoff.
  constexpr void reset() noexcept {
    cheap_ = 0;
    promoted_ = false;
  }
};
static_assert(sizeof(ColdPromotion) == 16);

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
/// The caller defines the evidence cohort and resets before using a changed
/// qualified complete procedure or evidence cohort; Loom interprets no identity.
class Policy {
  enum class SampleState : std::uint8_t { empty, cheap, expensive };
  Options options_;
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
  [[gnu::always_inline]] void touch(Epoch now) noexcept {
    if (now < last_epoch_) forget_sample();
    last_epoch_ = now;
    if (state_ != SampleState::empty && now - sample_epoch_ > options_.max_age)
      forget_sample();
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
  /// Forget feedback and cadence when the caller changes its qualified complete
  /// procedure or evidence cohort. Options remain unchanged.
  void reset() noexcept;

  /// Call once per operation that may choose its path. An eligible call consumes
  /// one cadence boundary; calls with no useful choice do not. A skipped or
  /// rejected requested sample remains due. The caller bounds sample work.
  /// Expiry can request a probe every operation; the interval is no rate limit.
  [[nodiscard, gnu::always_inline]] bool sample_due(Epoch now,
      Paths paths, Prior prior = Prior::unknown) noexcept {
    touch(now);
    if (!eligible(paths, prior)) return false;
    if (state_ == SampleState::empty || (state_ == SampleState::cheap && !confirmed())) return true;
    if (remaining_) --remaining_;
    return !remaining_;
  }

  /// Zero work/time leaves all state unchanged. Accepted
  /// evidence is rounded upward without overflow. A first expensive sample gets
  /// a next-boundary retry; a second backs off. Cheap confirmations also retry
  /// immediately until qualified. No hints or interleaved timings enter here.
  [[nodiscard]] bool observe(Epoch, SerialSample) noexcept;

  /// Select after an optional completed sample, using its completion epoch.
  /// Caller-known direct is a prior, not fabricated evidence. Legal paths win.
  /// Still-fresh evidence can cover a batch after unreported interference, and
  /// small samples can miss heterogeneity. This supplies no latency guarantee.
  [[nodiscard, gnu::always_inline]] Decision select(Epoch now,
      Paths paths, Prior prior = Prior::unknown) noexcept {
    touch(now);
    if (!paths.ring_legal || !paths.ring_width)
      return paths.direct_legal ? Decision{Path::direct, Reason::only_direct} :
          Decision{Path::unavailable, Reason::no_legal_path};
    if (!paths.direct_legal) return {Path::ring, Reason::only_ring};
    if (prior == Prior::direct) return {Path::direct, Reason::caller_known};
    if (std::min(paths.independent_requests, paths.ring_width) < options_.minimum_independent)
      return {Path::direct, Reason::insufficient_independence};
    if (confirmed()) return {Path::direct, Reason::cheap_serial_sample};
    return {Path::ring, Reason::no_cheap_sample};
  }
};

} // namespace loom::execution
