#pragma once
#include <ikea/hashtable/accumulate.h>
#include <span>
#include <vector>
namespace merge_study {
namespace ht = ikea::hashtable;
namespace impl = ht::detail::accumulation;
using ht::byte;
using ht::word;
using access = ht::author::table_access;
using ht::reserve_overflow;
using impl::target_bucket;
using impl::target_quotient;
using impl::swiss_key;
inline constexpr auto none = ht::detail::no_slot;
enum class merge_status { complete, incompatible, alias, needs_overflow, needs_workspace };
constexpr merge_status research_status(ht::accumulation_status status) {
    switch (status) {
    case ht::accumulation_status::complete:
        return merge_status::complete;
    case ht::accumulation_status::incompatible:
        return merge_status::incompatible;
    case ht::accumulation_status::alias:
        return merge_status::alias;
    case ht::accumulation_status::needs_overflow:
        return merge_status::needs_overflow;
    }
    std::unreachable();
}
template <class D, class S>
constexpr std::size_t group_count = std::min(D::bucket_count, S::bucket_count);
template <class D, class S> merge_status compatible(D& dst, const S& src) {
    return research_status(impl::compatible(dst, src));
}
template <class D, class Lazy, class Reduce>
[[gnu::always_inline]] merge_status put_routed(D& dst, word bucket, word q,
                                               const typename D::value_type& value, Lazy&& lazy,
                                               Reduce& reduce) {
    return research_status(
        impl::put_routed(dst, bucket, q, value, std::forward<Lazy>(lazy), reduce));
}
template <bool Defer = false, class D, class S, class Reduce>
merge_status accumulate_images(D& dst, const S& src, Reduce&& reduce) {
    return research_status(impl::accumulate_images<Defer>(dst, src, std::forward<Reduce>(reduce)));
}
template <class D, class S, class Reduce>
merge_status accumulate_points(D& dst, const S& src, Reduce&& reduce) {
    return research_status(impl::accumulate_points(dst, src, std::forward<Reduce>(reduce)));
}
template <class D, class S, class Reduce>
[[gnu::always_inline]] merge_status primary_bucket(D& dst, const S& src, std::size_t bucket,
                                                   Reduce& reduce) {
    const auto from = access::get(src);
    const auto* p = from.primary.data + bucket * S::stride;
    for (unsigned row = 0; row < S::N; ++row)
        if (ht::detail::live_row<S>(p, row)) {
            const word q = [&] {
                if constexpr (S::bitmap)
                    return word(row);
                else
                    return ht::point<S::encoding>(p, row, S::N);
            }();
            const auto status = put_routed(
                dst, target_bucket<D, S>(bucket, q), target_quotient<D, S>(bucket, q),
                access::primary_value<S>(p, row),
                [&] {
                    const auto key = from.mix.recover(bucket, q);
                    return swiss_key<D>{key, access::get(dst).mix(key)};
                },
                reduce);
            if (status != merge_status::complete)
                return status;
        }
    return merge_status::complete;
}
/// Fixed geometry/capacity. Discard dst on failure/exception. Sources coherent.
template <class D, class S, class Reduce>
[[nodiscard]] merge_status accumulate(D& dst, const S& src, Reduce&& reduce) {
    if (const auto s = compatible(dst, src); s != merge_status::complete)
        return s;
    for (std::size_t b = 0; b < S::bucket_count; ++b)
        if (const auto s = primary_bucket(dst, src, b, reduce); s != merge_status::complete)
            return s;
    const auto from = access::get(src);
    for (std::size_t i = 0; i < from.groups * 16; ++i)
        if (from.controls.data[i] < 128) {
            const auto* tuple = from.slots.data + i * S::tuple_bytes;
            const auto key = ht::load<S::key_bytes>(tuple);
            const auto h = access::get(dst).mix(key);
            typename S::value_type value{};
            if constexpr (value.size())
                std::memcpy(value.data(), tuple + S::key_bytes, value.size());
            if (const auto s = put_routed(
                    dst, h.bucket, h.quotient, value, [&] { return swiss_key<D>{key, h}; }, reduce);
                s != merge_status::complete)
                return s;
        }
    return merge_status::complete;
}
struct kway_workspace {
    struct spill_ref {
        std::size_t source, next;
        const byte* tuple;
        word bucket, q, hash;
        byte tag;
    };
    std::unique_ptr<std::size_t[]> heads;
    std::unique_ptr<spill_ref[]> spills;
    std::size_t group_capacity = 0, spill_capacity = 0;
    void reserve(std::size_t groups, std::size_t spill_count) {
        if (groups > group_capacity) {
            heads = std::make_unique_for_overwrite<std::size_t[]>(groups);
            group_capacity = groups;
        }
        if (spill_count > spill_capacity) {
            spills = std::make_unique_for_overwrite<spill_ref[]>(spill_count);
            spill_capacity = spill_count;
        }
    }
    std::size_t bytes() const {
        return group_capacity * sizeof(std::size_t) + spill_capacity * sizeof(spill_ref);
    }
};
/// Strict source order, initial dst first. Homogeneous inputs; dst may differ.
template <class D, class S, class Reduce>
[[nodiscard]] merge_status accumulate_kway(D& dst, std::span<const S* const> sources,
                                           kway_workspace& work, Reduce&& reduce) {
    std::size_t spill_count = 0;
    for (const auto* src : sources) {
        if (!src)
            return merge_status::incompatible;
        if (const auto s = compatible(dst, *src); s != merge_status::complete)
            return s;
        const auto count = access::get(*src).spill;
        if (count > none - spill_count)
            return merge_status::needs_workspace;
        spill_count += count;
    }
    if (sources.empty())
        return merge_status::complete;
    constexpr auto groups = group_count<D, S>;
    if (work.group_capacity < groups || work.spill_capacity < spill_count)
        return merge_status::needs_workspace;
    std::fill_n(work.heads.get(), groups, none);
    std::size_t used = 0;
    // Reverse-source prepending preserves source order within every group.
    for (std::size_t n = sources.size(); n-- > 0;) {
        const auto from = access::get(*sources[n]);
        for (std::size_t i = 0; i < from.groups * 16; ++i)
            if (from.controls.data[i] < 128) {
                const auto* tuple = from.slots.data + i * S::tuple_bytes;
                const auto h = access::get(dst).mix(ht::load<S::key_bytes>(tuple));
                const auto group = h.bucket / (D::bucket_count / groups);
                work.spills[used] = {n,          work.heads[group], tuple,        h.bucket,
                                     h.quotient, h.swiss,           h.fingerprint};
                work.heads[group] = used++;
            }
    }
    constexpr auto parents = S::bucket_count / groups;
    for (std::size_t g = 0; g < groups; ++g) {
        auto pending = work.heads[g];
        for (std::size_t n = 0; n < sources.size(); ++n) {
            for (std::size_t p = g * parents; p < (g + 1) * parents; ++p)
                if (const auto s = primary_bucket(dst, *sources[n], p, reduce);
                    s != merge_status::complete)
                    return s;
            while (pending != none && work.spills[pending].source == n) {
                const auto& ref = work.spills[pending];
                typename S::value_type value{};
                if constexpr (value.size())
                    std::memcpy(value.data(), ref.tuple + S::key_bytes, value.size());
                if (const auto s = put_routed(
                        dst, ref.bucket, ref.q, value,
                        [&] {
                            return swiss_key<D>{ht::load<S::key_bytes>(ref.tuple),
                                                {ref.bucket, ref.q, ref.hash, ref.tag}};
                        },
                        reduce);
                    s != merge_status::complete)
                    return s;
                pending = ref.next;
            }
        }
        ht::require(pending == none, "ordered spill partition drained");
    }
    return merge_status::complete;
}
template <class D, class S>
    requires(std::tuple_size_v<typename D::value_type> == 0)
[[nodiscard]] merge_status accumulate(D& dst, const S& src) {
    return accumulate(dst, src, [](auto a, auto) { return a; });
}
template <class D, class S>
    requires(std::tuple_size_v<typename D::value_type> == 0)
[[nodiscard]] merge_status accumulate_kway(D& dst, std::span<const S* const> sources,
                                           kway_workspace& work) {
    return accumulate_kway(dst, sources, work, [](auto a, auto) { return a; });
}

} // namespace merge_study
