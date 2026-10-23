#pragma once
#include <ikea/author/chain.h>
#include <ikea/streampack/author/native.h>

namespace ikea::streampack::author {
#if defined(__aarch64__) || defined(__AVX2__)
/// Sixteen slot offsets/lengths plus a live mask. Stages share native::read16()
/// with direct composition. The owner holds source/plan state through completion;
/// suspension occurs after returning, never inside a bounded native stage.
template <unsigned Slots = 8>
using chain = ikea::author::chain<native::packet, std::uint16_t, Slots,
                                         std::make_index_sequence<native::packet::parts>>;
/// Scalar location handoff for a geometry→payload dependency. Offset and length
/// stay in integer arguments; the owner retains the payload base and identity.
struct pipeline_location {
    using vector = std::uintptr_t;
    static constexpr unsigned parts = 2;
    std::array<vector, parts> v;
    static pipeline_location from(location p) {
        return {{p.offset, p.length}};
    }
    unsigned offset() const {
        return v[0];
    }
    unsigned length() const {
        return v[1];
    }
};
template <unsigned Slots = 8>
using point_chain = ikea::author::chain<pipeline_location, bool, Slots,
                                               std::make_index_sequence<pipeline_location::parts>>;
#endif
} // namespace ikea::streampack::author
