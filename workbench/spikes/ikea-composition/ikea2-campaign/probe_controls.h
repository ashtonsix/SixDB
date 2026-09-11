#pragma once
#include <cstddef>
#include <cstdint>

namespace seriespack_measurement {
// Immediate LocalPack/ScanPack controls, using their admitted width-valid u8
// source contract. Full arrays contain a multiple of 256 values; no suffix.
struct predecessor_codec {
    void (*encode)(const std::uint8_t*, std::uint8_t*, std::size_t);
    void (*decode)(const std::uint8_t*, std::uint8_t*, std::size_t);
    std::uint64_t (*get)(const std::uint8_t*, std::size_t);
    void (*get16)(const std::uint8_t*, std::size_t, std::uint64_t*);
};
// Point/group indices name the original full array. get16 starts at an index
// divisible by16 and writes exactly16 u64 values. Narrow get16 retains the
// original native byte read, followed by an inline u8-to-u64 widening bridge;
// its cost is part of this materializing control. ScanPack uses the original
// default ScanReader::fragment_classes lowering.
[[nodiscard]] predecessor_codec predecessor(unsigned width, bool striped) noexcept;
struct predecessor_u64_codec {
    void (*encode)(const std::uint64_t*, std::uint8_t*, std::size_t);
    void (*decode)(const std::uint8_t*, std::uint64_t*, std::size_t);
    std::uint64_t (*get)(const std::uint8_t*, std::size_t);
    void (*get16)(const std::uint8_t*, std::size_t, std::uint64_t*);
};
// The wider-body probe implemented width56. Both its eight-value packet and
// optional AVX-512 32-value encoder region are retained as named controls.
[[nodiscard]] predecessor_u64_codec predecessor56(bool region32 = false) noexcept;
[[nodiscard]] const char* predecessor_target() noexcept;
}
