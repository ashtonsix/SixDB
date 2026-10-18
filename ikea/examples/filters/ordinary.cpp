#include <ikea/seriespack.h>
#include <ikea/tuplepack.h>
#include <array>
#include <cassert>

namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
using C = ikea::comparison;

int main() {
    using F = sp::format<5>;
    alignas(64) std::array<std::uint8_t, 40> packed{};
    const auto attached =
        sp::view<F, std::uint8_t>::attach(64, {{{packed, F::tile_bytes}, {}, {}}});
    assert(attached);
    auto series = *attached;
    std::array<tp::byte, 64> tuples{};
    for (unsigned r = 0; r < 64; ++r) {
        const auto value = (r * 7) % 32;
        sp::set_unchecked(series, r, value);
        tuples[r] = tp::byte((value << 3) | (r % 8));
    }

    // One code occupies the upper five bits; the other three bits are neighbors.
    auto layout = *tp::layout::make(1, std::array<tp::code, 2>{{{0, 3, 5}, {0, 0, 3}}});
    auto source = *tp::view::bind(layout, tuples, 64, 1);
    auto plan = *tp::reader<64, 32>::make(layout, std::array<tp::byte, 1>{0});
    auto read = *tp::bind_reader(plan, source);
    auto predicate = *tp::bind_filter(read);

    constexpr unsigned first = 7, count = 32;
    constexpr std::uint64_t active = 0xa53c0918;
    // Both masks use original-row coordinates: bit i names first+i.
    const auto below = *sp::filter<C::less>(series, first, count, 18, active);
    const auto above = *predicate.filter<C::greater_equal>(first, 4, active);
    const auto selected = below & above;
    std::uint64_t expected = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto value = ((first + i) * 7) % 32;
        if ((active >> i & 1) && value >= 4 && value < 18)
            expected |= std::uint64_t{1} << i;
    }
    assert(selected == expected);

#if defined(__aarch64__) || defined(__AVX2__)
    // An authored consumer can share a projection between neighboring predicates.
    // A one-code map puts row i in packet byte i; other maps need ordering().
    const auto native = tp::native_reader(read);
    assert(native.admit(first, active));
    const auto values = native.get_unchecked(first, active);
    const auto range = tp::filter_values<C::greater_equal, 64>(values, 4, active) &
                       tp::filter_values<C::less, 64>(values, 18, active);
    assert(range == expected);
#endif

    // The scalar operand is never narrowed to the stored width.
    assert(sp::filter<C::less>(series, first, count, 256, active) == active);
    assert(predicate.filter<C::equal>(first, 256, active) == 0);
    // A bucket can place 24 keys directly after a one-byte header. One filter
    // returns the complete 24-bit mask without reading a padded fourth tile.
    using Keys = sp::format<16>;
    std::array<std::uint8_t, 1 + 24 * 2> bucket{};
    auto keys = *sp::view<Keys, std::uint8_t>::attach(
        24, {{{std::span(bucket).subspan(1), Keys::tile_bytes}, {}, {}}});
    for (unsigned i = 0; i < 24; ++i)
        sp::set_unchecked(keys, i, i * 11);
    assert(sp::filter<C::equal>(keys, 0, 24, 23 * 11) == (1u << 23));
    // indexof returns a position relative to the requested window, or count.
    assert(sp::indexof(keys, 8, 16, 23 * 11) == 15);
    assert(sp::indexof(keys, 8, 16, 999) == 16);
}
