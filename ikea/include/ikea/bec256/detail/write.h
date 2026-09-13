#pragma once
#include <ikea/bec256/codec.h>
#include <ikea/bec256/detail/native/emit.h>
namespace ikea::bec256::detail {
// Immediate admission stays visible to the operation body: no call boundary
// spills native groups merely to check byte/control geometry. The deferred
// encoding API still exposes a compiled cold admission entry.
[[gnu::always_inline]] inline bool overlaps_control(const void *a, std::size_t a_size,
                                                    const void *b, std::size_t b_size) noexcept {
    if (!a_size || !b_size)
        return false;
    const auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
    return x <= y ? y - x < a_size : x - y < b_size;
}
[[gnu::always_inline]] inline std::expected<void, error>
admit_write(const destination &target, std::size_t offset, unsigned bytes_,
            const source_write_journal &effects, const void *protected_control,
            std::size_t protected_bytes) noexcept {
    if (offset > target.storage.size() || bytes_ > target.storage.size() - offset)
        return std::unexpected(error::capacity);
    if (!bytes_)
        return {};
    if (effects.used >= effects.storage.size())
        return std::unexpected(error::effects);
    auto *output = target.storage.data() + offset;
    if (overlaps_control(output, bytes_, protected_control, protected_bytes) ||
        overlaps_control(output, bytes_, &target, sizeof(target)) ||
        overlaps_control(output, bytes_, &effects, sizeof(effects)) ||
        overlaps_control(output, bytes_, effects.storage.data(), effects.storage.size_bytes()) ||
        overlaps_control(effects.storage.data(), effects.storage.size_bytes(), protected_control,
                         protected_bytes) ||
        overlaps_control(effects.storage.data(), effects.storage.size_bytes(), &target,
                         sizeof(target)) ||
        overlaps_control(effects.storage.data(), effects.storage.size_bytes(), &effects,
                         sizeof(effects)))
        return std::unexpected(error::overlap);
    return {};
}
struct checked_sink {
    destination &target;
    std::size_t offset;
    source_write_journal &effects;
    std::expected<unsigned, error> population_error() const noexcept {
        return std::unexpected(error::population);
    }
    std::expected<unsigned, error> empty() const noexcept {
        if (offset > target.storage.size())
            return std::unexpected(error::capacity);
        return 0;
    }
    [[gnu::always_inline]] std::expected<unsigned, error>
    operator()(const std::uint64_t *values, const std::uint64_t *widths) const noexcept {
        const auto n = encoded_bytes(widths);
        if (auto admitted = admit_write(target, offset, n, effects, nullptr, 0); !admitted)
            return std::unexpected(admitted.error());
        if (n) {
            effects.before(target, {0, offset, n});
            emit_exact(values, widths,
                       reinterpret_cast<std::uint8_t *>(target.storage.data()) + offset, n);
        }
        return n;
    }
    [[gnu::always_inline]] std::expected<unsigned, error>
    singleton(std::uint8_t value) const noexcept {
        if (auto admitted = admit_write(target, offset, 1, effects, nullptr, 0); !admitted)
            return std::unexpected(admitted.error());
        effects.before(target, {0, offset, 1});
        target.storage[offset] = byte{value};
        return 1;
    }
};
// The encoder owns population checking and the cheap width statistic. This
// adapter changes only the result vocabulary; admission/exact emission stays
// shared with unconditional writes, and is never reached on a decline.
struct selective_sink {
    checked_sink base;
    unsigned enum_bit_cutoff;
    using result = std::expected<std::optional<unsigned>, error>;
    result population_error() const noexcept { return std::unexpected(error::population); }
    result empty() const noexcept { return base.empty(); }
    result declined() const noexcept { return std::optional<unsigned>{}; }
    [[gnu::always_inline]] result operator()(const std::uint64_t *v,
                                             const std::uint64_t *n) const noexcept {
        return base(v, n);
    }
    [[gnu::always_inline]] result singleton(std::uint8_t value) const noexcept {
        return base.singleton(value);
    }
};
} // namespace ikea::bec256::detail
