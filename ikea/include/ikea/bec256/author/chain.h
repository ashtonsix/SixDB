#pragma once
#include <ikea/bec256/author/native.h>
#include <ikea/detail/native_chain.h>

namespace ikea::bec256 {
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
/// Two decoded blocks, flattened at continuation boundaries. The low/high
/// halves have caller-defined original identities; adjacency is not required.
struct pipeline_bits {
#if defined(IKEA_BEC256_AVX512)
    using vector = __m512i;
    static constexpr unsigned parts = 1;
    std::array<vector, parts> v;
    static pipeline_bits from(native::pair value) { return {{value}}; }
    native::pair get() const { return v[0]; }
#else
    using vector = uint8x16_t;
    static constexpr unsigned parts = 4;
    std::array<vector, parts> v;
    static pipeline_bits from(native::pair value) {
        return {{value.val[0], value.val[1], value.val[2], value.val[3]}};
    }
    native::pair get() const { return {{v[0], v[1], v[2], v[3]}}; }
#endif
};
/// Shared inline bodies can be stages at a useful native grain. The caller
/// defines Mask semantics and retains source bindings through completion.
/// Explicit owner suspension is after run returns, never inside a stage.
template <unsigned Slots = 8, class Mask = std::uint64_t>
using chain = ikea::detail::native_chain<pipeline_bits, Mask, Slots, error,
                                         std::make_index_sequence<pipeline_bits::parts>>;
#endif
} // namespace ikea::bec256
