#pragma once
#include <ikea/bytes.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace ikea::tuplepack {
inline constexpr std::uint8_t hole = 255;
struct code {
    std::uint8_t offset, shift, width;
    bool operator==(const code&) const = default;
};
enum class error { description, map, duplicate, value, range, stride, capacity, effects, overflow, overlap };
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
    /// Shared 'TP' law version 2 framing, then unit bytes and rank-ordered
    /// (byte offset, bit shift, width) triples. No placement or native controls.
    std::size_t encoded_size() const noexcept {
        return 6 + count_ * 3;
    }
    [[nodiscard]] std::expected<std::size_t, error> encode(ikea::mutable_bytes) const;
    /// Requires exactly one complete description; rejects trailing/unknown data.
    [[nodiscard]] static std::expected<layout, error> decode(ikea::bytes);
};
} // namespace ikea::tuplepack
