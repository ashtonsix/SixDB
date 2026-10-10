#include "read_fusion.h"
#include <object/store_impl.h>

template struct orbital::object_detail::History<compact_history::ShapePhysical>;
template class orbital::ObjectStore<compact_history::ShapePhysical>;

namespace orbital::object_detail {
compact_history::Statistics Access<compact_history::ShapePhysical>::statistics(
    const Catalog& store, const compact_version_probe::Memory& memory) noexcept {
  if (!store.core_) return {};
  const auto s = store.statistics();
  compact_history::Statistics result{memory.live, memory.peak, s.objects, s.versions, s.scopes, s.core_bytes, s.entry_bytes, s.node_bytes, s.alternative_count, memory.calls, s.history_capacity_bytes, s.descriptor_directory_capacity_bytes, s.copied_metadata_bytes, s.captured_descriptor_bytes, s.reclamation_work, s.last_sweep_bytes, s.last_sweep_cuts, s.maintenance_refusals, s.object_directory_capacity_bytes, s.debt_capacity_bytes, s.cut_capacity_bytes, s.debt_objects, s.maintenance_remaining_entries, s.entry_visits, s.version_visits, s.cut_visits, s.debt_visits, s.plan_work, s.maintenance_restarts, s.debt_reservation_work, s.reclaimed_versions, s.completed_sweeps, s.head_refreshes, s.maintenance_pending};
  return result;
}
} // namespace orbital::object_detail
