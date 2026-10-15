#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace loom_locality {

using Epoch = std::uint64_t;
inline constexpr std::uint32_t unknown_domain = std::numeric_limits<std::uint32_t>::max();

/// Caller-owned cohort identity, not an address-directory key. The access
/// population includes the selected footprint/distribution and sampled direct
/// procedure. Rebinding, owner reuse or publication changes the corresponding
/// incarnation/generation tag. Worker/cache tags name the assigned placement;
/// a migration updates them before reusing evidence. They confer no load rights.
struct Tag {
  std::uint64_t owner_incarnation{};
  std::uint64_t generation{};
  std::uint64_t access_population{};
  std::uint32_t worker{unknown_domain};
  std::uint32_t cache_domain{unknown_domain};

  [[gnu::always_inline]] friend bool operator==(const Tag&, const Tag&) = default;
  [[gnu::always_inline]] bool qualified() const noexcept {
    return owner_incarnation && access_population && worker != unknown_domain &&
        cache_domain != unknown_domain;
  }
};

enum class Path : std::uint8_t { unavailable, direct, ring };
enum class Control : std::uint8_t { adaptive, fixed_direct, fixed_ring, caller_known_direct };
enum class EvidenceKind : std::uint8_t { unknown, cheap_serial, expensive_serial };
enum class Reason : std::uint8_t {
  no_legal_path, only_direct, only_ring, insufficient_independence,
  cheap_serial_sample, no_cheap_sample, fixed_control, caller_known
};

struct Policy {
  std::uint32_t cheap_ns_per_request{};
  Epoch horizon_epochs{};
  std::uint16_t minimum_samples{2};
  std::uint32_t minimum_independent{2};
};

/// Measure the complete useful consumer before/after a bounded serial sample.
/// Width must be one independent chain; pipelined reciprocal throughput cannot
/// establish cheap serial access. Hint issuance is diagnostic and supplies no
/// completed demand evidence. The caller includes sampling cost in operation time.
struct Observation {
  Path path{Path::unavailable};
  std::uint64_t elapsed_ns{};
  std::uint64_t completed_requests{};
  std::uint64_t issued_hints{};
  std::uint32_t sample_width{};
};

struct Evidence {
  EvidenceKind kind{EvidenceKind::unknown};
  std::uint32_t ns_per_request{};
  std::uint16_t cheap_samples{};
};

/// One compact record per caller cohort. It observes sample performance, never
/// actual cache residency. All access is at an ordinary owner boundary; no clock,
/// atomic, physical retention or callback instrumentation is hidden here.
class Record {
  Tag tag_{};
  Epoch sample_epoch_{};
  std::uint32_t ns_per_request_{};
  std::uint32_t cutoff_{};
  std::uint16_t cheap_samples_{};
  bool valid_{};
  bool expensive_{};

  [[gnu::always_inline]] bool fresh(const Tag& tag, Epoch now, const Policy& policy) const noexcept {
    // Full outer epochs deliberately avoid low-bit timestamp aliasing. Backward
    // epochs (including full uint64 wrap) and long idle periods become unknown.
    return valid_ && tag.qualified() && tag_ == tag && cutoff_ == policy.cheap_ns_per_request &&
        now >= sample_epoch_ && now - sample_epoch_ <= policy.horizon_epochs;
  }

public:
  void invalidate() noexcept { valid_ = false; cheap_samples_ = 0; }

  /// Disabled/invalid observations consume no evidence and preserve the record.
  /// A valid sample on a new tag/epoch/cutoff starts a new confidence sequence.
  [[nodiscard]] bool observe(const Tag& tag, Epoch now, Observation sample,
      const Policy& policy, bool instrumentation = true) noexcept {
    if (!instrumentation || !tag.qualified() || sample.path != Path::direct ||
        sample.sample_width != 1 || !sample.elapsed_ns || !sample.completed_requests ||
        !policy.cheap_ns_per_request || !policy.minimum_samples) return false;

    // Round upward without overflowing elapsed_ns + completed_requests - 1.
    const auto cost = sample.elapsed_ns / sample.completed_requests +
        std::uint64_t(sample.elapsed_ns % sample.completed_requests != 0);
    const bool previous = fresh(tag, now, policy);
    if (!previous) cheap_samples_ = 0;
    ns_per_request_ = static_cast<std::uint32_t>(
        std::min(cost, std::uint64_t(std::numeric_limits<std::uint32_t>::max())));
    expensive_ = cost > policy.cheap_ns_per_request;
    if (!expensive_) {
      if (cheap_samples_ != std::numeric_limits<std::uint16_t>::max()) ++cheap_samples_;
    } else cheap_samples_ = 0;
    tag_ = tag;
    sample_epoch_ = now;
    cutoff_ = policy.cheap_ns_per_request;
    valid_ = true;
    return true;
  }

  [[nodiscard, gnu::always_inline]] Evidence inspect(
      const Tag& tag, Epoch now, const Policy& policy) const noexcept {
    if (!policy.cheap_ns_per_request || !policy.minimum_samples || !fresh(tag, now, policy)) return {};
    const auto kind = expensive_ ? EvidenceKind::expensive_serial :
        cheap_samples_ >= policy.minimum_samples ? EvidenceKind::cheap_serial : EvidenceKind::unknown;
    return {kind, ns_per_request_, cheap_samples_};
  }
};

static_assert(sizeof(Tag) == 32);
static_assert(sizeof(Record) <= 64);

/// Legal paths and useful independent requests are caller facts. ring_width is
/// the actual bank's configured width, not a universal hardware occupancy limit.
struct Paths {
  bool direct_legal{};
  bool ring_legal{};
  std::uint32_t independent_requests{};
  std::uint32_t ring_width{};
};

struct Decision { Path path; Reason reason; };

/// Ordinary boundary decision. Fixed controls ignore observations; caller-known
/// direct is an explicit prior supplied by the caller, not fabricated evidence.
[[nodiscard, gnu::always_inline]] inline Decision decide(const Record& record, const Tag& tag, Epoch now,
    Paths paths, const Policy& policy, Control control = Control::adaptive) noexcept {
  if (!paths.direct_legal && !paths.ring_legal) return {Path::unavailable, Reason::no_legal_path};
  if (control == Control::fixed_direct || control == Control::caller_known_direct) {
    if (!paths.direct_legal) return {Path::unavailable, Reason::no_legal_path};
    return {Path::direct, control == Control::fixed_direct ? Reason::fixed_control : Reason::caller_known};
  }
  if (control == Control::fixed_ring) {
    if (!paths.ring_legal || !paths.ring_width) return {Path::unavailable, Reason::no_legal_path};
    return {Path::ring, Reason::fixed_control};
  }
  if (!paths.ring_legal || !paths.ring_width) {
    return paths.direct_legal ? Decision{Path::direct, Reason::only_direct} :
        Decision{Path::unavailable, Reason::no_legal_path};
  }
  if (!paths.direct_legal) return {Path::ring, Reason::only_ring};
  if (!policy.minimum_independent ||
      std::min(paths.independent_requests, paths.ring_width) < policy.minimum_independent)
    return {Path::direct, Reason::insufficient_independence};
  if (record.inspect(tag, now, policy).kind == EvidenceKind::cheap_serial)
    return {Path::direct, Reason::cheap_serial_sample};
  return {Path::ring, Reason::no_cheap_sample};
}

} // namespace loom_locality
