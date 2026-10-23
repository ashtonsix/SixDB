#pragma once
#include <ikea/seriespack/author/expression.h>

namespace ikea::seriespack::author::range_detail {
template <class E> auto physical_source(const E&) {
    return nullptr;
}
template <unsigned K, unsigned H, unsigned P, class S>
const S*
physical_source(const value_expression<
                K, H, payload_expression<P, leaf<S, field_kind::body>, leaf<S, field_kind::tail>>,
                leaf<S, field_kind::head0>, leaf<S, field_kind::head1>>& value) {
    using F = typename S::layout_type;
    if constexpr (K != F::width || H != F::heads)
        return nullptr;
    const S* source;
    if constexpr (P >= 8)
        source = &value.payload.body.source;
    else if constexpr (P)
        source = &value.payload.tail.source;
    else
        source = &value.head0.source;
    if constexpr (P >= 8)
        if (source != &value.payload.body.source)
            return nullptr;
    if constexpr (P % 8)
        if (source != &value.payload.tail.source)
            return nullptr;
    if constexpr (H >= 8)
        if (source != &value.head0.source)
            return nullptr;
    if constexpr (H == 16)
        if (source != &value.head1.source)
            return nullptr;
    return source;
}
} // namespace ikea::seriespack::author::range_detail
