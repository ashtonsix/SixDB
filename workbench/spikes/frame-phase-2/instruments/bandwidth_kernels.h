#pragma once
#include <cstddef>
#include <cstdint>

namespace frame_instruments {
using word = std::uintptr_t;
using chase_fn = void (*)(word*, std::uint64_t);
std::uint64_t scan_once(const std::uint64_t* values, std::size_t count);
chase_fn chase_for(unsigned chains);
} // namespace frame_instruments
