#include <loom/hardware.h>
#include <loom/ring.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using Clock = std::chrono::steady_clock;
void check(bool condition, const char* message) {
  if (!condition) { std::cerr << "loom_hardware_check: " << message << '\n'; std::abort(); }
}
template<class T> void check(const orbital::Result<T>& result, const char* message) { check(bool(result), message); }
template<class F> void until(F predicate, const char* message) {
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  while (!predicate()) {
    check(Clock::now() < deadline, message);
    std::this_thread::yield();
  }
}
loom::detail::HardwareEvidence useful_evidence() {
  loom::detail::HardwareEvidence evidence;
  evidence.width_complete = evidence.training_complete = true;
  for (auto& curve : evidence.ns_per_load) curve = {100, 60, 10, 10, 11, 11, 12, 12, 13, 14};
  for (auto& row : evidence.training) for (unsigned i = 0; i < row.size(); ++i)
    row[i] = {90, 100, 10, 15, i < 2 ? 80.0 : 40.0};
  return evidence;
}
void inference_checks() {
  auto evidence = useful_evidence();
  auto result = loom::detail::qualify_hardware(evidence);
  check(result.preferred_width == 4 && result.training_accesses == 3, "independent common width/forward suffix");
  evidence.width_complete = false;
  result = loom::detail::qualify_hardware(evidence);
  check(!result.preferred_width && result.training_accesses == 3, "missing width erased independent training");
  evidence = useful_evidence();
  evidence.ns_per_load[0][0] = std::numeric_limits<double>::quiet_NaN();
  result = loom::detail::qualify_hardware(evidence);
  check(!result.preferred_width && result.training_accesses == 3, "nonfinite width inference");
  evidence = useful_evidence();
  evidence.ns_per_load[1].fill(100);
  evidence.ns_per_load[1].back() = 10;
  check(!loom::detail::qualify_hardware(evidence).preferred_width, "one seed right-censoring accepted");
  evidence = useful_evidence();
  for (auto& value : evidence.ns_per_load[1]) value *= 2;
  check(!loom::detail::qualify_hardware(evidence).preferred_width, "unstable independent curves accepted");
  evidence = useful_evidence();
  evidence.ns_per_load[1] = {100, 100, 100, 100, 10, 10, 11, 11, 12, 12};
  check(loom::detail::qualify_hardware(evidence).preferred_width == 12, "near-best intersection selected separately per seed");
  evidence = useful_evidence();
  for (auto& value : evidence.ns_per_load[0]) value = 0;
  check(!loom::detail::qualify_hardware(evidence).preferred_width, "zero timing accepted");
  evidence = useful_evidence();
  for (auto& row : evidence.training) row.back().cold_p10 = 10;
  result = loom::detail::qualify_hardware(evidence);
  check(result.preferred_width == 4 && !result.training_accesses, "overlapping controls created a training suffix");
  evidence = useful_evidence();
  evidence.training[3][6].trained_median = 95;
  check(!loom::detail::qualify_hardware(evidence).training_accesses, "one inconsistent suffix point accepted");
  evidence = useful_evidence();
  evidence.training_complete = false;
  result = loom::detail::qualify_hardware(evidence);
  check(result.preferred_width == 4 && !result.training_accesses, "unsupported training erased independent width");
}

struct Probe {
  std::atomic<unsigned> calls{}, active{}, peak{}, finished{};
  std::atomic<bool> entered{}, release{}, cancelled{};
  loom::HardwareState outcome{loom::HardwareState::calibrated_local};
  loom::detail::HardwareEvidence evidence{useful_evidence()};
  bool wait{};
  loom::HardwareService* reentrant_service{};
  std::atomic<bool> reentered{};
};
loom::detail::HardwareCalibrationResult controlled(const loom::HardwareContext& hardware,
    loom::HardwareCalibrationLimits limits, std::stop_token stop, void* context) noexcept {
  auto& probe = *static_cast<Probe*>(context);
  ++probe.calls;
  const auto active = ++probe.active;
  auto peak = probe.peak.load();
  while (peak < active && !probe.peak.compare_exchange_weak(peak, active)) {}
  std::stop_callback reenter(stop, [&] {
    if (probe.reentrant_service) {
      auto refused = probe.reentrant_service->request(hardware);
      check(!refused && refused.error() == orbital::Error::unavailable, "stop callback request was not refused");
      probe.reentrant_service->cancel();
      probe.reentered = true;
    }
  });
  probe.entered = true;
  const auto deadline = Clock::now() + limits.wall_time;
  while (probe.wait && !probe.release.load() && !stop.stop_requested() && Clock::now() < deadline)
    std::this_thread::yield();
  --probe.active;
  ++probe.finished;
  if (stop.stop_requested()) { probe.cancelled = true; return {loom::HardwareState::cancelled, {}}; }
  if (Clock::now() >= deadline) return {loom::HardwareState::timed_out, {}};
  return {probe.outcome, probe.evidence};
}
loom::HardwareOptions options(Probe& probe) {
  loom::HardwareOptions result;
  result.table = {};
  result.calibrator = controlled;
  result.calibrator_context = &probe;
  return result;
}
loom::HardwareSnapshot settled(const std::shared_ptr<loom::HardwareProfile>& profile) {
  until([&] { return profile->snapshot().state != loom::HardwareState::pending; }, "calibration did not settle");
  return profile->snapshot();
}
void defaults(const loom::HardwareSnapshot& snapshot, loom::HardwareState state, const char* message) {
  check(snapshot.state == state && snapshot.profile.revision == 0 && snapshot.profile.preferred_width == 8 &&
        snapshot.profile.training_accesses == 0 && !snapshot.width_qualified && !snapshot.training_qualified, message);
}

void lookup_checks(const loom::HardwareContext& real) {
  auto numbered = real;
  numbered.qualified = true;
  auto peer = numbered;
  peer.cpu = (peer.cpu + 1) % 1024;
  check(loom::compatible_hardware_context(numbered, peer), "qualified context depended on CPU numbering");
  numbered.qualified = peer.qualified = false;
  check(!loom::compatible_hardware_context(numbered, peer), "unknown contexts crossed CPU identities");
  peer = numbered;
  peer.qualified = true;
  check(!loom::compatible_hardware_context(numbered, peer), "qualification mismatch accepted");
  for (const auto& entry : loom::hardware_table()) {
    check(entry.format == 1 && entry.validated && !entry.signature.empty(), "compiled retained table qualification");
    Probe probe;
    auto config = options(probe);
    config.table = {&entry, 1};
    loom::HardwareService service(config);
    auto context = real;
    context.signature = entry.signature;
    context.geometry = entry.geometry;
    context.qualified = true;
    auto handle = service.request(context);
    check(handle, "retained table request");
    const auto snapshot = (*handle)->snapshot();
    check(snapshot.state == loom::HardwareState::lookup && snapshot.profile.preferred_width == (entry.preferred_width ? entry.preferred_width : 8) &&
          snapshot.profile.training_accesses == entry.training_accesses && !probe.calls, "compiled table missed or calibrated");
  }
  Probe probe;
  auto config = options(probe);
  const loom::HardwareTableEntry hit{real.signature, real.geometry, 4, 3};
  config.table = {&hit, 1};
  loom::HardwareService service(config);
  auto handle = service.request(real);
  check(handle, "controlled exact hit");
  const auto snapshot = (*handle)->snapshot();
  check(snapshot.state == loom::HardwareState::lookup && snapshot.profile.revision == 1 &&
        snapshot.profile.preferred_width == 4 && snapshot.profile.training_accesses == 3 && !probe.calls,
        "exact hit did not publish synchronously");
  auto same = real;
  same.cpu = (same.cpu + 1) % 1024;
  auto coalesced = service.request(same);
  check(coalesced && *coalesced == *handle, "equal context depended on CPU numbering");

  for (unsigned invalidation = 0; invalidation < 5; ++invalidation) {
    Probe local;
    auto altered = hit;
    auto context = real;
    auto local_config = options(local);
    if (invalidation == 0) context.signature += ' ';
    if (invalidation == 1) altered.format = 2;
    if (invalidation == 2) altered.validated = false;
    if (invalidation == 3) context.qualified = false;
    if (invalidation == 4) altered.geometry.line_bytes = altered.geometry.line_bytes == 64 ? 128 : 64;
    local_config.table = {&altered, 1};
    local_config.calibrate = false;
    loom::HardwareService invalid(local_config);
    auto refused_hit = invalid.request(context);
    check(refused_hit, "invalidated lookup request");
    defaults((*refused_hit)->snapshot(), loom::HardwareState::disabled, "stale context/format/geometry table hit");
    check(!local.calls, "disabled invalidated lookup calibrated");
  }
  const loom::HardwareTableEntry partial{real.signature, real.geometry, 0, 3};
  config.table = {&partial, 1};
  loom::HardwareService partial_service(config);
  auto partial_handle = partial_service.request(real);
  check(partial_handle, "partial lookup request");
  const auto partial_snapshot = (*partial_handle)->snapshot();
  check(partial_snapshot.state == loom::HardwareState::lookup && partial_snapshot.profile.preferred_width == 8 &&
        !partial_snapshot.width_qualified && partial_snapshot.profile.training_accesses == 3,
        "unknown table width discarded qualified training");
}

void lifecycle_checks(const loom::HardwareContext& real) {
  Probe probe;
  probe.wait = true;
  loom::HardwareService service(options(probe));
  auto first = service.request(real);
  check(first, "miss request");
  until([&] { return probe.entered.load(); }, "background calibration not entered");
  defaults((*first)->snapshot(), loom::HardwareState::pending, "defaults absent during calibration");
  std::array<std::shared_ptr<loom::HardwareProfile>, 8> handles;
  std::array<std::jthread, 8> requesters;
  for (unsigned i = 0; i < handles.size(); ++i) requesters[i] = std::jthread([&, i] {
    auto result = service.request(real);
    check(result, "concurrent duplicate request");
    handles[i] = *result;
  });
  for (auto& requester : requesters) requester.join();
  for (const auto& handle : handles) check(handle == *first, "equal context created another record");
  check(probe.calls == 1, "duplicate request launched another calibration");
  probe.release = true;
  const auto snapshot = settled(*first);
  check(snapshot.state == loom::HardwareState::calibrated_local && snapshot.profile.revision == 1 &&
        snapshot.profile.preferred_width == 4 && snapshot.profile.training_accesses == 3,
        "qualified local observations were not published");

  Probe queued;
  queued.wait = true;
  {
    auto config = options(queued);
    config.context_limit = 2;
    loom::HardwareService serial(config);
    queued.reentrant_service = &serial;
    auto a = serial.request(real);
    check(a, "first queued context");
    until([&] { return queued.entered.load(); }, "serial calibration not entered");
    auto other = real;
    other.signature += "-other";
    auto b = serial.request(other);
    check(b && *a != *b, "distinct context was coalesced");
    other.signature += "-third";
    auto full = serial.request(other);
    check(!full && full.error() == orbital::Error::capacity, "context registry bound ignored");
    check(queued.calls == 1, "queued contexts allocated concurrently");
    serial.cancel();
    defaults(settled(*a), loom::HardwareState::cancelled, "active cancellation published data");
    defaults(settled(*b), loom::HardwareState::cancelled, "queued cancellation not settled");
    check(!serial.request(real), "cancelled service accepted a request");
  }
  check(queued.active == 0 && queued.finished == 1 && queued.cancelled && queued.peak == 1 && queued.reentered,
        "service destruction did not join active calibration");
  std::shared_ptr<loom::HardwareProfile> survivor;
  Probe destroyed;
  destroyed.wait = true;
  {
    loom::HardwareService owner(options(destroyed));
    auto handle = owner.request(real);
    check(handle, "surviving handle request");
    survivor = *handle;
    until([&] { return destroyed.entered.load(); }, "destroyed calibration not entered");
  }
  defaults(survivor->snapshot(), loom::HardwareState::cancelled, "survivor lost cancellation snapshot");
  check(destroyed.finished == 1 && destroyed.active == 0, "destructor left a running callback");

  for (unsigned outcome = 0; outcome < 4; ++outcome) {
    Probe local;
    if (outcome == 0) local.outcome = loom::HardwareState::failed;
    if (outcome == 1) local.evidence = {};
    if (outcome == 2) { local.wait = true; }
    if (outcome == 3) local.evidence.width_complete = false;
    auto config = options(local);
    if (outcome == 2) config.limits.wall_time = std::chrono::milliseconds(2);
    loom::HardwareService current(config);
    auto handle = current.request(real);
    check(handle, "outcome request");
    const auto result = settled(*handle);
    if (outcome == 0) defaults(result, loom::HardwareState::failed, "failed probe changed defaults");
    if (outcome == 1) defaults(result, loom::HardwareState::calibrated_local, "unknown probe invented a profile");
    if (outcome == 2) defaults(result, loom::HardwareState::timed_out, "timed-out probe changed defaults");
    if (outcome == 3) check(result.profile.preferred_width == 8 && !result.width_qualified &&
        result.profile.training_accesses == 3 && result.training_qualified, "partial calibration erased independent field");
  }
  for (unsigned boundary = 0; boundary < 6; ++boundary) {
    auto config = options(probe);
    if (boundary == 0) config.limits.memory_bytes = config.limits.mapping_bytes;
    if (boundary == 1) config.limits.mapping_bytes = std::numeric_limits<std::size_t>::max();
    if (boundary == 2) config.limits.wall_time = std::chrono::milliseconds::zero();
    if (boundary == 3) config.context_limit = 0;
    auto context = real;
    if (boundary == 4) context.geometry.line_bytes = 63;
    if (boundary == 5) {
      context.geometry = {65536, 65536};
      config.limits.mapping_bytes = 1024 * 1024;
    }
    loom::HardwareService invalid(config);
    auto result = invalid.request(context);
    check(!result && result.error() == orbital::Error::invalid_argument, "invalid budget/geometry accepted");
  }
  // Exercise the real in-process probe, not a helper that reports synthetic data.
  auto config = loom::HardwareOptions{};
  config.table = {};
  config.limits.mapping_bytes = 1024 * 1024;
  config.limits.memory_bytes = 2 * 1024 * 1024;
  config.limits.wall_time = std::chrono::milliseconds(1);
  loom::HardwareService bounded(config);
  auto handle = bounded.request(real);
  check(handle, "bounded real calibration request");
  defaults(settled(*handle), loom::HardwareState::timed_out, "real probe wall cap did not retain defaults");
}

struct Job { const std::uint64_t* values{}; std::uint64_t result{}, completed{}; };
using Ring = loom::Ring<std::uint64_t, std::uint64_t, std::uint64_t, Job*>;
LOOM_NATIVE_CC void native(Ring* ring, Ring::Slot* slot, std::uint64_t cursor,
    std::uint64_t remaining, std::uint64_t sum, Job* job) {
  sum += job->values[cursor];
  cursor = (cursor + 7) & 63;
  if (!--remaining) { job->result = sum; ++job->completed; ring->complete(*slot); }
  [[clang::musttail]] return Ring::yield(ring, slot, cursor, remaining, sum, job);
}
void foreground_checks(const loom::HardwareContext& context) {
  Probe probe;
  probe.wait = true;
  loom::HardwareService service(options(probe));
  auto handle = service.request(context);
  check(handle, "foreground miss request");
  until([&] { return probe.entered.load(); }, "foreground probe not entered");
  loom::Worker worker;
  check(loom::adopt(worker, context.cpu), "foreground lane adoption");
  auto arena = orbital::allocation::open_arena(8 * 1024 * 1024);
  check(arena, "foreground arena");
  auto invalid_context = context;
  invalid_context.qualified = !invalid_context.qualified;
  loom::HardwareOptions invalid_options;
  invalid_options.calibrate = false;
  invalid_options.table = {};
  loom::HardwareService invalid_service(invalid_options);
  auto invalid_handle = invalid_service.request(invalid_context);
  check(invalid_handle, "mismatched qualification handle");
  check(!loom::configure(worker, *arena, 0, *invalid_handle) && !worker.memory,
        "Worker attached a differently qualified context");
  check(loom::configure(worker, *arena, 0, *handle), "foreground Worker attach");
  Ring ring;
  check(ring.configure(worker, {4, 2}), "foreground Ring configure");
  auto allocation = loom::allocate(worker, {.bytes = 64 * sizeof(std::uint64_t), .alignment = 64});
  check(allocation, "foreground allocation");
  auto use = loom::hold(*allocation, {0, 64 * sizeof(std::uint64_t)});
  check(use, "foreground qualified Hold");
  auto* values = reinterpret_cast<std::uint64_t*>(loom::bytes(*use).data());
  for (unsigned i = 0; i < 64; ++i) std::construct_at(values + i, i * std::uint64_t(13) + 9);
  std::array<Job, 6> jobs;
  for (auto& job : jobs) job.values = values;
  const auto dependency = loom::bind_dependency({loom::MemoryPattern::irregular, 64}, loom::bytes(*use));
  check(dependency, "foreground demand binding");
  for (unsigned i = 0; i < jobs.size(); ++i)
    check(ring.push(native, {i, 17, 0, &jobs[i]}, *dependency), "foreground Ring admission");
  check(ring.spin({12}) == 12 && ring.pending() == 6 && !probe.finished, "foreground did not progress during miss");
  check(!ring.hint_policy().profile.revision && !worker.hints.profile.revision, "pending calibration mutated native policy");
  probe.release = true;
  const auto result = settled(*handle);
  check(result.profile.revision == 1, "foreground calibrated result");
  check(loom::refresh_hints(worker) && ring.refresh_hints(), "returned-boundary policy adoption");
  check(ring.hint_policy().profile.training_accesses == 3 && worker.hints.profile.preferred_width == 4,
        "policy adoption fields missing");
  while (ring.pending()) check(ring.spin({5}) > 0, "foreground resumed progress");
  for (unsigned i = 0; i < jobs.size(); ++i) {
    std::uint64_t expected{};
    for (unsigned step = 0; step < 17; ++step) expected += ((i + step * 7) & 63) * 13 + 9;
    check(jobs[i].completed == 1 && jobs[i].result == expected, "foreground independent native values/once");
  }
  check(ring.counters().visits == 102 && ring.counters().promoted == 2, "foreground independent visit/promotion counts");
  check(ring.close(), "foreground Ring close");
  *use = {};
  *allocation = {};
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  while (loom::shutdown(worker).kind() != orbital::Progress::Kind::done) {
    check(Clock::now() < deadline, "foreground Worker shutdown");
    loom::poll(worker, {256, 0});
  }
  loom::relinquish(worker);
}
} // namespace

int main() {
  auto context = loom::discover_hardware();
  check(context && loom::valid_hint_geometry(context->geometry) && !context->signature.empty(), "Linux hardware discovery");
  check(!loom::discover_hardware(-2), "invalid discovery CPU accepted");
  inference_checks();
  lookup_checks(*context);
  lifecycle_checks(*context);
  foreground_checks(*context);
  std::cout << "hardware discovery, exact lookup, bounded calibration, coalescing, cancellation and native progress passed\n";
}
