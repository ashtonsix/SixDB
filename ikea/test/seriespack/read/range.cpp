#include "range.h"
#include <limits>

namespace {
// Tiles before the first requested row's tile are never read. Logical row 0
// starts lead tiles before a page boundary, so those tiles sit in a protected
// page and the readable page begins at the range's first tile. A read that
// rounds its start down to a 16-, 32- or 64-row packet faults (Local8 first 8,
// count 56 on the old partial-packet path; first 16, count 48 on a widened
// AVX-512 group).
template <class F> void check_lower_bound(std::size_t lead_tiles) {
    ikea_test::format_scope<F> format_context{"lower tile bound", true};
    static_assert(F::storage == sp::geometry::local);
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    const std::size_t lead = lead_tiles * F::tile_rows, rows = lead + 512;
    const std::size_t bytes = rows / F::tile_rows * F::tile_bytes, before = lead_tiles * F::tile_bytes;
    IKEA_CHECK(before <= page);
    const std::size_t span = page + (bytes - before + page - 1) / page * page;
    auto* map = static_cast<std::byte*>(mmap(nullptr, span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    IKEA_CHECK(map != MAP_FAILED);
    auto* origin = map + page - before;
    std::vector<std::uint64_t> truth(rows);
    std::mt19937_64 random(F::width + lead_tiles);
    for (auto& x : truth)
        x = random() & ((std::uint64_t{1} << F::width) - 1);
    auto view = sp::view<F>::attach(rows, {{{std::span(origin, bytes), F::tile_bytes}, {}, {}}});
    IKEA_CHECK(view);
    const auto writer = sp::bind_writer(*view);
    std::vector<ikea::journal::entry> records(*writer->effect_capacity_initialize());
    ikea::journal effects{records};
    IKEA_CHECK(writer->template erase<std::uint64_t>().initialize(truth, effects));
    IKEA_CHECK(mprotect(map, page, PROT_NONE) == 0);
    const auto reader = sp::bind_reader(*sp::dense(view->as_const()));
    std::vector<std::uint64_t> out(80);
    for (std::size_t first = lead; first < lead + 24; ++first)
        for (std::size_t n = 1; n <= 64; ++n) {
            ikea_test::scope scenario{"lower bound range", first, n};
            reader.read_unchecked(first, n, out.data());
            std::uint64_t want = 0, less = 0;
            for (std::size_t j = 0; j < n; ++j) {
                IKEA_CHECK(out[j] == truth[first + j]);
                want += truth[first + j], less += truth[first + j] < truth[first];
            }
            IKEA_CHECK(reader.sum_unchecked(first, n) == want);
            IKEA_CHECK(reader.template count_unchecked<ikea::comparison::less>(first, n, truth[first]) == less);
        }
    munmap(map, span);
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

void check_ranges_striped();
void check_ranges() {
#if defined(__aarch64__) || defined(__AVX2__)
    check_maximum();
    check_empty();
    for (const std::size_t lead_tiles : {1, 2, 3}) {
        check_lower_bound<sp::layout<1>>(lead_tiles);
        check_lower_bound<sp::layout<3>>(lead_tiles);
        check_lower_bound<sp::layout<8>>(lead_tiles);
        check_lower_bound<sp::layout<12>>(lead_tiles);
        check_lower_bound<sp::layout<16>>(lead_tiles);
    }
    // Placed sources: gapped strides, heads and guards.
    check_placed_ranges<sp::layout<7>>();
    check_placed_ranges<sp::layout<12, sp::geometry::local, 8>>();
    check_placed_ranges<sp::layout<64, sp::geometry::local, 16>>();
    check_placed_ranges<sp::layout<5, sp::geometry::striped>>();
    check_placed_ranges<sp::layout<22, sp::geometry::striped, 8>>();
    // Local widths through each range kernel (residual-only, one-, two- and
    // three-byte bodies); check_maximum covers K = 64. Sampled: every
    // instantiation inlines whole range drivers.
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
#endif
    check_ranges_striped();
}
