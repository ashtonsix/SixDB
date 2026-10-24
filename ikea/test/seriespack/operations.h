#pragma once
#include <ikea/seriespack/author/filter.h>
#include <ikea/seriespack/reader.h>
#include <ikea/seriespack/erased_reader.h>
#include <ikea/seriespack/writer.h>
#include "support.h"
#include <array>
#include <numeric>
#include <vector>

/// The reader's range operations and their checked forms: bounds before any
/// access, exactly the result words written, high bits of the last zero.
template <class Reader, class Expected>
void check_reader_operations(const Reader& reader, const Expected& expected, std::size_t count) {
#if defined(__aarch64__) || defined(__AVX2__)
    for (auto first : {std::size_t{0}, std::size_t{3}, std::size_t{128}, count})
        for (auto n : {std::size_t{0}, std::min(std::size_t{65}, count - first), count - first}) {
            if (first > count)
                continue;
            ikea_test::scope scenario{"reader range operations", first, n};
            std::uint64_t want = 0, ge = 0;
            const auto rhs = count ? expected[count / 3] : 0;
            for (auto i = first; i < first + n; ++i)
                want += expected[i], ge += expected[i] >= rhs;
            IKEA_CHECK(reader.sum_unchecked(first, n) == want);
            IKEA_CHECK(reader.sum(first, n) == want);
            IKEA_CHECK(reader.template count<ikea::comparison::greater_equal>(first, n, rhs) == ge);
            const auto words = n / 64 + (n % 64 != 0);
            std::vector<std::uint64_t> out(words + 1, 0xbad);
            IKEA_CHECK(reader.template filter_words<ikea::comparison::greater_equal>(first, n, rhs, out));
            for (std::size_t i = 0; i < 64 * words; ++i) {
                const bool bit = out[i / 64] >> (i % 64) & 1;
                IKEA_CHECK(bit == (i < n && expected[first + i] >= rhs));
            }
            IKEA_CHECK(out[words] == 0xbad);
            if (words) {
                std::vector<std::uint64_t> small(words - 1, 0xbad);
                IKEA_CHECK(reader.template filter_words<ikea::comparison::less>(first, n, rhs, small).error() ==
                           ikea::seriespack::error::capacity);
                for (auto w : small)
                    IKEA_CHECK(w == 0xbad);
            }
        }
    IKEA_CHECK(reader.sum(count, 1).error() == ikea::seriespack::error::range);
    if (count)
        IKEA_CHECK(reader.sum(1, count).error() == ikea::seriespack::error::range);
    IKEA_CHECK(reader.template count<ikea::comparison::equal>(count + 1, 0, 0).error() == ikea::seriespack::error::range);
    std::array<std::uint64_t, 1> one{0xbad};
    if (count)
        IKEA_CHECK(reader.template filter_words<ikea::comparison::equal>(1, count, 0, one).error() ==
                   ikea::seriespack::error::range);
    IKEA_CHECK(one[0] == 0xbad);
#endif
}

#if defined(__aarch64__) || defined(__AVX2__)
/// A scan group's values to out; returns its rows.
#if defined(__aarch64__) || (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
template <unsigned K, unsigned N>
unsigned store_group(std::uint64_t* out, const ikea::seriespack::native_group::values<K, N>& x) {
    ikea::seriespack::native_group::store(out, x);
    return N;
}
#else
template <unsigned K> unsigned store_group(std::uint64_t* out, const ikea::seriespack::native::values<K>& x) {
    ikea::seriespack::native::store16(out, x);
    return 16;
}
#endif
#endif

/// Unconditioned range SUM (modulo 2^64, maximum values included) and
/// one-pass range comparisons of an expression, against expected values.
template <class Expr, class Expected>
void check_range_expression(const Expr& expr, const Expected& expected, std::size_t count) {
#if defined(__aarch64__) || defined(__AVX2__)
    namespace cp = ikea::seriespack::author;
    for (auto first : {std::size_t{0}, std::size_t{1}, std::size_t{64}, std::size_t{256}, count})
        for (auto n : {std::size_t{0}, std::min(std::size_t{70}, count - first), count - first}) {
            if (first > count)
                continue;
            ikea_test::scope scenario{"unconditioned range", first, n};
            std::uint64_t want = 0;
            for (auto i = first; i < first + n; ++i)
                want += expected[i];
            IKEA_CHECK(cp::total_regions(expr, first, n) == want);
            {
                static constexpr std::uint8_t weights[16] = {16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 255};
                std::uint64_t weighted = 0;
                for (auto i = first; i < first + n; ++i)
                    weighted += weights[i % 16] * static_cast<std::uint64_t>(expected[i]);
                IKEA_CHECK(cp::weighted_sum_regions(expr, first, n, weights) == weighted);
            }
            // scan_regions visits every row of the range exactly once, in
            // order, with its value; groups are aligned to their size.
            {
                std::size_t next = first;
                cp::scan_regions(
                    expr, first, n,
                    [&](std::size_t row, const auto& x) {
                        std::array<std::uint64_t, 64> values;
                        const auto rows = store_group(values.data(), x);
                        IKEA_CHECK(row == next && row % rows == 0 && row + rows <= first + n);
                        for (unsigned j = 0; j < rows; ++j)
                            IKEA_CHECK(values[j] == expected[row + j]);
                        next += rows;
                    },
                    [&](std::size_t row, std::uint64_t value) {
                        IKEA_CHECK(row == next && value == expected[row]);
                        ++next;
                    });
                IKEA_CHECK(next == first + n);
            }
            const auto rhs = expected[first + n / 2 < count ? first + n / 2 : 0];
            auto compare = [&]<ikea::comparison Op>() {
                std::vector<std::uint64_t> words((n + 63) / 64 + 1, 0xbad);
                cp::filter_regions<Op>(expr, first, n, rhs, [&](std::size_t w, std::uint64_t bits) {
                    IKEA_CHECK(w < (n + 63) / 64);
                    words[w] = bits;
                });
                for (std::size_t w = 0; w < (n + 63) / 64; ++w) {
                    std::uint64_t bits = 0;
                    for (std::size_t j = 0; j < 64 && 64 * w + j < n; ++j)
                        bits |= std::uint64_t{ikea::compare<Op>(expected[first + 64 * w + j], rhs)} << j;
                    IKEA_CHECK(words[w] == bits);
                }
                IKEA_CHECK(words.back() == 0xbad);
            };
            // Equality and one order here; range.cpp covers every comparison.
            compare.template operator()<ikea::comparison::equal>();
            compare.template operator()<ikea::comparison::greater_equal>();
        }
#endif
}

/// An erased reader's range operations, compiled or decoded, against expected
/// values: every comparison, masked sums with and without a repeating
/// pattern, exact filter words and the checked forms.
template <class U, class Expected>
void check_erased_operations(const ikea::seriespack::erased_reader<U>& reader, const Expected& expected, std::size_t count) {
    using ikea::comparison;
    std::vector<std::uint16_t> masks(count / 16 + 2);
    for (std::size_t k = 0; k < masks.size(); ++k)
        masks[k] = static_cast<std::uint16_t>(0xb6db ^ (k * 0x2311));
    for (auto first : {std::size_t{0}, std::size_t{5}, std::size_t{64}, std::size_t{256}, count})
        for (auto n : {std::size_t{0}, std::min(std::size_t{70}, count - first), count - first}) {
            if (first > count)
                continue;
            ikea_test::scope scenario{"erased range operations", first, n};
            std::uint64_t want = 0, masked = 0, pattern = 0;
            for (auto i = first; i < first + n; ++i) {
                const std::size_t k = i / 16 - first / 16;
                want += expected[i];
                masked += (masks[k] >> (i % 16) & 1) ? expected[i] : 0;
                pattern += (masks[k % 16] >> (i % 16) & 1) ? expected[i] : 0;
            }
            IKEA_CHECK(reader.sum_unchecked(first, n) == want && reader.sum(first, n) == want);
            IKEA_CHECK(reader.sum_masked_unchecked(first, n, masks.data()) == masked);
            IKEA_CHECK(reader.sum_masked_unchecked(first, n, masks.data(), 16) == pattern);
            // A non-power-of-two period, and all-zero and full masks.
            {
                std::uint64_t period3 = 0;
                for (auto i = first; i < first + n; ++i)
                    period3 += (masks[(i / 16 - first / 16) % 3] >> (i % 16) & 1) ? expected[i] : 0;
                IKEA_CHECK(reader.sum_masked_unchecked(first, n, masks.data(), 3) == period3);
                static constexpr std::uint16_t none[1] = {0}, all[1] = {0xffff};
                IKEA_CHECK(reader.sum_masked_unchecked(first, n, none, 1) == 0);
                IKEA_CHECK(reader.sum_masked_unchecked(first, n, all, 1) == want);
            }
            {
                static constexpr std::uint8_t weights[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 255};
                std::uint64_t weighted = 0;
                for (auto i = first; i < first + n; ++i)
                    weighted += weights[i % 16] * static_cast<std::uint64_t>(expected[i]);
                IKEA_CHECK(reader.weighted_sum_unchecked(first, n, weights) == weighted);
            }
            for (const auto rhs : {std::uint64_t{0}, std::uint64_t(count ? expected[count / 3] : 0), ~std::uint64_t{0}})
                for (const auto op : {comparison::equal, comparison::less, comparison::less_equal, comparison::greater,
                                      comparison::greater_equal}) {
                    std::uint64_t c = 0;
                    std::vector<std::uint64_t> bits(n / 64 + 2, 0);
                    for (std::size_t i = 0; i < n; ++i) {
                        const auto x = static_cast<std::uint64_t>(expected[first + i]);
                        const bool keep = op == comparison::equal ? x == rhs : op == comparison::less ? x < rhs
                                          : op == comparison::less_equal ? x <= rhs : op == comparison::greater ? x > rhs : x >= rhs;
                        c += keep;
                        bits[i / 64] |= std::uint64_t{keep} << (i % 64);
                    }
                    IKEA_CHECK(reader.count_unchecked(op, first, n, rhs) == c && reader.count(op, first, n, rhs) == c);
                    const auto words = n / 64 + (n % 64 != 0);
                    std::vector<std::uint64_t> out(words + 1, 0xbad);
                    IKEA_CHECK(reader.filter_words(op, first, n, rhs, out));
                    for (std::size_t w = 0; w < words; ++w)
                        IKEA_CHECK(out[w] == bits[w]);
                    IKEA_CHECK(out[words] == 0xbad);
                }
        }
    IKEA_CHECK(reader.sum(count, 1).error() == ikea::seriespack::error::range);
    std::array<std::uint64_t, 1> none{0xbad};
    if (count >= 65)
        IKEA_CHECK(reader.filter_words(comparison::less, 0, 65, 0, none).error() == ikea::seriespack::error::capacity);
    IKEA_CHECK(none[0] == 0xbad);
    for (std::size_t i = 0; i < count; i += 7)
        IKEA_CHECK(reader.point_kernel()(reader.resource(), i) == expected[i]);
}

/// Every runtime width and geometry, bound at run time (no per-width test
/// instantiation): values built by the runtime initializer, read back through
/// compiled and decoded range operations, with a narrow reader where it fits.
inline void check_runtime_widths() {
#if defined(__aarch64__) || defined(__AVX2__)
    namespace sp = ikea::seriespack;
    for (unsigned width = 1; width <= 64; ++width)
        for (const auto storage : {sp::geometry::local, sp::geometry::striped}) {
            if (storage == sp::geometry::striped && !sp::striped_width(width))
                continue;
            ikea_test::scope scenario{"runtime width", width, unsigned(storage)};
            const std::size_t tile_rows = storage == sp::geometry::local ? 8 : 32 * (8 / std::gcd(width % 8, 8u));
            const std::size_t rows = 3 * tile_rows + 37, bytes = (rows + tile_rows - 1) / tile_rows * tile_rows * width / 8;
            std::vector<ikea::byte> storage_bytes(bytes);
            std::vector<std::uint64_t> values(rows);
            std::uint64_t state = width * 0x9e3779b97f4a7c15ULL + unsigned(storage);
            for (auto& x : values) {
                state = state * 6364136223846793005ULL + 1442695040888963407ULL;
                x = width == 64 ? state : state >> (64 - width);
            }
            values[rows - 1] = width == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
            const sp::dense_output out{width, storage, storage_bytes, rows};
            std::vector<ikea::journal::entry> records(*sp::effect_capacity_initialize(out));
            ikea::journal effects{records};
            IKEA_CHECK(sp::initialize(out, std::span<const std::uint64_t>(values), effects));
            const sp::dense_input in{width, storage, storage_bytes, rows};
            const auto plain = sp::bind_reader<std::uint64_t>(in);
            const auto ranges = sp::bind_range_reader<std::uint64_t>(in);
            IKEA_CHECK(plain && !plain->compiled_ranges() && ranges && ranges->compiled_ranges());
            check_erased_operations(*ranges, values, rows);
            check_erased_operations(*plain, values, rows);
            if (width <= 8) {
                const auto narrow = sp::bind_range_reader<std::uint8_t>(in);
                IKEA_CHECK(narrow && narrow->compiled_ranges());
                check_erased_operations(*narrow, values, rows);
            }
        }
#endif
}
