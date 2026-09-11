#pragma once
#include <ikea/seriespack/representation.h>

namespace ikea::seriespack {
/// Remaining-payload policy only. Head separation is the independent H argument
/// of preset_format; an ISA-oriented preset does not restrict the execution ISA.
enum class preset { compact, bulk_x86, bulk_arm };
constexpr geometry resolve(preset p, unsigned payload) {
    if (p == preset::compact)
        return geometry::local;
    if (p == preset::bulk_arm)
        return striped_width(payload) ? geometry::striped : geometry::local;
    return ((payload > 0 && payload < 8) || payload == 12 || payload == 20) ? geometry::striped
                                                                            : geometry::local;
}
template <unsigned K, preset P = preset::compact, unsigned H = 0>
using preset_format = format<K, resolve(P, K - H), H>;
enum class tile_spacing { tight, cacheline };
/// Suggested independent-plane placement. A compound owner may instead supply
/// explicit strides/offsets for interleaving; that does not change the wire law.
template <class F>
constexpr representation describe_preset(std::uint64_t count,
                                         tile_spacing spacing = tile_spacing::tight) {
    auto stride = [&](std::uint64_t n) {
        return !n ? 0 : spacing == tile_spacing::tight ? n : (n + 63) & ~std::uint64_t{63};
    };
    return {F::width,
            F::heads,
            F::storage,
            count,
            {stride(F::tile_bytes), stride(F::heads >= 8 ? F::tile_rows : 0),
             stride(F::heads == 16 ? F::tile_rows : 0)}};
}
} // namespace ikea::seriespack
