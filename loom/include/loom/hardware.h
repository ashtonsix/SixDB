#pragma once

// Startup facts and bounded local observations. Workers copy a profile at an
// operation boundary; native continuations never query this service.

#include <loom/hints.h>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace loom {

struct HardwareContext {
  int cpu{-1};
  HintGeometry geometry;
  /// Canonical reported CPU/kernel/cache/page/SMT/governor context. CPU numbering
  /// is excluded; full equality qualifies a lookup, rather than an ISA label.
  std::string signature;
  bool qualified{};
};
[[nodiscard]] orbital::Result<HardwareContext> discover_hardware(int cpu = -1);
[[nodiscard]] bool compatible_hardware_context(const HardwareContext&, const HardwareContext&) noexcept;
/// Cheap geometry/default setup when no shared startup service is attached.
[[nodiscard]] orbital::Result<HintGeometry> reported_hint_geometry(int cpu = -1);

/// Conditioned forward-one-line latency observation after one demand access,
/// within a native page, with randomized regions and separated cold/hot controls.
/// This identifies neither a hardware engine nor continued cache residency.
enum class ForwardObservation { unknown, observed_benefit, no_observed_benefit };

enum class HardwareOrigin { provisional, supplied, retained, local };
enum class HardwareQualification {
  not_measured, qualified, unsupported, incomplete, weak, mixed, censored,
  failed, cancelled, timed_out, disabled
};
enum class HardwareMethod { none, supplied, random_encoded_chains, stream_training, single_demand_forward };
enum class HardwarePageMode { unknown, base, huge, mixed };
enum class HardwareInterference { unknown, isolated, foreground_conditional };
struct HardwareObservationScope {
  unsigned method_version{}, control_protocol{};
  std::size_t mapping_bytes{}, base_page_bytes{};
  int measurement_cpu{-1};
  HardwarePageMode page_mode{HardwarePageMode::unknown};
  HardwareInterference interference{HardwareInterference::unknown};
};
struct HardwareFieldObservation {
  HardwareOrigin origin{HardwareOrigin::provisional};
  HardwareQualification qualification{HardwareQualification::not_measured};
  HardwareMethod method{HardwareMethod::none};
  HardwareObservationScope scope;
};
/// Empty metadata denotes caller-supplied values, not retained measurements.
struct HardwareRetainedEvidence {
  std::string_view source_sha256;
  unsigned probe_format{}, control_protocol{};
  std::size_t mapping_bytes{};
  std::uint16_t width_candidates{}; ///< Bits index detail::hardware_widths.
  std::array<std::string_view, 4> measurement_source_digests{};
  unsigned measurement_source_count{};
  HardwareQualification width_qualification{HardwareQualification::not_measured};
};
struct HardwareTableEntry {
  std::string_view signature;
  HintGeometry geometry;
  unsigned preferred_width{}, training_accesses{}; ///< Zero means unresolved.
  unsigned format{1};
  bool validated{true};
  /// Starting-line parity within the page; unknown is independent per parity.
  std::array<ForwardObservation, 2> adjacent_forward{};
  HardwareRetainedEvidence evidence{};
};
[[nodiscard]] std::span<const HardwareTableEntry> hardware_table() noexcept;

enum class HardwareState {
  pending, lookup, calibrated_local, failed, cancelled, timed_out, disabled
};
struct HardwareSnapshot {
  HintProfile profile;
  /// Pending learning may already carry independently qualified table fields.
  HardwareState state{HardwareState::pending};
  bool width_qualified{}, training_qualified{};
  std::array<ForwardObservation, 2> adjacent_forward{};
};

struct HardwareCalibrationLimits {
  std::size_t mapping_bytes{64 * 1024 * 1024};
  /// Mapping plus pointer-order scratch, excluding fixed thread/library overhead.
  std::size_t memory_bytes{72 * 1024 * 1024};
  std::chrono::milliseconds wall_time{5000};
};

struct HardwareControl {
  double cold_p10{}, cold_median{}, hot_median{}, hot_p90{}, trained_median{};
};
struct HardwareEvidence {
  std::array<std::array<double, 10>, 2> ns_per_load{};
  /// Four rows: two independent seeds, each with two repetitions.
  std::array<std::array<HardwareControl, 8>, 4> training{};
  bool width_complete{}, training_complete{};
  /// Single-demand controls: trained_median is the untouched forward neighbor.
  /// Four rows are two seeds, each with two repetitions; completed per parity.
  std::array<std::array<HardwareControl, 2>, 4> adjacent_forward{};
  std::array<bool, 2> adjacent_complete{};
};
struct HardwareNumaPages { unsigned node{}; std::size_t pages{}; };
struct HardwareMappingReceipt {
  bool smaps_known{}, numa_known{};
  std::size_t mapping_bytes{}, kernel_page_bytes{}, mmu_page_bytes{}, anonymous_huge_bytes{};
  bool no_hugepage{}; ///< Observed VmFlags nh, not the madvise request.
  std::vector<HardwareNumaPages> residence;
};
struct HardwareObservations {
  HardwareFieldObservation width, training;
  std::array<HardwareFieldObservation, 2> adjacent_forward;
  std::vector<unsigned> width_candidates;
  std::vector<unsigned> retained_width_candidates; ///< Historical candidates, even when censored.
  /// Complete compact controls, including unresolved or rejected local results.
  HardwareEvidence local_evidence;
  bool local_attempted{};
  /// Before/after the sweeps. Inaccessible or ambiguous mappings stay unknown.
  std::array<HardwareMappingReceipt, 2> mapping_receipts{};
  std::string retained_source_sha256;
  unsigned retained_probe_format{}, retained_control_protocol{};
  std::vector<std::string> retained_measurement_source_digests;
};

struct HardwareInspection {
  HardwareSnapshot snapshot;
  std::shared_ptr<const HardwareObservations> observations;
};

namespace detail {
inline constexpr std::array<unsigned, 10> hardware_widths{1, 2, 4, 8, 12, 16, 24, 32, 48, 64};
inline constexpr std::array<unsigned, 8> hardware_training{1, 2, 3, 4, 6, 8, 12, 16};
using HardwareControl = loom::HardwareControl;
using HardwareEvidence = loom::HardwareEvidence;
[[nodiscard]] unsigned hardware_cpu_count(std::string_view) noexcept;
[[nodiscard]] std::vector<HardwareNumaPages> hardware_numa_pages(std::string_view,
    std::size_t mapping_bytes, std::size_t page_bytes);
struct HardwareInference {
  unsigned preferred_width{}, training_accesses{};
  std::array<ForwardObservation, 2> adjacent_forward{};
  std::uint16_t width_candidates{};
};
[[nodiscard]] HardwareInference qualify_hardware(const HardwareEvidence&) noexcept;
struct HardwareCalibrationResult {
  HardwareState state{HardwareState::failed};
  HardwareEvidence evidence;
  /// Custom calibrators leave native_protocol false unless their actual method
  /// obeys the native protocol; absent receipts never imply base/NUMA placement.
  bool native_protocol{};
  int measurement_cpu{-1};
  std::array<bool, 4> unsupported{}; ///< Width, training, forward parity 0/1.
  std::array<HardwareMappingReceipt, 2> mapping_receipts{};
};
/// Test seam: callbacks must obey both limits and cooperative cancellation, and
/// must not retain arguments. Their context outlives the service's joined thread.
using HardwareCalibrator = HardwareCalibrationResult (*)(const HardwareContext&,
    HardwareCalibrationLimits, std::stop_token, void*) noexcept;
} // namespace detail

struct HardwareOptions {
  HardwareCalibrationLimits limits;
  std::size_t context_limit{16};
  bool calibrate{true};
  /// Table is read synchronously during request; the caller keeps its strings
  /// alive for those calls. An empty table deliberately forces qualified misses.
  std::span<const HardwareTableEntry> table{hardware_table()};
  detail::HardwareCalibrator calibrator{};
  void* calibrator_context{};
};

class HardwareProfile {
  friend class HardwareService;
  mutable std::mutex mutex_;
  HardwareContext context_;
  HardwareSnapshot snapshot_;
  std::shared_ptr<const HardwareObservations> observations_;
public:
  /// Coarse returned-boundary read; no lock is held while native work executes.
  [[nodiscard]] HardwareSnapshot snapshot() const noexcept;
  /// Immutable cold evidence: existing readers retain their original revision.
  /// This avoids copying controls during ordinary Worker policy adoption.
  [[nodiscard]] std::shared_ptr<const HardwareObservations> observations() const noexcept;
  /// Capture policy and evidence together when inspecting an in-flight update.
  [[nodiscard]] HardwareInspection inspect() const noexcept;
  [[nodiscard]] const HardwareContext& context() const noexcept { return context_; }
};

/// Share explicitly between Workers. One serial background thread bounds memory;
/// equal reported contexts coalesce even when requested on different CPU numbers.
/// Destruction cancels and joins. Surviving handles retain their final snapshots.
class HardwareService {
  HardwareOptions options_;
  std::mutex mutex_;
  std::condition_variable_any changed_;
  std::vector<std::shared_ptr<HardwareProfile>> profiles_;
  std::jthread thread_;
  bool closing_{};
  void run(std::stop_token) noexcept;
public:
  explicit HardwareService(HardwareOptions options = {}) : options_(options) {}
  ~HardwareService();
  HardwareService(const HardwareService&) = delete;
  HardwareService& operator=(const HardwareService&) = delete;
  /// Known fields and defaults are available on return. Missing fields queue
  /// once unless calibration is disabled; a fully known hit needs no job.
  /// No caller waits for a probe or background allocation.
  [[nodiscard]] orbital::Result<std::shared_ptr<HardwareProfile>> request(HardwareContext);
  /// Cooperative cancellation for the owned service; joining remains destruction's duty.
  void cancel() noexcept;
};

} // namespace loom
