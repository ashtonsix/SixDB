#include <loom/execution.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>

namespace {
using namespace loom::execution;

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "loom_execution_check: " << message << '\n';
    std::abort();
  }
}

constexpr Paths choices{true, true, 32, 32};
constexpr Options settings{10, 100, 2, 2, 4};

Policy policy(Options options = settings) {
  auto result = Policy::bind(options);
  check(bool(result), "valid options refused");
  return std::move(*result);
}

void decision(Policy& state, Epoch now, Paths paths,
    Path path, Reason reason, const char* message, Prior prior = Prior::unknown) {
  const auto selected = state.select(now, paths, prior);
  check(selected.path == path && selected.reason == reason, message);
}

void cheap(Policy& state, Epoch now) {
  check(state.observe(now, {10, 1}), "complete cheap serial sample refused");
}

void confirm(Policy& state, Epoch now = 10) {
  cheap(state, now);
  cheap(state, now + 1);
}

void options_and_legal_paths() {
  auto state = policy();
  const auto& options = state.options();
  check(options.cheap_ns_per_request == 10 && options.max_age == 100 &&
      options.minimum_samples == 2 && options.minimum_independent == 2 &&
      options.sample_interval == 4, "bound options changed");
  std::array invalid{settings, settings, settings, settings, settings};
  invalid[0].cheap_ns_per_request = 0;
  invalid[1].max_age = 0;
  invalid[2].minimum_samples = 0;
  invalid[3].minimum_independent = 0;
  invalid[4].sample_interval = 0;
  for (const auto value : invalid) {
    const auto result = Policy::bind(value);
    check(!result && result.error() == orbital::Error::invalid_argument,
        "zero policy option accepted or wrong error returned");
  }

  struct Case { Paths paths; Path path; Reason reason; };
  constexpr std::array cases{
    Case{{false, false, 32, 32}, Path::unavailable, Reason::no_legal_path},
    Case{{false, true, 32, 0}, Path::unavailable, Reason::no_legal_path},
    Case{{true, false, 32, 32}, Path::direct, Reason::only_direct},
    Case{{true, true, 32, 0}, Path::direct, Reason::only_direct},
    Case{{false, true, 0, 32}, Path::ring, Reason::only_ring},
    Case{{true, true, 1, 32}, Path::direct, Reason::insufficient_independence},
    Case{{true, true, 32, 1}, Path::direct, Reason::insufficient_independence},
    Case{{true, true, 2, 2}, Path::ring, Reason::no_cheap_sample},
  };
  for (const auto& item : cases)
    decision(state, 1, item.paths, item.path, item.reason,
        "legality or independence decision");
  decision(state, 1, choices, Path::direct, Reason::caller_known,
      "caller prior ignored", Prior::direct);
  check(!state.sample_due(1, choices, Prior::direct), "known-direct prior requested sampling");
  decision(state, 1, choices, Path::ring, Reason::no_cheap_sample,
      "caller prior fabricated serial evidence");

  confirm(state);
  for (const auto& item : cases) {
    const auto expected_path = item.reason == Reason::no_cheap_sample ? Path::direct : item.path;
    const auto expected_reason = item.reason == Reason::no_cheap_sample ?
        Reason::cheap_serial_sample : item.reason;
    decision(state, 12, item.paths, expected_path, expected_reason,
        "evidence overrode legal paths or independence");
  }
}

void confirmation_and_rounding() {
  auto state = policy();
  check(state.sample_due(1, choices), "new choice did not request a sample");
  check(state.observe(1, {100, 10}), "inclusive cutoff sample refused");
  decision(state, 1, choices, Path::ring, Reason::no_cheap_sample,
      "one cheap sample established two-sample confidence");
  check(state.sample_due(2, choices), "incomplete confidence did not retry immediately");
  check(state.observe(2, {99, 10}), "fractional cheap sample refused");
  decision(state, 2, choices, Path::direct, Reason::cheap_serial_sample,
      "ceiling at the inclusive cutoff failed to qualify");
  check(state.observe(3, {101, 10}), "fractional expensive sample refused");
  decision(state, 3, choices, Path::ring, Reason::no_cheap_sample,
      "fraction above cutoff was truncated or retained confidence");
  cheap(state, 4);
  decision(state, 4, choices, Path::ring, Reason::no_cheap_sample,
      "expensive sample failed to break consecutive cheap confirmations");
  cheap(state, 5);
  decision(state, 5, choices, Path::direct, Reason::cheap_serial_sample,
      "new confirmation sequence failed to qualify");
  state.reset();
  decision(state, 6, choices, Path::ring, Reason::no_cheap_sample, "reset retained evidence");
  check(state.sample_due(6, choices), "reset retained sampling backoff");
}

void explicit_reset_ends_evidence_cohort() {
  auto state = policy();
  confirm(state);
  decision(state, 12, choices, Path::direct, Reason::cheap_serial_sample,
      "initial complete samples did not qualify");
  check(!state.sample_due(12, choices), "confirmed evidence ignored cadence");
  state.reset();
  decision(state, 13, choices, Path::ring, Reason::no_cheap_sample,
      "explicit reset retained confirmed evidence");
  check(state.sample_due(13, choices), "explicit reset retained sampling cadence");
  decision(state, 11, choices, Path::ring, Reason::no_cheap_sample,
      "old epoch resurrected evidence after reset");
  decision(state, 14, choices, Path::ring, Reason::no_cheap_sample,
      "forward epoch resurrected evidence after reset");

  cheap(state, 14);
  state.reset();
  cheap(state, 15);
  decision(state, 15, choices, Path::ring, Reason::no_cheap_sample,
      "reset retained a partial confirmation");
  check(state.sample_due(15, choices), "new partial confirmation inherited old cadence");
  cheap(state, 16);
  decision(state, 16, choices, Path::direct, Reason::cheap_serial_sample,
      "fresh confirmations after reset did not qualify");

  check(state.observe(17, {11, 1}) && state.observe(18, {11, 1}),
      "complete expensive samples refused");
  check(!state.sample_due(18, choices), "repeated expensive evidence ignored backoff");
  state.reset();
  check(state.sample_due(19, choices), "reset retained expensive-sample backoff");
  decision(state, 19, choices, Path::ring, Reason::no_cheap_sample,
      "reset resurrected the earlier cheap confirmation");
}

void expiry_and_rollback() {
  auto options = settings;
  options.max_age = 4;
  auto state = policy(options);
  confirm(state);
  decision(state, 15, choices, Path::direct, Reason::cheap_serial_sample,
      "evidence expired at inclusive age limit");
  decision(state, 16, choices, Path::ring, Reason::no_cheap_sample,
      "evidence remained qualified past age limit");
  decision(state, 15, choices, Path::ring, Reason::no_cheap_sample,
      "expired evidence resurrected at an earlier epoch");

  state = policy();
  confirm(state, 100);
  decision(state, 110, choices, Path::direct, Reason::cheap_serial_sample,
      "fresh forward decision failed");
  decision(state, 109, choices, Path::ring, Reason::no_cheap_sample,
      "rollback newer than the sample retained evidence");
  decision(state, 111, choices, Path::ring, Reason::no_cheap_sample,
      "forward time resurrected evidence after rollback");

  state = policy();
  confirm(state, 100);
  decision(state, 110, {true, false, 32, 32}, Path::direct, Reason::only_direct,
      "sole direct path changed legality at a forward boundary");
  decision(state, 109, choices, Path::ring, Reason::no_cheap_sample,
      "sole direct selection failed to establish a rollback boundary");

  state = policy();
  confirm(state, 100);
  check(!state.sample_due(110, choices), "first interval call unexpectedly due");
  check(state.sample_due(109, choices), "sample boundary rollback did not invalidate cadence");
  decision(state, 111, choices, Path::ring, Reason::no_cheap_sample,
      "sample boundary rollback failed to invalidate evidence");

  state = policy();
  constexpr auto last = std::numeric_limits<Epoch>::max();
  confirm(state, last - 1);
  decision(state, last, choices, Path::direct, Reason::cheap_serial_sample,
      "last epoch rejected before wrap");
  decision(state, 0, choices, Path::ring, Reason::no_cheap_sample,
      "epoch wrap aliased old evidence");
  decision(state, last, choices, Path::ring, Reason::no_cheap_sample,
      "return to pre-wrap epoch resurrected evidence");

  state = policy(options);
  confirm(state);
  check(!state.sample_due(12, choices), "confirmed sample ignored interval");
  check(state.sample_due(16, choices), "idle expiry remained hidden behind cadence");
  check(state.sample_due(17, choices), "skipped expired sample ceased to be due");
  decision(state, 17, choices, Path::ring, Reason::no_cheap_sample,
      "sparse expired calls retained cheap evidence");
}

void cadence_and_ineligible_operations() {
  auto state = policy();
  check(state.observe(1, {11, 1}), "first expensive sample refused");
  check(state.sample_due(2, choices), "first expensive sample did not get immediate retry");
  check(state.sample_due(3, choices), "skipped expensive retry ceased to be due");
  check(state.observe(3, {11, 1}), "second expensive sample refused");
  constexpr std::array ineligible{
    Paths{false, false, 32, 32}, Paths{true, false, 32, 32},
    Paths{false, true, 32, 32}, Paths{true, true, 32, 0},
    Paths{true, true, 1, 32}, Paths{true, true, 32, 1},
  };
  for (unsigned boundary = 1; boundary <= settings.sample_interval; ++boundary) {
    for (const auto paths : ineligible)
      check(!state.sample_due(3, paths), "ineligible operation requested a sample");
    check(!state.sample_due(3, choices, Prior::direct), "caller prior consumed sampling cadence");
    for (unsigned lookup = 0; lookup < 8; ++lookup)
      decision(state, 3, choices, Path::ring, Reason::no_cheap_sample,
          "selection after expensive sample");
    check(state.sample_due(3, choices) == (boundary == settings.sample_interval),
        "eligible cadence boundary count changed");
  }
  check(state.sample_due(3, choices), "skipped due sample reset the interval");
  cheap(state, 3);
  check(state.sample_due(3, choices), "partial cheap confirmation inherited expensive backoff");
  cheap(state, 3);
  for (unsigned boundary = 1; boundary <= settings.sample_interval; ++boundary)
    check(state.sample_due(3, choices) == (boundary == settings.sample_interval),
        "confirmed cheap sample cadence boundary count changed");

  // Ineligible operations still advance time and invalidate stale evidence,
  // although they do not consume an eligible sampling cadence boundary.
  confirm(state, 10);
  check(!state.sample_due(112, {true, false, 32, 32}),
      "sole direct path requested a sample");
  check(state.sample_due(113, choices), "ineligible expiry preserved stale backoff");
  decision(state, 113, choices, Path::ring, Reason::no_cheap_sample,
      "ineligible expiry preserved stale evidence");
  confirm(state, 200);
  check(!state.sample_due(210, {true, false, 32, 32}),
      "ineligible forward boundary requested a sample");
  check(!state.sample_due(209, {true, false, 32, 32}),
      "ineligible rollback boundary requested a sample");
  check(state.sample_due(211, choices), "ineligible rollback preserved stale backoff");
  decision(state, 211, choices, Path::ring, Reason::no_cheap_sample,
      "ineligible rollback preserved stale evidence");

  auto large = settings;
  large.sample_interval = std::numeric_limits<std::uint32_t>::max();
  state = policy(large);
  confirm(state);
  for (unsigned call = 0; call < 1000; ++call)
    check(!state.sample_due(11, choices), "maximum interval overflowed to immediate sampling");
  state.reset();
  check(state.sample_due(11, choices), "reset did not clear maximum interval");
}

void rejected_observations_preserve_state() {
  constexpr std::array invalid_samples{SerialSample{0, 1}, SerialSample{1, 0}, SerialSample{0, 0}};
  for (const auto sample : invalid_samples) {
    auto state = policy();
    confirm(state);
    check(!state.observe(1'000'000, sample), "zero observation accepted");
    check(!state.observe(0, sample), "zero backward observation accepted");
    decision(state, 12, choices, Path::direct, Reason::cheap_serial_sample,
        "rejected sample changed age or confidence");
    for (unsigned call = 1; call <= settings.sample_interval; ++call)
      check(state.sample_due(12, choices) == (call == settings.sample_interval),
          "rejected sample consumed or reset cadence");
    check(!state.observe(12, sample), "due zero sample accepted");
    check(state.sample_due(12, choices), "rejected requested sample stopped being due");
  }
  // A rejected sample must not refresh old valid evidence, even at a later epoch.
  auto options = settings;
  options.max_age = 4;
  auto state = policy(options);
  confirm(state);
  check(!state.observe(15, {0, 1}), "zero elapsed sample refreshed evidence");
  decision(state, 16, choices, Path::ring, Reason::no_cheap_sample,
      "rejected observation refreshed expiry");
}

void numeric_limits_and_rebinding() {
  auto options = settings;
  options.cheap_ns_per_request = std::numeric_limits<std::uint32_t>::max();
  options.minimum_samples = 1;
  auto state = policy(options);
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  check(state.observe(1, {maximum, 1}), "huge legitimate serial cost refused");
  decision(state, 1, choices, Path::ring, Reason::no_cheap_sample,
      "huge serial cost saturated into cheap evidence");
  check(state.observe(2, {maximum, 2}), "large divided serial cost refused");
  decision(state, 2, choices, Path::ring, Reason::no_cheap_sample,
      "overflowing rounded cost became cheap evidence");
  check(state.observe(3, {maximum, maximum}), "huge completed-request count refused");
  decision(state, 3, choices, Path::direct, Reason::cheap_serial_sample,
      "huge equal numerator and denominator failed to round to one");

  options.cheap_ns_per_request = 1;
  state = policy(options);
  check(state.observe(1, {maximum, maximum - 1}), "huge fractional sample refused");
  decision(state, 1, choices, Path::ring, Reason::no_cheap_sample,
      "huge fractional quotient failed to round upward");
  check(state.observe(2, {maximum - 1, maximum}), "sub-unit serial sample refused");
  decision(state, 2, choices, Path::direct, Reason::cheap_serial_sample,
      "positive sub-unit sample did not round to one");

  options = settings;
  options.minimum_samples = std::numeric_limits<std::uint16_t>::max();
  state = policy(options);
  for (unsigned sample = 1; sample < options.minimum_samples; ++sample)
    cheap(state, 1);
  decision(state, 1, choices, Path::ring, Reason::no_cheap_sample,
      "maximum confirmation threshold qualified early");
  cheap(state, 1);
  decision(state, 1, choices, Path::direct, Reason::cheap_serial_sample,
      "maximum confirmation threshold never qualified");
  for (unsigned sample = 0; sample < 10; ++sample) cheap(state, 1);
  decision(state, 1, choices, Path::direct, Reason::cheap_serial_sample,
      "confirmation counter wrapped after saturation");

  auto old = policy();
  confirm(old);
  options = settings;
  options.cheap_ns_per_request = 9;
  auto replacement = policy(options);
  decision(replacement, 12, choices, Path::ring, Reason::no_cheap_sample,
      "new policy inherited old evidence under changed options");
  check(replacement.sample_due(12, choices), "new policy inherited old cadence");
  decision(old, 12, choices, Path::direct, Reason::cheap_serial_sample,
      "new binding changed existing policy evidence");
}

void heterogeneous_samples_are_not_a_guarantee() {
  // A broad caller population contains both cheap and expensive complete
  // requests. The first two completed samples happen to hit its cheap members;
  // the policy has no unreported result or cache knowledge to distinguish them.
  constexpr std::array<SerialSample, 3> complete_requests{{{2, 1}, {3, 1}, {200, 1}}};
  auto state = policy();
  check(state.observe(1, complete_requests[0]) &&
      state.observe(2, complete_requests[1]), "heterogeneous prefix refused");
  decision(state, 3, choices, Path::direct, Reason::cheap_serial_sample,
      "cheap subset did not establish its limited prediction");
  check(complete_requests[2].elapsed_ns > settings.cheap_ns_per_request,
      "heterogeneous fixture lost expensive next request");
  // Fresh evidence still predicts direct before that expensive completion is
  // reported. Consumer qualification and representative samples remain necessary.
  check(state.observe(3, complete_requests[2]), "expensive next request refused");
  decision(state, 3, choices, Path::ring, Reason::no_cheap_sample,
      "contradictory complete sample failed to revoke cheap prediction");
}
} // namespace

int main() {
  options_and_legal_paths();
  confirmation_and_rounding();
  explicit_reset_ends_evidence_cohort();
  expiry_and_rollback();
  cadence_and_ineligible_operations();
  rejected_observations_preserve_state();
  numeric_limits_and_rebinding();
  heterogeneous_samples_are_not_a_guarantee();
}
