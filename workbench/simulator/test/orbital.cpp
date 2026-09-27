#include "orbital.hpp"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace sixdb::sim;
using namespace sixdb::sim::orbital;
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::uint64_t complete(const Result& r) { std::uint64_t n{}; for (const auto& [name, c] : r.cohorts) n += c.completed; return n; }
void healthy(const Result& r, std::uint64_t count) {
  if (!r.violations.empty()) { for (const auto& e : r.violations) std::cerr << e << '\n'; }
  check(r.violations.empty(), "safety violation"); check(!r.execution.budget_exhausted, "event budget exhausted");
  check(complete(r) == count, "offers failed to complete"); check(r.missing_incidents.empty(), "authored incident did not fire");
}
}
int main() {
  try {
    Case base; base.point_writes = 4;
    auto normal = run_case(base); healthy(normal, 5); check(normal.latest_values.at(cell(0, 99)) == 406, "checked result lost old cut");
    std::cout << "checked transform + continuing points passed\n";
    for (auto incident : {Incident::one_follower, Incident::quorum_pause, Incident::consumer_reset, Incident::coordinator_reset, Incident::checker_reset}) {
      auto c = base; c.incident = incident; auto r = run_case(c); healthy(r, 5);
      check(r.triggered_incidents.size() == 1, "fault receipt missing"); std::cout << name(incident) << " passed\n";
    }
    Case durable_cut = base; durable_cut.incident = Incident::coordinator_reset;
    Options fifo; fifo.ordering = Ordering::fifo; Simulation cut_sim(fifo); auto cut_case = assemble(cut_sim, durable_cut);
    OpId committed_op{}; bool stopped{}, escaped_callback{};
    cut_sim.observe([&](const Record& record) {
      if (!committed_op && record.kind == "storage.write" && record.actor == coordinator(0) && record.detail.starts_with("decision/")) committed_op = record.operation;
      if (committed_op && record.kind == "process.crash" && record.actor == coordinator(0)) stopped = true;
      if (committed_op && record.operation == committed_op && record.kind == "actor.receive" && record.actor == coordinator(0) && !stopped) escaped_callback = true;
    });
    cut_sim.start(); auto cut_execution = cut_sim.run(durable_cut.until_ns); healthy(cut_case.result(cut_sim, cut_execution), 5);
    check(committed_op && stopped && !escaped_callback, "durable cut was not before callback");
    std::cout << "actual write-before-callback recovery passed\n";
    Case transfer; transfer.transactions = {{1, 100'000, 0, Program::transfer, {cell(0, 0), cell(1, 0)}, {cell(0, 0), cell(1, 0)}, 0, "transfer"}};
    auto moved = run_case(transfer); healthy(moved, 1); check(moved.latest_values.at(cell(0, 0)) == 99 && moved.latest_values.at(cell(1, 0)) == 101, "two-shard transfer differs");
    std::cout << "two-shard transfer passed\n";
    Case mismatch = base; mismatch.negative = Negative::corrupt_checker; auto aborted = run_case(mismatch);
    check(aborted.violations.empty() && aborted.cohorts.at("checked").failed == 1 && complete(aborted) == 4, "disagreement did not safely abort");
    std::cout << "verified mismatch abort passed\n";
    Case skip = base; skip.negative = Negative::skip_verification; auto unsafe = run_case(skip); check(!unsafe.violations.empty(), "missing verification control escaped oracle");
    std::cout << "missing verification negative passed\n";
    Case pending; pending.until_ns = 800'000; pending.checker_delay_ns = 2'000'000;
    pending.transactions = {{1, 100'000, 0, Program::checked_sum, {cell(0, 0)}, {cell(0, 99)}, 0, "checked"},
      {2, 400'000, 0, Program::transfer, {cell(0, 1), cell(0, 99)}, {cell(0, 1), cell(0, 99)}, 0, "dependent"},
      {3, 400'000, 1, Program::put, {}, {cell(1, 0)}, 123, "independent"}};
    auto waiting = run_case(pending); check(waiting.violations.empty() && waiting.cohorts.at("dependent").unfinished == 1 && waiting.cohorts.at("independent").completed == 1, "pending predecessor failed isolation");
    pending.negative = Negative::skip_pending; auto skipped = run_case(pending); check(!skipped.violations.empty(), "finite-prefix pending control escaped oracle");
    std::cout << "finite-prefix pending negative + independent work passed\n";
    Case disconnected = base; disconnected.links = std::vector<Link>{}; auto no_route = run_case(disconnected); check(no_route.violations.empty() && complete(no_route) == 0, "explicit topology secretly supplied connectivity");
    std::cout << "explicit disconnected topology passed\n";
    Case future = base; future.until_ns = 100; auto early = run_case(future);
    check(early.observed_until_ns <= future.until_ns && early.cohorts.at("checked").arrived == 0 && early.cohorts.at("point").not_yet_offered == 4, "future offers disguised as received traffic");
    std::cout << "censored arrival accounting passed\n";
    Case placed = base; placed.placement[coordinator(0)] = client; placed.incident = Incident::consumer_reset; auto placement = run_case(placed); healthy(placement, 5);
    std::cout << "physical placement passed\n";
    Case bad = base; bad.placement[999] = 1; bool rejected{}; try { (void)run_case(bad); } catch (const std::runtime_error&) { rejected = true; } check(rejected, "unknown placement role ignored");
    std::ostringstream trace, choices; Options capture; capture.decisions_out = &choices; auto traced = run_case(base, capture, &trace);
    std::istringstream replay(choices.str()); Options replay_options; replay_options.decisions_in = &replay; auto replayed = run_case(base, replay_options);
    check(traced.trace_hash == replayed.trace_hash && traced.completed_at == replayed.completed_at && traced.violations == replayed.violations, "record/replay differs");
    auto silent = run_case(base); check(silent.trace_hash == traced.trace_hash && silent.records == traced.records, "trace attachment changed experiment");
    std::cout << "trace independence + replay passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& e) { std::cerr << "orbital model test failed: " << e.what() << '\n'; return EXIT_FAILURE; }
}
