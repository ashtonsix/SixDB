#include "cases.h"
#include <ikea/seriespack.h>
#include <random>

#if defined(__aarch64__) || defined(__AVX2__)
// The runtime-width point setter against a bound writer over identical bytes:
// the same stored bytes, records naming the dense_output and covering exactly
// its footprint, and unchanged bytes and effects on every refusal.
template <class F> void check_runtime_set() {
    ikea_test::format_scope<F> format_context{"runtime point set", true};
    constexpr std::size_t rows = 3 * F::tile_rows + 5;
    const std::size_t bytes = (rows + F::tile_rows - 1) / F::tile_rows * F::tile_bytes;
    std::vector<ikea::byte> a(bytes), b(bytes);
    std::vector<std::uint64_t> values(rows);
    std::mt19937_64 random(F::width);
    const auto top = F::width == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << F::width) - 1;
    for (auto& x : values)
        x = random() & top;
    const sp::dense_output target{F::width, F::storage, a, rows};
    std::vector<ikea::journal::entry> setup(*sp::effect_capacity_initialize(target));
    ikea::journal init{setup};
    IKEA_CHECK(sp::initialize(target, std::span<const std::uint64_t>(values), init));
    b = a;
    auto view = sp::view<F>::attach(rows, {{{std::span(b), F::tile_bytes}, {}, {}}});
    IKEA_CHECK(view);
    const auto writer = sp::bind_writer(*view);
    IKEA_CHECK(writer);
    for (unsigned k = 0; k < 200; ++k) {
        const std::size_t row = random() % rows;
        const std::uint64_t value = random() & top;
        std::array<ikea::journal::entry, 8> records;
        ikea::journal effects{records};
        const auto footprint = sp::footprint(target, row);
        IKEA_CHECK(footprint && footprint->store_count >= 1);
        IKEA_CHECK(sp::set(target, row, value, effects));
        std::array<ikea::journal::entry, 8> typed_records;
        ikea::journal typed{typed_records};
        IKEA_CHECK(writer->set(row, value, typed));
        IKEA_CHECK(a == b);
        values[row] = value;
        // Records name target and cover exactly the footprint's store bytes.
        std::vector<bool> stored(bytes), recorded(bytes);
        for (unsigned j = 0; j < footprint->store_count; ++j)
            for (std::size_t o = 0; o < footprint->stores[j].size; ++o)
                stored[footprint->stores[j].offset + o] = true;
        for (std::size_t j = 0; j < effects.used; ++j) {
            IKEA_CHECK(records[j].source == &target && records[j].plane == 0);
            for (std::size_t o = 0; o < records[j].size; ++o)
                recorded[records[j].offset + o] = true;
        }
        IKEA_CHECK(stored == recorded);
        // The value's bytes lie inside the read footprint.
        IKEA_CHECK(footprint->read_count >= 1);
    }
    const auto reader = sp::bind_reader<std::uint64_t>(sp::dense_input{F::width, F::storage, a, rows});
    IKEA_CHECK(reader);
    for (std::size_t i = 0; i < rows; ++i)
        IKEA_CHECK(reader->get_unchecked(i) == values[i]);
    // Refusals change neither bytes nor effects.
    const auto before = a;
    std::array<ikea::journal::entry, 8> records;
    ikea::journal effects{records};
    IKEA_CHECK(sp::set(target, rows, std::uint64_t{0}, effects).error() == sp::error::range);
    if constexpr (F::width < 64)
        IKEA_CHECK(sp::set(target, 0, top + 1, effects).error() == sp::error::value);
    ikea::journal none{std::span<ikea::journal::entry>()};
    IKEA_CHECK(sp::set(target, 0, std::uint64_t{1}, none).error() == sp::error::effects);
    // A journal already full refuses, as a bound writer does.
    std::array<ikea::journal::entry, 1> one_record;
    ikea::journal full{one_record};
    full.used = 1;
    IKEA_CHECK(sp::set(target, 0, std::uint64_t{1}, full).error() == sp::error::effects && full.used == 1);
    IKEA_CHECK(effects.used == 0 && none.used == 0 && a == before);
    // A narrow input type sets a wider layout, as a bound writer accepts it.
    {
        std::array<ikea::journal::entry, 8> narrow_records;
        ikea::journal narrow{narrow_records};
        IKEA_CHECK(sp::set(target, 1, std::uint8_t{1}, narrow));
        IKEA_CHECK(sp::bind_reader<std::uint64_t>(sp::dense_input{F::width, F::storage, a, rows})->get_unchecked(1) == 1);
        if constexpr (F::width >= 16)
            IKEA_CHECK(sp::set(target, 2, std::uint16_t{65535}, narrow));
        if constexpr (F::width >= 32)
            IKEA_CHECK(sp::set(target, 3, std::uint32_t{0xffffffffu}, narrow));
        if constexpr (F::width < 8)
            IKEA_CHECK(sp::set(target, 1, std::uint8_t{255}, narrow).error() == sp::error::value);
    }
    IKEA_CHECK(sp::footprint(target, rows).error() == sp::error::range);
}
#endif
void check_mutation_dense() {
#if defined(__aarch64__) || defined(__AVX2__)
    // Dense bulk execution must preserve the same range, selection, summary
    // and rejection laws as placed execution, across native lane boundaries.
    check_mutation_format<sp::layout<8>, true>();
    check_mutation_format<sp::layout<12>, true>();
    check_mutation_format<sp::layout<16>, true>();
    check_mutation_format<sp::layout<31>, true>();
    check_mutation_format<sp::layout<32>, true>();
    check_mutation_format<sp::layout<33>, true>();
    check_mutation_format<sp::layout<56>, true>();
    check_mutation_format<sp::layout<64>, true>();
    check_runtime_set<sp::layout<3>>();
    check_runtime_set<sp::layout<12>>();
    check_runtime_set<sp::layout<20>>();
    check_runtime_set<sp::layout<32>>();
    check_runtime_set<sp::layout<64>>();
    check_runtime_set<sp::layout<5, sp::geometry::striped>>();
    check_runtime_set<sp::layout<14, sp::geometry::striped>>();
    check_runtime_set<sp::layout<20, sp::geometry::striped>>();
#endif
}
