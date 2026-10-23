#pragma once
#include "resolve.h"
namespace ikea::hashtable::detail::accumulation {
template <unsigned R> [[gnu::always_inline]] word spread_columns(word v) {
    static_assert(R > 0 && R < 8);
    constexpr word columns = 0x0101010101010101ULL >> (8 * (8 - R));
#if defined(__BMI2__) && defined(__x86_64__)
    return _pdep_u64(v, columns);
#else
    constexpr word multiply = [] {
        word m = 0;
        for (unsigned i = 0; i < R; ++i)
            m |= word{1} << (7 * i);
        return m;
    }();
    // The R-bit copies start seven bits apart, so R<=7 creates no carries.
    // Masking the diagonal retains input bit i at output bit 8*i.
    return ((v & ht::mask<R>)*multiply) & columns;
#endif
}
template <unsigned E> [[gnu::always_inline]] void assign_point(std::uint8_t* p, unsigned row, word q) {
    constexpr unsigned B = E / 8, R = E % 8;
    const auto lane = row % 8;
    auto* tile = p + (row / 8) * E;
    if constexpr (B)
        ht::store<B>(tile + lane * B, q >> R);
    if constexpr (R) {
        constexpr word columns = 0x0101010101010101ULL >> (8 * (8 - R));
        auto* tail = tile + 8 * B;
        ht::store<R>(tail,
                     (ht::load<R>(tail) & ~(columns << lane)) | (spread_columns<R>(q) << lane));
    }
}
template <class D> [[gnu::always_inline]] void occupy_point(std::uint8_t* p, unsigned row, word q) {
    if constexpr (D::bitmap)
        ht::detail::occupy<D>(p, row, q);
    else {
        assign_point<D::encoding>(p, row, q);
        p[D::flags_offset + row / 8] &= std::uint8_t(~(1u << (row % 8)));
    }
}
struct column_primary_store {
    template <class D> [[gnu::always_inline]] static void occupy(std::uint8_t* p, unsigned row, word q) {
        occupy_point<D>(p, row, q);
    }
};
template <class D, class Lazy, class Reduce>
[[gnu::always_inline]] merge_status put_point(D& dst, std::uint8_t* primary, word& added, word bucket,
                                              word q, const typename D::value_type& value,
                                              Lazy&& lazy, Reduce& reduce) {
    return put_with_store<column_primary_store>(dst, primary, added, bucket, q, value,
                                                std::forward<Lazy>(lazy), reduce);
}
template <class D, class S, class Reduce>
[[nodiscard]] merge_status accumulate_points(D& dst, const S& src, Reduce&& reduce) {
    if (const auto s = compatible(dst, src); s != merge_status::complete)
        return s;
    const auto from = access::get(src);
    auto to = access::get(dst);
    const auto* source_primary = ikea::to_u8(from.primary.data);
    auto* target_primary = ikea::to_u8(to.primary.data);
    word added = 0;
    const auto source_mix = from.mix;
    const auto target_mix = to.mix;
    for (std::size_t bucket = 0; bucket < S::bucket_count; ++bucket) {
        const auto* p = source_primary + bucket * S::stride;
        for (unsigned row = 0; row < S::N; ++row)
            if (ht::detail::live_row<S>(p, row)) {
                const auto q = [&] {
                    if constexpr (S::bitmap)
                        return word(row);
                    else
                        return ht::point<S::encoding>(p, row, S::N);
                }();
                const auto status = put_point(
                    dst, target_primary, added, target_bucket<D, S>(bucket, q),
                    target_quotient<D, S>(bucket, q), access::primary_value<S>(p, row),
                    [&] {
                        const auto k = source_mix.recover(bucket, q);
                        return swiss_key<D>{k, target_mix(k)};
                    },
                    reduce);
                if (status != merge_status::complete)
                    return status;
            }
    }
    for (std::size_t i = 0; i < from.groups * 16; ++i)
        if (ht::detail::occupied_control(ikea::to_u8(from.controls.data)[i])) {
            const auto* tuple = ikea::to_u8(from.slots.data) + i * S::tuple_bytes;
            const auto key = ht::load<S::key_bytes>(tuple);
            const auto h = target_mix(key);
            typename S::value_type value{};
            if constexpr (value.size())
                std::memcpy(value.data(), tuple + S::key_bytes, value.size());
            if (const auto s = put_point(
                    dst, target_primary, added, h.bucket, h.quotient, value,
                    [&] { return swiss_key<D>{key, h}; }, reduce);
                s != merge_status::complete)
                return s;
        }
    to.size += added;
    return merge_status::complete;
}
} // namespace ikea::hashtable::detail::accumulation
