#pragma once
#include <ikea/seriespack/reader.h>

namespace ikea::seriespack::detail {
/// The compiled range operations of one dense headless layout, over the
/// resource an erased dense reader carries (its first byte). Each call binds
/// the dense reader over [0, first + count); no state outlives it.
template <class F> struct dense_operations {
    static auto bound(const void* data, std::size_t rows) {
        return reader<dense_source<F>>(dense_source<F>{static_cast<const ikea::byte*>(data), rows});
    }
    static std::uint64_t sum(const void* data, std::size_t first, std::size_t count) {
        return bound(data, first + count).sum_unchecked(first, count);
    }
    static std::uint64_t sum_masked(const void* data, std::size_t first, std::size_t count, const std::uint16_t* masks,
                                    std::size_t period) {
        const dense_source<F> source{static_cast<const ikea::byte*>(data), first + count};
        const std::size_t base = first / 16;
        return author::sum_regions(author::expression(source), first, count, author::all_rows{},
                                   [&](std::size_t origin) __attribute__((always_inline)) {
                                       const std::size_t k = origin / 16 - base;
                                       return masks[period ? k % period : k];
                                   });
    }
    template <comparison Op> static std::uint64_t count_as(const void* data, std::size_t first, std::size_t count, std::uint64_t rhs) {
        return bound(data, first + count).template count_unchecked<Op>(first, count, rhs);
    }
    template <comparison Op>
    static void filter_as(const void* data, std::size_t first, std::size_t count, std::uint64_t rhs, std::uint64_t* words) {
        bound(data, first + count).template filter_words_unchecked<Op>(first, count, rhs, words);
    }
    static std::uint64_t count(const void* data, comparison op, std::size_t first, std::size_t count, std::uint64_t rhs) {
        switch (op) {
        case comparison::equal: return count_as<comparison::equal>(data, first, count, rhs);
        case comparison::less: return count_as<comparison::less>(data, first, count, rhs);
        case comparison::less_equal: return count_as<comparison::less_equal>(data, first, count, rhs);
        case comparison::greater: return count_as<comparison::greater>(data, first, count, rhs);
        case comparison::greater_equal: return count_as<comparison::greater_equal>(data, first, count, rhs);
        }
        return 0;
    }
    static void filter_words(const void* data, comparison op, std::size_t first, std::size_t count, std::uint64_t rhs,
                             std::uint64_t* words) {
        switch (op) {
        case comparison::equal: return filter_as<comparison::equal>(data, first, count, rhs, words);
        case comparison::less: return filter_as<comparison::less>(data, first, count, rhs, words);
        case comparison::less_equal: return filter_as<comparison::less_equal>(data, first, count, rhs, words);
        case comparison::greater: return filter_as<comparison::greater>(data, first, count, rhs, words);
        case comparison::greater_equal: return filter_as<comparison::greater_equal>(data, first, count, rhs, words);
        }
    }
    static std::uint64_t weighted_sum(const void* data, std::size_t first, std::size_t count, const std::uint8_t* weights) {
        return bound(data, first + count).weighted_sum_unchecked(first, count, weights);
    }
    static constexpr range_operations table{&sum, &sum_masked, &count, &filter_words, &weighted_sum};
};
/// The compiled range operations of a runtime-width dense layout, or null
/// (src/seriespack/operations_*.cpp, eight widths per unit).
const range_operations* compiled_operations_1(unsigned width, geometry storage) noexcept;
const range_operations* compiled_operations_9(unsigned width, geometry storage) noexcept;
const range_operations* compiled_operations_17(unsigned width, geometry storage) noexcept;
const range_operations* compiled_operations_25(unsigned width, geometry storage) noexcept;
const range_operations* compiled_operations_33(unsigned width, geometry storage) noexcept;
const range_operations* compiled_operations_41(unsigned width, geometry storage) noexcept;
const range_operations* compiled_operations_49(unsigned width, geometry storage) noexcept;
const range_operations* compiled_operations_57(unsigned width, geometry storage) noexcept;
inline const range_operations* compiled_operations(unsigned width, geometry storage) noexcept {
    constexpr const range_operations* (*units[8])(unsigned, geometry) noexcept = {
        compiled_operations_1,  compiled_operations_9,  compiled_operations_17, compiled_operations_25,
        compiled_operations_33, compiled_operations_41, compiled_operations_49, compiled_operations_57};
    return width == 0 || width > 64 ? nullptr : units[(width - 1) / 8](width, storage);
}
} // namespace ikea::seriespack::detail
