#include "../support.h"
#pragma once
#include "../fixture.h"
#include <ikea2/seriespack/detail/mutation/physical.h>
#include "verify.h"

#if defined(__aarch64__) || defined(__AVX2__)
struct mutation_selection {
    unsigned pattern;
    std::uint16_t operator()(std::size_t origin) const {
        if (pattern == 0)
            return 0xffff;
        if (pattern == 1)
            return 0xb6db;
        if (pattern == 2)
            return (origin / 16) % 4 == 1 ? 0 : 0xaaaa;
        return origin % 64 == 16 ? 0 : 0xb6db;
    }
};

// Compile each tested operation once per format. The library's inline kernel
// bodies remain fully optimized inside these calls; the assertion driver need
// not duplicate them for every input/mask/rejection scenario.
template <class F>
[[gnu::noinline]] auto
checked_mutation_range(const sp::view<F, std::uint8_t>& destination, std::size_t first,
                       std::span<const std::uint64_t> input, mutation_selection selected,
                       sp::sum_change& summary, sp::write_journal& journal) {
    return sp::replace(destination, first, input, selected, summary, journal);
}
template <class F>
[[gnu::noinline]] void
unchecked_mutation_regions(const sp::view<F, std::uint8_t>& destination, std::size_t first,
                           std::size_t count, const std::uint64_t* input,
                           mutation_selection selected, sp::sum_change& summary,
                           sp::write_journal& journal) {
    sp::replace_regions_unchecked(destination, first, count, input, selected, summary, journal);
}
template <class F, bool Dense = false> void check_mutation_format() {
    ikea2_test::format_scope<F> format_context{"physical mutation", Dense};
    placed<F> storage;
    if constexpr (Dense) {
        static_assert(F::heads == 0);
        storage.planes[0].stride = F::tile_bytes;
        storage.planes[0].bytes = storage.planes[0].bytes.first(storage.tiles * F::tile_bytes);
        for (std::size_t t = 0; t < storage.tiles; ++t)
            old::detail::encode_tile<F::width, old::geometry::local8>(
                storage.truth.data() + t * F::tile_rows,
                storage.planes[0].bytes.data() + t * F::tile_bytes);
    }
    auto result = sp::view<F, std::uint8_t>::attach(storage.count, storage.planes);
    IKEA2_CHECK(result);
    const auto destination = *result;
    std::array<std::vector<std::uint8_t>, 3> before;
    auto snapshot = [&] {
        for (unsigned p = 0; p < 3; ++p)
            before[p].assign(storage.planes[p].bytes.begin(), storage.planes[p].bytes.end());
    };
    std::array<sp::byte_write, 12> records;
    sp::write_journal journal{records};
    sp::sum_change summary;
    std::array<std::uint64_t, 16> input;
    std::mt19937_64 random(F::width * 13 + F::heads);
    std::uint64_t delta = 0;
    auto verify = [&] {
        verify_mutation_coverage(
            storage.planes, before, journal.entries(),
            {F::tile_bytes, F::heads >= 8 ? F::tile_rows : 0, F::heads == 16 ? F::tile_rows : 0});
        // The old independently checked wire encoder supplies the final-byte
        // oracle, including inactive values and padding in a partial last tile.
        for (std::size_t t = 0; t < storage.tiles; ++t) {
            if constexpr (F::payload) {
                std::array<std::uint64_t, F::tile_rows> values;
                for (unsigned j = 0; j < F::tile_rows; ++j) {
                    values[j] = storage.truth[t * F::tile_rows + j];
                    if constexpr (F::payload < 64)
                        values[j] &= (std::uint64_t{1} << F::payload) - 1;
                }
                std::array<std::uint8_t, F::tile_bytes> expected;
                constexpr auto G = F::storage == sp::geometry::local ? old::geometry::local8
                                                                     : old::geometry::striped;
                old::detail::encode_tile<F::payload, G>(values.data(), expected.data());
                IKEA2_CHECK(
                    std::memcmp(expected.data(),
                                storage.planes[0].bytes.data() + t * storage.planes[0].stride,
                                expected.size()) == 0);
            }
            for (unsigned p = 1; p <= F::heads / 8; ++p)
                for (unsigned j = 0; j < F::tile_rows; ++j)
                    IKEA2_CHECK(
                        storage.planes[p].bytes[t * storage.planes[p].stride + j] ==
                        std::uint8_t(storage.truth[t * F::tile_rows + j] >> (F::width - 8 * p)));
        }
    };
    for (auto bits : {std::uint16_t{0}, std::uint16_t{0xffff}, std::uint16_t{0xb6db},
                      std::uint16_t{1}, std::uint16_t{0xaaaa}}) {
        const std::size_t first = bits == 1 ? 512 : 32;
        ikea2_test::scope scenario{"native region", first, 16, bits};
        for (auto& x : input) {
            x = random();
            if constexpr (F::width < 64)
                x &= (std::uint64_t{1} << F::width) - 1;
        }
        snapshot();
        journal.used = 0;
        IKEA2_CHECK(sp::replace16(destination, first, std::span<const std::uint64_t>(input), bits,
                                  summary, journal));
        for (unsigned j = 0; j < 16; ++j)
            if (bits & (1u << j)) {
                delta += input[j] - storage.truth[first + j];
                storage.truth[first + j] = input[j];
            }
        IKEA2_CHECK(summary.finish() == delta);
        if (!bits)
            IKEA2_CHECK(journal.used == 0);
        verify();
    }
    // A range spans prefix, complete tiles and suffix. Empty regions must
    // bypass both the full-tile writer and their source reads/effects.
    std::array<std::uint64_t, 512> range_input;
    std::array<sp::byte_write, 512> range_records;
    for (unsigned pattern = 0; pattern < 3; ++pattern) {
        ikea2_test::scope scenario{"aligned range", 16, 512, pattern};
        for (auto& x : range_input) {
            x = random();
            if constexpr (F::width < 64)
                x &= (std::uint64_t{1} << F::width) - 1;
        }
        const mutation_selection mask{pattern};
        snapshot();
        journal = {range_records};
        unchecked_mutation_regions(destination, 16, 512, range_input.data(), mask, summary,
                                   journal);
        for (unsigned j = 0; j < 512; ++j)
            if (mask((j + 16) / 16 * 16) & (1u << (j % 16))) {
                delta += range_input[j] - storage.truth[j + 16];
                storage.truth[j + 16] = range_input[j];
            }
        IKEA2_CHECK(summary.finish() == delta);
        verify();
    }
    std::vector<std::uint64_t> arbitrary(storage.count);
    for (auto first :
         {std::size_t{0}, std::size_t{1}, std::size_t{17}, std::size_t{513}, storage.count}) {
        const auto count = storage.count - first;
        ikea2_test::scope scenario{"arbitrary range", first, count};
        for (auto& x : arbitrary) {
            x = random();
            if constexpr (F::width < 64)
                x &= (std::uint64_t{1} << F::width) - 1;
        }
        const mutation_selection mask{3};
        snapshot();
        journal = {range_records};
        IKEA2_CHECK(checked_mutation_range(destination, first,
                                           std::span<const std::uint64_t>(arbitrary.data(), count),
                                           mask, summary, journal));
        for (std::size_t j = 0; j < count; ++j)
            if (mask((first + j) / 16 * 16) & (1u << ((first + j) % 16))) {
                delta += arbitrary[j] - storage.truth[first + j];
                storage.truth[first + j] = arbitrary[j];
            }
        IKEA2_CHECK(summary.finish() == delta);
        verify();
    }
    // Rejection at the far end must precede even the first scalar-edge write.
    if constexpr (F::width < 64) {
        for (auto& x : arbitrary)
            x = 0;
        arbitrary[storage.count - 2] = std::uint64_t{1} << F::width;
        snapshot();
        const auto prior = summary.finish();
        journal = {range_records};
        IKEA2_CHECK(!checked_mutation_range(
            destination, 1, std::span<const std::uint64_t>(arbitrary.data(), storage.count - 1),
            mutation_selection{0}, summary, journal));
        IKEA2_CHECK(summary.finish() == prior && journal.used == 0);
        verify();
    }
    snapshot();
    const auto prior_delta = summary.finish();
    journal.used = 0;
    sp::write_journal no_space{{}};
    IKEA2_CHECK(!sp::replace16(destination, 0, std::span<const std::uint64_t>(input), 0xffff,
                               summary, no_space));
    IKEA2_CHECK(!sp::replace16(destination, 1, std::span<const std::uint64_t>(input), 0xffff,
                               summary, journal));
    if constexpr (F::width < 64) {
        input[0] = std::uint64_t{1} << F::width;
        IKEA2_CHECK(!sp::replace16(destination, 0, std::span<const std::uint64_t>(input), 1,
                                   summary, journal));
    }
    IKEA2_CHECK(summary.finish() == prior_delta && journal.used == 0);
    for (unsigned p = 0; p < 3; ++p)
        IKEA2_CHECK(
            std::equal(before[p].begin(), before[p].end(), storage.planes[p].bytes.begin()));
}
#endif

template <unsigned First> void check_mutation_widths() {
#if defined(__aarch64__) || defined(__AVX2__)
    sp::detail::each<8>([](auto index) {
        constexpr unsigned k = First + index - 1;
        check_mutation_format<sp::format<k + 1>>();
        if constexpr (k + 1 <= 7)
            check_mutation_format<sp::format<k + 1>, true>();
        if constexpr (k + 1 >= 8)
            check_mutation_format<sp::format<k + 1, sp::geometry::local, 8>>();
        if constexpr (k + 1 >= 16)
            check_mutation_format<sp::format<k + 1, sp::geometry::local, 16>>();
        if constexpr (sp::striped_width(k + 1))
            check_mutation_format<sp::format<k + 1, sp::geometry::striped>>();
        if constexpr (k + 1 > 8 && sp::striped_width(k + 1 - 8))
            check_mutation_format<sp::format<k + 1, sp::geometry::striped, 8>>();
        if constexpr (k + 1 > 16 && sp::striped_width(k + 1 - 16))
            check_mutation_format<sp::format<k + 1, sp::geometry::striped, 16>>();
    });
#endif
}
