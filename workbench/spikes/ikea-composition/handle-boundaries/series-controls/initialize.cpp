#include "controls.h"
#include <ikea/seriespack/write.h>
#include <cstdlib>

namespace series_controls {
namespace {
template <class L> auto attach(const destination& dst) {
    // The admitted selector chose this callback: L is dst.control's law.
    // Extent checking below does not authenticate another encoding's bytes.
    return sp::view<L>::attach(dst.count, {{{dst.bytes, L::tile_bytes}, {}, {}}});
}
template <class View> void resolve(const destination& dst, const View& view, ikea::journal& effects) {
    // Resolve the WHOLE journal: a command may have coalesced an old last entry.
    // Earlier entries already use stable destination identities. No old entry
    // may survive with a transient view identity or changed backing generation.
    for (auto& entry : effects.storage.first(effects.used))
        if (entry.source == &view) entry.source = &dst;
}
template <class L>
std::expected<void, sp::error> initialize(const destination& dst, std::span<const word> input, ikea::journal& effects) {
    auto view = attach<L>(dst);
    if (!view) return std::unexpected(view.error());
    auto write = sp::bind_writer(*view);
    if (!write) return std::unexpected(write.error());
    auto result = write->initialize(input, effects);
    if (result) resolve(dst, *view, effects);
    return result;
}
template <class L>
word typed_initialize(const destination& dst, std::span<const word> a, std::span<const word> b,
                      unsigned reuse, word epoch, ikea::journal& effects) {
    auto view = attach<L>(dst); if (!view) std::abort();
    auto write = sp::bind_writer(*view); if (!write) std::abort();
    word sum = 0;
    for (unsigned q = 0; q < reuse; ++q) {
        effects.used = 0;
        if (!write->initialize((epoch + q) & 1 ? a : b, effects)) std::abort();
        resolve(dst, *view, effects);
        sum += initialized(dst, effects);
    }
    return sum;
}
template <std::size_t... K> constexpr auto initialize_table(std::index_sequence<K...>) {
    std::array<initialize_function, keys> result{};
    ([&] {
        result[K] = initialize<sp::layout<K + 1>>;
        if constexpr (sp::striped_width(K + 1)) result[K + 64] = initialize<sp::layout<K + 1, sp::geometry::striped>>;
    }(), ...);
    return result;
}
template <std::size_t... K> constexpr auto typed_table(std::index_sequence<K...>) {
    std::array<typed_initialize_function, keys> result{};
    ([&] {
        result[K] = typed_initialize<sp::layout<K + 1>>;
        if constexpr (sp::striped_width(K + 1)) result[K + 64] = typed_initialize<sp::layout<K + 1, sp::geometry::striped>>;
    }(), ...);
    return result;
}
}
const std::array<initialize_function, keys> initialize_columns = initialize_table(std::make_index_sequence<64>{});
const std::array<typed_initialize_function, keys> typed_initialize_columns = typed_table(std::make_index_sequence<64>{});
std::expected<void, sp::error> initialize_dispatch(const destination& dst, std::span<const word> input, ikea::journal& effects) {
    // Same 64-width checked setup pattern as Frame's current pack adapter;
    // Local/Striped cases reuse initialize<L>, never a second encoding body.
    std::expected<void, sp::error> result = std::unexpected(sp::error::description);
    sp::detail::each<64>([&](auto k) {
        if (dst.control.width() != k + 1) return;
        if (dst.control.geometry() == sp::geometry::local)
            result = initialize<sp::layout<k + 1>>(dst, input, effects);
        else if constexpr (sp::striped_width(k + 1))
            result = initialize<sp::layout<k + 1, sp::geometry::striped>>(dst, input, effects);
    });
    return result;
}
word initialized(const destination& dst, ikea::journal& effects) {
    // One compiler observation per complete initialize command, never per row.
    // The caller's owner may consume every initialized byte at this boundary.
    asm volatile("" : : "r"(dst.bytes.data()) : "memory");
    word value = effects.used;
    for (const auto& entry : effects.entries()) {
        if (entry.source != &dst || entry.plane != 0 || entry.offset > dst.bytes.size() ||
            entry.size > dst.bytes.size() - entry.offset) std::abort();
        value += entry.offset + entry.size;
    }
    return value + (dst.bytes.empty() ? 0 : std::to_integer<unsigned>(dst.bytes.front()));
}
}
