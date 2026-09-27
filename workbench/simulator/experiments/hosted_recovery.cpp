#include "client.hpp"
#include "orbital.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace sixdb::sim;
using namespace sixdb::sim::orbital;
constexpr auto source = consumer(0, 1);
constexpr auto required_checker = checker(1);
constexpr Time downtime = 5'000'000;

enum class Mode { none, process, host };
std::string_view name(Mode mode) {
  return mode == Mode::none ? "none" : mode == Mode::process ? "process" : "host";
}
Mode mode_from(std::string_view text) {
  if (text == "none") return Mode::none;
  if (text == "process") return Mode::process;
  if (text == "host") return Mode::host;
  throw std::invalid_argument("mode must be none, process, or host");
}
std::uint64_t field(std::string_view json, std::string_view key) {
  auto prefix = '"' + std::string(key) + "\":";
  auto start = json.find(prefix);
  if (start == json.npos) throw std::runtime_error("missing experiment observation field");
  start += prefix.size();
  std::uint64_t value{};
  auto [end, error] = std::from_chars(json.data() + start, json.data() + json.size(), value);
  if (error != std::errc{} || end == json.data() + start)
    throw std::runtime_error("invalid experiment observation field");
  return value;
}

struct Config {
  Mode mode{Mode::host};
  std::uint64_t memory{256 << 10};
  Time until{40'000'000};
  std::uint64_t events{1'000'000};
};

Case scenario(const Config& config) {
  Case c;
  c.name = "hosted-recovery-" + std::string(name(config.mode));
  c.until_ns = config.until;
  c.max_events = config.events;
  c.retry_ns = 1'000'000;
  c.checker_delay_ns = 2'000'000;
  c.placement[required_checker] = source;
  c.transactions = {{1, 100'000, 0, Program::checked_sum,
                     {cell(0, 0), cell(0, 1), cell(0, 2), cell(0, 3)}, {cell(0, 99)}, 0, "checked"}};
  for (unsigned i = 0; i < 4; ++i) {
    c.transactions.push_back({2 + i, 500'000 + i * 40'000, 0, Program::put,
                              {}, {cell(0, i)}, 1000 + i, "source"});
    c.transactions.push_back({6 + i, 500'000 + i * 40'000, 1, Program::put,
                              {}, {cell(1, i)}, 2000 + i, "independent"});
  }
  c.transactions.push_back({10, 750'000, 1, Program::transfer,
                            {cell(0, 99), cell(1, 99)}, {cell(0, 99), cell(1, 99)}, 0, "dependent"});
  for (auto id : roles()) {
    if (id == required_checker) continue;
    Host h; h.id = id; h.disk.latency_ns = c.disk_ns;
    if (id == source) h.memory_bytes = config.memory;
    c.hosts.push_back(h);
  }
  c.links.emplace();
  for (const auto& a : c.hosts) for (const auto& b : c.hosts) if (a.id != b.id) {
    Link link; link.from = a.id; link.to = b.id;
    link.propagation_ns = a.id == source || b.id == source ? 300'000 : 1'000;
    c.links->push_back(link);
  }
  return c;
}

struct Observations {
  std::optional<Record> armed, boundary;
  std::vector<Record> actions;
  std::map<ActorId, std::uint64_t> incarnations, crashes, restarts;
  std::map<Tx, Time> responses;
  std::vector<std::string> errors;
  std::optional<Result> before_return;
  std::optional<Time> first_source_read, first_recovered_source_read, first_checker_report;
  Time action_at{}, return_at{};
  std::uint64_t memory_after_action{}, memory_before_action{};
  bool fired{}, returned{}, selected_callback_delivered{}, host_reset{};

  void observe(Simulation& sim, const Record& r) {
    if (r.actor) incarnations[r.actor] = std::max(incarnations[r.actor], r.incarnation);
    if (!armed && r.kind == "operation.submit" && r.actor == required_checker && r.detail == "compute")
      armed = r;
    if (armed && !boundary && r.kind == "network.arrive" && r.actor == source &&
        (r.peer == leader(0) || r.peer == follower(0, 0) || r.peer == follower(0, 1))) {
      boundary = r;
      sim.pause(); // The runtime finishes this event, but has not delivered its posted callback.
    }
    if (boundary && r.kind == "actor.receive" && r.actor == source && r.operation == boundary->operation)
      selected_callback_delivered = true;
    if (r.kind == "process.crash") { ++crashes[r.actor]; actions.push_back(r); }
    if (r.kind == "process.restart" && r.incarnation > 1) { ++restarts[r.actor]; actions.push_back(r); }
    if (r.kind == "host.reset") { host_reset = true; actions.push_back(r); }
    if (r.kind == "orbital.response") {
      auto tx = field(r.detail, "tx");
      if (!responses.emplace(tx, r.time).second) errors.emplace_back("duplicate terminal client response");
      if ((tx == 1 || tx == 10) && !first_checker_report)
        errors.emplace_back("checked/dependent response preceded required checker durable report");
    }
    if (r.kind == "orbital.checked_report" && r.actor == required_checker && !first_checker_report)
      first_checker_report = r.time;
    if (r.kind == "orbital.private_read" && r.actor == source && r.detail.find("\"ready\":true") != r.detail.npos) {
      if (!first_source_read) first_source_read = r.time;
      if (r.incarnation > 1 && !first_recovered_source_read) first_recovered_source_read = r.time;
    }
  }
};

struct Outcome {
  Result result;
  Observations observations;
  Usage usage;
};

Outcome run(const Config& config, Options options = {}, std::ostream* trace = nullptr) {
  if (!config.until || !config.events || !config.memory)
    throw std::invalid_argument("positive horizon, event budget, and hosted memory required");
  Simulation simulation(options);
  auto c = scenario(config);
  auto experiment = assemble(simulation, c, trace);
  Observations seen;
  simulation.observe([&](const Record& r) { seen.observe(simulation, r); });
  simulation.start();
  Run execution;
  auto advance = [&] {
    if (execution.events >= config.events) {
      execution.budget_exhausted = execution.pending;
      return;
    }
    auto step = simulation.run(config.until, config.events - execution.events);
    execution.events += step.events;
    execution.budget_exhausted = execution.budget_exhausted || step.budget_exhausted;
    execution.pending = step.pending;
    execution.paused = step.paused;
  };
  advance();
  if (seen.boundary) {
    seen.action_at = simulation.now();
    seen.return_at = seen.action_at + downtime;
    seen.memory_before_action = simulation.usage(source).memory;
    if (seen.selected_callback_delivered) seen.errors.emplace_back("selected receive callback escaped boundary pause");
    if (config.mode == Mode::process) { simulation.crash(source); seen.fired = true; }
    if (config.mode == Mode::host) { simulation.power_loss(source); seen.fired = true; }
    seen.memory_after_action = simulation.usage(source).memory;
    simulation.at(seen.return_at - 1, "hosted-outage-observation", [&](Simulation& sim) {
      seen.before_return = experiment.result(sim);
    });
    simulation.at(seen.return_at, "hosted-return", [&](Simulation& sim) {
      if (config.mode != Mode::none) sim.restart(source);
      if (config.mode == Mode::host) sim.restart(required_checker);
      seen.returned = true;
    });
    advance();
  }
  simulation.finish_replay();
  auto result = experiment.result(simulation, execution);
  if (!seen.boundary) result.missing_incidents.push_back("hosted-receive-boundary");
  else result.triggered_incidents.push_back("hosted-receive-boundary");
  if (config.mode != Mode::none) {
    auto incident = "hosted-" + std::string(name(config.mode));
    (seen.fired ? result.triggered_incidents : result.missing_incidents).push_back(incident);
  }
  auto check = [&](bool yes, std::string message) { if (!yes) seen.errors.push_back(std::move(message)); };
  std::uint64_t offered{}, arrived{}, terminal{}, unfinished{};
  for (const auto& [cohort_name, cohort] : result.cohorts) {
    offered += cohort.offered; arrived += cohort.arrived;
    terminal += cohort.completed + cohort.failed; unfinished += cohort.unfinished;
    check(cohort.offered == cohort.completed + cohort.failed + cohort.unfinished, "terminal cohort denominator differs");
    check(cohort.offered == cohort.arrived + cohort.not_yet_offered, "arrival cohort denominator differs");
    check(cohort.completed + cohort.failed <= cohort.arrived, "terminal transaction never arrived");
  }
  check(offered == c.transactions.size() && terminal == seen.responses.size(), "authored/terminal independent denominator differs");
  check(terminal + unfinished == offered && arrived <= offered, "aggregate denominator differs");
  for (const auto& [tx, at] : seen.responses)
    check(result.completed_at.contains(tx) && result.completed_at.at(tx) == at, "terminal time differs from client receipt");
  if (seen.boundary) {
    check(seen.armed && seen.armed->time <= seen.boundary->time, "receive boundary preceded required checker invocation");
    check(seen.boundary->incarnation == 1 && seen.armed->incarnation == 1, "fault selected an unexpected initial incarnation");
    check(seen.crashes[source] == (config.mode == Mode::none ? 0 : 1), "consumer crash count differs from requested mode");
    check(seen.crashes[required_checker] == (config.mode == Mode::host ? 1 : 0), "required checker crash count differs from failure scope");
    check(seen.host_reset == (config.mode == Mode::host), "device reset differs from requested mode");
    for (const auto& [actor, count] : seen.crashes)
      check(!count || actor == source || (config.mode == Mode::host && actor == required_checker), "unrelated process was stopped");
    if (config.mode != Mode::none) check(!seen.selected_callback_delivered, "pre-crash resident packet callback escaped into an incarnation");
    if (seen.returned) {
      check(seen.restarts[source] == (config.mode == Mode::none ? 0 : 1), "consumer restart count differs");
      check(seen.restarts[required_checker] == (config.mode == Mode::host ? 1 : 0), "checker restart count differs");
    }
  }
  if (result.cohorts.at("checked").completed && result.cohorts.at("dependent").completed) {
    check(result.latest_values.at(cell(0, 99)) == 405 && result.latest_values.at(cell(1, 99)) == 1,
          "checked old cut followed by two-shard transfer has incorrect values");
    if (seen.fired) check(seen.first_recovered_source_read.has_value(), "checked completion lacks recovered consumer private read");
  }
  for (unsigned i = 0; i < 4; ++i) {
    if (seen.responses.contains(2 + i)) check(result.latest_values.at(cell(0, i)) == 1000 + i, "source replacement missing after completion");
    if (seen.responses.contains(6 + i)) check(result.latest_values.at(cell(1, i)) == 2000 + i, "independent replacement missing after completion");
  }
  auto usage = simulation.usage(source);
  check(usage.memory_peak <= config.memory, "host exceeded authored resident-memory budget");
  result.violations.insert(result.violations.end(), seen.errors.begin(), seen.errors.end());
  return {std::move(result), std::move(seen), usage};
}

void write(std::ostream& out, const Outcome& outcome, const Config& config) {
  std::ostringstream base; write_json(base, outcome.result);
  auto json = base.str(); json.erase(json.find_last_of('}'));
  out << json << ",\"hosted_recovery\":{\"mode\":\"" << name(config.mode)
      << "\",\"consumer\":" << source << ",\"required_checker\":" << required_checker
      << ",\"host\":" << source << ",\"memory_budget\":" << config.memory;
  const auto& seen = outcome.observations;
  auto record = [&](std::string_view key, const std::optional<Record>& value) {
    out << ",\"" << key << "\":";
    if (value) sixdb::sim::write_json(out, *value); else out << "null";
  };
  record("armed", seen.armed); record("boundary", seen.boundary);
  out << ",\"fired\":" << (seen.fired ? "true" : "false")
      << ",\"action_at_ns\":" << seen.action_at << ",\"return_at_ns\":" << seen.return_at
      << ",\"returned\":" << (seen.returned ? "true" : "false")
      << ",\"selected_callback_delivered\":" << (seen.selected_callback_delivered ? "true" : "false")
      << ",\"memory_before_action\":" << seen.memory_before_action
      << ",\"memory_after_action\":" << seen.memory_after_action
      << ",\"host_memory_peak\":" << outcome.usage.memory_peak
      << ",\"host_memory_final\":" << outcome.usage.memory
      << ",\"host_refused\":" << outcome.usage.refused << ",\"host_dropped\":" << outcome.usage.dropped;
  out << ",\"incarnations\":{";
  bool comma{};
  for (auto actor : {source, required_checker}) { if (comma) out << ','; comma = true;
    auto found = seen.incarnations.find(actor);
    out << '"' << actor << "\":" << (found == seen.incarnations.end() ? 0 : found->second);
  }
  out << "},\"actions\":["; comma = false;
  for (const auto& action : seen.actions) { if (comma) out << ','; comma = true; sixdb::sim::write_json(out, action); }
  out << "],\"first_recovered_source_read_ns\":";
  if (seen.first_recovered_source_read) out << *seen.first_recovered_source_read; else out << "null";
  out << ",\"first_source_read_ns\":";
  if (seen.first_source_read) out << *seen.first_source_read; else out << "null";
  out << ",\"first_required_checker_report_ns\":";
  if (seen.first_checker_report) out << *seen.first_checker_report; else out << "null";
  out << ",\"before_return\":";
  if (seen.before_return) {
    out << "{\"observed_until_ns\":" << seen.before_return->observed_until_ns << ",\"cohorts\":{";
    comma = false;
    for (const auto& [cohort_name, cohort] : seen.before_return->cohorts) {
      if (comma) out << ','; comma = true;
      out << '"' << cohort_name << "\":{\"offered\":" << cohort.offered << ",\"arrived\":" << cohort.arrived
          << ",\"completed\":" << cohort.completed << ",\"failed\":" << cohort.failed << ",\"unfinished\":" << cohort.unfinished << '}';
    }
    out << "}}";
  } else out << "null";
  out << "}}\n";
}

void require(bool yes, std::string_view message) { if (!yes) throw std::runtime_error(std::string(message)); }
void healthy(const Outcome& o) {
  if (!o.result.violations.empty()) throw std::runtime_error(o.result.violations.front());
  require(o.result.missing_incidents.empty(), "required boundary/incident missing");
  require(!o.result.execution.budget_exhausted, "unexpected event budget exhaustion");
  for (const auto& [name, cohort] : o.result.cohorts) require(!cohort.failed && !cohort.unfinished, "authored work failed to finish");
}
void self_test() {
  for (auto mode : {Mode::none, Mode::process, Mode::host}) {
    Config config; config.mode = mode;
    auto outcome = run(config); healthy(outcome);
    require(outcome.observations.before_return.has_value(), "outage observation missing");
    const auto& before = *outcome.observations.before_return;
    require(before.cohorts.at("source").completed == 4 && before.cohorts.at("independent").completed == 4,
            "healthy writers failed to progress across hosted outage");
    if (mode != Mode::none)
      require(before.cohorts.at("checked").unfinished == 1 && before.cohorts.at("dependent").unfinished == 1,
              "required checker outage did not hold dependent result");
    require(outcome.observations.first_source_read.has_value(), "required checker never queried its source");
    for (Tx tx = 2; tx <= 5; ++tx)
      require(*outcome.observations.first_source_read > outcome.result.completed_at.at(tx),
              "delayed checker first read did not follow continuing source writes");
  }
  Config config;
  std::ostringstream trace, choices; Options capture; capture.seed = 7; capture.decisions_out = &choices;
  auto captured = run(config, capture, &trace); healthy(captured);
  std::istringstream input(choices.str()); Options replay; replay.decisions_in = &input;
  auto replayed = run(config, replay);
  require(captured.result.trace_hash == replayed.result.trace_hash && captured.result.completed_at == replayed.result.completed_at,
          "hosted fault replay differs");
  Options silent_options; silent_options.seed = 7;
  auto silent = run(config, silent_options);
  require(silent.result.trace_hash == captured.result.trace_hash && silent.result.records == captured.result.records,
          "trace observer changed hosted run");
  config.events = 1; auto truncated = run(config);
  require(truncated.result.execution.events == 1 && truncated.result.execution.budget_exhausted &&
          !truncated.result.missing_incidents.empty() && truncated.result.completed_at.empty(),
          "budget-censored case disguised as complete experiment");
  config.events = 1'000'000; config.until = 100;
  auto early = run(config);
  require(!early.result.execution.budget_exhausted && early.result.cohorts.at("source").not_yet_offered == 4 &&
          early.result.completed_at.empty() && !early.result.missing_incidents.empty(), "early deadline lost authored denominator");
  config.until = 40'000'000; config.memory = 16 << 10;
  auto pressure = run(config);
  require(pressure.result.violations.empty() && pressure.result.missing_incidents.empty() &&
          pressure.result.cohorts.at("source").completed == 4 && pressure.result.cohorts.at("independent").completed == 4 &&
          pressure.result.cohorts.at("checked").unfinished == 1 && pressure.result.cohorts.at("dependent").unfinished == 1 &&
          pressure.usage.refused > 0, "finite recovery pressure did not retain survivors and censored work");
  std::cout << "hosted lifecycle, isolation, replay, trace, and censoring checks passed\n";
}
} // namespace

int main(int argc, char** argv) {
  try {
    Config config; Options options; sixdb::sim::client::Evidence evidence;
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") { self_test(); return EXIT_SUCCESS; }
    for (int i = 1; i < argc; ++i) {
      std::string_view key = argv[i];
      if (i + 1 == argc) throw std::invalid_argument("option requires a value");
      std::string_view value = argv[++i];
      if (evidence.option(key, value)) continue;
      if (key == "--seed") options.seed = sixdb::sim::client::number(value);
      else if (key == "--mode") config.mode = mode_from(value);
      else if (key == "--memory") config.memory = sixdb::sim::client::number(value);
      else if (key == "--until") config.until = sixdb::sim::client::number(value);
      else if (key == "--events") config.events = sixdb::sim::client::number(value);
      else throw std::invalid_argument("unknown option: " + std::string(key));
    }
    evidence.open(options);
    auto outcome = run(config, options, evidence.trace());
    evidence.finish();
    write(std::cout, outcome, config);
    evidence.finish();
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "hosted recovery experiment: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
