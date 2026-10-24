#pragma once
#include <ikea/seriespack/author/filter.h>
#include <ikea/seriespack/reader.h>
#include "support.h"
#include <array>
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
