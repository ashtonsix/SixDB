// Runtime-width SeriesPack packing for the private Frame material. Its 64
// width instantiations compile in packing.cpp alone, so encoding policy edits
// in material.cpp don't recompile the width matrix.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace frame_encoding {
inline std::size_t plane_bytes(unsigned k, std::size_t count) {
    return (count + 7) / 8 * k;  // Local geometry: 8-row tiles of k bytes
}

// Encode values (already relative to the base) as a Local SeriesPack plane of
// runtime width k, through the real typed mutation path.
void pack(unsigned k, std::span<const std::uint64_t> values, std::uint8_t *out);
}  // namespace frame_encoding
