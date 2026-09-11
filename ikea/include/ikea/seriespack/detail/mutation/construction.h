#pragma once
#include <ikea/seriespack/detail/mutation/fields.h>
#include <ikea/seriespack/detail/mutation/physical.h>

namespace ikea::seriespack {
/// The complete physical fields of a new array, including final-tile slack.
/// Interleaved owners' stride gaps are excluded. Construction writes this union.
template <class F, class Emit>
void visit_storage(const view<F, std::uint8_t>& destination, Emit&& emit) {
    const auto count = destination.size();
    if (!count)
        return;
    const auto tiles = count / F::tile_rows + (count % F::tile_rows != 0);
    detail::tile_fields<F, 15>([&](unsigned p, std::size_t offset, std::size_t bytes) {
        const auto stride = destination.stream(p).stride;
        if (stride == bytes)
            emit(byte_write{p, offset, tiles * bytes});
        else
            for (std::size_t t = 0; t < tiles; ++t)
                emit(byte_write{p, t * stride + offset, bytes});
    });
}

#if defined(__aarch64__) || defined(__AVX2__)
/// Fresh/reinitialized storage, disjoint admitted inputs, no prior-value law.
/// Padding becomes zero; complete native regions write once. Coverage does not
/// choose isolation, allocation, page COW or publication for the new array.
template <class F, class U, class Coverage>
void initialize_unchecked(view<F, std::uint8_t> destination, const U* input, Coverage& coverage) {
    const auto count = destination.size();
    if (!count)
        return;
    visit_storage(destination, [&](byte_write bytes) { coverage.before(bytes); });
    // Partial striped tiles share residual bytes between native regions. A
    // final Local octet can also fall through the scalar edge writer. Initialize
    // only that last tile before any preserving edge/fragment stores can read it.
    if (count % F::tile_rows || (F::tile_rows == 8 && count % 16)) {
        const auto last = (count - 1) / F::tile_rows;
        detail::tile_fields<F, 15>([&](unsigned p, std::size_t offset, std::size_t bytes) {
            const auto& plane = destination.stream(p);
            std::memset(plane.bytes.data() + last * plane.stride + offset, 0, bytes);
        });
    }
    no_summary summary;
    no_coverage recorded;
    replace_unchecked(
        destination, 0, count, input, [](std::size_t) { return std::uint16_t{0xffff}; }, summary,
        recorded);
}

template <class F, class U>
std::expected<void, error> initialize(const view<F, std::uint8_t>& destination,
                                      std::span<const U> input, write_journal& effects) {
    if (input.size() != destination.size())
        return std::unexpected(error::range);
    if constexpr (F::width < sizeof(U) * 8)
        for (const auto value : input)
            if (value >> F::width)
                return std::unexpected(error::value);
    std::size_t needed = 0;
    visit_storage(destination, [&](byte_write) { ++needed; });
    if (effects.used > effects.storage.size() || needed > effects.storage.size() - effects.used)
        return std::unexpected(error::capacity);
    initialize_unchecked(destination, input.data(), effects);
    return {};
}
#endif
} // namespace ikea::seriespack
