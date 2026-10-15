#include "locality.h"

// Explicit ordinary-boundary entries for disassembly and a separately timed
// decision-only consumer. No production callback acquires these wrappers.
extern "C" [[gnu::noinline]] loom_locality::Decision locality_adaptive_decision(
    const loom_locality::Record& record, const loom_locality::Tag& tag,
    loom_locality::Epoch epoch, loom_locality::Paths paths,
    const loom_locality::Policy& policy) noexcept {
  return loom_locality::decide(record, tag, epoch, paths, policy);
}

extern "C" [[gnu::noinline]] loom_locality::Decision locality_known_direct_decision(
    const loom_locality::Record& record, const loom_locality::Tag& tag,
    loom_locality::Epoch epoch, loom_locality::Paths paths,
    const loom_locality::Policy& policy) noexcept {
  return loom_locality::decide(record, tag, epoch, paths, policy,
      loom_locality::Control::caller_known_direct);
}
