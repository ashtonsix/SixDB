#pragma once
#include <ikea/bytes.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace ikea::hashtable {
using word = std::uint64_t;
/// Opaque value bytes. Numeric interpretation belongs to the enclosing owner.
template <std::size_t N> using bytes = std::array<std::byte, N>;
enum class error { range };
inline std::string_view describe(error value) noexcept {
    switch (value) {
    case error::range: return "storage range outside assigned extent";
    }
    return "unknown Hashtable error";
}
/// Native-endian object-byte helpers, not a portable persisted integer format.
template <unsigned N> inline word load(const std::byte* p) noexcept {
    static_assert(N <= 8);
    word value = 0;
    std::memcpy(&value, p, N);
    return value;
}
template <unsigned N> inline void store(std::byte* p, word value) noexcept {
    static_assert(N <= 8);
    std::memcpy(p, &value, N);
}
} // namespace ikea::hashtable
