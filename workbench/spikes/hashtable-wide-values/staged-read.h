#pragma once
#include "model.h"
#include <array>

namespace wide {
// Sixteen outstanding queries, matching the native Loom bank. Each batch first
// resolves ID-cell addresses, then loads IDs/prefetches payloads, then consumes.
// Consumption admits the next batch's key matches without retaining temporaries.
template <unsigned Bytes, ht::lookup_hint Hint, unsigned Distance, class M, class R>
[[gnu::noinline]] R staged_read_range(const M& model, const Key* queries, std::size_t first,
                                     std::size_t count, std::size_t limit) {
    struct Pending { const byte* address{}; Key key{}; bool found{}; };
    std::array<Pending, 16> pending;
    R result;
    std::size_t next = first;
    const auto end = first + count;
    const auto admit = [&](Pending& item) {
        if constexpr (Distance)
            if (next + Distance < limit) model.prefetch(queries[next + Distance]);
        item.key = queries[next++];
        const auto cell = model.template locate_id<Hint>(item.key);
        item.address = cell.address; item.found = cell.found;
        __builtin_prefetch(item.address, 0, 3);
        __builtin_prefetch(item.address + cell_bytes<M::kind> - 1, 0, 3);
    };
    auto active = std::min(pending.size(), count);
    for (std::size_t lane = 0; lane < active; ++lane) admit(pending[lane]);
    while (active) {
        for (std::size_t lane = 0; lane < active; ++lane) {
            auto& item = pending[lane];
            item.address = model.resolve_id(item.address);
            __builtin_prefetch(item.address, 0, 3);
            if constexpr (Bytes > 64) __builtin_prefetch(item.address + 64, 0, 3);
            if constexpr (Bytes > 1) __builtin_prefetch(item.address + Bytes - 1, 0, 3);
        }
        const auto following = std::min(pending.size(), end - next);
        for (std::size_t lane = 0; lane < active; ++lane) {
            auto& item = pending[lane];
            word sum{};
            for (unsigned at = 0; at < Bytes; at += 8) sum += ht::load<8>(item.address + at);
            result.digest += sum;
            result.hits += item.found;
            result.keys += item.found ? item.key : 0;
            if (lane < following) admit(item);
        }
        active = following;
    }
    return result;
}
} // namespace wide
