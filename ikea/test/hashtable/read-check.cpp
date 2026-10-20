#include <ikea/hashtable/detail/read-batch.h>
#include <iostream>
#include <random>
#include <vector>

namespace ht = ikea::hashtable;

template <unsigned Groups> void check_masks() {
    std::mt19937_64 rng(871 + Groups);
    for (unsigned count : {Groups, 8u, 64u}) {
        for (unsigned offset : {0u, 1u, 3u, 15u}) {
            std::vector<ht::byte> backing(count * 16 + offset);
            auto* controls = backing.data() + offset;
            for (unsigned trial = 0; trial < 1200; ++trial) {
                const ht::byte tag = ht::byte(2 + rng() % 254);
                for (unsigned i = 0; i < count * 16; ++i) {
                    const auto kind = rng() % 16;
                    controls[i] = kind < 3 ? tag : kind < 5 ? ht::byte(kind - 3) : 0x38;
                }
                const std::size_t first = rng() % count, step = trial % count;
                const auto got =
                    ht::detail::compare_swiss_read<Groups>(controls, first, step, count - 1, tag);
                // Independently walk one whole logical group at a time.
                ht::word expected = 0;
                auto group = first, next_step = step;
                bool terminated = false;
                for (unsigned i = 0; i < Groups; ++i) {
                    bool empty = false;
                    for (unsigned lane = 0; lane < 16; ++lane) {
                        const auto code = controls[group * 16 + lane];
                        if (!terminated && code == tag) {
                            expected |= ht::word{1} << (i * 16 + lane);
                            ht::require(ht::detail::swiss_read_group(first, step, i, count - 1) ==
                                            group,
                                        "candidate slot reconstruction");
                            ht::require(
                                ht::detail::swiss_read_slot<Groups>(first, step, i * 16 + lane,
                                                                    count - 1) == group * 16 + lane,
                                "candidate slot address");
                        }
                        empty |= code == 0;
                    }
                    terminated |= empty;
                    group = (group + (++next_step)) % count;
                }
                ht::require(got.candidates == expected, "logical group candidate prefix");
                ht::require(bool(got.empties) == terminated, "logical group termination");
                ht::require(ht::detail::swiss_read_group(first, step, Groups, count - 1) == group,
                            "batched continuation");
            }
        }
    }
    // Candidates after an empty in the SAME group remain visible; candidates
    // after an empty GROUP must disappear. Include the highest packed bit.
    std::vector<ht::byte> controls(Groups * 16, 255);
    controls[0] = 0;
    auto got = ht::detail::compare_swiss_read<Groups>(controls.data(), 0, 0, Groups - 1, 255);
    ht::require(got.candidates == 0xfffe, "whole terminating group retained");
    controls[0] = 1;
    got = ht::detail::compare_swiss_read<Groups>(controls.data(), 0, 0, Groups - 1, 255);
    const auto all = Groups == 4 ? ~ht::word{0} : ht::word{0xffffffff};
    ht::require(got.candidates == (all & ~ht::word{1}) && !got.empties,
                "tombstones do not terminate; highest lane survives");
}

int main() {
    check_masks<2>();
    check_masks<4>();
    std::cout
        << "PASS: triangular read batches, wrap, unaligned backing, empty-group prefix and slots\n";
}
