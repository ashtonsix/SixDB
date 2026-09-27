#pragma once

#include <sixdb/sim/runtime.hpp>

#include <cstdint>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sixdb::sim::orbital {

/// Integer-cell application fixture. The physical runtime never interprets
/// scopes, positions, programs, values or checked-extension transcripts.
using Scope = std::uint32_t;
using Tx = std::uint64_t;
enum class Program { put, checked_sum, transfer };
struct Transaction {
  Tx id{};
  Time at{};
  std::uint32_t origin{};
  Program program{Program::put};
  std::vector<Scope> reads, writes;
  std::int64_t value{};
  std::string cohort{"point"};
};

/// Stable role identities in this reference composition, not a production ABI.
inline constexpr ActorId client = 1;
inline constexpr ActorId coordinator(std::uint32_t shard) { return 10 + shard; }
inline constexpr ActorId leader(std::uint32_t shard) { return 20 + shard; }
inline constexpr ActorId follower(std::uint32_t shard, std::uint32_t copy) { return 30 + shard * 2 + copy; }
inline constexpr ActorId consumer(std::uint32_t shard, std::uint32_t copy) { return 40 + shard * 3 + copy; }
inline constexpr ActorId checker(std::uint32_t copy) { return 60 + copy; }
inline constexpr Scope cell(std::uint32_t shard, std::uint32_t key) { return shard * 1000 + key; }

enum class Incident { none, one_follower, quorum_pause, consumer_reset, coordinator_reset, checker_reset };
/// Provisional default: a waiting request protects its scopes once no older
/// live request conflicts. The other policies are explicit experimental controls.
/// Policy and per-record closure stay fixed through replay of the whole lineage.
enum class QueuePolicy { no_overtaking, eligible_first, older_conflicts_drain, oldest_live };
enum class Negative { none, skip_pending, skip_verification, corrupt_checker };
struct Case {
  std::string name{"checked-old-cut"};
  std::uint32_t point_writes{12};
  Time point_start_ns{500'000}, interval_ns{40'000};
  Time checker_delay_ns{800'000}, until_ns{5'000'000};
  Time link_ns{1'000}, disk_ns{4'000}, retry_ns{100'000};
  std::uint64_t memory_bytes{16 << 20}, storage_bytes{64 << 20};
  std::uint64_t max_events{1'000'000};
  Incident incident{Incident::none};
  Negative negative{Negative::none};
  QueuePolicy queue_policy{QueuePolicy::older_conflicts_drain};
  /// Empty selects the common checked-transform plus continuing-writes fixture.
  std::vector<Transaction> transactions;
  /// Omitted roles each get their own host/process. Placement changes real
  /// sharing and failure scope; process identities remain equal to role IDs.
  std::map<ActorId, HostId> placement;
  /// Nonempty is the complete authored host set. Empty supplies default hosts.
  std::vector<Host> hosts;
  /// Nullopt supplies the default directed mesh; an explicit empty set supplies
  /// no inter-host connectivity. Same-host traffic still uses host resources.
  std::optional<std::vector<Link>> links;
};
struct Cohort {
  /// offered is the complete authored arrival schedule, including censored arrivals.
  std::uint64_t offered{}, arrived{}, not_yet_offered{}, completed{}, failed{}, unfinished{};
  Time max_latency_ns{};
};
struct Result {
  std::string name;
  QueuePolicy queue_policy{QueuePolicy::older_conflicts_drain};
  std::map<std::string, Cohort> cohorts;
  std::vector<std::string> violations;
  std::vector<std::string> triggered_incidents, missing_incidents;
  std::map<Tx, Time> completed_at;
  std::map<Scope, std::int64_t> latest_values;
  std::uint64_t wire_bytes{}, durable_bytes{}, memory_peak{}, records{}, trace_hash{};
  std::uint64_t accepted{}, admitted_unfinished{};
  Time observed_until_ns{};
  Run execution;
};

/// Authored inputs remain available to independent campaign/trace checkers.
std::vector<Transaction> transactions(const Case&);
std::vector<ActorId> roles();
std::string_view name(Incident);
std::string_view name(Negative);
std::string_view name(QueuePolicy);

/// Installs the actors, physical composition, authored arrivals, observers and
/// selected incident. The caller owns start/run/staged faults and replay.
class Experiment {
 public:
  ~Experiment();
  Experiment(Experiment&&) noexcept;
  Experiment& operator=(Experiment&&) noexcept;
  Experiment(const Experiment&) = delete;
  Experiment& operator=(const Experiment&) = delete;
  [[nodiscard]] Result result(const Simulation&, Run execution = {}) const;
 private:
  struct State;
  explicit Experiment(std::shared_ptr<State>);
  std::shared_ptr<State> state_;
  friend Experiment assemble(Simulation&, const Case&, std::ostream*);
};
Experiment assemble(Simulation&, const Case&, std::ostream* trace = nullptr);
Result run_case(const Case&, Options = {}, std::ostream* trace = nullptr);
void write_json(std::ostream&, const Result&);

} // namespace sixdb::sim::orbital
