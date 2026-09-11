#pragma once
#include <ikea/seriespack/detail/wire.h>
namespace ikea::seriespack::detail {
/// Occupied fields within one physical tile. This shared physical description
/// drives writable-leaf admission and construction coverage/initialization.
/// Point/region footprints remain separate because their issued stores can be
/// narrower than a field, or preserve neighboring bits within it.
template <class F, unsigned Fields, class Emit> void tile_fields(Emit&& emit) {
    if constexpr (F::payload && (Fields & 3)) {
        if constexpr ((Fields & 3) == 3)
            emit(0u, std::size_t{0}, F::tile_bytes);
        else if constexpr (F::storage == geometry::local) {
            if constexpr (F::body && (Fields & 1))
                emit(0u, std::size_t{0}, std::size_t{8 * F::body});
            if constexpr (F::tail && (Fields & 2))
                emit(0u, std::size_t{8 * F::body}, std::size_t{F::tail});
        } else {
            if constexpr (F::body && (Fields & 1))
                each<F::tile_rows / 32>([&](auto group) {
                    emit(0u, body_offset<F>(group * 32), std::size_t{32 * F::body});
                });
            if constexpr (F::tail && (Fields & 2))
                each<F::tail / std::gcd(F::tail, 8u)>(
                    [&](auto stripe) { emit(0u, stripe_offset<F>(stripe), std::size_t{32}); });
        }
    }
    each<F::heads / 8>([&](auto head) {
        if constexpr (Fields & (4u << head))
            emit(unsigned(head + 1), std::size_t{0}, F::tile_rows);
    });
}
} // namespace ikea::seriespack::detail
