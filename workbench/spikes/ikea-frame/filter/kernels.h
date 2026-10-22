// Private Frame experiment over maintained SeriesPack planes and filters.
#pragma once
#include <ikea/comparison.h>
#include <cstddef>
#include <cstdint>
#include <span>

namespace frame_filter {
using u64 = std::uint64_t;
struct planes {
    const std::uint8_t *payload{}, *head0{}, *head1{};
    std::size_t count{};
};
struct operations {
    void (*pack)(std::span<const u64>, std::uint8_t *, std::uint8_t *, std::uint8_t *);
    u64 (*filter)(planes, std::size_t, unsigned, u64, ikea::comparison, bool);
    // Complete admitted plane; optional u8 run ends-minus-one weight its ranks.
    u64 (*count_ge)(planes, u64, bool, const std::uint8_t *);
};
// heads is 0, 8 or 16 and cannot exceed width. No per-block erased decoder.
const operations &kernels(unsigned width, unsigned heads);
} // namespace frame_filter
