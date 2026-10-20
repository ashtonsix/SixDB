#include "batch.h"
#include <iostream>
#include <random>
#include <vector>
namespace ht = ikea::hashtable;

template <unsigned Groups> void check() {
    std::mt19937_64 rng(491 + Groups);
    for (unsigned count : {Groups, 8u, 16u, 64u}) {
        std::vector<ht::byte> controls(count * 16);
        for (unsigned trial = 0; trial < 2000; ++trial) {
            const ht::byte tag = trial % 3 ? ht::byte(2 + rng() % 254) : 255;
            for (auto& code : controls) {
                const auto r = rng() % 16;
                code = r < 4 ? tag : r < 6 ? ht::byte(r - 4) : ht::byte(2 + rng() % 254);
            }
            const std::size_t first = trial % 2 ? count - 1 : rng() % count;
            const std::size_t step = trial % count;
            const auto packet = ht::batch::scan<Groups, true>(controls.data(), count, first, step, tag);
            // Byte-wise independent oracle: decide termination only after every
            // lane in the current logical group has been considered.
            std::vector<std::size_t> expected, actual;
            ht::word free = 0;
            bool terminated = false;
            auto group = first, next_step = step;
            for (unsigned i = 0; i < Groups; ++i) {
                ht::require(packet.groups[i] == group, "triangular group order");
                bool empty = false;
                for (unsigned lane = 0; lane < 16; ++lane) {
                    const auto slot = group * 16 + lane;
                    const auto code = controls[slot];
                    if (!terminated) {
                        if (code == tag) expected.push_back(slot);
                        if (code < 2) free |= ht::word{1} << (i * 16 + lane);
                    }
                    empty |= code == 0;
                }
                terminated |= empty;
                group = (group + (++next_step)) % count;
            }
            for (auto mask = packet.candidates; mask; mask &= mask - 1)
                actual.push_back(packet.slot(std::countr_zero(mask)));
            ht::require(actual == expected, "exact tuple-check order and prefix");
            ht::require(packet.vacancies == free, "vacancy prefix");
            ht::require(bool(packet.empties) == terminated, "termination");
            ht::require(packet.next_group == group && packet.next_step == next_step, "resume state");
        }
    }
    // Every candidate after an empty lane in the SAME group stays eligible;
    // every candidate in a LATER logical group is suppressed, including bit 63.
    std::vector<ht::byte> controls(Groups * 16, 0xa6);
    controls[0] = 0;
    const auto p = ht::batch::scan<Groups>(controls.data(), Groups, 0, 0, 0xa6);
    ht::require(p.candidates == 0xfffe, "whole terminating group retained");
    std::cout << Groups * 16 << " bytes: ordered masks, wrap, vacancies and tuple traces passed\n";
}
template <unsigned Bytes> void check_line() {
    std::mt19937_64 rng(913 + Bytes);
    for (unsigned count : {4u, 8u, 64u}) {
        std::vector<ht::byte> storage(count * 16 + 64);
        auto* controls = reinterpret_cast<ht::byte*>((reinterpret_cast<std::uintptr_t>(storage.data()) + 63) & ~std::uintptr_t{63});
        for (unsigned trial = 0; trial < 4000; ++trial) {
            for (unsigned i = 0; i < count * 16; ++i) {
                const unsigned r = rng() % 16;
                controls[i] = r < 4 ? 0xa6 : r < 6 ? ht::byte(r - 4) : 0x38;
            }
            const auto first = trial % count;
            const auto packet = ht::batch::first_line<Bytes, true>(controls, count, first, 0xa6);
            auto group = first;
            std::size_t step = 0;
            bool terminated = false;
            std::vector<std::size_t> expected, actual;
            ht::word free = 0;
            for (;;) {
                bool empty = false;
                for (unsigned lane = 0; lane < 16; ++lane) {
                    const auto slot = group * 16 + lane;
                    const auto code = controls[slot];
                    if (!terminated) {
                        if (code == 0xa6) expected.push_back(slot);
                        if (code < 2) free |= ht::word{1} << (slot - packet.base_group * 16);
                    }
                    empty |= code == 0;
                }
                terminated |= empty;
                const auto next = (group + (++step)) % count;
                if (next <= group || next < packet.base_group ||
                    (next - packet.base_group) * 16 >= Bytes) {
                    group = next;
                    break;
                }
                group = next;
            }
            for (auto mask = packet.candidates; mask; mask &= mask - 1)
                actual.push_back(packet.slot(std::countr_zero(mask)));
            ht::require(actual == expected && packet.vacancies == free, "bounded candidate/vacancy prefix");
            ht::require(packet.next_group == group && packet.next_step == step, "bounded continuation");
            ht::require(bool(packet.empties) == terminated, "bounded empty selection");
            const auto first_line = reinterpret_cast<std::uintptr_t>(controls + first * 16) / 64;
            ht::require(reinterpret_cast<std::uintptr_t>(controls + packet.base_group * 16) / 64 == first_line &&
                        reinterpret_cast<std::uintptr_t>(controls + packet.base_group * 16 + Bytes - 1) / 64 == first_line,
                        "no additional control cache line");
        }
    }
    alignas(64) ht::byte controls[64];
    std::fill(std::begin(controls), std::end(controls), 0x38);
    controls[32] = 0; controls[63] = 0xa6;
    const auto p = ht::batch::first_line<Bytes>(controls, 4, Bytes == 64 ? 0 : 3, 0xa6);
    ht::require(p.candidates == (ht::word{1} << (Bytes - 1)) && !p.empties,
                "empty in an unvisited physical group must be ignored");
    std::cout << Bytes << " bytes: bounded first-line masks and continuation passed\n";
}
int main() { check<2>(); check<4>(); check_line<32>(); check_line<64>(); }
