#include "runtime.h"
#include <string_view>

int main(int argc, char **argv) {
    // Longer samples for variable observations; no unrelated sweep rerun.
    runtime::iterations = 1000000;
    runtime::repeats = 9;
    runtime::check_only = argc > 1 && std::string_view(argv[1]) == "--check";
    std::puts("workload,width_or_cases,op,active,candidate,metric,rep,ns_window");
#if defined(__aarch64__)
    runtime::run_width<55>();
    runtime::run_width<60>();
#else
    runtime::run_width<43>();
#endif
    std::fprintf(stderr, "focused recheck passed; checksum=%llu\n",
                 (unsigned long long)runtime::observed);
}
