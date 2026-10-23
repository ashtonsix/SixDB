#pragma once
#include <array>
#include <cstdint>
#include <type_traits>

namespace ikea::tuplepack {
/// Decoded unsigned code values, independent of storage bytes and ISA carriers.
/// Eight slots use a little-positioned integer; wider packets use numeric arrays.
template <unsigned N>
using packet = std::conditional_t<N == 8, std::uint64_t, std::array<std::uint8_t, N>>;
}
