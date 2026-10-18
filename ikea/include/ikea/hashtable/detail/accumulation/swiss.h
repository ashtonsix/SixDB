#pragma once
#include "resolve.h"
#include <array>
namespace ikea::hashtable::detail::accumulation {
template <class D, bool Values = true, class Emit>
[[gnu::always_inline]] void primary_prefetch_offsets(Emit&& emit) {
    constexpr auto extent = [] {
        if constexpr (Values && std::tuple_size_v<typename D::value_type> == 8)
            return D::value_offset + D::N * 8;
        else
            return D::B;
    }();
    for (std::size_t offset = 0; offset < extent; offset += 64)
        emit(offset);
    // Bucket starts can be less aligned than the allocation. Sampling every
    // 64 bytes covers the end only when its final fragment fits that alignment.
    if constexpr ((extent - 1) % 64 >= D::bucket_alignment)
        emit(extent - 1);
}
template <bool Values = true, class D>
[[gnu::always_inline]] void prefetch_primary(word bucket, D& dst) {
    const auto to = access::get(dst);
    const auto* p = to.primary.data + bucket * D::stride;
    // The complete span earns its extra request on larger owners. Small hot
    // owners retain the measured prefix plan; all selection is compile-time.
    if constexpr (std::size_t(D::bucket_count) * D::stride >= 4u * 1024u * 1024u) {
        primary_prefetch_offsets<D, Values>(
            [&](std::size_t offset) { __builtin_prefetch(p + offset, 1, 3); });
    } else {
        __builtin_prefetch(p, 1, 3);
        if constexpr (Values && std::tuple_size_v<typename D::value_type> == 8)
            for (unsigned line = 0; line < (D::N * 8 + 63) / 64; ++line)
                __builtin_prefetch(p + D::value_offset + line * 64, 1, 3);
    }
}
// Bounded lookahead belongs to one accumulation call. It neither reorders
// sources nor allocates a workspace. Each source has at most one row per key.
template <class D, class S, class Reduce>
[[nodiscard]] merge_status accumulate_swiss_pipelined(D& dst, const S& src, Reduce& reduce) {
    const auto from = access::get(src);
    auto to = access::get(dst);
    struct swiss_ref {
        const byte* tuple;
        word key;
        typename D::mix_type::result hash;
    };
    std::array<swiss_ref, 32> spill;
    std::size_t scan = 0;
    while (scan < from.groups * 16) {
        unsigned used = 0;
        while (scan < from.groups * 16 && used < spill.size()) {
            const auto i = scan++;
            if (from.controls.data[i] >= 128)
                continue;
            const auto* tuple = from.slots.data + i * S::tuple_bytes;
            const auto key = ht::load<S::key_bytes>(tuple);
            const auto h = to.mix(key);
            spill[used++] = {tuple, key, h};
            prefetch_primary(h.bucket, dst);
            if (to.groups)
                __builtin_prefetch(to.controls.data + (h.swiss & (to.groups - 1)) * 16, 1, 3);
        }
        for (unsigned i = 0; i < used; ++i) {
            const auto& r = spill[i];
            typename S::value_type value{};
            if constexpr (value.size())
                std::memcpy(value.data(), r.tuple + S::key_bytes, value.size());
            if (const auto s = put_routed(
                    dst, r.hash.bucket, r.hash.quotient, value,
                    [&] { return swiss_key<D>{r.key, r.hash}; }, reduce);
                s != merge_status::complete)
                return s;
        }
    }
    return merge_status::complete;
}
} // namespace ikea::hashtable::detail::accumulation
