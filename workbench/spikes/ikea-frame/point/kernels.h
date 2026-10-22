#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace frame_points {
struct local_operations {
    std::uint64_t (*point)(const std::uint8_t *, std::size_t);
    void (*range)(const std::uint8_t *, std::size_t, std::size_t, std::uint64_t *);
};
// Private trusted Local-plane operations. The owner admits width1..64,
// complete occupied tiles, logical coordinates and retained source placement.
extern const std::array<local_operations, 65> local_catalogue;
inline const local_operations &local_kernels(unsigned width) { return local_catalogue[width]; }
inline constexpr std::size_t shared_kernel_bytes() { return sizeof(local_catalogue); }
} // namespace frame_points
