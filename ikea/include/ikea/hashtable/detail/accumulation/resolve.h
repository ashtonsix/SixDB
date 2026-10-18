#pragma once
#include "common.h"
namespace ikea::hashtable::detail::accumulation {
template <class D, class S> constexpr word target_bucket(word bucket, word q) {
    if constexpr (D::bucket_bits >= S::bucket_bits) {
        constexpr auto delta = D::bucket_bits - S::bucket_bits;
        const auto prefix = [&] {
            if constexpr (delta == 64)
                return word{0};
            else
                return bucket << delta;
        }();
        if constexpr (D::K == 64)
            return prefix;
        else
            return prefix | (q >> D::K);
    } else
        return bucket >> (S::bucket_bits - D::bucket_bits);
}
template <class D, class S> constexpr word target_quotient(word bucket, word q) {
    if constexpr (D::bucket_bits >= S::bucket_bits)
        return q & ht::mask<D::K>;
    else {
        constexpr auto delta = S::bucket_bits - D::bucket_bits;
        if constexpr (S::K == 64)
            return q;
        else
            return ((bucket & ht::mask<delta>) << S::K) | q;
    }
}
template <class D> struct swiss_key {
    word key;
    typename D::mix_type::result hash;
};
struct maintained_primary_store {
    template <class D> [[gnu::always_inline]] static void occupy(byte* p, unsigned row, word q) {
        ht::detail::occupy<D>(p, row, q);
    }
};
/// One primary/Swiss resolver. Store and counter location are compile-time data
/// placement choices; hit/miss, overflow and failure semantics live here once.
template <class Store, class D, class Lazy, class Reduce>
[[gnu::always_inline]] merge_status put_with_store(D& dst, byte* primary, word& count, word bucket,
                                                   word q, const typename D::value_type& value,
                                                   Lazy&& lazy, Reduce& reduce) {
    auto to = access::get(dst);
    auto* p = primary + bucket * D::stride;
    const auto row = access::primary_find<D>(p, q);
    if (row != D::N) {
        access::write_value<D>(p, row, reduce(access::primary_value<D>(p, row), value));
        return merge_status::complete;
    }
    const auto vacancy = [&] {
        if constexpr (D::bitmap)
            return unsigned(q);
        else {
            const auto free = ht::load<D::T>(p + D::flags_offset);
            return free ? unsigned(std::countr_zero(free)) : D::N;
        }
    }();
    bool resolved = false;
    swiss_key<D> key{};
    std::size_t available = none;
    auto resolve = [&](bool search) {
        if (!resolved) {
            key = lazy();
            resolved = true;
        }
        if (!to.groups)
            return none;
        auto group = key.hash.swiss & (to.groups - 1);
        std::size_t step = 0;
        for (std::size_t visited = 0; visited < to.groups; ++visited) {
            const auto* controls = to.controls.data + group * 16;
            if (search) {
                auto matches = ht::equal_controls(controls, key.hash.fingerprint);
                while (matches) {
                    const auto i = std::countr_zero(matches);
                    matches &= matches - 1;
                    const auto index = group * 16 + i;
                    if (ht::load<D::key_bytes>(to.slots.data + index * D::tuple_bytes) == key.key)
                        return index;
                }
            }
            const auto empty = ht::equal_controls(controls, control_empty);
            const auto free = empty | ht::equal_controls(controls, control_tombstone);
            if (available == none && free)
                available = group * 16 + std::countr_zero(free);
            if (empty)
                return none;
            group = (group + (++step)) & (to.groups - 1);
        }
        return none;
    };
    if constexpr (!D::bitmap)
        if (p[D::overflow_offset]) {
            const auto found = resolve(true);
            if (found != none) {
                typename D::value_type previous{};
                if constexpr (previous.size())
                    std::memcpy(previous.data(),
                                to.slots.data + found * D::tuple_bytes + D::key_bytes,
                                previous.size());
                const auto next = reduce(previous, value);
                if constexpr (next.size())
                    std::memcpy(to.slots.data + found * D::tuple_bytes + D::key_bytes, next.data(),
                                next.size());
                return merge_status::complete;
            }
        }
    if (vacancy != D::N) {
        Store::template occupy<D>(p, vacancy, q);
        access::write_value<D>(p, vacancy, value);
        ++count;
        return merge_status::complete;
    }
    if constexpr (!D::bitmap) {
        if (!to.groups || (to.spill + to.deleted + 1) * 8 > to.groups * 16 * 7)
            return merge_status::needs_overflow;
        if (!resolved)
            resolve(false);
        if (available == none)
            return merge_status::needs_overflow;
        auto* tuple = to.slots.data + available * D::tuple_bytes;
        ht::store<D::key_bytes>(tuple, key.key);
        if constexpr (value.size())
            std::memcpy(tuple + D::key_bytes, value.data(), value.size());
        to.deleted -= to.controls.data[available] == control_tombstone;
        to.controls.data[available] = key.hash.fingerprint;
        ++to.spill;
        ++count;
        if (p[D::overflow_offset] != 255)
            ++p[D::overflow_offset];
        return merge_status::complete;
    }
    return merge_status::needs_overflow;
}
template <class D, class Lazy, class Reduce>
[[gnu::always_inline]] merge_status put_routed(D& dst, word bucket, word q,
                                               const typename D::value_type& value, Lazy&& lazy,
                                               Reduce& reduce) {
    auto to = access::get(dst);
    return put_with_store<maintained_primary_store>(dst, to.primary.data, to.size, bucket, q, value,
                                                    std::forward<Lazy>(lazy), reduce);
}
template <class D, class S> merge_status compatible(D& dst, const S& src) {
    static_assert(D::key_width == S::key_width &&
                  std::is_same_v<typename D::value_type, typename S::value_type>);
    const auto a = access::get(dst);
    const auto b = access::get(src);
    if (a.primary.data == b.primary.data)
        return merge_status::alias;
    return a.mix.seed == b.mix.seed ? merge_status::complete : merge_status::incompatible;
}
} // namespace ikea::hashtable::detail::accumulation
