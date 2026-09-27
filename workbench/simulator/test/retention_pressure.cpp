#include "retention_pressure.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace sixdb::sim;
namespace model = retention_pressure;
void require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}
bool has(const std::vector<std::string>& values, std::string_view fragment) {
  return std::ranges::any_of(values, [&](const auto& value) { return value.find(fragment) != std::string::npos; });
}
void positives() {
  for (auto [memory, storage] : {std::pair{8192U, 16384U}, std::pair{4096U, 8192U}})
    for (auto incident : {model::Incident::none, model::Incident::checkpoint_reset, model::Incident::replay_cursor_reset})
      for (auto seed : {1U, 7U, 19U}) {
        model::Case config; config.memory_bytes = memory; config.storage_bytes = storage; config.incident = incident;
        auto result = model::run_case(config, {.seed = seed});
        require(result.violations.empty(), "healthy pressure fixture violated safety");
        require(result.missing_incidents.empty(), "healthy pressure fixture missed required incident");
        require(result.reconstruction_refusals > 0 && result.pressure_released, "refusal/release control did not execute");
        require(result.independent_during_pressure > 0, "independent producer made no progress while old reconstruction waited");
        require(result.reader_root_released && result.replay_root_advanced && result.current_recovered_after_gc,
                "root releases or actual post-GC checkpoint reconstruction missing");
        require(result.source_restarts == 2, "source restart accounting included initial startup or omitted recovery");
        require(result.source_usage.memory_peak <= memory && result.source_usage.reserved_storage == 0,
                "physical charge escaped capacity or leaked write reservation");
        require(result.source_usage.durable == 338 && result.reclaimed_bytes >= 8000,
                "retirement did not reclaim old chain while retaining current state");
        require(!result.execution.budget_exhausted && !result.execution.pending, "finite workload did not drain");
        for (const auto& [name, cohort] : result.cohorts) {
          require(cohort.arrived == cohort.offered && cohort.completed == cohort.offered && !cohort.failed && !cohort.unfinished,
                  "healthy case lost an authored obligation");
        }
      }
}
void negatives() {
  for (auto seed : {1U, 7U, 19U}) {
    model::Case config; config.negative = model::Negative::skip_replay_root;
    auto broken = model::run_case(config, {.seed = seed});
    require(has(broken.violations, "retired live root dependency") && has(broken.violations, "required read unavailable"),
            "omitted replay root did not produce independent failure evidence");
    require(broken.cohorts.at("replay").failed == 1 && broken.cohorts.at("old-reader").completed == 1,
            "reader success incorrectly satisfied replay obligation");
    require(broken.cohorts.at("source").completed == 24 && broken.cohorts.at("independent").completed == 24,
            "negative root control did not isolate the replay obligation");
    config.negative = model::Negative::lose_retry;
    broken = model::run_case(config, {.seed = seed});
    require(broken.violations.empty(), "lost continuation is unfinished work, not a fabricated safety violation");
    require(broken.reconstruction_refusals == 1 && broken.pressure_released, "lost-retry control missed initial refusal or later relief");
    require(broken.cohorts.at("independent").completed == 24 && broken.cohorts.at("source").unfinished > 0 &&
            broken.cohorts.at("old-reader").unfinished == 1 && broken.cohorts.at("replay").unfinished == 1,
            "lost continuation did not stay unfinished after resource pressure ended");
    for (const auto& [name, cohort] : broken.cohorts)
      require(cohort.completed + cohort.failed + cohort.unfinished == cohort.offered, "negative control lost offered accounting");
  }
}
void temporal_oracle_controls() {
  std::vector<Record> records;
  model::Case config;
  auto result = model::run_case(config, {.seed = 7}, nullptr, [&](const Record& record) { records.push_back(record); });
  require(result.violations.empty() && model::audit(config, records).empty(), "captured healthy history failed independent recheck");
  auto find = [&](std::string_view kind, ActorId actor) {
    return std::ranges::find_if(records, [&](const auto& record) { return record.kind == kind && record.actor == actor; });
  };
  {
    auto altered = records;
    auto source = std::ranges::find_if(altered, [](const auto& record) { return record.kind == "pressure.completed" && record.actor == 10; });
    source->tag = 999;
    auto errors = model::audit(config, altered);
    require(has(errors, "source completion lacks durable authored write") && has(errors, "completion without offered work"),
            "forged completion escaped durable-prefix/offer oracle");
  }
  {
    auto altered = records; auto terminal = find("pressure.completed", 12);
    require(terminal != records.end(), "reader completion prerequisite absent");
    altered.insert(altered.begin(), *terminal);
    auto errors = model::audit(config, altered);
    require(has(errors, "reader completion lacks bytes or durable root release") && has(errors, "duplicate terminal completion"),
            "early or duplicate reader completion escaped oracle");
  }
  {
    auto altered = records;
    std::erase_if(altered, [](const auto& record) { return record.kind == "pressure.old_bytes"; });
    require(has(model::audit(config, altered), "reader completion lacks bytes or durable root release"),
            "successful reply without actual reconstruction escaped oracle");
  }
  {
    auto altered = records;
    auto bytes = std::ranges::find_if(altered, [](const auto& record) { return record.kind == "pressure.current_recovered"; });
    require(bytes != altered.end(), "current checkpoint reconstruction prerequisite absent");
    bytes->detail.back() ^= 1;
    require(has(model::audit(config, altered), "recovered current bytes differ from durable root"),
            "corrupted checkpoint reconstruction escaped byte oracle");
  }
  {
    auto altered = records;
    std::erase_if(altered, [](const auto& record) {
      return record.actor == 1 && record.incarnation >= 2 &&
          (record.kind == "pressure.read_result" || record.kind == "storage.read");
    });
    require(has(model::audit(config, altered), "current recovery lacks same-incarnation read chain"),
            "claimed reconstruction without actual incarnation reads escaped oracle");
  }
  {
    auto altered = records;
    std::erase_if(altered, [](const auto& record) { return record.kind == "storage.read"; });
    require(has(model::audit(config, altered), "read result lacks matching physical read receipt"),
            "self-reported read bytes without physical receipts escaped oracle");
  }
}
void exact_replay_and_censoring() {
  model::Case config; config.incident = model::Incident::replay_cursor_reset;
  std::ostringstream choices;
  auto first = model::run_case(config, {.seed = 19, .decisions_out = &choices});
  std::istringstream replay(choices.str());
  auto second = model::run_case(config, {.seed = 999, .decisions_in = &replay});
  require(first.trace_hash == second.trace_hash && first.source_usage.written == second.source_usage.written,
          "exact replay changed charged work or trace");
  config.until_ns = 1000;
  auto short_run = model::run_case(config);
  require(!short_run.missing_incidents.empty() && short_run.violations.empty(), "censoring became success or a safety violation");
  require(short_run.cohorts.at("source").offered == 24 && !short_run.cohorts.at("source").arrived &&
          short_run.cohorts.at("source").unfinished == 24, "future arrivals disappeared from offered denominator");
}
} // namespace
int main() {
  try {
    positives(); negatives(); temporal_oracle_controls(); exact_replay_and_censoring();
    std::cout << "retention pressure: competing roots, independent progress, refusals, recovery, negative controls and replay passed\n";
  } catch (const std::exception& error) {
    std::cerr << "retention pressure failure: " << error.what() << '\n'; return 1;
  }
}
