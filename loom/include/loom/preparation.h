#pragma once

// Off-lane storage preparation in the Worker's actual Pool. The caller selects
// the Lane and optional initializer; success becomes visible on the owner Lane.

#include <loom/worker.h>
#include <orbital/preparation.h>

namespace loom {
using orbital::allocation::Preparation;
using orbital::allocation::PreparedBytes;
using orbital::allocation::cancel;
using orbital::allocation::retry_cleanup;
using orbital::allocation::take;

/// Prepare on a caller-selected service Lane; the Worker keeps storage ownership.
/// No Task is required. Keep the frame, constructor context and route stable
/// through owner completion, including explicit cleanup of retained failure debt.
[[nodiscard]] inline orbital::Result<void> prepare(Worker& worker,
    Preparation& operation, const orbital::allocation::AllocationRequest& request,
    orbital::worker::Lane& preparer) {
  assert(orbital::worker::on_lane(worker.lane));
  if (!worker.memory) return std::unexpected(orbital::Error::unavailable);
  return orbital::allocation::prepare(*worker.memory, operation, request, preparer);
}
} // namespace loom
