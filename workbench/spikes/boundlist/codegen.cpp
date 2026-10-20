#include "geometry.h"
#include "scan-count.h"
#include <utility>
namespace bl = boundlist_spike;
namespace sn = ikea::boundlist::native;

template <class W> [[gnu::always_inline]] std::uint64_t prefix_pair(const W* data, unsigned low, unsigned high) {
    unsigned carry = 0, a = 0, b = 0;
    [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
        auto one = [&](auto index) __attribute__((always_inline)) {
            constexpr unsigned i = index * sn::lanes;
            const auto x = sn::load<W>(reinterpret_cast<const std::byte*>(data + i));
            const auto local = sn::prefix(x);
            const auto positions = sn::add(local, sn::splat(carry));
            a |= sn::greater(positions, low) << i;
            b |= sn::greater(positions, high - 1) << i;
            carry += sn::last(local);
        };
        (one(std::integral_constant<unsigned, I>{}), ...);
    }(std::make_index_sequence<16 / sn::lanes>{});
    return a | (std::uint64_t(b) << 32);
}
// Straight-line local-prefix witnesses. high>0, two relative queries and a
// complete readable 16-width group are admitted. Scheduling models cover these
// bodies, not the complete branching directory search or memory hierarchy.
extern "C" [[gnu::noinline]] std::uint64_t bl_prefix8_pair(const std::uint8_t* p, unsigned lo, unsigned hi) {
    return prefix_pair(p, lo, hi);
}
extern "C" [[gnu::noinline]] std::uint64_t bl_prefix16_pair(const std::uint16_t* p, unsigned lo, unsigned hi) {
    return prefix_pair(p, lo, hi);
}
extern "C" [[gnu::noinline]] std::uint64_t bl_direct16_pair(const std::uint16_t* p, unsigned lo, unsigned hi) {
    const auto masks = bl::native_pair_mask<bl::edge::after, bl::edge::at_or_after>(p, 16, std::uint16_t(lo), std::uint16_t(hi));
    return masks[0] | (std::uint64_t(masks[1]) << 32);
}
extern "C" [[gnu::noinline]] std::uint64_t bl_checkpoint16_find(const std::uint16_t* p, unsigned q) {
    const auto mask = bl::native_mask<bl::edge::at_or_after>(p, 16, std::uint16_t(q));
    return mask ? std::countr_zero(mask) : 16;
}
extern "C" [[gnu::noinline]] unsigned bl_byte_owner16(const std::uint8_t* p, unsigned q) {
    unsigned rank;
    [[clang::always_inline]] rank = ikea::boundlist::detail::owner16_native(p, q);
    return rank;
}
extern "C" [[gnu::noinline]] unsigned bl_byte_owner32(const std::uint8_t* p, unsigned q) {
    unsigned carry = 0, mask = 0;
    [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
        auto one = [&](auto index) __attribute__((always_inline)) {
            constexpr unsigned i = index * sn::lanes;
            const auto x = sn::prefix(sn::load<std::uint8_t>(reinterpret_cast<const std::byte*>(p + i)));
            mask |= sn::greater(sn::add(x, sn::splat(carry)), q) << i;
            carry += sn::last(x);
        };
        (one(std::integral_constant<unsigned, I>{}), ...);
    }(std::make_index_sequence<16 / sn::lanes>{});
    return mask ? std::countr_zero(mask) : 16;
}
extern "C" [[gnu::noinline]] unsigned bl_checkpoint16_select(const std::uint16_t* p, unsigned n, unsigned q) {
    const bl::direct_view<std::uint16_t, 1> v{p, n, {}};
    return bl::group_for<bl::edge::at_or_after, bl::method::select_binary>(v, std::uint16_t(q));
}
extern "C" [[gnu::noinline]] unsigned bl_scan256_count16(const std::uint16_t* p, unsigned q) {
    return boundlist_count_control::scan<true>(p, 256, std::uint16_t(q));
}
