#pragma once
#include <ikea/bitset256.h>
#include <ikea/comparison.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ikea::frame {
using u128 = unsigned __int128;
inline constexpr unsigned block_rows = 256, group_blocks = 16, unit_blocks = 256;
inline constexpr std::size_t unit_rows = std::size_t{block_rows} * unit_blocks;
/// Failures precede result writes. Allocation failure has no published effects.
enum class error { range, description, contents, unsupported, unavailable, capacity, resources, authority };
std::string_view describe(error) noexcept;
/// Author law coordinates: [block] contains original rows within that block.
using block_rows_set = std::array<bitset256, unit_blocks>;
using block_args = std::array<std::uint64_t, unit_blocks>;
/// Empty selections have count=0, sum=0, min=UINT64_MAX, max=0.
struct summary {
    u128 sum{};
    std::uint64_t count{}, min{~std::uint64_t{0}}, max{};
};
} // namespace ikea::frame
