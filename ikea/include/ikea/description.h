#pragma once
#include <ikea/bytes.h>
#include <array>
#include <optional>

namespace ikea::description {
using tag = std::array<char, 2>;
inline constexpr std::size_t header_size = 5;
inline constexpr std::size_t maximum_body_size = 65535;

// Frames a layout in its parent's schema; never prepended to a data block.
// The module owns body validation and maps refusal to its own error domain.
inline bool write_header(mutable_bytes output, tag module, unsigned version,
                         std::size_t body_size) noexcept {
    if (version > 255 || body_size > maximum_body_size ||
        output.size() < header_size + body_size)
        return false;
    output[0] = byte(module[0]);
    output[1] = byte(module[1]);
    output[2] = byte(version);
    output[3] = byte(body_size & 255);
    output[4] = byte(body_size >> 8);
    return true;
}
inline std::optional<bytes> read_body(bytes input, tag module, unsigned version) noexcept {
    if (version > 255 || input.size() < header_size || input[0] != byte(module[0]) ||
        input[1] != byte(module[1]) || input[2] != byte(version))
        return std::nullopt;
    const auto size = std::to_integer<unsigned>(input[3]) |
                      (std::to_integer<unsigned>(input[4]) << 8);
    if (input.size() - header_size != size)
        return std::nullopt;
    return input.subspan(header_size);
}
} // namespace ikea::description
