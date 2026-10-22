#pragma once
#include <algorithm>
#include <cassert>
#include <numeric>
#include <random>
#include <vector>

namespace frame_measure {
// A complete round balances position and every within-trial predecessor pair.
// Relabel arms and shuffle trials independently each round. Odd populations
// need the reversed square too. Request/data RNGs remain caller-owned.
inline std::vector<std::vector<unsigned>> balanced_round(unsigned arms, std::mt19937& random) {
    assert(arms);
    std::vector<unsigned> labels(arms), first(arms);
    std::iota(labels.begin(), labels.end(), 0);
    std::shuffle(labels.begin(), labels.end(), random);
    for (unsigned i = 1; i < arms; ++i) first[i] = i % 2 ? (i + 1) / 2 : arms - i / 2;
    std::vector<std::vector<unsigned>> out;
    for (unsigned shift = 0; shift < arms; ++shift) {
        std::vector<unsigned> row(arms);
        for (unsigned i = 0; i < arms; ++i) row[i] = labels[(first[i] + shift) % arms];
        out.push_back(row);
        if (arms % 2) { std::reverse(row.begin(), row.end()); out.push_back(std::move(row)); }
    }
    std::shuffle(out.begin(), out.end(), random);
    return out;
}
} // namespace frame_measure
