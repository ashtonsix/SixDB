#pragma once
#include <cstdint>

namespace ikea::integers::wide56 {
inline constexpr unsigned packet_values=8;
inline constexpr unsigned packet_bytes=56;
inline constexpr unsigned block_values=256;
inline constexpr unsigned block_bytes=block_values*7;
inline constexpr std::uint64_t value_mask=0x00ffffffffffffffULL;
}

// Trusted outer endpoints: exactly 256 width-valid u64 values and 1,792
// packed bytes, with disjoint buffers. Packed bytes need no alignment or
// padding. Point indices are below 256; get16 indices are also multiples of 16.
// Eight consecutive values occupy one exact 56-byte little-endian AoS packet.
extern "C" {
std::uint64_t wide56_local_get1(const std::uint8_t* packed,unsigned index) noexcept;
void wide56_local_get16(const std::uint8_t* packed,unsigned index,std::uint64_t* values) noexcept;
void wide56_local_encode256(const std::uint64_t* values,std::uint8_t* packed) noexcept;
void wide56_local_decode256(const std::uint8_t* packed,std::uint64_t* values) noexcept;
std::uint64_t wide56_local_sum256(const std::uint8_t* packed) noexcept;
}
