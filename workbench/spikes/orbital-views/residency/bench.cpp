#include "core.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <tuple>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char** argv) {
  rlimit no_core{0, 0};
  residency::require(setrlimit(RLIMIT_CORE, &no_core) == 0, "disable fault-check core dumps");
  bool check = argc > 1 && std::string(argv[1]) == "--check";
  bool memory = argc > 1 && std::string(argv[1]) == "--memory";
  if (memory) { residency::memory_sweep(false); return 0; }
  using Case = std::tuple<std::string, residency::Profile, std::string>;
  std::vector<Case> cases;
  for (std::string method : {"page_copy", "run_copy", "file_lazy", "file_runs", "file_populate",
                            "anon_eager", "retained_copy", "retained_take", "pool_rebind"})
    for (const auto& profile : residency::profiles())
      for (std::string readers : {"none", "held", "late_map", "late_pinned", "late_1_in_8"})
        cases.emplace_back(method, profile, readers);
  if (argc > 1 && std::string(argv[1]) == "--reverse") std::reverse(cases.begin(), cases.end());
  residency::print_header(); std::fflush(stdout);
  for (const auto& [method, profile, readers] : cases) {
    pid_t child = fork(); residency::require(child >= 0, "fork cell");
    if (!child) { residency::print(residency::pipeline(method, profile, readers, check ? 2 : 64)); _exit(0); }
    int status; residency::require(waitpid(child, &status, 0) == child && status == 0, "cell exit");
  }
  if (check) residency::memory_sweep(true);
}
