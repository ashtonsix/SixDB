#pragma once
#include <ikea2/seriespack/detail/wire.h>
#include <ikea2/seriespack/view.h>
#include <ikea2/seriespack/effects.h>
namespace ikea2::seriespack {
/// This footprint belongs to write16: complete Local packets, contiguous
/// body bytes, and the one or two striped residual fields it actually stores.
/// Masks suppress an empty region; nonempty selected writes preserve inactive
/// values in native lanes and may issue stores covering those neighbors.
template <class F, unsigned Fields = 15, class Emit>
inline void visit_writes16(const view<F, std::uint8_t>& v, std::size_t i, Emit&& emit) {
    const auto lane = i % F::tile_rows, tile = i / F::tile_rows;
    if constexpr (F::payload) {
        const auto offset = tile * v.stream(0).stride;
        if constexpr (F::storage == geometry::local) {
            detail::each<2>([&](auto p) {
                const auto start = offset + p * v.stream(0).stride;
                if constexpr ((Fields & 3) == 3)
                    emit(byte_write{0, start, F::tile_bytes});
                else {
                    if constexpr (F::body && (Fields & 1))
                        emit(byte_write{0, start, 8 * F::body});
                    if constexpr (F::tail && (Fields & 2))
                        emit(byte_write{0, start + 8 * F::body, F::tail});
                }
            });
        } else {
            if constexpr (F::body && (Fields & 1))
                emit(byte_write{0, offset + detail::body_offset<F>(lane), 16 * F::body});
            if constexpr (F::tail && (Fields & 2))
                detail::tail_fragments<F::tail>(lane / 32, [&](unsigned s, unsigned, unsigned) {
                    emit(byte_write{0, offset + detail::stripe_offset<F>(s) + lane % 32, 16});
                });
        }
    }
    detail::each<F::heads / 8>([&](auto p) {
        if constexpr (Fields & (4u << p)) {
            const auto& head = v.stream(p + 1);
            const auto offset = tile * head.stride + lane;
            if constexpr (F::storage == geometry::local) {
                emit(byte_write{p + 1, offset, 8});
                emit(byte_write{p + 1, offset + head.stride, 8});
            } else
                emit(byte_write{p + 1, offset, 16});
        }
    });
}

/// Exact scalar-store footprint, including bytes with preserved neighboring
/// bits. A physical point update does not imply exclusive ownership of a byte.
template <class F, unsigned Fields = 15, class Emit>
inline void visit_writes_point(const view<F, std::uint8_t>& v, std::size_t i, Emit&& emit) {
    const auto lane = i % F::tile_rows, tile = i / F::tile_rows;
    if constexpr (F::payload) {
        const auto offset = tile * v.stream(0).stride;
        if constexpr (F::body && (Fields & 1))
            emit(byte_write{0, offset + detail::body_offset<F>(lane), F::body});
        if constexpr (F::tail && (Fields & 2)) {
            if constexpr (F::storage == geometry::local)
                emit(byte_write{0, offset + 8 * F::body, F::tail});
            else
                detail::tail_fragments<F::tail>(lane / 32, [&](unsigned stripe, unsigned,
                                                               unsigned) {
                    emit(byte_write{0, offset + detail::stripe_offset<F>(stripe) + lane % 32, 1});
                });
        }
    }
    detail::each<F::heads / 8>([&](auto p) {
        if constexpr (Fields & (4u << p))
            emit(byte_write{p + 1, tile * v.stream(p + 1).stride + lane, 1});
    });
}

} // namespace ikea2::seriespack
