#pragma once

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace workbench::pmu {
enum class Mode { off, basic, stalls };
struct EventSpec {
  std::string name;
  std::uint32_t type{};
  std::uint64_t config{};
};
/// name:type:config, with an ASCII name and unsigned decimal or 0x numbers.
/// Throws invalid_argument for malformed/overflowing input. Parse before timing;
/// the runner must separately qualify raw encodings against the actual PMU.
[[nodiscard]] EventSpec parse_event_spec(std::string_view);

/// Linux counting only, for the constructing thread's user-space execution.
/// Construct before timing; start/stop once on that thread, around the complete
/// request loop and physical drain. All storage and FDs are prepared cold.
/// The required cycles/instructions pair shares one fresh group. Optional open
/// failures are retained; no count scaling, sampling or per-visit observation.
/// Kernel/PMU failures do not throw from start/stop and produce explicit JSON
/// status with null counts. off performs no perf syscall, even with extras.
class Session {
public:
  explicit Session(Mode, std::span<const EventSpec> extras = {});
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  Session(Session&&) = delete;
  Session& operator=(Session&&) = delete;
  void start() noexcept;
  void stop() noexcept;
  void write_json(std::ostream&) const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
