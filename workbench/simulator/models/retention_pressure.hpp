#pragma once

#include <sixdb/sim/runtime.hpp>

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace sixdb::sim::retention_pressure {

enum class Incident { none, checkpoint_reset, replay_cursor_reset };
enum class Negative { none, skip_replay_root, lose_retry };
struct Case {
  std::uint32_t writes{24};
  std::uint64_t memory_bytes{8192}, storage_bytes{16384};
  Time until_ns{20'000'000}, disk_ns{4000}, retry_ns{20'000}, pressure_ns{200'000};
  Incident incident{Incident::none};
  Negative negative{Negative::none};
};
struct Cohort {
  std::uint64_t offered{}, arrived{}, completed{}, failed{}, unfinished{};
};
struct Result {
  std::map<std::string, Cohort> cohorts;
  std::vector<std::string> violations, missing_incidents;
  std::uint64_t reconstruction_refusals{}, independent_during_pressure{}, reclaimed_bytes{},
      source_restarts{}, trace_hash{};
  bool pressure_released{}, reader_root_released{}, replay_root_advanced{}, current_recovered_after_gc{};
  Usage source_usage;
  Run execution;
};

/// One serialized root authority, fixed immutable chunks, and two distinct
/// application obligations. No distributed reclamation or native mapping claim.
Result run_case(const Case& = {}, Options = {}, std::ostream* trace = nullptr,
                std::function<void(const Record&)> inspect = {});
std::vector<std::string> audit(const Case&, const std::vector<Record>&);
void write_json(std::ostream&, const Result&);
std::string_view name(Incident);
std::string_view name(Negative);

} // namespace sixdb::sim::retention_pressure
