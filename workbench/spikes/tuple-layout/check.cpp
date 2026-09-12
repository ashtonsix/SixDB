#include "edge.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace tuple_probe;
int main() {
    unsigned cases = 0;
    for (unsigned layout = 0; layout < 6; ++layout) {
        for (unsigned selected = 1; selected < 8; ++selected) {
            const auto m = map(selected);
            const auto operation = bind(layout, selected);
            // Exhaust all 256 old byte patterns and all 256 new logical triples.
            // Surrounding bytes guard the actual store span; the oracle checks
            // unselected bits within every shared byte as well.
            for (unsigned old = 0; old < 256; ++old) {
                std::array<byte, 48> actual, expected;
                actual.fill(0xa5);
                for (unsigned lane = 0; lane < 16; ++lane)
                    actual[16 + lane] = byte(old + lane);
                const auto read_expected = reference_read(actual.data() + 16, layouts[layout], m);
                if (bytes(operation.read(actual.data() + 16)) != read_expected) {
                    std::fprintf(stderr, "read layout=%s selected=%u old=%u\n", names[layout], selected, old);
                    return 1;
                }
                const auto baseline = actual;
                for (unsigned replacement = 0; replacement < 256; ++replacement) {
                    std::array<byte, 64> input{};
                    for (unsigned lane = 0; lane < 16; ++lane) {
                        const unsigned value = (replacement + lane) & 255;
                        input[lane] = value & 1;
                        input[16 + lane] = (value >> 1) & 15;
                        input[32 + lane] = value >> 5;
                    }
                    actual = expected = baseline;
                    reference_write(expected.data() + 16, layouts[layout], m, input);
                    operation.write(actual.data() + 16, load_packet(input.data()));
                    if (actual != expected) {
                        std::fprintf(stderr, "write layout=%s selected=%u old=%u new=%u\n",
                                     names[layout], selected, old, replacement);
                        return 1;
                    }
                    ++cases;
                }
            }
        }
    }
    std::printf("42 bindings; 10752 reads; %u partial/full writes; byte and neighbor checks passed\n", cases);
}
