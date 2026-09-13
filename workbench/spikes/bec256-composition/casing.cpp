#include "casing.h"
#include <bit>

namespace bec_study {
[[gnu::noinline]] std::expected<unsigned, bc::error>
staged_encode(std::span<const bc::byte, 32> input, unsigned population, bc::destination &target,
              std::size_t offset, ikea::source_write_journal &effects) {
    auto encoded = bc::prepare(input, population);
    if (!encoded)
        return std::unexpected(encoded.error());
    if (auto written = encoded->write(target, offset, effects); !written)
        return std::unexpected(written.error());
    return encoded->bytes();
}
std::expected<bound_encoder, bc::error> bound_encoder::bind(bc::destination &target,
                                                            ikea::source_write_journal &effects) {
    // The prototype reuses the production alias checker over the entire plane.
    // Its unsigned footprint type bounds this experiment to <4 GiB planes.
    if (target.storage.size() > UINT32_MAX)
        return std::unexpected(bc::error::capacity);
    if (auto admitted =
            bc::detail::admit_write(target, 0, target.storage.size(), effects, nullptr, 0);
        !admitted)
        return std::unexpected(admitted.error());
    return bound_encoder(target, effects);
}
namespace {
struct bound_sink {
    bc::destination &target;
    ikea::source_write_journal &effects;
    std::size_t offset;
    std::expected<unsigned, bc::error> population_error() const {
        return std::unexpected(bc::error::population);
    }
    std::expected<unsigned, bc::error> empty() const {
        if (offset > target.storage.size())
            return std::unexpected(bc::error::capacity);
        return 0;
    }
    std::expected<void, bc::error> admit(unsigned n) const {
        if (offset > target.storage.size() || n > target.storage.size() - offset)
            return std::unexpected(bc::error::capacity);
        if (n && effects.used >= effects.storage.size())
            return std::unexpected(bc::error::effects);
        return {};
    }
    std::expected<unsigned, bc::error> operator()(const std::uint64_t *values,
                                                  const std::uint64_t *widths) const {
        const auto n = bc::detail::encoded_bytes(widths);
        if (auto proof = admit(n); !proof)
            return std::unexpected(proof.error());
        if (n) {
            effects.before(target, {0, offset, n});
            bc::detail::emit_exact(values, widths,
                                   reinterpret_cast<std::uint8_t *>(target.storage.data()) + offset,
                                   n);
        }
        return n;
    }
    std::expected<unsigned, bc::error> singleton(std::uint8_t value) const {
        if (auto proof = admit(1); !proof)
            return std::unexpected(proof.error());
        effects.before(target, {0, offset, 1});
        target.storage[offset] = bc::byte{value};
        return 1;
    }
};
} // namespace
[[gnu::noinline]] std::expected<unsigned, bc::error>
bound_encoder::encode(std::span<const bc::byte, 32> input, unsigned population,
                      std::size_t offset) const {
#if defined(IKEA_BEC256_AVX512)
    return bc::detail::avx512::encode_to(bc::native::load(input.data()), population,
                                         bound_sink{target_, effects_, offset});
#elif defined(__aarch64__)
    return bc::detail::neon::encode_to(bc::native::load(input.data()), population,
                                       bound_sink{target_, effects_, offset});
#else
    return staged_encode(input, population, target_, offset, effects_);
#endif
}
} // namespace bec_study
