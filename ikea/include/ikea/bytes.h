#pragma once
#include <ikea/config.h>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace ikea {
using byte = std::byte;
using bytes = std::span<const byte>;
using mutable_bytes = std::span<byte>;

// Native byte kernels use the unsigned-character aliasing rule. A new target
// must provide this eight-bit mapping, or supply its own byte access bodies.
static_assert(CHAR_BIT == 8 && std::is_same_v<std::uint8_t, unsigned char>);
inline std::uint8_t* to_u8(byte* data) noexcept {
    return reinterpret_cast<std::uint8_t*>(data);
}
inline const std::uint8_t* to_u8(const byte* data) noexcept {
    return reinterpret_cast<const std::uint8_t*>(data);
}
template <std::size_t N>
inline std::span<std::uint8_t, N> to_u8(std::span<byte, N> data) noexcept {
    return std::span<std::uint8_t, N>(to_u8(data.data()), data.size());
}
template <std::size_t N>
inline std::span<const std::uint8_t, N> to_u8(std::span<const byte, N> data) noexcept {
    return std::span<const std::uint8_t, N>(to_u8(data.data()), data.size());
}
} // namespace ikea
