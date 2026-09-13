#include <ikea/bec256/author/analysis.h>

namespace ikea::bec256 {
unsigned estimate_bytes(std::span<const byte, 32> input) noexcept {
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
    return native::estimate_bytes(native::load(input.data()));
#else
    return detail::estimate(
        detail::features_scalar(reinterpret_cast<const std::uint8_t *>(input.data())));
#endif
}
unsigned enum_bits(std::span<const byte, 32> input) noexcept {
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
    return native::enum_bits(native::load(input.data()));
#else
    unsigned cost = 0;
    for (auto b : input)
        cost += detail::byte_width[std::popcount(std::to_integer<unsigned>(b))];
    return cost;
#endif
}
} // namespace ikea::bec256
