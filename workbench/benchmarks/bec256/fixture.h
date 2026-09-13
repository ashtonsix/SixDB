#pragma once
#include <ikea/bec256.h>
#include <algorithm>
#include <bit>
#include <cassert>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace bec_bench {
namespace bc = ikea::bec256;
inline constexpr unsigned block_count = 256;
inline constexpr std::array<unsigned, 15> populations{1,   2,   4,   8,   16,  32,  64, 128,
                                                      192, 224, 240, 248, 252, 254, 255};
inline std::string shape_name(unsigned shape) {
    return shape < 3 ? std::array<std::string, 3>{"random", "runs", "terminal"}[shape]
                     : "population" + std::to_string(populations[shape - 3]);
}
inline unsigned population(const bc::plain_block &bits) {
    unsigned result = 0;
    for (auto value : bits)
        result += std::popcount(std::to_integer<unsigned>(value));
    return result;
}
struct fixture {
    std::array<bc::plain_block, block_count> plain{}, query{};
    std::array<std::array<bc::byte, 64>, block_count> encoded{};
    std::array<unsigned, block_count> pop{};
    std::vector<bc::source> exact, padded;
    double mean_bytes = 0;

    explicit fixture(unsigned shape) {
        std::mt19937_64 rng(0xbec256);
        exact.reserve(block_count);
        padded.reserve(block_count);
        for (unsigned i = 0; i < block_count; ++i) {
            for (unsigned j = 0; j < 32; ++j) {
                plain[i][j] = shape == 0   ? bc::byte(rng())
                              : shape == 1 ? bc::byte(j < i % 33 ? 255 : 0)
                                           : bc::byte(i % 2 ? 255 : 0);
                query[i][j] = bc::byte(rng());
            }
            if (shape >= 3) {
                std::array<unsigned, 256> order;
                std::iota(order.begin(), order.end(), 0);
                std::shuffle(order.begin(), order.end(), rng);
                plain[i].fill(bc::byte{0});
                for (unsigned j = 0; j < populations[shape - 3]; ++j)
                    plain[i][order[j] / 8] |= bc::byte(1u << (order[j] % 8));
            }
            pop[i] = population(plain[i]);
            auto candidate = bc::prepare(plain[i], pop[i]);
            assert(candidate);
            std::copy(candidate->body().begin(), candidate->body().end(), encoded[i].begin());
            auto a = bc::source::admit(std::span(encoded[i]).first(candidate->bytes()),
                                       candidate->bytes(), pop[i]);
            auto b = bc::source::admit(encoded[i], candidate->bytes(), pop[i]);
            assert(a && b);
            exact.push_back(*a);
            padded.push_back(*b);
            mean_bytes += candidate->bytes() / double(block_count);
        }
    }
    fixture(const fixture &) = delete; // Sources borrow the arrays above.
};
void register_composition_cases(unsigned shapes);
} // namespace bec_bench
