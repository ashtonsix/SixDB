#include <ikea/bec256.h>
#include <ikea/bec256/author/analysis.h>
#include <ikea/bec256/author/write.h>
#include "reference.h"
#include "predictor_reference.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>

namespace bc = ikea::bec256;
void check_analysis() {
    std::mt19937_64 rng(0xbec512a);
    alignas(64) std::array<bc::byte, 64> inputs{};
    unsigned attempts = 0;
    for (unsigned shape = 0; shape < 4096; ++shape) {
        for (auto &b : inputs)
            b = bc::byte(rng());
        // Exercise every minority-piece boundary, both terminals, transitions
        // across word/vector boundaries, and the enum-cost-zero model piece.
        if (shape < 257) {
            inputs.fill(bc::byte{0});
            std::array<unsigned, 256> order;
            for (unsigned j = 0; j < 256; ++j)
                order[j] = j;
            std::shuffle(order.begin(), order.end(), rng);
            for (unsigned j = 0; j < shape; ++j)
                inputs[order[j] / 8] |= bc::byte(1u << (order[j] % 8));
            for (unsigned j = 0; j < 32; ++j)
                inputs[32 + j] = ~inputs[j];
        } else if (shape < 2048) {
            for (auto &b : inputs)
                b = bc::byte((rng() & 1) ? 255 : 0);
        }
        for (unsigned half = 0; half < 2; ++half) {
            const auto input = std::span<const bc::byte, 32>(inputs.data() + half * 32, 32);
            const auto features = bec_reference::predictor_features(input.data());
            const auto p = features[0], cost = features[1];
            assert(bc::enum_bits(input) == cost);
            assert(bc::estimate_bytes(input) == bec_reference::predictor(input.data()));
            std::array<bc::byte, 64> wire{};
            const auto bytes = bec_reference::encode(input.data(), wire.data());
            for (unsigned cutoff :
                 {0u, cost, cost + 1, 144u, 224u, 225u, std::numeric_limits<unsigned>::max()}) {
                std::array<bc::byte, 128> output;
                output.fill(bc::byte{0xa5});
                const auto before = output;
                bc::destination target{output};
                std::array<ikea::owner_write, 2> entries{};
                ikea::source_write_journal effects{entries};
                auto result = bc::encode_if_promising(input, p, cutoff, target, 19, effects);
                assert(result);
                const bool decline = p && p != 256 && cost >= cutoff;
                assert(bool(*result) == !decline);
                if (decline) {
                    assert(output == before && effects.used == 0);
                    // A full, pre-existing journal is untouched by a decline.
                    effects.before(target, {0, 100, 1});
                    effects.before(target, {0, 103, 1});
                    std::array<bc::byte, sizeof(entries)> full;
                    std::memcpy(full.data(), entries.data(), sizeof(entries));
                    auto skipped_full =
                        bc::encode_if_promising(input, p, cutoff, target, 19, effects);
                    assert(skipped_full && !*skipped_full && effects.used == 2 &&
                           output == before &&
                           !std::memcmp(entries.data(), full.data(), sizeof(entries)));
                    // Declines neither require nor reserve destination/effect capacity.
                    bc::destination none{{}};
                    ikea::source_write_journal no_effects{};
                    auto skipped = bc::encode_if_promising(input, p, cutoff, none, 999, no_effects);
                    assert(skipped && !*skipped && no_effects.used == 0);
                } else {
                    assert(**result == bytes && effects.used == unsigned(bytes != 0));
                    assert(std::equal(wire.begin(), wire.begin() + bytes, output.begin() + 19));
                    for (unsigned j = 0; j < output.size(); ++j)
                        if (j < 19 || j >= 19 + bytes)
                            assert(output[j] == bc::byte{0xa5});
                    if (bytes) {
                        assert(entries[0].source == &target && entries[0].bytes.offset == 19 &&
                               entries[0].bytes.size == bytes);
                    }
                }
                // Population checking precedes even an unconditional heuristic decline.
                const auto after = output;
                const auto used = effects.used;
                std::array<bc::byte, sizeof(entries)> journal_before;
                std::memcpy(journal_before.data(), entries.data(), sizeof(entries));
                auto invalid = bc::encode_if_promising(input, p + 1, 0, target, 19, effects);
                assert(!invalid && invalid.error() == bc::error::population);
                assert(output == after && effects.used == used &&
                       !std::memcmp(entries.data(), journal_before.data(), sizeof(entries)));
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
                effects.used = 0;
                output = before;
                auto native = bc::native::encode_if_promising(bc::native::load(input.data()), p,
                                                              cutoff, target, 19, effects);
                assert(native == result);
                if (decline)
                    assert(output == before && effects.used == 0);
                else
                    assert(std::equal(wire.begin(), wire.begin() + bytes, output.begin() + 19));
#endif
                ++attempts;
            }
            // Accepted path still rejects insufficient capacity/effects atomically.
            std::array<bc::byte, 64> output{};
            bc::destination none{{}};
            ikea::source_write_journal no_effects{};
            auto failed = bc::encode_if_promising(input, p, 225, none, 1, no_effects);
            assert(!failed && failed.error() == bc::error::capacity);
            if (bytes) {
                bc::destination target{output};
                failed = bc::encode_if_promising(input, p, 225, target, 0, no_effects);
                assert(!failed && failed.error() == bc::error::effects &&
                       std::all_of(output.begin(), output.end(),
                                   [](auto b) { return b == bc::byte{0}; }));
            }
        }
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
        const auto pair = bc::native::load_pair(inputs.data());
        const auto estimates = bc::native::estimate_bytes(pair);
        const auto costs = bc::native::enum_bits(pair);
        for (unsigned half = 0; half < 2; ++half) {
            assert(estimates[half] == bec_reference::predictor(inputs.data() + half * 32));
            assert(costs[half] == bec_reference::predictor_features(inputs.data() + half * 32)[1]);
        }
#endif
    }
    std::printf("Bec256: 8192 predictor cases and %u threshold cases passed\n",
                attempts);
}
