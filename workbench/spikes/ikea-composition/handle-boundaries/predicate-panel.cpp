// Outer control only. The actual fixtures, independent byte oracles,
// conditioning and timed loops remain in frozen filter-composition sources.
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace panel_before_runtime {
extern unsigned iterations, repeats;
extern std::uint64_t observed;
extern bool check_only;
}
namespace panel_after_runtime {
extern unsigned iterations, repeats;
extern std::uint64_t observed;
extern bool check_only;
}
namespace panel_duplicate_runtime {
extern unsigned iterations, repeats;
extern std::uint64_t observed;
extern bool check_only;
}
void panel_before_tuples_2();
void panel_after_tuples_2();
void panel_duplicate_tuples_2();

namespace {
struct Arm {
    const char* name;
    unsigned* iterations;
    unsigned* repeats;
    std::uint64_t* observed;
    bool* check;
    void (*run)();
};
const std::array arms{
    Arm{"before", &panel_before_runtime::iterations, &panel_before_runtime::repeats,
        &panel_before_runtime::observed, &panel_before_runtime::check_only, panel_before_tuples_2},
    Arm{"after", &panel_after_runtime::iterations, &panel_after_runtime::repeats,
        &panel_after_runtime::observed, &panel_after_runtime::check_only, panel_after_tuples_2},
    Arm{"duplicate", &panel_duplicate_runtime::iterations, &panel_duplicate_runtime::repeats,
        &panel_duplicate_runtime::observed, &panel_duplicate_runtime::check_only, panel_duplicate_tuples_2}};
constexpr std::array<std::array<unsigned, 3>, 5> orders{{
    {{0, 1, 2}}, {{1, 2, 0}}, {{2, 0, 1}}, {{2, 1, 0}}, {{1, 0, 2}}}};
}

int main(int argc, char** argv) {
    const bool check = argc > 1 && std::string_view(argv[1]) == "--check";
    const unsigned iterations = check || argc < 2 ? 300000 : unsigned(std::strtoul(argv[1], nullptr, 10));
    const unsigned trials = check ? 1 : argc < 3 ? 5 : unsigned(std::strtoul(argv[2], nullptr, 10));
    assert(iterations && trials == (check ? 1 : 5));
    std::puts("workload,width_or_cases,op,active,candidate,metric,rep,ns_window");
    std::uint64_t expected = 0;
    bool have_expected = false;
    for (unsigned trial = 0; trial < trials; ++trial)
        for (unsigned index : orders[trial]) {
            const auto& arm = arms[index];
            *arm.iterations = iterations;
            *arm.repeats = 1;
            *arm.observed = 0;
            *arm.check = check;
            std::printf("# begin,%u,%s\n", trial, arm.name);
            arm.run();
            std::printf("# end,%u,%s,%llu\n", trial, arm.name,
                        static_cast<unsigned long long>(*arm.observed));
            if (have_expected)
                assert(*arm.observed == expected);
            expected = *arm.observed;
            have_expected = true;
        }
    std::fprintf(stderr, "predicate-panel: actual op2 fixture oracles and cross-arm consumed checksums passed\n");
}
