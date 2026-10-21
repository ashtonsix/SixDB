#include <ikea/hashtable/lookup.h>
#include <ikea/hashtable/detail/hash.h>
#include <iostream>
#include <stdexcept>

namespace ht = ikea::hashtable;
int main() {
    ht::lookup_selector selector;
    ht::require(selector.preferred_hint() == ht::lookup_hint::predictable, "predictable default");
    std::uint64_t tick = 0, clocks = 0, processed = 0;
    auto clock = [&] { ++clocks; return tick; };
    auto feed = [&](unsigned p, unsigned u) {
        for (unsigned i = 0; i < 192; ++i) {
            const unsigned count = i % 2 ? 37 : 251;
            selector.run(count, [&](auto hint, auto first, auto size) {
                ht::require(first == 0 && size == count, "one short external batch");
                tick += size * (decltype(hint)::value == ht::lookup_hint::predictable ? p : u);
                processed += size;
            }, clock);
        }
    };
    feed(100, 20);
    ht::require(selector.preferred_hint() == ht::lookup_hint::unpredictable,
                "unequal short batches choose cheaper useful work");
    feed(20, 100);
    ht::require(selector.preferred_hint() == ht::lookup_hint::predictable, "follow cost reversal");
    feed(100, 95);
    ht::require(selector.preferred_hint() == ht::lookup_hint::predictable, "bias small advantage");
    const auto stats = selector.statistics();
    ht::require(stats.predictable + stats.unpredictable == processed && stats.probes == 18 &&
                    clocks == 2 * stats.probes && stats.switches == 2,
                "no clocks per key or nonsampling block; correct useful accounting");
    selector.reset();
    for (auto n : {0u, 1u, 31u, 32u, 33u, 255u, 256u, 257u, 511u, 512u, 513u, 4097u}) {
        unsigned next = 0;
        selector.run(n, [&](auto, auto first, auto size) {
            ht::require(first == next && size && size <= 256 && size <= n - next,
                        "disjoint ordered ranges and tail");
            next += size;
        }, clock);
        ht::require(next == n, "exactly once including empty input");
    }
    selector.reset();
    bool threw = false;
    try {
        selector.run(1024, [](auto, auto, auto) { throw std::runtime_error("caller"); }, clock);
    } catch (const std::runtime_error&) { threw = true; }
    ht::require(threw && selector.statistics().probes == 0, "throw does not observe incomplete work");
    selector.run(1, [&](auto hint, auto, auto) {
        ht::require(decltype(hint)::value == ht::lookup_hint::predictable, "throw retains phase");
    }, clock);
    std::cout << "PASS: useful-work cost reversal, predictable bias, ordered tails, clock scope and exceptions\n";
}
