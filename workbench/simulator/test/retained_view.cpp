// Same semantic retention questions as the learning spike, through native ports.
#include "retained_view.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace sixdb::sim;
namespace model = retained_view;

void require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}
bool has(const std::vector<std::string>& errors, std::string_view fragment) {
  return std::ranges::any_of(errors, [&](const auto& error) { return error.find(fragment) != std::string::npos; });
}
void successful_roots_and_faults() {
  for (auto root : {model::Root::reader, model::Root::replay})
    for (auto incident : {model::Incident::none, model::Incident::checkpoint_device_reset,
                         model::Incident::checkpoint_process_loss})
      for (auto seed : {1U, 7U, 19U}) {
        model::Case config; config.root = root; config.incident = incident;
        auto result = model::run_retained_view(config, {.seed = seed});
        if (!result.violations.empty()) throw std::runtime_error(result.violations.front());
        require(result.missing_milestones.empty(), "healthy root/fault fixture missed required work");
        require(result.old_read_complete && result.checkpoint_read_complete, "old/current bytes were not both reconstructed");
        require(!result.execution.budget_exhausted && !result.execution.pending, "finite fixture did not drain");
        require(result.resets == (incident == model::Incident::checkpoint_device_reset ? 3U : 2U), "device reset coverage wrong");
        require(result.usage.memory_peak <= config.memory_bytes && result.usage.memory == 4096, "resident budget or workspace lifetime wrong");
        require(result.usage.reserved_storage == 0, "checkpoint leaked durable reservation");
        require(result.usage.durable < 1200, "old base/operations survived final collection");
        if (incident == model::Incident::checkpoint_process_loss)
          require(result.bytes_after_process_loss == 1032, "pending checkpoint lost its physical buffer borrow");
        if (incident != model::Incident::none) require(result.incident_exercised, "checkpoint fault never fired");
      }
}
void negative_root_and_checkpoint_controls() {
  for (auto root : {model::Root::reader, model::Root::replay})
    for (auto seed : {1U, 7U, 19U}) {
      model::Case config; config.root = root; config.negative = model::Negative::drop_live_root;
      auto result = model::run_retained_view(config, {.seed = seed});
      require(has(result.violations, "retired live dependency"), "missing root protection escaped prefix oracle");
      require(has(result.violations, "required reconstruction unavailable"), "bad reclamation did not cause a real read failure");
      require(!result.old_read_complete && result.resets == 1, "negative root fixture did not reach intended reset/read");
    }
  model::Case config; config.negative = model::Negative::publish_before_checkpoint;
  auto result = model::run_retained_view(config);
  require(has(result.violations, "head published before matching checkpoint durable"), "early head publication escaped oracle");
  require(has(result.violations, "data/checkpoint/4:missing"), "early publication did not lose actual recovery");
  require(!result.old_read_complete && result.resets == 1, "early publication control did not exercise reset");
}
void finite_capacity_is_unfinished_not_success() {
  model::Case config; config.memory_bytes = 4096;
  auto result = model::run_retained_view(config);
  require(result.violations.empty(), "capacity refusal is not itself a correctness violation");
  require(!result.old_read_complete && !result.missing_milestones.empty(), "capacity refusal silently reported success");
  require(result.usage.refused > 0, "tight memory did not reach physical admission");
  require(std::ranges::any_of(result.evidence, [](const auto& r) { return r.kind == "retained.blocked"; }), "capacity aftermath has no explicit observation");
}
void independent_oracle_rejects_fake_old_read() {
  auto result = model::run_retained_view();
  auto latest = std::ranges::find_if(result.evidence, [](const auto& r) { return r.kind == "retained.reconstructed"; });
  auto old = std::ranges::find_if(result.evidence, [](const auto& r) {
    return r.kind == "retained.reconstructed" && r.detail.find("roots/reader") != std::string::npos;
  });
  require(latest != result.evidence.end() && old != result.evidence.end(), "oracle negative prerequisites missing");
  // Preserve root identity while substituting the complete descriptor/payload
  // for a latest read. Binary record lengths are adjusted with the identity.
  auto corrupted = latest->detail;
  auto at = corrupted.find("head");
  require(at >= 4 && at != std::string::npos, "fixture encoding changed");
  corrupted.replace(at, 4, "roots/reader");
  corrupted[at - 4] = 12;
  old->detail = std::move(corrupted);
  auto errors = model::audit(result.evidence);
  require(has(errors, "reconstruction differs from durable root"), "consistent latest bytes passed old-root oracle");
}
void exact_replay() {
  model::Case config; config.incident = model::Incident::checkpoint_device_reset;
  std::ostringstream transcript;
  auto first = model::run_retained_view(config, {.seed = 19, .decisions_out = &transcript});
  std::istringstream replay(transcript.str());
  auto second = model::run_retained_view(config, {.seed = 999, .decisions_in = &replay});
  require(first.trace_hash == second.trace_hash, "native retained fixture replay changed trace");
  require(first.usage.written == second.usage.written && first.usage.read == second.usage.read,
          "replay changed charged physical work");
}
} // namespace

int main() {
  try {
    successful_roots_and_faults();
    negative_root_and_checkpoint_controls();
    finite_capacity_is_unfinished_not_success();
    independent_oracle_rejects_fake_old_read();
    exact_replay();
    std::cout << "retained-view fixture: roots, reclamation, restart, borrow, negative controls and replay passed\n";
  } catch (const std::exception& error) {
    std::cerr << "retained-view fixture failure: " << error.what() << '\n'; return 1;
  }
}
