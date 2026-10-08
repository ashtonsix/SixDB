#pragma once

#include "policy.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace loom_policy_spike {

enum class Dispatch { fixed_inline, code_lut_switch, procedure_lut, continuation_lut };

struct NativeStats {
  /// Logical input lines consumed, not all retired CPU load instructions.
  std::uint64_t loads{}, checksum{}, visits{}, elapsed_ns{};
};

/// One stable owner-lane Ring family and one held input bank, reused across
/// batches. Construction initializes accessible native bytes outside timing.
/// Nodes is a power of two >=16; a linear visit consumes a complete 16-line group.
/// Destroy on the constructing thread after the last batch returns.
class NativeWork {
public:
  NativeWork(Geometry, std::size_t width, std::size_t nodes, int cpu = -1);
  ~NativeWork();
  NativeWork(const NativeWork&) = delete;
  NativeWork& operator=(const NativeWork&) = delete;
  NativeWork(NativeWork&&) = delete;
  NativeWork& operator=(NativeWork&&) = delete;

  /// Each job takes rounds visits. Policy/LUT preparation and independent
  /// expected-result checks are outside timing; initial hints, enqueue and all
  /// spins are timed. Every batch drains fully before return, so no live frame
  /// observes a replaced LUT. Geometry and physical Ring capacity remain fixed;
  /// preferred_width selects actual admitted work within that prepared capacity.
  NativeStats batch(const Policy&, Dispatch, std::size_t rounds,
      Pattern = Pattern::irregular);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace loom_policy_spike
