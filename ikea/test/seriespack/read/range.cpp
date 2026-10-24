#include "../support.h"
#include "../fixture.h"
#include "../operations.h"
#include <ikea/seriespack.h>
#include <cstring>
#include <limits>

// Range operations at their boundaries: the occupied bytes of a partial final
// tile end at a protected page, so a read of the next tile or allocation
// padding faults; maximum values must stay in an unconditioned SUM.
namespace {
template <class F> struct guarded_tiles {
    std::size_t rows, bytes, span;
    std::uint8_t* map;
    std::byte* data;
    explicit guarded_tiles(std::size_t n) : rows(n) {
        const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
        bytes = (n + F::tile_rows - 1) / F::tile_rows * F::tile_bytes;
        span = (bytes + page - 1) / page * page;
        map = static_cast<std::uint8_t*>(
            mmap(nullptr, span + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        IKEA_CHECK(map != MAP_FAILED);
        IKEA_CHECK(mprotect(map + span, page, PROT_NONE) == 0);
        data = reinterpret_cast<std::byte*>(map + span - bytes);
    }
    ~guarded_tiles() {
        munmap(map, span + static_cast<std::size_t>(sysconf(_SC_PAGESIZE)));
    }
    guarded_tiles(const guarded_tiles&) = delete;
};

template <class F> void check_guarded(std::size_t rows) {
    ikea_test::format_scope<F> format_context{"guarded range operations", true};
    guarded_tiles<F> tiles(rows);
    std::vector<std::uint64_t> truth(rows);
    std::mt19937_64 random(F::width * 7 + rows);
    for (auto& x : truth) {
        x = random();
        if constexpr (F::width < 64)
            x &= (std::uint64_t{1} << F::width) - 1;
    }
    // Maximum values in the final tile, where scalar edges take them.
    for (std::size_t i = rows - std::min<std::size_t>(rows, 5); i < rows; ++i)
        truth[i] = F::width == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << F::width) - 1;
    auto view = sp::view<F>::attach(rows, {{{std::span(tiles.data, tiles.bytes), F::tile_bytes}, {}, {}}});
    IKEA_CHECK(view);
    const auto writer = sp::bind_writer(*view);
    IKEA_CHECK(writer);
    std::vector<ikea::journal::entry> records(*writer->effect_capacity_initialize());
    ikea::journal effects{records};
    IKEA_CHECK(writer->template erase<std::uint64_t>().initialize(truth, effects));
    const auto source = view->as_const();
    const auto dense = sp::dense(source);
    IKEA_CHECK(dense);
    check_reader_operations(sp::bind_reader(*dense), truth, rows);
    // Every comparison, at a present value and both extremes.
    for (const auto rhs : {truth[rows / 2], std::uint64_t{0}, truth[rows - 1]}) {
        auto compare = [&]<ikea::comparison Op>() {
            for (const std::size_t first : {std::size_t{0}, std::size_t{64}})
                if (first <= rows) {
                    std::uint64_t want = 0;
                    for (auto i = first; i < rows; ++i)
                        want += ikea::compare<Op>(truth[i], rhs);
                    IKEA_CHECK(sp::bind_reader(*dense).template count_unchecked<Op>(first, rows - first, rhs) == want);
                }
        };
        compare.template operator()<ikea::comparison::equal>();
        compare.template operator()<ikea::comparison::less>();
        compare.template operator()<ikea::comparison::less_equal>();
        compare.template operator()<ikea::comparison::greater>();
        compare.template operator()<ikea::comparison::greater_equal>();
    }
    std::vector<std::uint64_t> output(rows);
    for (std::size_t first = rows - std::min<std::size_t>(rows, 80); first < rows; ++first) {
        const auto n = rows - first;
        sp::bind_reader(*dense).read_unchecked(first, n, output.data());
        for (std::size_t j = 0; j < n; ++j)
            IKEA_CHECK(output[j] == truth[first + j]);
    }
}

// Placed (gapped strides, heads) and guarded expressions over the shared
// fixture's independently encoded tiles.
template <class F> void check_placed_ranges() {
    ikea_test::format_scope<F> format_context{"placed range operations"};
    placed<F> storage;
    const auto source = storage.source();
    const auto expr = cp::expression(source);
    check_range_expression(expr, storage.truth, storage.count);
    if constexpr (F::heads == 8 && F::width > 8)
        check_range_expression(cp::guarded(expr), storage.truth, storage.count);
    check_reader_operations(sp::bind_reader(source), storage.truth, storage.count);
}

void check_maximum() {
    // Sixteen maximum 64-bit values: their modulo-2^64 sum, not zero.
    using F = sp::layout<64>;
    std::array<ikea::byte, 128> bytes;
    std::memset(bytes.data(), 0xff, bytes.size());
    const auto view = sp::const_view<F>::attach_unchecked(16, {{{std::span(bytes), F::tile_bytes}, {}, {}}});
    const auto want = std::uint64_t{0} - 16;
    IKEA_CHECK(sp::bind_reader(view).sum_unchecked(0, 16) == want);
    IKEA_CHECK(sp::bind_reader(view).sum_unchecked(3, 13) == std::uint64_t{0} - 13);
    IKEA_CHECK(sp::bind_reader(*sp::dense(view)).sum(0, 16) == want);
}

void check_empty() {
    // An empty call reads nothing, even from inaccessible bytes.
    using F = sp::layout<5>;
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    auto* dead = static_cast<std::uint8_t*>(mmap(nullptr, page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    IKEA_CHECK(dead != MAP_FAILED);
    const auto view = sp::const_view<F>::attach_unchecked(
        512, {{{std::as_bytes(std::span(dead, 512 / 8 * F::tile_bytes)), F::tile_bytes}, {}, {}}});
    const auto reader = sp::bind_reader(*sp::dense(view));
    std::array<std::uint64_t, 1> word{0xbad};
    IKEA_CHECK(reader.sum(64, 0) == 0);
    IKEA_CHECK(reader.template count<ikea::comparison::less>(64, 0, 9) == 0);
    IKEA_CHECK(reader.template filter_words<ikea::comparison::less>(64, 0, 9, std::span(word).first(0)));
    IKEA_CHECK(word[0] == 0xbad);
    munmap(dead, page);
}
} // namespace

void check_ranges() {
#if defined(__aarch64__) || defined(__AVX2__)
    check_maximum();
    check_empty();
    check_placed_ranges<sp::layout<7>>();
    check_placed_ranges<sp::layout<12, sp::geometry::local, 8>>();
    check_placed_ranges<sp::layout<64, sp::geometry::local, 16>>();
    check_placed_ranges<sp::layout<5, sp::geometry::striped>>();
    check_placed_ranges<sp::layout<22, sp::geometry::striped, 8>>();
    // Local widths through each range kernel (residual-only, one-, two- and
    // three-byte bodies), then striped widths; check_maximum covers K = 64.
    // Sampled: every instantiation inlines whole range drivers.
    check_guarded<sp::layout<1>>(1003);
    check_guarded<sp::layout<3>>(1003);
    check_guarded<sp::layout<6>>(1003);
    check_guarded<sp::layout<7>>(1003);
    for (const auto rows : {std::size_t{1}, std::size_t{129}, std::size_t{1031}}) {
        check_guarded<sp::layout<9>>(rows);
        check_guarded<sp::layout<15>>(rows);
        check_guarded<sp::layout<20>>(rows);
        check_guarded<sp::layout<24>>(rows);
        check_guarded<sp::layout<26>>(rows);
    }
    check_guarded<sp::layout<1, sp::geometry::striped>>(5 * 256 + 100);
    check_guarded<sp::layout<3, sp::geometry::striped>>(5 * 256 + 100);
    check_guarded<sp::layout<6, sp::geometry::striped>>(5 * 256 + 100);
    check_guarded<sp::layout<10, sp::geometry::striped>>(1100);
    check_guarded<sp::layout<15, sp::geometry::striped>>(1100);
    check_guarded<sp::layout<20, sp::geometry::striped>>(1100);
#endif
}
