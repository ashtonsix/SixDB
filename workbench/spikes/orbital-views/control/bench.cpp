#include "mechanisms.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
  const bool reverse = argc == 2 && std::string(argv[1]) == "--reverse";
  auto cpus = control::available_cpus();
  if (cpus.size() < 4) { std::cerr << "Need at least four allowed CPUs for ownership cells\n"; return 2; }
  // libc's single-thread shortcut should not change reference counting cells.
  std::thread([] {}).join();
  struct Case { std::string family, policy, shape; unsigned threads; std::function<control::Receipt()> run; };
  std::vector<Case> cases;
  for (unsigned threads : {1U, 2U, 4U})
    for (bool shared : {false, true}) for (bool cut : {false, true})
      for (auto policy : {control::Owners::global, control::Owners::group, control::Owners::object}) {
        auto name = policy == control::Owners::global ? "global" : policy == control::Owners::group ? "group" : "object";
        std::string shape = std::string(shared ? "shared-groups" : "separate-groups") + (cut ? "+cut" : "+local");
        cases.push_back({"ownership", name, shape, threads,
            [=, &cpus] { return control::ownership(policy, threads, shared, cut, 65536, cpus); }});
      }
  for (auto shape : {"sparse-contiguous", "sparse-scattered", "dense"}) {
    std::vector<std::size_t> selected;
    auto count = std::string(shape) == "dense" ? 256U : 16U;
    for (unsigned p = 0; p < count; ++p)
      selected.push_back(std::string(shape) == "sparse-scattered" ? (p * 73 + 19) % 256 : p);
    for (bool cached : {false, true}) for (auto policy : {control::Metadata::ordered, control::Metadata::bitmap})
      cases.push_back({"readiness", policy == control::Metadata::ordered ? "ordered" : "bitmap",
          std::string(shape) + (cached ? "+cached" : "+fresh"), 1,
          [=] { return control::readiness(policy, cached, 256, selected, count == 256 ? 2048 : 8192); }});
    for (auto policy : {control::Lifetime::window, control::Lifetime::page, control::Lifetime::store}) {
      auto name = policy == control::Lifetime::window ? "window" : policy == control::Lifetime::page ? "page" : "store";
      cases.push_back({"lifetime", name, shape, 1,
          [=] { return control::lifetime(policy, 256, selected, count == 256 ? 2048 : 8192); }});
    }
    for (bool compact : {false, true})
      cases.push_back({"combined", compact ? "bitmap-window" : "ordered-page", shape, 1,
          [=] { return control::combined(compact, 256, selected, count == 256 ? 2048 : 8192); }});
  }
  for (unsigned revisions : {16U, 256U, 4096U})
    for (auto policy : {control::History::ordered, control::History::contiguous, control::History::captured}) {
      auto name = policy == control::History::ordered ? "ordered" : policy == control::History::contiguous ? "contiguous" : "captured";
      cases.push_back({"history", name, std::to_string(revisions), 1,
          [=] { return control::history(policy, revisions, 262144); }});
    }
  if (reverse) std::reverse(cases.begin(), cases.end());
  std::cout << "family,policy,shape,threads,operations,units,checksum,holds,wall_ns,ns_per_operation,ns_per_unit\n";
  for (auto& c : cases) {
    control::pin_thread(cpus[0]);
    // Thread workers need the original affinity list, captured before pinning.
    auto r = c.run();
    std::cout << c.family << ',' << c.policy << ',' << c.shape << ',' << c.threads << ','
        << r.operations << ',' << r.units << ',' << r.checksum << ',' << r.holds << ','
        << r.elapsed_ns << ',' << r.elapsed_ns / r.operations << ',' << r.elapsed_ns / r.units << '\n';
  }
}
