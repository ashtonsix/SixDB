#pragma once
#include <ikea/bec256/codec.h>

namespace ikea::bec256 {
/// Approximate headless body bytes, rounded/clamped to 0..47; empty/full return
/// zero. Uses a fixed statistical model, not a bound or exact allocation size.
/// Reads exactly 32 plain bytes during this call; retains nothing and emits no
/// effects. Estimates across related blocks can have correlated errors.
/// The estimate excludes all caller-owned population, length and directory data.
[[nodiscard]] unsigned estimate_bytes(std::span<const byte, 32> input) noexcept;

/// Exact sum of byte-enumeration field widths, in bits (0..224). Excludes the
/// population tree, so it is neither total encoded size nor an incompressibility
/// test. Useful to calibrate encode_if_promising()'s caller-selected cutoff.
[[nodiscard]] unsigned enum_bits(std::span<const byte, 32> input) noexcept;
} // namespace ikea::bec256
