#include <loom/hardware.h>
#include <loom/ring.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using Clock = std::chrono::steady_clock;
using Forward = loom::ForwardObservation;
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
  evidence.adjacent_complete = {true, true};
  for (auto& row : evidence.adjacent_forward) {
    row[0] = {90, 100, 10, 15, 40};
    row[1] = {90, 100, 10, 15, 95};
  }
  return evidence;
}
void parser_checks() {
  check(loom::detail::hardware_cpu_count("0") == 1 &&
      loom::detail::hardware_cpu_count("0-2,8,10-11") == 6, "valid CPU lists changed canonical sharing width");
  for (auto value : {"", "junk", "-1", "1-", "0,,1", "0,", "1-0", "1--2", "0-2,2", "4,2", "0-1024", "4294967296"})
    check(!loom::detail::hardware_cpu_count(value), "malformed/overlapping CPU list became reported sharing width");
  const auto pages = loom::detail::hardware_numa_pages("default anon=4 N0=1 N3=3 kernelpagesize_kB=4", 16384, 4096);
  check(pages.size() == 2 && pages[0].node == 0 && pages[0].pages == 1 && pages[1].node == 3 && pages[1].pages == 3,
      "full unique NUMA receipt lost membership");
  for (auto value : {"N0=2 N0=2 kernelpagesize_kB=4", "N0=3 kernelpagesize_kB=4",
      "N0=5 kernelpagesize_kB=4", "N0=4", "N0=4 kernelpagesize_kB=2", "N0=4 kernelpagesize_kB=4 kernelpagesize_kB=4",
      "N0=0 N1=4 kernelpagesize_kB=4", "Ngarbage=4 kernelpagesize_kB=4", "N0=4294967296 kernelpagesize_kB=4"})
    check(loom::detail::hardware_numa_pages(value, 16384, 4096).empty(), "duplicate/partial/malformed NUMA receipt qualified full mapping");
}
void inference_checks() {
  auto evidence = useful_evidence();
  auto result = loom::detail::qualify_hardware(evidence);
  check(result.preferred_width == 4 && result.training_accesses == 3, "independent common width/forward suffix");
  check(result.adjacent_forward == std::array{Forward::observed_benefit, Forward::no_observed_benefit},
        "separated consistent single-demand parities did not qualify independently");
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

  evidence = useful_evidence();
  evidence.adjacent_complete[0] = false;
  result = loom::detail::qualify_hardware(evidence);
  check(result.adjacent_forward == std::array{Forward::unknown, Forward::no_observed_benefit},
        "incomplete forward parity invented facts or erased independent parity");
  evidence = useful_evidence();
  evidence.adjacent_forward[3][0].trained_median = 95;
  check(loom::detail::qualify_hardware(evidence).adjacent_forward[0] == Forward::unknown,
        "one inconsistent forward repetition qualified");
  evidence = useful_evidence();
  evidence.adjacent_forward[1][1].cold_p10 = 10;
  result = loom::detail::qualify_hardware(evidence);
  check(result.adjacent_forward == std::array{Forward::observed_benefit, Forward::unknown},
        "overlapping controls became no-benefit knowledge");
  evidence = useful_evidence();
  for (auto& row : evidence.adjacent_forward) row[0].trained_median = 82;
  check(loom::detail::qualify_hardware(evidence).adjacent_forward[0] == Forward::unknown,
        "intermediate forward observation escaped unknown band");
  evidence = useful_evidence();
  evidence.adjacent_forward[0][0].trained_median = std::numeric_limits<double>::quiet_NaN();
  check(loom::detail::qualify_hardware(evidence).adjacent_forward[0] == Forward::unknown,
        "nonfinite forward observation qualified");
  evidence = useful_evidence();
  evidence.adjacent_forward[0][1].hot_median = 0;
  check(loom::detail::qualify_hardware(evidence).adjacent_forward[1] == Forward::unknown,
        "invalid hot control qualified no-benefit");
}

struct Probe {
  std::atomic<unsigned> calls{}, active{}, peak{}, finished{};
  std::atomic<bool> entered{}, release{}, cancelled{};
  loom::HardwareState outcome{loom::HardwareState::calibrated_local};
  loom::detail::HardwareEvidence evidence{useful_evidence()};
  bool wait{}, native_protocol{};
  std::array<bool, 4> unsupported{};
  std::array<loom::HardwareMappingReceipt, 2> mapping_receipts;
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
  return {probe.outcome, probe.evidence, probe.native_protocol, hardware.cpu, probe.unsupported, probe.mapping_receipts};
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
        snapshot.profile.training_accesses == 0 && !snapshot.width_qualified && !snapshot.training_qualified &&
        snapshot.adjacent_forward == std::array{Forward::unknown, Forward::unknown}, message);
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
    config.calibrate = false;
    loom::HardwareService service(config);
    auto context = real;
    context.signature = entry.signature;
    context.geometry = entry.geometry;
    context.qualified = true;
    auto handle = service.request(context);
    check(handle, "retained table request");
    const auto snapshot = (*handle)->snapshot();
    check(snapshot.state == loom::HardwareState::lookup && snapshot.profile.preferred_width == (entry.preferred_width ? entry.preferred_width : 8) &&
          snapshot.profile.training_accesses == entry.training_accesses && snapshot.adjacent_forward == entry.adjacent_forward &&
          !probe.calls, "compiled table missed, dropped forward observations or calibrated");
  }
  Probe probe;
  auto config = options(probe);
  const loom::HardwareTableEntry hit{real.signature, real.geometry, 4, 3, 1, true,
      {Forward::observed_benefit, Forward::no_observed_benefit}};
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
  config.calibrate = false;
  loom::HardwareService partial_service(config);
  auto partial_handle = partial_service.request(real);
  check(partial_handle, "partial lookup request");
  const auto partial_snapshot = (*partial_handle)->snapshot();
  check(partial_snapshot.state == loom::HardwareState::lookup && partial_snapshot.profile.preferred_width == 8 &&
        !partial_snapshot.width_qualified && partial_snapshot.profile.training_accesses == 3,
        "unknown table width discarded qualified training");
}

void partial_lookup_checks(const loom::HardwareContext& real) {
  // The probe deliberately disagrees with known table values. Only missing
  // fields may be learned, and duplicate requests must share the same job.
  for (unsigned missing = 0; missing < 3; ++missing) {
    Probe probe;
    probe.wait = true;
    const loom::HardwareTableEntry entry{real.signature, real.geometry,
        missing == 0 ? 16u : 0u, missing == 1 ? 6u : 0u};
    auto config = options(probe);
    config.table = {&entry, 1};
    loom::HardwareService service(config);
    auto handle = service.request(real);
    check(handle, "partial hit request");
    const auto initial = (*handle)->snapshot();
    check(initial.state == loom::HardwareState::pending &&
        initial.profile.preferred_width == (missing == 0 ? 16u : 8u) &&
        initial.profile.training_accesses == (missing == 1 ? 6u : 0u) &&
        initial.width_qualified == (missing == 0) && initial.training_qualified == (missing == 1) &&
        initial.profile.revision == (missing == 2 ? 0u : 1u), "partial hit did not return known/default fields while learning");
    until([&] { return probe.entered.load(); }, "partial hit did not calibrate");
    auto duplicate = service.request(real);
    check(duplicate && *duplicate == *handle && probe.calls == 1, "partial hit duplicated calibration");
    probe.release = true;
    const auto learned = settled(*handle);
    check(learned.state == loom::HardwareState::calibrated_local && learned.width_qualified &&
        learned.training_qualified && learned.profile.preferred_width == (missing == 0 ? 16u : 4u) &&
        learned.profile.training_accesses == (missing == 1 ? 6u : 3u) &&
        learned.profile.revision == (missing == 2 ? 1u : 2u), "missing field adoption overwrote table knowledge or lost its revision");
    duplicate = service.request(real);
    check(duplicate && *duplicate == *handle && probe.calls == 1, "settled partial hit restarted calibration");
  }

  for (const auto outcome : {loom::HardwareState::calibrated_local, loom::HardwareState::failed,
       loom::HardwareState::timed_out, loom::HardwareState::cancelled}) {
    Probe probe;
    probe.wait = true;
    probe.outcome = outcome;
    probe.evidence = {};
    const loom::HardwareTableEntry entry{real.signature, real.geometry, 16, 0};
    auto config = options(probe);
    config.table = {&entry, 1};
    loom::HardwareService service(config);
    auto handle = service.request(real);
    check(handle, "partial unsuccessful request");
    until([&] { return probe.entered.load(); }, "partial unsuccessful calibration not entered");
    if (outcome == loom::HardwareState::cancelled) service.cancel();
    else probe.release = true;
    const auto retained = settled(*handle);
    check(retained.state == outcome && retained.profile.revision == 1 &&
        retained.width_qualified && !retained.training_qualified &&
        retained.profile.preferred_width == 16 && !retained.profile.training_accesses,
        "unsuccessful learning erased known table fields or invented a revision");
  }
}

void forward_lookup_checks(const loom::HardwareContext& real) {
  {
    Probe probe;
    probe.wait = true;
    const loom::HardwareTableEntry entry{real.signature, real.geometry, 0, 0, 1, true,
        {Forward::no_observed_benefit, Forward::no_observed_benefit}};
    auto config = options(probe);
    config.table = {&entry, 1};
    loom::HardwareService service(config);
    auto handle = service.request(real);
    check(handle, "forward-only table knowledge request");
    const auto initial = (*handle)->snapshot();
    check(initial.state == loom::HardwareState::pending && initial.profile.revision == 1 &&
        !initial.width_qualified && !initial.training_qualified && initial.profile.preferred_width == 8 &&
        !initial.profile.training_accesses && initial.adjacent_forward == entry.adjacent_forward,
        "forward-only table knowledge lost defaults or revision");
    until([&] { return probe.entered.load(); }, "forward-only table did not learn missing scheduling fields");
    probe.release = true;
    const auto learned = settled(*handle);
    check(learned.profile.revision == 2 && learned.width_qualified && learned.training_qualified &&
        learned.profile.preferred_width == 4 && learned.profile.training_accesses == 3 &&
        learned.adjacent_forward == entry.adjacent_forward, "missing scheduling fields overwrote forward-only table knowledge");
  }
  // Both scheduling fields are known. Only one forward parity is missing;
  // the controlled probe deliberately contradicts the retained parity.
  for (unsigned missing = 0; missing < 2; ++missing) {
    Probe probe;
    probe.wait = true;
    loom::HardwareTableEntry entry{real.signature, real.geometry, 16, 6, 1, true,
        {Forward::no_observed_benefit, Forward::observed_benefit}};
    entry.adjacent_forward[missing] = Forward::unknown;
    auto config = options(probe);
    config.table = {&entry, 1};
    loom::HardwareService service(config);
    auto handle = service.request(real);
    check(handle, "single unresolved forward parity request");
    const auto initial = (*handle)->snapshot();
    check(initial.state == loom::HardwareState::pending && initial.profile.revision == 1 &&
        initial.profile.preferred_width == 16 && initial.profile.training_accesses == 6 &&
        initial.adjacent_forward == entry.adjacent_forward, "known scheduling fields hid missing forward learning");
    until([&] { return probe.entered.load(); }, "missing forward parity did not calibrate");
    const auto duplicate = service.request(real);
    check(duplicate && *duplicate == *handle && probe.calls == 1, "missing forward parity duplicated calibration");
    probe.release = true;
    const auto learned = settled(*handle);
    auto expected = entry.adjacent_forward;
    expected[missing] = missing == 0 ? Forward::observed_benefit : Forward::no_observed_benefit;
    check(learned.state == loom::HardwareState::calibrated_local && learned.profile.revision == 2 &&
        learned.adjacent_forward == expected && learned.profile.preferred_width == 16 &&
        learned.profile.training_accesses == 6, "forward adoption overwrote retained knowledge or scheduling fields");
  }

  for (const auto outcome : {loom::HardwareState::calibrated_local, loom::HardwareState::failed,
       loom::HardwareState::timed_out, loom::HardwareState::cancelled}) {
    Probe probe;
    probe.wait = true;
    probe.outcome = outcome;
    probe.evidence.adjacent_complete[1] = false;
    // Parity zero contradicts the table, and parity one has no complete row.
    const loom::HardwareTableEntry entry{real.signature, real.geometry, 16, 6, 1, true,
        {Forward::no_observed_benefit, Forward::unknown}};
    auto config = options(probe);
    config.table = {&entry, 1};
    loom::HardwareService service(config);
    auto handle = service.request(real);
    check(handle, "unsuccessful forward observation request");
    until([&] { return probe.entered.load(); }, "unsuccessful forward observation did not enter");
    if (outcome == loom::HardwareState::cancelled) service.cancel();
    else probe.release = true;
    const auto retained = settled(*handle);
    check(retained.state == outcome && retained.profile.revision == 1 &&
        retained.adjacent_forward == entry.adjacent_forward && retained.profile.preferred_width == 16 &&
        retained.profile.training_accesses == 6, "unknown/failed/cancelled forward probe changed retained facts");
  }

  Probe invalid;
  auto config = options(invalid);
  const loom::HardwareTableEntry invalid_entry{real.signature, real.geometry, 16, 6, 1, true,
      {static_cast<Forward>(999), Forward::no_observed_benefit}};
  config.table = {&invalid_entry, 1};
  config.calibrate = false;
  loom::HardwareService service(config);
  auto handle = service.request(real);
  check(handle && (*handle)->snapshot().adjacent_forward ==
      std::array{Forward::unknown, Forward::no_observed_benefit}, "invalid retained forward enum became knowledge");

  // Exercise the actual partial calibration path with only forward observations
  // unresolved. On ARM no EL0 maintenance is attempted: unknown settles without
  // erasing independent table facts. On x86 a tiny allowance may censor a probe.
  const loom::HardwareTableEntry known{real.signature, real.geometry, 16, 6};
  loom::HardwareOptions local;
  local.table = {&known, 1};
  local.limits.mapping_bytes = 1024 * 1024;
  local.limits.memory_bytes = 2 * 1024 * 1024;
  local.limits.wall_time = std::chrono::milliseconds(1);
  loom::HardwareService bounded(local);
  auto actual = bounded.request(real);
  check(actual, "actual forward-only calibration request");
  const auto result = settled(*actual);
  check(result.state != loom::HardwareState::lookup && result.profile.preferred_width == 16 &&
      result.profile.training_accesses == 6 && result.profile.revision >= 1 && result.profile.revision <= 2,
      "actual forward-only path dropped retained facts or skipped missing observations");
#if defined(__aarch64__)
  check(result.adjacent_forward == std::array{Forward::unknown, Forward::unknown},
      "unsupported ARM maintenance invented forward observations");
#endif
}

void observation_checks(const loom::HardwareContext& real) {
  using Origin = loom::HardwareOrigin;
  using Reason = loom::HardwareQualification;
  using Method = loom::HardwareMethod;
  const auto compiled = loom::hardware_table();
  check(!compiled.empty(), "retained corpus absent");
  for (const auto& entry : compiled) {
    Probe probe;
    auto config = options(probe);
    config.table = {&entry, 1}; config.calibrate = false;
    auto context = real;
    context.signature = entry.signature; context.geometry = entry.geometry; context.qualified = true;
    loom::HardwareService service(config);
    auto handle = service.request(context);
    check(handle, "retained observation request");
    const auto record = (*handle)->observations();
    check(record && record->retained_source_sha256.size() == 64 && record->retained_probe_format == 1 &&
        record->retained_measurement_source_digests.size() == entry.evidence.measurement_source_count &&
        !record->local_attempted, "retained source/probe metadata absent or falsely local");
    if (entry.preferred_width) {
      check(record->width.origin == Origin::retained && record->width.qualification == Reason::qualified &&
          record->width.method == Method::random_encoded_chains && record->width.scope.mapping_bytes == 256 * 1024 * 1024 &&
          record->width.scope.interference == loom::HardwareInterference::isolated &&
          record->width.scope.measurement_cpu == -1 && record->width.scope.page_mode == loom::HardwarePageMode::base &&
          std::find(record->width_candidates.begin(), record->width_candidates.end(), entry.preferred_width) !=
              record->width_candidates.end(), "historical width candidates lost isolated footprint/provenance");
    }
  }
  {
    Probe probe;
    const loom::HardwareTableEntry synthetic{real.signature, real.geometry, 4, 3, 1, true,
        {Forward::observed_benefit, Forward::no_observed_benefit}};
    auto config = options(probe); config.table = {&synthetic, 1};
    loom::HardwareService service(config);
    auto handle = service.request(real);
    check(handle, "supplied observations request");
    const auto record = (*handle)->observations();
    check(record->width.origin == Origin::supplied && record->training.origin == Origin::supplied &&
        record->adjacent_forward[0].origin == Origin::supplied && record->retained_source_sha256.empty() &&
        record->width.scope.mapping_bytes == 0 && record->width.scope.page_mode == loom::HardwarePageMode::unknown &&
        record->width_candidates == std::vector<unsigned>{4}, "synthetic table acquired retained measurement provenance");
  }
  {
    Probe probe;
    probe.wait = true; probe.native_protocol = true;
    for (auto& receipt : probe.mapping_receipts) {
      receipt = {true, true, 1024 * 1024, real.geometry.page_bytes, real.geometry.page_bytes, 0, true,
          {{3, 1024 * 1024 / real.geometry.page_bytes}}};
    }
    auto entry = *std::find_if(compiled.begin(), compiled.end(), [](const auto& value) { return value.preferred_width != 0; });
    entry.training_accesses = 0;
    entry.adjacent_forward = {Forward::unknown, Forward::unknown};
    auto context = real; context.signature = entry.signature; context.geometry = entry.geometry; context.qualified = true;
    auto config = options(probe); config.table = {&entry, 1};
    loom::HardwareService service(config);
    auto handle = service.request(context);
    check(handle, "mixed origin observations request");
    const auto initial_inspection = (*handle)->inspect();
    const auto initial = initial_inspection.observations;
    until([&] { return probe.entered.load(); }, "mixed origin calibration did not enter");
    std::atomic<bool> inspected{};
    std::jthread inspector([&] {
      while (!inspected.load()) {
        const auto together = (*handle)->inspect();
        check((together.snapshot.state == loom::HardwareState::pending && !together.observations->local_attempted) ||
            (together.snapshot.state == loom::HardwareState::calibrated_local && together.observations->local_attempted &&
             together.observations->training.qualification == Reason::qualified), "inspection paired policy with another evidence update");
      }
    });
    probe.release = true;
    const auto snapshot = settled(*handle);
    inspected = true;
    inspector.join();
    const auto final_inspection = (*handle)->inspect();
    const auto final = final_inspection.observations;
    check(initial_inspection.snapshot.profile.revision == 1 && final_inspection.snapshot.profile.revision == 2 &&
        final_inspection.snapshot.profile.training_accesses == 3, "combined inspection lost matching policy revision");
    check(final != initial && !initial->local_attempted && initial->training.origin == Origin::provisional &&
        final->width.origin == Origin::retained && final->width.scope.mapping_bytes == 256 * 1024 * 1024 &&
        final->training.origin == Origin::local && final->training.qualification == Reason::qualified &&
        final->training.method == Method::stream_training && final->training.scope.mapping_bytes == 1024 * 1024 &&
        final->training.scope.control_protocol == 2 && final->training.scope.measurement_cpu == context.cpu &&
        final->training.scope.interference == loom::HardwareInterference::foreground_conditional &&
        final->local_evidence.training[3][2].trained_median == 40 &&
        final->local_evidence.ns_per_load[0][0] == 100 && final->mapping_receipts[1].residence[0].node == 3 &&
        snapshot.profile.preferred_width == entry.preferred_width,
        "partial learning lost immutable evidence, mixed origins, controls or actual mapping receipt");
  }
  for (unsigned failure = 0; failure < 5; ++failure) {
    Probe probe;
    probe.wait = true; probe.native_protocol = true;
    const loom::HardwareTableEntry known{real.signature, real.geometry, 16, 0};
    auto config = options(probe); config.table = {&known, 1};
    if (failure == 0) probe.outcome = loom::HardwareState::failed;
    if (failure == 1) probe.outcome = loom::HardwareState::timed_out;
    if (failure == 3) probe.unsupported[1] = true;
    if (failure == 4) probe.evidence.training_complete = false;
    loom::HardwareService service(config);
    auto handle = service.request(real);
    check(handle, "failed conditional observations request");
    until([&] { return probe.entered.load(); }, "failed conditional observations did not enter");
    if (failure == 2) service.cancel(); else probe.release = true;
    settled(*handle);
    const auto record = (*handle)->observations();
    const std::array expected{Reason::failed, Reason::timed_out, Reason::cancelled, Reason::unsupported, Reason::incomplete};
    check(record->width.origin == Origin::supplied && record->width.qualification == Reason::qualified &&
        record->training.qualification == expected[failure], "failed/unsupported/incomplete learning misreported known or missing field");
  }
  for (unsigned uncertainty = 0; uncertainty < 3; ++uncertainty) {
    Probe probe;
    if (uncertainty == 0) probe.evidence.adjacent_forward[1][0].cold_p10 = 10;
    if (uncertainty == 1) probe.evidence.adjacent_forward[1][0].trained_median = 95;
    if (uncertainty == 2) { probe.evidence.ns_per_load[1].fill(100); probe.evidence.ns_per_load[1].back() = 10; }
    loom::HardwareService service(options(probe));
    auto handle = service.request(real);
    check(handle, "uncertain observations request");
    settled(*handle);
    const auto record = (*handle)->observations();
    check(uncertainty == 2 ? record->width.qualification == Reason::censored :
        record->adjacent_forward[0].qualification == (uncertainty == 0 ? Reason::weak : Reason::mixed),
        "weak/mixed/censored evidence lost its unknown reason");
  }
}

void actual_observation_receipts(const loom::HardwareContext& real) {
  // Only width is unresolved; this actual native sweep makes a 1 MiB mapping,
  // rather than relying on the supplied receipt fixtures above.
  const loom::HardwareTableEntry prefetch_known{real.signature, real.geometry, 0, 3, 1, true,
      {Forward::observed_benefit, Forward::no_observed_benefit}};
  loom::HardwareOptions config;
  config.table = {&prefetch_known, 1};
  config.limits.mapping_bytes = 1024 * 1024;
  config.limits.memory_bytes = 2 * 1024 * 1024;
  config.limits.wall_time = std::chrono::milliseconds(1000);
  loom::HardwareService service(config);
  auto handle = service.request(real);
  check(handle, "actual receipt calibration request");
  const auto snapshot = settled(*handle);
  const auto record = (*handle)->observations();
  check(snapshot.state == loom::HardwareState::calibrated_local && record->local_attempted &&
      record->local_evidence.width_complete && record->width.origin == loom::HardwareOrigin::local &&
      record->width.scope.mapping_bytes == 1024 * 1024 && record->width.scope.measurement_cpu == real.cpu &&
      record->width.scope.interference == loom::HardwareInterference::foreground_conditional &&
      record->training.origin == loom::HardwareOrigin::supplied, "actual width observation lost mapping or conditional origin");
  for (const auto& curve : record->local_evidence.ns_per_load)
    for (auto value : curve) check(std::isfinite(value) && value > 0, "actual width controls not retained");
  for (const auto& receipt : record->mapping_receipts) {
    check(receipt.mapping_bytes == 1024 * 1024, "actual mapping footprint changed in receipt");
    if (receipt.smaps_known)
      check(receipt.no_hugepage && receipt.kernel_page_bytes == real.geometry.page_bytes &&
          receipt.mmu_page_bytes == real.geometry.page_bytes && !receipt.anonymous_huge_bytes,
          "MADV_NOHUGEPAGE contradicted actual smaps without being reported");
    if (receipt.numa_known) {
      std::size_t pages{};
      for (const auto& residence : receipt.residence) pages += residence.pages;
      check(pages == 1024 * 1024 / real.geometry.page_bytes, "NUMA receipt attributes an enclosing/partial mapping");
    }
  }
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

  // Real partial calibration exercises a training-only sweep (or unsupported
  // training without a mapping), and a width-only sweep. No measured value is
  // required; the independently known field must survive any probe outcome.
  for (bool known_width : {false, true}) {
    const loom::HardwareTableEntry entry{real.signature, real.geometry,
        known_width ? 16u : 0u, known_width ? 0u : 6u};
    config.table = {&entry, 1};
    loom::HardwareService partial(config);
    auto partial_handle = partial.request(real);
    check(partial_handle, "bounded real partial calibration request");
    const auto result = settled(*partial_handle);
    check(result.state != loom::HardwareState::lookup && result.profile.revision >= 1 &&
        result.profile.revision <= 2, "bounded real partial hit did not attempt missing-field learning");
    check(known_width ? result.width_qualified && result.profile.preferred_width == 16 :
        result.training_qualified && result.profile.training_accesses == 6,
        "bounded real partial calibration replaced known table evidence");
  }
}

struct Job { const std::uint64_t* values{}; std::uint64_t result{}, completed{}; };
using Ring = loom::Ring<std::uint64_t, std::uint64_t, std::uint64_t, Job*>;
LOOM_NATIVE_CC void native(Ring::Cursor ring, Ring::Slot* slot, std::uint64_t cursor,
    std::uint64_t remaining, std::uint64_t sum, Job* job) {
  sum += job->values[cursor];
  cursor = (cursor + 7) & 63;
  if (!--remaining) { job->result = sum; ++job->completed; ring->complete(*slot); }
  [[clang::musttail]] return Ring::yield(ring, slot, cursor, remaining, sum, job);
}
void foreground_checks(const loom::HardwareContext& context, bool partial_hit) {
  Probe probe;
  probe.wait = true;
  const loom::HardwareTableEntry entry{context.signature, context.geometry, 16, 0};
  auto config = options(probe);
  if (partial_hit) config.table = {&entry, 1};
  loom::HardwareService service(config);
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
  const auto initial_revision = partial_hit ? 1u : 0u;
  check(ring.hint_policy().profile.revision == initial_revision &&
      worker.hints.profile.revision == initial_revision, "pending calibration mutated native policy");
  probe.release = true;
  const auto result = settled(*handle);
  const auto learned_revision = partial_hit ? 2u : 1u;
  check(result.profile.revision == learned_revision, "foreground calibrated result");
  check(loom::refresh_hints(worker) && worker.hints.profile.revision == learned_revision &&
      ring.hint_policy().profile.revision == initial_revision && ring.pending() == 6,
      "Worker adoption changed Ring snapshot or pending work");
  check(ring.refresh_hints() && ring.hint_policy().profile.revision == learned_revision,
      "returned-boundary Ring policy adoption");
  check(ring.hint_policy().profile.training_accesses == 3 &&
      worker.hints.profile.preferred_width == (partial_hit ? 16u : 4u),
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
  parser_checks();
  inference_checks();
  lookup_checks(*context);
  partial_lookup_checks(*context);
  forward_lookup_checks(*context);
  observation_checks(*context);
  actual_observation_receipts(*context);
  lifecycle_checks(*context);
  foreground_checks(*context, false);
  foreground_checks(*context, true);
  std::cout << "hardware discovery, exact lookup, bounded calibration, coalescing, cancellation and native progress passed\n";
}
