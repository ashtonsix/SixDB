#pragma once
#include <ikea/frame/description.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace ikea::frame::detail {
enum class image_error { geometry, truncated, descriptor, membership, leaf, contents, trailing };
using image_preset = pack_preset;
struct array_section {
    std::size_t offset{}, size{}, rows{};
};
struct group_manifest {
    std::array<std::uint64_t, 16> lo{};
    std::array<std::uint8_t, 16> width{}, slot{};
    std::array<std::uint16_t, 16> rows{}, start{}, count{};
    std::array<array_section, 65> arrays{};
    unsigned blocks{}, capacity{}, used{};
    std::size_t positions{}, values{}, leaf_offset{};
};
/// Admission over exactly one complete immutable group image. This checks the
/// parent/leaf relationship, not just the validity of each leaf independently.
/// No source byte is read until its enclosing range has been checked. Absent
/// groups need neither an address nor a readable halo.
[[nodiscard]] std::expected<group_manifest, image_error> validate_group(
    std::span<const std::byte>, image_preset, bool bounded, std::size_t part_rows, unsigned group);
} // namespace ikea::frame::detail
