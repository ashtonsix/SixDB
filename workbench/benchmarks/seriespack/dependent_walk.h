#pragma once

#include <cstddef>
#include <cstdint>

namespace seriespack_measurement {
// Benchmark state only. The nonce advances across batches and fixture reuse;
// unsigned wrap is intentional. Identical initial state and returned values
// define one provider-independent query prefix. Time calibration can consume
// different prefix lengths for different providers.
struct dependent_walk {
    std::size_t index = 0;
    std::uint64_t nonce = 0;

    void advance(std::uint64_t value, std::uint64_t seed, std::size_t count) noexcept {
        // A full-width Weyl contribution avoids concentrating mixer inputs
        // in roughly [0,count) when both index and decoded values are small.
        auto next = std::uint64_t(index) + value + seed + (++nonce) * 0x9e3779b97f4a7c15ULL;
        next ^= next >> 30; next *= 0xbf58476d1ce4e5b9ULL;
        next ^= next >> 27; next *= 0x94d049bb133111ebULL;
        index = (next ^ (next >> 31)) % count;
    }
};
} // namespace seriespack_measurement
