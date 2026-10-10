#pragma once
#include <cstddef>

// Diagnostic packet shared by the selected catalog and experimental controls.
// This is driver observation, not an Orbital runtime interface.
namespace compact_history {
struct Statistics {
  std::size_t requested_bytes{}, peak_bytes{}, objects{}, versions{}, scopes{};
  std::size_t core_bytes{}, entry_bytes{}, node_bytes{}, alternative_count{};
  std::size_t allocation_calls{};
  std::size_t history_capacity_bytes{}, descriptor_directory_capacity_bytes{};
  std::size_t copied_metadata_bytes{}, captured_descriptor_bytes{}, reclamation_work{};
  std::size_t last_sweep_bytes{}, last_sweep_cuts{}, maintenance_refusals{};
  std::size_t object_directory_capacity_bytes{}, debt_capacity_bytes{};
  std::size_t cut_capacity_bytes{}, debt_objects{}, maintenance_remaining_entries{};
  std::size_t entry_visits{}, version_visits{}, cut_visits{}, debt_visits{}, plan_work{}, maintenance_restarts{};
  std::size_t debt_reservation_work{}, reclaimed_versions{}, completed_sweeps{}, head_refreshes{};
  bool maintenance_pending{};
};
} // namespace compact_history
