#include "../support.h"
#include "../fixture.h"
#include <ikea2/seriespack/detail/mutation/construction.h>

template <class F> void check_construction_format() {
    ikea2_test::format_scope<F> format_context{"construction"};
#if defined(__aarch64__) || defined(__AVX2__)
    for (std::size_t count : {0,  1,  7,  8,   9,   15,  16,  17,  31,  32,  33,
                              63, 64, 65, 127, 128, 129, 255, 256, 257, 513, 529}) {
        ikea2_test::scope scenario{"extent", 0, count};
        placed<F> storage;
        const auto destination = *sp::view<F, std::uint8_t>::attach(count, storage.planes);
        std::vector<std::uint64_t> input(count);
        for (std::size_t i = 0; i < count; ++i)
            input[i] = storage.truth[i] ^ (~std::uint64_t{0} >> (64 - F::width));
        std::array<std::vector<std::uint8_t>, 3> wanted, before;
        for (unsigned p = 0; p < 3; ++p)
            wanted[p] = before[p] = std::vector<std::uint8_t>(storage.planes[p].bytes.begin(),
                                                              storage.planes[p].bytes.end());
        const auto tiles = count / F::tile_rows + (count % F::tile_rows != 0);
        // Independent wire oracle, with zero logical values in final slack.
        for (std::size_t t = 0; t < tiles; ++t) {
            std::array<std::uint64_t, F::tile_rows> values{};
            for (std::size_t j = 0; j < F::tile_rows && t * F::tile_rows + j < count; ++j)
                values[j] = input[t * F::tile_rows + j];
            if constexpr (F::heads >= 8)
                for (std::size_t j = 0; j < F::tile_rows; ++j)
                    wanted[1][t * storage.planes[1].stride + j] = values[j] >> (F::width - 8);
            if constexpr (F::heads == 16)
                for (std::size_t j = 0; j < F::tile_rows; ++j)
                    wanted[2][t * storage.planes[2].stride + j] = values[j] >> (F::width - 16);
            if constexpr (F::payload) {
                if constexpr (F::payload < 64)
                    for (auto& value : values)
                        value &= (std::uint64_t{1} << F::payload) - 1;
                constexpr auto G = F::storage == sp::geometry::local ? old::geometry::local8
                                                                     : old::geometry::striped;
                old::detail::encode_tile<F::payload, G>(
                    values.data(), wanted[0].data() + t * storage.planes[0].stride);
            }
        }
        std::vector<sp::byte_write> records(storage.truth.size() / 8 * 3 + 16);
        sp::write_journal effects{records};
        if (count) {
            sp::write_journal full{{}};
            IKEA2_CHECK(!sp::initialize(destination, std::span<const std::uint64_t>(input), full));
            if constexpr (F::width < 64) {
                const auto last = input.back();
                input.back() = std::uint64_t{1} << F::width;
                IKEA2_CHECK(
                    !sp::initialize(destination, std::span<const std::uint64_t>(input), effects));
                IKEA2_CHECK(!effects.used);
                input.back() = last;
            }
            for (unsigned p = 0; p < 3; ++p)
                IKEA2_CHECK(std::equal(before[p].begin(), before[p].end(),
                                       storage.planes[p].bytes.begin()));
        }
        IKEA2_CHECK(sp::initialize(destination, std::span<const std::uint64_t>(input), effects));
        for (unsigned p = 0; p < 3; ++p) {
            IKEA2_CHECK(
                std::equal(wanted[p].begin(), wanted[p].end(), storage.planes[p].bytes.begin()));
            for (std::size_t b = 0; b < wanted[p].size(); ++b)
                if (before[p][b] != wanted[p][b]) {
                    bool covered = false;
                    for (const auto& write : effects.entries())
                        covered |=
                            write.plane == p && b >= write.offset && b - write.offset < write.size;
                    IKEA2_CHECK(covered);
                }
        }
    }
#endif
}
void check_construction() {
    check_construction_format<sp::format<1>>();
    check_construction_format<sp::format<7>>();
    check_construction_format<sp::format<8>>();
    check_construction_format<sp::format<31>>();
    check_construction_format<sp::format<56>>();
    check_construction_format<sp::format<64>>();
    check_construction_format<sp::format<7, sp::geometry::striped>>();
    check_construction_format<sp::format<12, sp::geometry::striped>>();
    check_construction_format<sp::format<23, sp::geometry::local, 16>>();
    check_construction_format<sp::format<8, sp::geometry::local, 8>>();
    check_construction_format<sp::format<28, sp::geometry::striped, 16>>();
}
