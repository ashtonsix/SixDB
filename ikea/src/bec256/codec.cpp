#include <ikea/bec256/author/write.h>
#include <ikea/bec256/detail/scalar.h>
#include <ikea/bec256/detail/write.h>
#include <algorithm>
#include <bit>
#include <cstring>

namespace ikea::bec256 {
void decode(const source &input, std::span<byte, 32> output) noexcept {
    if (input.population() == 0 || input.population() == 256) {
        std::memset(output.data(), input.population() ? 255 : 0, 32);
        return;
    }
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
    native::store(output.data(), native::read(input));
#else
    // The scalar fallback reads exact fields, so it needs no suffix.
    plain_block values;
    detail::decode_scalar(reinterpret_cast<const std::uint8_t *>(input.storage().data()),
                          input.population(), reinterpret_cast<std::uint8_t *>(values.data()));
    std::memcpy(output.data(), values.data(), 32);
#endif
}
std::expected<encoding, error> prepare(std::span<const byte, 32> input,
                                       unsigned population) noexcept {
    unsigned actual = 0;
    for (auto value : input)
        actual += std::popcount(std::to_integer<unsigned char>(value));
    if (population != actual)
        return std::unexpected(error::population);
    encoding result;
    if (population == 0 || population == 256)
        return result;
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
    result.bytes_ =
        native::encode_unchecked(native::load(input.data()), population, result.data_.data());
#else
    result.bytes_ =
        detail::encode_scalar(reinterpret_cast<const std::uint8_t *>(input.data()), population,
                              reinterpret_cast<std::uint8_t *>(result.data_.data()));
#endif
    return result;
}
void decode_pair(const source &a, const source &b, std::span<byte, 64> output) noexcept {
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
    native::store_pair(output.data(), native::read_pair(a, b));
#else
    std::array<byte, 64> values;
    decode(a, std::span<byte, 32>(values.data(), 32));
    decode(b, std::span<byte, 32>(values.data() + 32, 32));
    std::memcpy(output.data(), values.data(), 64);
#endif
}
std::expected<void, error>
encoding::admit_write(const destination &target, std::size_t offset,
                      const source_write_journal &effects) const noexcept {
    return detail::admit_write(target, offset, bytes_, effects, this, sizeof(*this));
}
std::expected<void, error> encoding::write(destination &target, std::size_t offset,
                                           source_write_journal &effects) const noexcept {
    if (auto admitted = admit_write(target, offset, effects); !admitted)
        return admitted;
    write_unchecked(target, offset, effects);
    return {};
}
std::expected<unsigned, error> encode(std::span<const byte, 32> input, unsigned population,
                                      destination &target, std::size_t offset,
                                      source_write_journal &effects) noexcept {
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
    return native::encode(native::load(input.data()), population, target, offset, effects);
#else
    auto encoded = prepare(input, population);
    if (!encoded)
        return std::unexpected(encoded.error());
    if (auto written = encoded->write(target, offset, effects); !written)
        return std::unexpected(written.error());
    return encoded->bytes();
#endif
}
std::expected<std::optional<unsigned>, error>
encode_if_promising(std::span<const byte, 32> input, unsigned population, unsigned enum_bit_cutoff,
                    destination &target, std::size_t offset,
                    source_write_journal &effects) noexcept {
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
    return native::encode_if_promising(native::load(input.data()), population, enum_bit_cutoff,
                                       target, offset, effects);
#else
    unsigned actual = 0, cost = 0;
    for (auto b : input) {
        const auto p = std::popcount(std::to_integer<unsigned>(b));
        actual += p;
        cost += detail::byte_width[p];
    }
    if (actual != population)
        return std::unexpected(error::population);
    if (population && population != 256 && cost >= enum_bit_cutoff)
        return std::optional<unsigned>{};
    return encode(input, population, target, offset, effects);
#endif
}
} // namespace ikea::bec256
