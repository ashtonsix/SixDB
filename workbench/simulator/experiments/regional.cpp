#include "regional.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace sixdb::sim::regional {
namespace {
using namespace orbital;
constexpr Time local_link = 1'000;
constexpr Tx remote_tx = 1, bridge_tx = 2, first_local_tx = 100;
void require(bool condition, const char* message) { if (!condition) throw std::invalid_argument(message); }
Time add(Time a, Time b) { require(a <= std::numeric_limits<Time>::max() - b, "arrival timestamp overflow"); return a + b; }
Time multiply(Time a, Time b) { require(!b || a <= std::numeric_limits<Time>::max() / b, "arrival interval overflow"); return a * b; }
bool america(HostId host) {
  if (host == coordinator(1) || host == leader(1)) return true;
  for (std::uint32_t c = 0; c < 2; ++c) if (host == follower(1, c)) return true;
  for (std::uint32_t c = 0; c < 3; ++c) if (host == consumer(1, c)) return true;
  return false;
}
std::uint64_t number(std::string_view json, std::string_view field) {
  auto begin = json.find('"' + std::string(field) + "\":");
  require(begin != std::string_view::npos, "missing numeric semantic field"); begin += field.size() + 3;
  std::uint64_t out{}; auto [end, error] = std::from_chars(json.data() + begin, json.data() + json.size(), out);
  require(error == std::errc{}, "invalid numeric semantic field"); return out;
}
std::string_view text(std::string_view json, std::string_view field) {
  auto begin = json.find('"' + std::string(field) + "\":\"");
  require(begin != std::string_view::npos, "missing string semantic field"); begin += field.size() + 4;
  auto end = json.find('"', begin); require(end != std::string_view::npos, "unterminated string semantic field"); return json.substr(begin, end - begin);
}
Tx suffix(std::string_view key) {
  auto begin = key.find('/'); require(begin != std::string_view::npos, "durable milestone key lacks id"); ++begin;
  Tx out{}; auto [end, error] = std::from_chars(key.data() + begin, key.data() + key.size(), out);
  require(error == std::errc{} && end == key.data() + key.size(), "invalid durable milestone id"); return out;
}
void first(std::optional<Time>& slot, Time time) { if (!slot) slot = time; }
std::string quote(std::string_view text) {
  std::string out = "\"";
  for (auto c : text) { require(c >= 32 && c <= 126, "non-ASCII experiment label"); if (c == '\\' || c == '"') out += '\\'; out += c; }
  return out + '"';
}
void time_json(std::ostream& out, const std::optional<Time>& time) { if (time) out << *time; else out << "null"; }
void collect(std::map<Tx, Milestones>& milestones, const Record& record) {
  if (record.kind == "storage.write" && record.detail.starts_with("input/")) first(milestones[suffix(record.detail)].input_durable, record.time);
  if (record.kind == "storage.write" && record.detail.starts_with("position/")) first(milestones[suffix(record.detail)].position_durable, record.time);
  if (record.kind == "storage.write" && record.detail.starts_with("decision/")) first(milestones[suffix(record.detail)].decision_durable, record.time);
  if (record.kind != "orbital.applied" && record.kind != "orbital.output") return;
  if (number(record.detail, "copy") != 0) return;
  auto tx = number(record.detail, "tx"); auto shard = static_cast<std::uint32_t>(number(record.detail, "shard"));
  auto& participant = milestones[tx].participants[shard]; auto command = text(record.detail, "command");
  if (record.kind == "orbital.applied") {
    if (command == "acquire") first(participant.acquire_input, record.time);
    if (command == "fix") first(participant.fixed, record.time);
    if (command == "resolve") first(participant.resolved, record.time);
  } else {
    if (command == "acquire") first(participant.granted, record.time);
    if (command == "announce") first(participant.announced, record.time);
  }
}
void validate(Observation& observation) {
  const auto& result = observation.result;
  auto error = [&](bool valid, const char* message) { if (!valid) observation.result.violations.emplace_back(message); };
  std::map<std::string, std::uint64_t> expected;
  for (const auto& plan : observation.plans) ++expected[plan.cohort];
  error(result.cohorts.size() == expected.size(), "regional cohort inventory differs from authored plans");
  for (const auto& [name, count] : expected) {
    if (!result.cohorts.contains(name)) { error(false, "regional authored cohort missing from result"); continue; }
    const auto& c = result.cohorts.at(name);
    error(c.offered == count && c.completed + c.failed + c.unfinished == count, "regional offer denominator mismatch");
    error(c.arrived + c.not_yet_offered == count, "regional arrival denominator mismatch");
  }
  for (const auto& plan : observation.plans) {
    if (!result.completed_at.contains(plan.id)) continue;
    error(observation.milestones.contains(plan.id), "completed regional plan lacks milestones");
    if (!observation.milestones.contains(plan.id)) continue;
    const auto& m = observation.milestones.at(plan.id);
    error(m.input_durable.has_value() && m.position_durable.has_value() && m.decision_durable.has_value(), "completed regional plan lacks durable position/outcome");
    Time last_grant{}, first_announce = std::numeric_limits<Time>::max(), last_fix{};
    std::set<std::uint32_t> participants; for (auto scope : plan.writes) participants.insert(scope / 1000);
    for (auto shard : participants) {
      error(m.participants.contains(shard), "completed regional plan lacks participant evidence"); if (!m.participants.contains(shard)) continue;
      const auto& p = m.participants.at(shard);
      error(p.acquire_input && p.granted && p.announced && p.fixed && p.resolved, "completed regional plan lacks phase evidence");
      if (!(p.acquire_input && p.granted && p.announced && p.fixed && p.resolved)) continue;
      if (m.input_durable) error(plan.at <= *m.input_durable && *m.input_durable <= *p.acquire_input, "regional input admission outside offered/acquire interval");
      error(*p.acquire_input <= *p.granted && *p.granted <= *p.announced && *p.announced <= *p.fixed && *p.fixed <= *p.resolved && *p.resolved <= result.completed_at.at(plan.id), "regional participant phase order invalid");
      if (m.position_durable) error(*p.announced <= *m.position_durable && *m.position_durable <= *p.fixed, "regional durable position outside announcement/fix interval");
      last_grant = std::max(last_grant, *p.granted); first_announce = std::min(first_announce, *p.announced); last_fix = std::max(last_fix, *p.fixed);
    }
    error(last_grant <= first_announce, "regional announcement before all output reservations");
    if (m.decision_durable) error(last_fix <= *m.decision_durable && *m.decision_durable <= result.completed_at.at(plan.id), "regional outcome before all fixed participants");
  }
}
} // namespace

orbital::Case make_case(const Spec& spec) {
  require(spec.progress_rounds <= 64, "progress rounds exceed finite fixture bound");
  if (!spec.progress_rounds) require(spec.points > 0 && spec.points <= 192, "regional points must be in [1,192] for this finite experiment");
  else require(!spec.bridge && !spec.bridge_cut, "bridge flags do not apply to alternating-holder progress fixture");
  require(spec.interval_ns > 0 && spec.retry_ns > 0 && spec.until_ns > 0, "regional durations must be positive");
  Case c; c.name = spec.name; c.queue_policy = spec.queue_policy; c.until_ns = spec.until_ns; c.retry_ns = spec.retry_ns; c.max_events = spec.max_events;
  if (spec.progress_rounds) {
    // B is queued after x/y have live holders. Each younger narrow grant can
    // keep the other half of B's group occupied when a holder releases. All
    // arrivals are finite and the caller chooses the observation/drain window.
    c.transactions = {
      {1, 100'000, 0, Program::put, {}, {cell(0, 0)}, 10, "initial"},
      {2, 110'000, 0, Program::put, {}, {cell(0, 2)}, 10, "initial"},
      {3, 140'000, 0, Program::put, {}, {cell(0, 0), cell(0, 2)}, 20, "broad"}};
    for (std::uint32_t i = 0; i < spec.progress_rounds; ++i) for (std::uint32_t k = 0; k < 2; ++k)
      c.transactions.push_back({10 + 2 * i + k, 145'000 + i * 16'000 + k * 4'000, 0, Program::put, {}, {cell(0, 2 * k)}, 30 + i, "stream"});
  } else {
  c.transactions.push_back(Transaction{remote_tx, 100'000, 0, Program::transfer, {cell(0, 0), cell(1, 0)}, {cell(0, 0), cell(1, 0)}, 0, "regional"});
  if (spec.bridge || spec.bridge_cut) {
    const std::vector<Scope> coverage = spec.bridge_cut ? std::vector<Scope>{cell(0, 1), cell(0, 2), cell(0, 99)} : std::vector<Scope>{cell(0, 0), cell(0, 1), cell(0, 2)};
    c.transactions.push_back(Transaction{bridge_tx, 45'000'000, 0, Program::put, {}, coverage, 700, "bridge"});
  }
  const auto wave_size = (spec.points + 2) / 3;
  for (std::uint32_t i = 0; i < spec.points; ++i) {
    Transaction p; p.id = first_local_tx + i; p.origin = 0;
    p.at = add(add(50'000'000, multiply(i / wave_size, 80'000'000)), multiply(i % wave_size, spec.interval_ns));
    p.value = 1000 + i;
    switch (i % 4) {
      case 0: p.cohort = "disjoint"; p.writes = {cell(0, 3)}; break;
      case 1: p.cohort = "bridge-scope"; p.writes = {cell(0, 2)}; break;
      case 2: p.cohort = "direct-overlap"; p.writes = {cell(0, 0)}; break;
      case 3: p.cohort = "dependent"; p.program = Program::transfer; p.reads = p.writes = {cell(0, 0), cell(0, 1)}; break;
    }
    c.transactions.push_back(std::move(p));
  }
  }
  // Every shard retains a local prepared quorum and local consumers. The
  // coordinator sends only the two-shard participant traffic across the ocean.
  for (auto role : roles()) { Host host; host.id = role; host.disk.latency_ns = c.disk_ns; c.hosts.push_back(host); }
  if (spec.shared) {
    c.placement[coordinator(0)] = leader(0);
    c.placement[consumer(0, 0)] = leader(0);
  }
  c.links = std::vector<Link>{};
  for (const auto& source : c.hosts) for (const auto& target : c.hosts) if (source.id != target.id) {
    Link link; link.from = source.id; link.to = target.id; link.propagation_ns = america(source.id) == america(target.id) ? local_link : spec.region_ns;
    c.links->push_back(link);
  }
  return c;
}
Observation run(const Spec& spec, Options options, std::ostream* trace) {
  auto c = make_case(spec); Observation out{spec, {}, c.transactions, {}};
  Simulation sim(options); auto experiment = orbital::assemble(sim, c, trace);
  sim.observe([&](const Record& record) { collect(out.milestones, record); });
  sim.start(); auto execution = sim.run(spec.until_ns, spec.max_events); sim.finish_replay();
  out.result = experiment.result(sim, execution); validate(out); return out;
}
void write_json(std::ostream& out, const Observation& observation) {
  std::ostringstream core; orbital::write_json(core, observation.result); auto body = core.str();
  while (!body.empty() && (body.back() == '\n' || body.back() == '\r' || body.back() == ' ')) body.pop_back();
  require(!body.empty() && body.back() == '}', "invalid core result JSON"); body.pop_back(); out << body;
  const auto& p = observation.spec;
  out << ",\"experiment\":" << quote(p.progress_rounds ? "alternating-holder-progress-v1" : "regional-localisation-v1") << ",\"parameters\":{\"region_ns\":" << p.region_ns << ",\"interval_ns\":" << p.interval_ns << ",\"retry_ns\":" << p.retry_ns << ",\"until_ns\":" << p.until_ns << ",\"points\":" << p.points << ",\"progress_rounds\":" << p.progress_rounds << ",\"max_events\":" << p.max_events << ",\"bridge\":" << (p.bridge ? "true" : "false") << ",\"bridge_cut\":" << (p.bridge_cut ? "true" : "false") << ",\"shared\":" << (p.shared ? "true" : "false") << ",\"queue_policy\":" << quote(name(p.queue_policy)) << '}';
  out << ",\"plans\":["; bool comma{};
  for (const auto& plan : observation.plans) {
    if (comma) out << ','; comma = true;
    out << "{\"tx\":" << plan.id << ",\"at\":" << plan.at << ",\"origin\":" << plan.origin << ",\"program\":" << quote(plan.program == Program::put ? "put" : "transfer") << ",\"cohort\":" << quote(plan.cohort) << ",\"value\":" << plan.value;
    for (const auto& [name, scopes] : {std::pair{"reads", &plan.reads}, std::pair{"writes", &plan.writes}}) { out << ',' << quote(name) << ":["; bool separated{}; for (auto scope : *scopes) { if (separated) out << ','; separated = true; out << scope; } out << ']'; }
    out << '}';
  }
  out << "],\"milestones\":["; comma = false;
  for (const auto& [tx, m] : observation.milestones) {
    if (comma) out << ','; comma = true;
    out << "{\"tx\":" << tx << ",\"input_durable_ns\":"; time_json(out, m.input_durable); out << ",\"position_durable_ns\":"; time_json(out, m.position_durable); out << ",\"decision_durable_ns\":"; time_json(out, m.decision_durable);
    out << ",\"participants\":["; bool separated{};
    for (const auto& [shard, s] : m.participants) {
      if (separated) out << ','; separated = true; out << "{\"shard\":" << shard;
      for (const auto& [name, value] : {std::pair{"acquire_input_ns", s.acquire_input}, {"granted_ns", s.granted}, {"announced_ns", s.announced}, {"fixed_ns", s.fixed}, {"resolved_ns", s.resolved}}) { out << ',' << quote(name) << ':'; time_json(out, value); } out << '}';
    }
    out << "]}";
  }
  out << "],\"completion_latency_ns\":{"; comma = false;
  for (const auto& [cohort, counts] : observation.result.cohorts) {
    std::vector<Time> latencies;
    for (const auto& plan : observation.plans) if (plan.cohort == cohort && observation.result.completed_at.contains(plan.id)) latencies.push_back(observation.result.completed_at.at(plan.id) - plan.at);
    std::ranges::sort(latencies); if (comma) out << ','; comma = true; out << quote(cohort) << ":{\"samples\":" << latencies.size();
    if (!latencies.empty()) out << ",\"p50\":" << latencies[(latencies.size() - 1) / 2] << ",\"p95\":" << latencies[(95 * latencies.size() + 99) / 100 - 1] << ",\"max\":" << latencies.back();
    out << '}';
  }
  out << '}';
  if (p.progress_rounds) {
    const auto broad = observation.milestones.find(3);
    std::optional<Time> queued, granted;
    if (broad != observation.milestones.end() && broad->second.participants.contains(0)) {
      queued = broad->second.participants.at(0).acquire_input; granted = broad->second.participants.at(0).granted;
    }
    std::uint64_t younger{};
    if (queued) for (const auto& plan : observation.plans) {
      if (plan.cohort != "stream" || !observation.milestones.contains(plan.id)) continue;
      const auto& m = observation.milestones.at(plan.id); if (!m.participants.contains(0)) continue;
      const auto& phase = m.participants.at(0);
      if (phase.acquire_input && phase.granted && *queued < *phase.acquire_input && (!granted || *phase.granted < *granted)) ++younger;
    }
    out << ",\"admission_probe\":{\"broad_tx\":3,\"queue_input_ns\":"; time_json(out, queued);
    out << ",\"granted_ns\":"; time_json(out, granted);
    out << ",\"younger_grants_before_broad\":" << younger << ",\"queue_wait_censored\":" << (!granted ? "true" : "false");
    if (queued) out << ",\"observed_queue_wait_ns\":" << (granted.value_or(observation.result.observed_until_ns) - *queued);
    out << '}';
  }
  out << "}\n";
}
} // namespace sixdb::sim::regional
