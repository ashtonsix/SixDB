// Width-specialised plane filters that emit row masks, compiled once for the
// predicate emitter (predicate.h). Each is the SeriesPack filter_unchecked call
// that the catalogue's masked_count_ge/eq make (kernels.cpp), returning the
// window's bits instead of their popcount; no bit algorithm is copied.
#include "material.h"  // predicate.h, included at its end

#include <ikea/seriespack.h>

#include <utility>

namespace frame_encoding {
namespace {
namespace sp = ikea::seriespack;

// filter_unchecked may set bits outside a sparse active word (its 64-row
// windows can compare every lane), so every result is ANDed with it.
template <ikea::comparison Op, unsigned K>
void filter(const std::uint8_t *data, const std::uint64_t *active, std::uint64_t rhs, std::uint64_t *out) {
    const sp::dense_source<sp::layout<K>> source{reinterpret_cast<const std::byte*>(data), 256};
    for (unsigned word = 0; word < 4; ++word)
        out[word] = active[word] ? sp::author::filter_unchecked<Op>(sp::author::expression(source), word * 64, 64, rhs, active[word]) & active[word] : 0;
}

// lo <= r < end as (r >= lo) AND NOT (r >= end); the second filter runs only
// on words the first left non-empty.
template <unsigned K>
void between(const std::uint8_t *data, const std::uint64_t *active, std::uint64_t lo, std::uint64_t end,
             std::uint64_t *out) {
    constexpr auto ge = ikea::comparison::greater_equal;
    const sp::dense_source<sp::layout<K>> source{reinterpret_cast<const std::byte*>(data), 256};
    for (unsigned word = 0; word < 4; ++word) {
        std::uint64_t m = active[word] ? sp::author::filter_unchecked<ge>(sp::author::expression(source), word * 64, 64, lo, active[word]) & active[word] : 0;
        if (m)
            m &= ~sp::author::filter_unchecked<ge>(sp::author::expression(source), word * 64, 64, end, m);
        out[word] = m;
    }
}

template <std::size_t... K> constexpr plane_filter_catalogue_t catalogue(std::index_sequence<K...>) {
    return {{nullptr, filter<ikea::comparison::greater_equal, K + 1>...},
            {nullptr, filter<ikea::comparison::equal, K + 1>...},
            {nullptr, between<K + 1>...}};
}
}  // namespace

const plane_filter_catalogue_t plane_filters = catalogue(std::make_index_sequence<64>{});
}  // namespace frame_encoding
