#pragma once
#include <ikea/detail/native_chain.h>
#include <ikea/boundlist/author/native.h>

namespace ikea::boundlist::sidecar {
#if defined(__aarch64__) || defined(__AVX2__)
/// Sixteen slot offsets/sizes plus a live mask. Stages share native::read16()
/// with direct composition. The owner holds source/plan state through completion;
/// suspension occurs after returning, never inside a bounded native stage.
template <unsigned Slots = 8>
using chain = ikea::detail::native_chain<native::packet, std::uint16_t, Slots, error,
                                         std::make_index_sequence<native::packet::parts>>;
/// Scalar location handoff for a geometry→payload dependency. Offset and size
/// stay in integer arguments; the owner retains the payload base and identity.
struct pipeline_location {
    using vector = std::uintptr_t;
    static constexpr unsigned parts = 2;
    std::array<vector, parts> v;
    static pipeline_location from(location p) { return {{p.offset, p.size}}; }
    unsigned offset() const { return v[0]; }
    unsigned size() const { return v[1]; }
};
template <unsigned Slots = 8>
using point_chain = ikea::detail::native_chain<pipeline_location, bool, Slots, error,
                                               std::make_index_sequence<pipeline_location::parts>>;
#endif
} // namespace ikea::boundlist::sidecar
