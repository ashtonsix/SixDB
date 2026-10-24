#include "range.h"
#include <limits>

namespace {
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
