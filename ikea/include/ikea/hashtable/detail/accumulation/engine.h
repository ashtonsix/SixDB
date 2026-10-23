#pragma once
#include "codec.h"
#include "points.h"
#include "swiss.h"
namespace ikea::hashtable::detail::accumulation {
// With a small split fanout, keep the destination images in aligned L1 scratch
// and follow source row order. Whole-vector updates remain forwardable; the
// independent bucket states interleave without short per-group lookup loops.
template <bool DeferWide = false, class D, class S, class Reduce>
[[nodiscard, gnu::aligned(64)]] merge_status accumulate_images(D& dst, const S& src,
                                                               Reduce&& reduce) {
#if defined(__AVX512VBMI__) && defined(__GFNI__)
    if constexpr (!S::bitmap && image_codec<D>::supported &&
                  (D::bucket_bits <= S::bucket_bits || D::bucket_bits - S::bucket_bits <= 3)) {
        if (const auto status = compatible(dst, src); status != merge_status::complete)
            return status;
        using codec = image_codec<D>;
        using image_type = typename codec::image;
        constexpr unsigned children = [] {
            if constexpr (D::bucket_bits > S::bucket_bits)
                return 1u << (D::bucket_bits - S::bucket_bits);
            else
                return 1u;
        }();
        constexpr unsigned banks = codec::banks, lanes = codec::lanes;
        using live_type = std::conditional_t<(D::N <= 32), unsigned, word>;
        constexpr auto live_mask = live_type(ht::mask<D::N>);
        const auto from = access::get(src);
        auto to = access::get(dst);
        const auto* source_primary = ikea::to_u8(from.primary.data);
        auto* destination_primary = ikea::to_u8(to.primary.data);
        word added = 0; // Private batch: counters need only be coherent on successful return.
        alignas(64) std::array<__m512i, children * banks> images;
        std::array<live_type, children> live;
        std::array<word, S::N> decoded;
        constexpr bool deferred = DeferWide && std::tuple_size_v<typename D::value_type> == 64;
        std::array<std::uint8_t*, deferred ? S::N : 1> value_targets;
        for (std::size_t bucket = 0; bucket < S::bucket_count; ++bucket) {
            const auto* p = source_primary + bucket * S::stride;
            auto bits = (~ht::load<S::T>(p + S::flags_offset)) & ht::mask<S::N>;
            if (!bits)
                continue;
            prepare_source<S>(p, decoded.data());
            const auto base = target_bucket<D, S>(bucket, 0);
            auto* target = destination_primary + base * D::stride;
            for (unsigned child = 0; child < children; ++child)
                prefetch_primary(base + child, dst);
            for (unsigned child = 0; child < children; ++child) {
                const auto* out = target + child * D::stride;
                const auto image = codec::read(out);
                ht::each<banks>([&](auto bank) __attribute__((always_inline)) {
                    images[child * banks + bank] = image[bank];
                });
                live[child] = live_type(~ht::load<D::T>(out + D::flags_offset)) & live_mask;
            }
            unsigned dirty = 0;
            word pending_values = 0, existing_values = 0;
            auto queue_value = [&](unsigned row, unsigned slot, std::uint8_t* out, bool existing) {
                if constexpr (deferred) {
                    auto* value = out + D::value_offset + std::size_t(slot) * 64;
                    value_targets[row] = value;
                    pending_values |= word{1} << row;
                    if (existing)
                        existing_values |= word{1} << row;
                    __builtin_prefetch(value, 1, 3);
                } else {
                    const auto value = access::primary_value<S>(p, row);
                    access::write_value<D>(
                        out, slot,
                        existing ? reduce(access::primary_value<D>(out, slot), value) : value);
                }
            };
            while (bits) {
                const auto row = unsigned(std::countr_zero(bits));
                bits &= bits - 1;
                const auto original_q = source_quotient<S>(p, decoded.data(), row);
                const auto child = [&] {
                    if constexpr (D::bucket_bits <= S::bucket_bits)
                        return 0u;
                    else
                        return unsigned(original_q >> D::K);
                }();
                __builtin_assume(child < children);
                const auto q = target_quotient<D, S>(bucket, original_q);
                auto* out = target + child * D::stride;
                auto occupied = live[child];
                const auto query = codec::query(q);
                word matches = 0;
                ht::each<banks>([&](auto bank) __attribute__((always_inline)) {
                    matches |= codec::match(images[child * banks + bank], query) << (bank * lanes);
                });
                const auto matching = live_type(matches) & occupied;
                if (matching) {
                    const auto slot = unsigned(std::countr_zero(matching));
                    queue_value(row, slot, out, true);
                } else if (occupied != live_mask && !out[D::overflow_offset]) {
                    const auto slot = unsigned(std::countr_zero(live_type(~occupied)));
                    __builtin_assume(slot < D::N);
                    if constexpr (children == 1)
                        ht::each<banks>([&](auto bank) __attribute__((always_inline)) {
                            images[bank] = codec::insert(
                                images[bank], (live_type{1} << slot) >> (bank * lanes), q);
                        });
                    else if constexpr (banks == 1)
                        images[child] = codec::insert(images[child], 1u << slot, q);
                    else {
                        const auto index = child * banks + slot / lanes;
                        images[index] = codec::insert(images[index], 1u << (slot % lanes), q);
                    }
                    live[child] = occupied | (live_type{1} << slot);
                    dirty |= 1u << child;
                    queue_value(row, slot, out, false);
                    ++added;
                } else {
                    if (dirty & (1u << child)) {
                        image_type image;
                        ht::each<banks>([&](auto bank) __attribute__((always_inline)) {
                            image[bank] = images[child * banks + bank];
                        });
                        codec::write(out, image, occupied);
                    }
                    dirty &= ~(1u << child);
                    const auto status = put_routed(
                        dst, base + child, q, access::primary_value<S>(p, row),
                        [&] {
                            const auto key = from.mix.recover(bucket, original_q);
                            return swiss_key<D>{key, to.mix(key)};
                        },
                        reduce);
                    if (status != merge_status::complete)
                        return status;
                    const auto image = codec::read(out);
                    ht::each<banks>([&](auto bank) __attribute__((always_inline)) {
                        images[child * banks + bank] = image[bank];
                    });
                    live[child] = live_type(~ht::load<D::T>(out + D::flags_offset)) & live_mask;
                }
            }
            while (dirty) {
                const auto child = unsigned(std::countr_zero(dirty));
                dirty &= dirty - 1;
                image_type image;
                ht::each<banks>([&](auto bank) __attribute__((always_inline)) {
                    image[bank] = images[child * banks + bank];
                });
                codec::write(target + child * D::stride, image, live[child]);
            }
            // V64 is eight contiguous Local64 limbs at each physical value row.
            // Source keys are unique, so delaying values cannot create a read
            // of an earlier pending value in this source. All finish before
            // the next source or Swiss traversal can observe them.
            if constexpr (deferred)
                while (pending_values) {
                    const auto row = unsigned(std::countr_zero(pending_values));
                    pending_values &= pending_values - 1;
                    auto next = access::primary_value<S>(p, row);
                    if (existing_values & (word{1} << row)) {
                        typename D::value_type previous{};
                        std::memcpy(previous.data(), value_targets[row], 64);
                        next = reduce(previous, next);
                    }
                    std::memcpy(value_targets[row], next.data(), 64);
                }
        }
        to.size += added;
        return accumulate_swiss_pipelined(dst, src, reduce);
    } else
#endif
        return accumulate_points(dst, src, std::forward<Reduce>(reduce));
}
} // namespace ikea::hashtable::detail::accumulation
