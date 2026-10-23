#pragma once
#include <ikea/bytes.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ikea::streampack {
using ikea::byte;
using ikea::bytes;
inline constexpr unsigned slot_capacity = 256;
/// StreamPack command, placement or image failure. Retirement is a value-owner
/// condition; borrowed Intervals search does not own this state or error domain.
enum class error { description, capacity, alignment, overlap, contents, retired, value, effects, range };
/// Cold diagnostic text for this module's typed errors.
std::string_view describe(error) noexcept;
enum class geometry : std::uint8_t {
    absolute_ends,
    sizes,
    relative_ends
};
} // namespace ikea::streampack
