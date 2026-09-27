// An embeddable client: construction and observation use the same library as
// campaigns. Pausing is observational; the model never receives oracle state.
#include "orbital.hpp"

#include <iostream>
#include <stdexcept>

int main() {
  using namespace sixdb::sim;
  orbital::Case spec;
  spec.name = "interactive-checker-restart";
  spec.until_ns = 10'000'000;
  Simulation simulation({.seed = 7});
  auto experiment = orbital::assemble(simulation, spec);
  bool inspected = false;
  simulation.observe([&](const Record& record) {
    if (!inspected && record.kind == "orbital.private_read") {
      inspected = true;
      simulation.pause();
    }
  });
  simulation.start();
  auto first = simulation.run(spec.until_ns);
  if (!first.paused || !inspected) throw std::runtime_error("private-read milestone never reached");
  // This model's assembly explicitly assigns process ID = role ID.
  simulation.crash(orbital::checker(1));
  simulation.restart(orbital::checker(1));
  auto rest = simulation.run(spec.until_ns);
  rest.events += first.events;
  auto result = experiment.result(simulation, rest);
  orbital::write_json(std::cout, result);
  if (!result.violations.empty() || rest.budget_exhausted) return 1;
  for (const auto& [name, cohort] : result.cohorts)
    if (cohort.unfinished || cohort.failed) return 1;
}
