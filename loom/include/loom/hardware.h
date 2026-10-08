#pragma once

// Startup facts and bounded local observations. Workers copy a profile at an
// operation boundary; native continuations never query this service.

#include <loom/hints.h>
#include <array>
#include <chrono>
#include <condition_variable>
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

struct HardwareTableEntry {
  std::string_view signature;
  HintGeometry geometry;
  unsigned preferred_width{}, training_accesses{}; ///< Zero means unresolved.
  unsigned format{1};
  bool validated{true};
};
[[nodiscard]] std::span<const HardwareTableEntry> hardware_table() noexcept;

enum class HardwareState {
  pending, lookup, calibrated_local, failed, cancelled, timed_out, disabled
};
struct HardwareSnapshot {
  HintProfile profile;
  HardwareState state{HardwareState::pending};
  bool width_qualified{}, training_qualified{};
};

struct HardwareCalibrationLimits {
  std::size_t mapping_bytes{64 * 1024 * 1024};
  /// Mapping plus pointer-order scratch, excluding fixed thread/library overhead.
  std::size_t memory_bytes{72 * 1024 * 1024};
  std::chrono::milliseconds wall_time{5000};
};

namespace detail {
inline constexpr std::array<unsigned, 10> hardware_widths{1, 2, 4, 8, 12, 16, 24, 32, 48, 64};
inline constexpr std::array<unsigned, 8> hardware_training{1, 2, 3, 4, 6, 8, 12, 16};
struct HardwareControl {
  double cold_p10{}, cold_median{}, hot_median{}, hot_p90{}, trained_median{};
};
struct HardwareEvidence {
  std::array<std::array<double, 10>, 2> ns_per_load{};
  /// Four rows: two independent seeds, each with two repetitions.
  std::array<std::array<HardwareControl, 8>, 4> training{};
  bool width_complete{}, training_complete{};
};
struct HardwareInference {
  unsigned preferred_width{}, training_accesses{};
};
[[nodiscard]] HardwareInference qualify_hardware(const HardwareEvidence&) noexcept;
struct HardwareCalibrationResult {
  HardwareState state{HardwareState::failed};
  HardwareEvidence evidence;
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
public:
  /// Coarse returned-boundary read; no lock is held while native work executes.
  [[nodiscard]] HardwareSnapshot snapshot() const noexcept;
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
  /// Defaults are available on return. A hit does not launch calibration;
  /// misses queue once. No caller waits for a probe or background allocation.
  [[nodiscard]] orbital::Result<std::shared_ptr<HardwareProfile>> request(HardwareContext);
  /// Cooperative cancellation for the owned service; joining remains destruction's duty.
  void cancel() noexcept;
};

} // namespace loom
