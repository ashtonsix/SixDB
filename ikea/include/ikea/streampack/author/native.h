#pragma once
#include <ikea/streampack/author/read.h>
#include <ikea/streampack/detail/compact/author/native.h>

namespace ikea::streampack::native {
using namespace ikea::streampack::detail::compact::native;
#if defined(__aarch64__) || defined(__AVX2__)
using ikea::streampack::detail::compact::native::chunks;
/// Sixteen offsets and lengths, flattened into native registers at chain hops.
/// Slots outside the accompanying live mask have no logical value.
struct packet {
    using vector = native::vector;
    static constexpr unsigned parts = 2 * chunks;
    std::array<vector, parts> v;
    vector offsets(unsigned chunk) const { return v[chunk]; }
    vector lengths(unsigned chunk) const { return v[chunks + chunk]; }
};
struct group {
    packet values;
    std::uint16_t live;
};
using ikea::streampack::detail::compact::native::each_chunk;
/// One admitted group; can be a partial final group. Loads access initialized
/// whole groups, never the payload or a terminal checkpoint. Native results feed
/// a fused consumer directly; no mandatory decoded stack array.
template <class Access> [[gnu::always_inline]] inline group read16(Access a, unsigned group_index) {
    using W = typename Access::used_type;
    const auto &l = a.format();
    const auto first = group_index * 16;
    group out;
    out.live = ikea::streampack::detail::compact::native::decode16(
        a, group_index, [&](auto i, vector starts, vector reserved) __attribute__((always_inline)) {
            out.values.v[i] = starts;
            out.values.v[chunks + i] = l.has_used() ? load<W>(a.source().windows().lengths.data() +
                                                              a.used_offset(first + i * lanes))
                                                    : reserved;
        });
    if (l.has_tombstones())
        out.live &= ~ikea::streampack::detail::compact::detail::load<std::uint16_t>(
            a.source().windows().directory.data() + first / 8);
    return out;
}
#endif
} // namespace ikea::streampack::native
