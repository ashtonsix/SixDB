#pragma once

#include <cstddef>
#include <cstdint>

namespace seriespack_measurement {

// Fixed-shape Calico controls. n is a multiple of 256, values fit the bound
// width, and input/output are disjoint. Storage is exactly n*width/8 bytes,
// with no header or suffix. get16/set16's original index is a multiple of 16.
// Mutation uses Calico's trusted APIs and emits no effect summary.
struct prior_codec {
    void (*encode)(const std::uint64_t*, std::uint8_t*, std::size_t);
    void (*decode)(const std::uint8_t*, std::uint64_t*, std::size_t);
    std::uint64_t (*get)(const std::uint8_t*, std::size_t);
    void (*get16)(const std::uint8_t*, std::size_t, std::uint64_t*);
    void (*set)(std::uint8_t*, std::size_t, std::uint64_t);
    void (*set16)(std::uint8_t*, std::size_t, const std::uint64_t*);
};

// striped selects Calico's original interleaved residual map; false selects
// its bitplane residual map. This does not imply SeriesPack wire equivalence.
// Width selection occurs here, outside the measured bulk loop. Invalid widths
// produce an empty entry. The externally compiled BP_CPU chooses Calico tuning.
[[nodiscard]] prior_codec prior(unsigned width, bool striped) noexcept;
[[nodiscard]] const char* prior_target() noexcept;

// The independent residual-only control uses Calico Kernel directly, avoiding
// the planes API's u64 materialization adapter. Its valid widths are 1..7.
struct prior_u8_codec {
    void (*encode)(const std::uint8_t*, std::uint8_t*, std::size_t);
    void (*decode)(const std::uint8_t*, std::uint8_t*, std::size_t);
    std::uint8_t (*get)(const std::uint8_t*, std::size_t);
    void (*get16)(const std::uint8_t*, std::size_t, std::uint8_t*);
    void (*set)(std::uint8_t*, std::size_t, std::uint8_t);
    void (*set16)(std::uint8_t*, std::size_t, const std::uint8_t*);
};

[[nodiscard]] prior_u8_codec prior_u8(unsigned width, bool striped) noexcept;

} // namespace seriespack_measurement
