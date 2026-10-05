#include "cut.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  auto cpus = control::available_cpus();
  if (cpus.size() < 4) { std::cerr << "Requires four allowed CPUs\n"; return 2; }
  struct Case { std::string family, policy; unsigned producers, cadence; std::function<control::CutReceipt()> run; };
  std::vector<Case> cases;
  for (unsigned p : {1U, 2U, 4U}) for (auto kind : {control::CutKind::bitmap, control::CutKind::producers}) {
    auto policy = kind == control::CutKind::bitmap ? "shared-bitmap" : "producer-counters";
    for (unsigned cadence : {0U, 1U, 16U})
      cases.push_back({"updates", policy, p, cadence,
          [=, &cpus] { return control::cut_updates(kind, p, cadence, 32768, cpus); }});
    cases.push_back({"queries", policy, p, 1,
        [=] { return control::cut_queries(kind, p, 1048576); }});
  }
  if (argc == 2 && std::string(argv[1]) == "--reverse") std::reverse(cases.begin(), cases.end());
  std::cout << "family,policy,producers,cadence,publications,queries,final_cut,checksum,query_checksum,max_head_distance,max_unqueried_updates,wall_ns,ns_per_publication,ns_per_query\n";
  for (auto& c : cases) {
    control::pin_thread(cpus[0]);
    auto r = c.run();
    std::cout << c.family << ',' << c.policy << ',' << c.producers << ',' << c.cadence << ','
        << r.publications << ',' << r.queries << ',' << r.final_cut << ',' << r.checksum << ','
        << r.query_checksum << ',' << r.max_head_distance << ',' << r.max_unqueried_updates << ','
        << r.elapsed_ns << ',' << (r.publications ? r.elapsed_ns / r.publications : 0) << ','
        << (c.family == "queries" ? r.elapsed_ns / r.queries : 0) << '\n';
  }
}
