// The 64 width instantiations behind pack(), compiled once.
#include "packing.h"

#include <ikea/seriespack.h>

#include <cstdlib>
#include <vector>

namespace frame_encoding {
namespace sp = ikea::seriespack;
using u64 = std::uint64_t;

void pack(unsigned k, std::span<const u64> values, std::uint8_t *out) {
    bool done = false;
    sp::detail::each<64>([&](auto index) {
        constexpr unsigned K = decltype(index)::value + 1;
        if (k != K)
            return;
        using F = sp::format<K>;
        const std::size_t bytes = plane_bytes(K, values.size());
        auto v = sp::view<F, std::uint8_t>::attach(
            values.size(), {{{std::span(out, bytes), F::tile_bytes}, {}, {}}});
        if (!v)
            std::abort();
        auto prepared = sp::bind_mutation(*v);
        if (!prepared)
            std::abort();
        auto write = prepared->template erase<u64, sp::no_summary>();
        auto capacity = prepared->construction_effect_capacity();
        if (!capacity)
            std::abort();
        std::vector<sp::composition::owner_write> records(*capacity);
        ikea::source_write_journal effects{records};
        if (!write.initialize(values, effects))
            std::abort();
        done = true;
    });
    if (!done)
        std::abort();
}
}  // namespace frame_encoding
