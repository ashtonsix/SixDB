#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ikea::intervals::detail {
template <class T> inline T load(const std::byte* p) noexcept {
    T value;
    std::memcpy(&value, p, sizeof(T));
    if constexpr (std::endian::native == std::endian::big && sizeof(T) > 1)
        value = std::byteswap(value);
    return value;
}
template <class T> inline void store(std::byte* p, T value) noexcept {
    if constexpr (std::endian::native == std::endian::big && sizeof(T) > 1)
        value = std::byteswap(value);
    std::memcpy(p, &value, sizeof(T));
}
inline bool overlaps(const void* a, std::size_t an, const void* b, std::size_t bn) noexcept {
    if (!an || !bn)
        return false;
    const auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
    return x <= y ? y - x < an : x - y < bn;
}
} // namespace ikea::intervals::detail
