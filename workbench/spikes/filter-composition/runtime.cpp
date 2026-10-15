#include "runtime.h"
#include <algorithm>
#include <cstdlib>
#include <string_view>
namespace runtime {
std::vector<Entry> catalogue;
unsigned iterations = 100000, repeats = 5;
word observed = 0;
bool check_only = false;
Fixture::Fixture(unsigned k) : width(k), bytes(k * 2 * windows), truth(windows * 16) {
    word state = 0x1234567812345678ULL + k;
    auto random = [&] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    const word limit = ~word{0} >> (64 - k);
    for (unsigned i = 0; i < truth.size(); ++i) {
        word x = i % 31 == 0 ? 0 : i % 31 == 1 ? limit : random() & limit;
        truth[i] = x;
        auto *tile = bytes.data() + (i / 8) * k;
        for (unsigned b = 0; b < k / 8; ++b)
            tile[(i % 8) * (k / 8) + b] = x >> (k % 8 + b * 8);
        for (unsigned b = 0; b < k % 8; ++b)
            tile[8 * (k / 8) + b] |= ((x >> b) & 1) << (i % 8);
    }
    for (unsigned w = 0; w < windows; ++w)
        queries[w] = w % 2 ? random() & limit : truth[w * 16 + w % 16];
}
bool oracle(unsigned op, word a, word b) {
    switch (op) {
    case 0:
        return a == b;
    case 1:
        return a < b;
    case 2:
        return a <= b;
    case 3:
        return a > b;
    default:
        return a >= b;
    }
}
void verify(Call call, State &s, const Fixture &fixture, unsigned op) {
    const word limit = ~word{0} >> (64 - fixture.width);
    for (word q : {word{0}, word{1}, limit / 2, limit, limit + 1, ~word{0}})
        for (unsigned w : {0u, 1u, 31u, 63u})
            for (std::uint16_t active : {std::uint16_t(0), std::uint16_t(1), std::uint16_t(0xa53c),
                                         std::uint16_t(65535)}) {
                unsigned expected = 0;
                for (unsigned i = 0; i < 16; ++i)
                    if ((active >> i & 1) && oracle(op, fixture.truth[w * 16 + i], q))
                        expected |= 1u << i;
                assert(call(s, w * 16, q, active) == expected);
            }
}
// One real prepared call per window, same harness for bound fusion and CPS.
[[gnu::noinline]] void measure_bound(Call call, State prototype, const Fixture &fixture,
                                     unsigned op, const char *name, std::uint16_t active) {
    std::array<State, 8> states;
    states.fill(prototype);
    auto body = [&](unsigned w, unsigned j) __attribute__((always_inline)) {
        return call(states[j], std::size_t(w) * 16, fixture.queries[w], active);
    };
    measure(fixture, op, name, active, body);
}
void catalogue_bench() {
    if (check_only)
        return;
    // Same entries/results across controls; work chooses both the next schema
    // and data window. Each independent stream owns mutable query/result state.
    for (unsigned count : {20u, 320u}) {
        std::vector<Entry> entries;
        if (count == 20)
            for (auto &e : catalogue) {
                if (e.width == 3 || e.width == 12 || e.width == 32 || e.width == 64)
                    entries.push_back(e);
            }
        else
            entries = catalogue;
        assert(entries.size() == count);
        for (unsigned variant = 0; variant < 4; ++variant) {
            const char *name = variant == 0   ? "bound_fused"
                               : variant == 1 ? "shared"
                               : variant == 2 ? "selective"
                                              : "one_stage";
            std::vector<Call> calls;
            std::array<std::vector<State>, 8> states;
            for (auto &e : entries) {
                calls.push_back(variant == 0   ? e.fused
                                : variant == 1 ? e.shared
                                : variant == 2 ? e.selected
                                               : e.one);
                auto s = e.state;
                if (variant == 3)
                    s.plan = e.one_plan;
                for (auto &stream : states)
                    stream.push_back(s);
            }
            for (bool serial : {true, false})
                for (unsigned rep = 0; rep < repeats; ++rep) {
                    std::array<unsigned, 8> cursor{0, 7, 13, 21, 32, 41, 54, 63};
                    auto step = [&](unsigned j, unsigned salt) __attribute__((always_inline)) {
                        auto x = cursor[j], index = x % count, w = (x >> 16) & 63;
                        auto bits = calls[index](states[j][index], std::size_t(w) * 16,
                                                 entries[index].queries[w], 65535);
                        cursor[j] = unsigned((word(bits) * 0x9e3779b97f4a7c15ULL) >> 32) ^ salt;
                    };
                    for (unsigned i = 0; i < 2048; ++i)
                        for (unsigned j = 0; j < 8; ++j)
                            step(j, i);
                    auto start = std::chrono::steady_clock::now();
                    for (unsigned i = 0; i < iterations; ++i) {
                        if (serial)
                            step(0, i);
                        else
                            sp::detail::each<8>([&](auto j) { step(j, i); });
                    }
                    auto stop = std::chrono::steady_clock::now();
                    for (auto x : cursor)
                        observed += x;
                    auto ns = std::chrono::duration<double, std::nano>(stop - start).count() /
                              (double(iterations) * (serial ? 1 : 8));
                    std::printf("catalogue,%u,0,65535,%s,%s,%u,%.6f\n", count, name,
                                serial ? "dependent" : "independent8", rep, ns);
                }
        }
    }
}
} // namespace runtime
#ifndef FILTER_SEAMS_NO_MAIN
void widths_1();
void widths_9();
void widths_17();
void widths_25();
void widths_33();
void widths_41();
void widths_49();
void widths_57();
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
    widths_1();
    widths_9();
    widths_17();
    widths_25();
    widths_33();
    widths_41();
    widths_49();
    widths_57();
    catalogue_bench();
    std::fprintf(stderr, "checked 64 widths x 5 predicates x 3 prepared paths; checksum=%llu\n",
                 (unsigned long long)observed);
}

#endif
