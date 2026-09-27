#pragma once

#include "orbital.hpp"

#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace sixdb::sim::regional {
struct Spec {
  Time region_ns{20'000'000};
  Time interval_ns{20'000}, retry_ns{80'000'000}, until_ns{400'000'000};
  std::uint32_t points{48};
  /// Nonzero selects the separate finite alternating-holder progress fixture.
  std::uint32_t progress_rounds{};
  std::uint64_t max_events{1'000'000};
  bool bridge{}, bridge_cut{}, shared{};
  std::string shape{"regional"};
  orbital::Incident incident{orbital::Incident::none};
  orbital::QueuePolicy queue_policy{orbital::QueuePolicy::older_conflicts_drain};
  std::string name{"regional-localisation"};
};
struct Participant {
  std::optional<Time> acquire_input, granted, announced, fixed, resolved;
};
struct Milestones {
  std::optional<Time> input_durable, position_durable, decision_durable;
  std::map<std::uint32_t, Participant> participants;
};
struct Observation {
  Spec spec;
  orbital::Result result;
  std::vector<orbital::Transaction> plans;
  std::map<orbital::Tx, Milestones> milestones;
  std::uint64_t policy_probes{}, primary_probes{}, holder_visits{}, waiter_visits{}, barrier_visits{}, settles{}, max_waiting{}, max_live{}, max_holders{}, operation_refused{}, operation_dropped{};
};
/// The exact same application plans and local costs are used for the LAN and
/// WAN controls. Only directed cross-region propagation and authored sharing vary.
orbital::Case make_case(const Spec&);
Observation run(const Spec&, Options = {}, std::ostream* trace = nullptr);
/// Keeps the standard Orbital result at the top level and adds plans, declared
/// parameters, phase timestamps and matched-completion latency summaries.
void write_json(std::ostream&, const Observation&);
} // namespace sixdb::sim::regional
