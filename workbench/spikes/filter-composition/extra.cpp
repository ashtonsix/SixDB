#include "extra.h"
namespace extras {
void measure_bound_extra(Call call, Context prototype, const Fixture &fixture,
                         const std::array<word, windows> &upper, unsigned op, const char *name,
                         std::uint16_t active, const char *workload) {
    std::array<Context, 8> states;
    states.fill(prototype);
    auto body = [&](unsigned w, unsigned j) __attribute__((always_inline)) {
        return call(states[j], std::size_t(w) * 16, fixture.queries[w], upper[w], active);
    };
    measure(fixture, op, name, active, body, workload);
}
} // namespace extras
void pairs_0();
void pairs_1();
void tuples_0();
void tuples_1();
void tuples_2();
void tuples_3();
void tuples_4();

#ifndef FILTER_EXTRAS_NO_MAIN
int main(int argc, char **argv) {
    using namespace runtime;
    if (argc > 1) {
        if (std::string_view(argv[1]) == "--check")
            check_only = true;
        else
            iterations = std::stoul(argv[1]);
    }
    if (argc > 2)
        repeats = std::stoul(argv[2]);
    std::puts("workload,width_or_cases,op,active,candidate,metric,rep,ns_window");
    pairs_0();
    pairs_1();
    tuples_0();
    tuples_1();
    tuples_2();
    tuples_3();
    tuples_4();
    std::fprintf(stderr, "pair and TuplePack oracles passed; checksum=%llu\n",
                 (unsigned long long)observed);
}
#endif
