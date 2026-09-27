#include "regional.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace sixdb::sim;
namespace {
using namespace orbital;
void check(bool valid, const char* error) { if (!valid) throw std::runtime_error(error); }
void complete(const regional::Observation& o) {
  if (!o.result.violations.empty()) for (const auto& error : o.result.violations) std::cerr << error << '\n';
  check(o.result.violations.empty(), "regional safety/phase violation");
  check(!o.result.execution.budget_exhausted, "regional case censored by dispatch budget");
  std::uint64_t offers{};
  for (const auto& [name, c] : o.result.cohorts) { check(c.offered == c.arrived && c.offered == c.completed && !c.failed && !c.unfinished, "regional offers not fully accounted and completed"); offers += c.offered; }
  check(offers == (o.spec.progress_rounds ? 3 + 2 * o.spec.progress_rounds : o.spec.points + 1 + (o.spec.bridge || o.spec.bridge_cut)), "regional denominator differs from complete plan population");
}
Time delay(const Case& c, HostId from, HostId to) {
  auto link = std::ranges::find_if(*c.links, [&](const Link& l) { return l.from == from && l.to == to; });
  check(link != c.links->end(), "authored regional link absent"); return link->propagation_ns;
}
}
int main() {
  try {
    regional::Spec spec; spec.points = 12;
    const auto topology = regional::make_case(spec);
    for (std::uint32_t shard = 0; shard < 2; ++shard) {
      check(delay(topology, leader(shard), follower(shard, 0)) == 1'000, "regional witness quorum accidentally crosses WAN");
      check(delay(topology, follower(shard, 1), consumer(shard, 0)) == 1'000, "regional consumer path accidentally crosses WAN");
    }
    check(delay(topology, coordinator(0), leader(1)) == spec.region_ns && delay(topology, consumer(1, 0), coordinator(0)) == spec.region_ns, "cross-region protocol bypasses WAN");
    auto no_bridge = regional::run(spec); complete(no_bridge);
    const auto remote = no_bridge.milestones.at(1).participants.at(0);
    check(no_bridge.result.completed_at.at(100) < *remote.fixed, "disjoint writer waited for WAN local fix");
    check(no_bridge.result.completed_at.at(101) < *remote.fixed, "nonoverlapping k2 writer waited without bridge");
    check(no_bridge.milestones.at(102).participants.at(0).granted >= remote.fixed, "overlapping writer bypassed held reservation");
    check(no_bridge.result.completed_at.at(103) >= *remote.resolved, "dependent read skipped unresolved remote output");
    std::cout << "regional topology + phase localisation passed\n";
    spec.bridge = true; auto bridge = regional::run(spec); complete(bridge);
    const auto bridge_slot = bridge.milestones.at(2).participants.at(0);
    const auto held_remote = bridge.milestones.at(1).participants.at(0);
    const auto bridged = bridge.milestones.at(101).participants.at(0);
    check(bridge_slot.acquire_input < held_remote.fixed && bridged.acquire_input < held_remote.fixed, "bridge scenario missed the held reservation phase");
    check(bridged.granted >= bridge_slot.fixed, "overlapping admission queue did not expose bridge convoy");
    check(bridge.result.completed_at.at(100) < *held_remote.fixed, "disjoint k3 writer joined logical bridge convoy");
    std::cout << "bridge scope convoy with disjoint control passed\n";
    spec.bridge_cut = true; auto cut = regional::run(spec); complete(cut);
    check(cut.plans.size() == bridge.plans.size() && cut.plans.at(1).at == bridge.plans.at(1).at && cut.plans.at(1).writes.size() == bridge.plans.at(1).writes.size(), "bridge-cut changed operation count/arrival/declaration width");
    check(cut.result.completed_at.at(101) < *cut.milestones.at(1).participants.at(0).fixed, "cut W-to-B edge still delayed Y");
    check(cut.milestones.at(101).input_durable < cut.milestones.at(1).participants.at(0).fixed && bridge.milestones.at(101).input_durable < held_remote.fixed, "frontend admission explains claimed bridge convoy");
    spec.bridge_cut = false;
    std::cout << "equal-operation bridge-cut control passed\n";
    spec.queue_policy = QueuePolicy::eligible_first; auto eligible = regional::run(spec); complete(eligible);
    check(eligible.result.completed_at.at(101) < *eligible.milestones.at(1).participants.at(0).fixed, "eligible-first retained waiter-only bridge exclusion");
    check(eligible.milestones.at(102).participants.at(0).granted >= eligible.milestones.at(1).participants.at(0).fixed, "eligible-first bypassed a live conflicting holder");
    check(eligible.result.completed_at.at(103) >= *eligible.milestones.at(1).participants.at(0).resolved, "eligible-first bypassed unresolved read dependency");
    spec.queue_policy = QueuePolicy::no_overtaking;
    std::cout << "eligible-first removes only waiter exclusion passed\n";
    spec.shared = true; auto shared = regional::run(spec); complete(shared);
    auto shared_case = regional::make_case(spec);
    check(shared_case.placement.at(coordinator(0)) == leader(0) && shared_case.placement.at(consumer(0, 0)) == leader(0), "shared case only changed labels");
    check(shared.result.trace_hash != bridge.result.trace_hash, "physical sharing had no modeled execution effect");
    check(shared.result.completed_at.at(100) < *shared.milestones.at(1).participants.at(0).fixed, "shared disjoint writer waited for global WAN barrier");
    std::cout << "shared physical resources preserve disjoint progress passed\n";
    spec.shared = false; spec.region_ns = 1'000; auto lan = regional::run(spec); complete(lan);
    check(lan.result.completed_at.at(101) - lan.plans.at(3).at < 10'000'000, "LAN control retained WAN convoy duration");
    check(lan.plans.size() == bridge.plans.size(), "topology control changed offered work");
    for (std::size_t i = 0; i < lan.plans.size(); ++i) check(lan.plans[i].id == bridge.plans[i].id && lan.plans[i].at == bridge.plans[i].at && lan.plans[i].reads == bridge.plans[i].reads && lan.plans[i].writes == bridge.plans[i].writes, "topology control changed plan identity or coverage");
    std::cout << "matched LAN control passed\n";
    spec.until_ns = 60'000'000; auto censored = regional::run(spec);
    check(censored.result.violations.empty(), "finite horizon confused pending work with safety failure");
    std::uint64_t future{}; for (const auto& [name, c] : censored.result.cohorts) future += c.not_yet_offered;
    check(future == 8, "censored case lost future authored waves");
    std::cout << "regional finite-horizon accounting passed\n";
    spec.until_ns = 400'000'000; spec.region_ns = 20'000'000;
    std::ostringstream choices, trace; Options capture; capture.seed = 7; capture.decisions_out = &choices;
    auto recorded = regional::run(spec, capture, &trace); complete(recorded);
    std::istringstream log(choices.str()); Options replay; replay.seed = 7; replay.decisions_in = &log; auto repeated = regional::run(spec, replay);
    std::ostringstream left, right; regional::write_json(left, recorded); regional::write_json(right, repeated);
    check(left.str() == right.str(), "regional replay changed result or attribution");
    std::cout << "regional capture + replay attribution passed\n";
    regional::Spec progress; progress.progress_rounds = 32; progress.until_ns = 10'000'000;
    Options progress_options; progress_options.seed = 7;
    auto fair = regional::run(progress, progress_options);
    check(fair.result.violations.empty() && fair.result.cohorts.at("broad").completed == 1, "non-overtaking broad waiter failed to advance");
    const auto fair_queue = *fair.milestones.at(3).participants.at(0).acquire_input;
    const auto fair_grant = *fair.milestones.at(3).participants.at(0).granted;
    for (const auto& plan : fair.plans) if (plan.cohort == "stream") {
      const auto& phase = fair.milestones.at(plan.id).participants.at(0);
      if (phase.acquire_input && phase.granted && *phase.acquire_input > fair_queue) check(*phase.granted >= fair_grant, "default policy let a younger overlapping waiter overtake");
    }
    progress.queue_policy = QueuePolicy::eligible_first; auto prefix = regional::run(progress, progress_options);
    check(prefix.result.violations.empty() && !prefix.result.execution.budget_exhausted && prefix.result.cohorts.at("broad").unfinished == 1 && prefix.result.cohorts.at("stream").completed > 0, "alternating holders did not expose broad progress loss");
    check(!prefix.milestones.at(3).participants.at(0).granted, "broad waiter was granted inside the claimed blocked prefix");
    progress.until_ns = 40'000'000; auto drained = regional::run(progress, progress_options); complete(drained);
    const auto broad_queued = *drained.milestones.at(3).participants.at(0).acquire_input;
    const auto broad_granted = *drained.milestones.at(3).participants.at(0).granted;
    for (Tx holder : {1, 2}) {
      const auto& phase = drained.milestones.at(holder).participants.at(0);
      check(*phase.granted < broad_queued && broad_queued < *phase.fixed, "progress falsifier lacks two live initial holders");
    }
    std::size_t younger{};
    for (const auto& plan : drained.plans) if (plan.cohort == "stream") {
      const auto& phase = drained.milestones.at(plan.id).participants.at(0);
      if (*phase.acquire_input > broad_queued && *phase.granted < broad_granted) ++younger;
    }
    check(younger == 64, "extended stream did not all overtake broad waiter");
    for (const auto& [tx, at] : prefix.result.completed_at) check(drained.result.completed_at.at(tx) == at, "extending observation horizon changed the preceding execution");
    progress.progress_rounds = 8; auto shorter = regional::run(progress, progress_options); complete(shorter);
    check(*shorter.milestones.at(3).participants.at(0).granted < broad_granted, "more younger work did not extend broad waiting in falsifier");
    std::cout << "actual alternating-holder prefix + finite drainage passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& e) { std::cerr << "regional check: " << e.what() << '\n'; return EXIT_FAILURE; }
}
