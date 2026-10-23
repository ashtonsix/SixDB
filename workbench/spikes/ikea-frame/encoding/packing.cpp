// The 64 width instantiations behind pack(), compiled once.
#include "packing.h"

#include <ikea/seriespack.h>

#include <cstdlib>
#include <utility>
#include <vector>

namespace frame_encoding {
namespace sp = ikea::seriespack;
using u64 = std::uint64_t;

namespace {
// Calls f(integral_constant<I>) for each width index, in order.
template <class F, std::size_t... I> void each_width(F &&f, std::index_sequence<I...>) {
    (f(std::integral_constant<std::size_t, I>{}), ...);
}
}  // namespace

void pack(unsigned k, std::span<const u64> values, std::uint8_t *out) {
    bool done = false;
    each_width([&](auto index) {
        constexpr unsigned K = decltype(index)::value + 1;
        if (k != K)
            return;
        using F = sp::layout<K>;
        const std::size_t bytes = plane_bytes(K, values.size());
        auto v = sp::view<F>::attach(
            values.size(), {{{std::as_writable_bytes(std::span(out, bytes)), F::tile_bytes}, {}, {}}});
        if (!v)
            std::abort();
        auto prepared = sp::bind_writer(*v);
        if (!prepared)
            std::abort();
        auto write = prepared->template erase<u64, sp::author::no_summary>();
        auto capacity = prepared->effect_capacity_initialize();
        if (!capacity)
            std::abort();
        std::vector<ikea::journal::entry> records(*capacity);
        ikea::journal effects{records};
        if (!write.initialize(values, effects))
            std::abort();
        done = true;
    }, std::make_index_sequence<64>{});
    if (!done)
        std::abort();
}
}  // namespace frame_encoding
