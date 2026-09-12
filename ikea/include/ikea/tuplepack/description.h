#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace ikea::tuplepack {
using byte = std::uint8_t;
inline constexpr byte hole = 255;
struct code {
    byte offset, shift, width;
    bool operator==(const code&) const = default;
};
enum class error { description, map, duplicate, value, range, stride, capacity, overflow, overlap };
std::string_view describe(error) noexcept;

/// Owned physical description. Ranks are code indices, with no field/type
/// semantics. Bit positions are little-endian within each byte on every ISA.
class layout {
    std::array<code, 128> codes_{};
    unsigned bytes_ = 0, count_ = 0;
    layout() = default;

  public:
    [[nodiscard]] static std::expected<layout, error> make(unsigned bytes,
                                                           std::span<const code> codes);
    unsigned bytes() const noexcept {
        return bytes_;
    }
    std::span<const code> codes() const noexcept {
        return {codes_.data(), count_};
    }
    bool operator==(const layout&) const = default;
    /// Portable v1 encoding: 'TP', version 1, unit bytes, LE16 code count,
    /// then rank-ordered (byte offset, bit shift, width) triples. No native controls.
    std::size_t encoded_size() const noexcept {
        return 6 + count_ * 3;
    }
    [[nodiscard]] std::expected<std::size_t, error> encode(std::span<byte>) const;
    /// Requires exactly one complete description; rejects trailing/unknown data.
    [[nodiscard]] static std::expected<layout, error> decode(std::span<const byte>);
};
} // namespace ikea::tuplepack
