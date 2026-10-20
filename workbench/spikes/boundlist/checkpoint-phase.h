#pragma once
#include <ikea/boundlist/search.h>
namespace checkpoint_spike {
template<bool Padded,ikea::boundlist::edge E,class W,class O,class Q>
[[gnu::always_inline]] auto prepare(const ikea::boundlist::width_source<W,O>& source,Q point) {
    namespace b=ikea::boundlist;
    using result=b::detail::width_search<W,E>;
    const b::detail::prepared_query<O,Q> query(point);
    if(query.relation) return result{nullptr,query.relation<0?0u:source.count,0};
    const auto n=source.groups();
    if(!n || !b::detail::meets<E>(source.extent,query.value))return result{nullptr,source.count,0};
    assert(n<=16);
    // Padded proves sixteen checkpoints readable, not sixteen assigned groups.
    // Ignore inactive comparisons; their stored values may be arbitrary.
    const auto mask=b::detail::native_mask<E>(source.checkpoints,Padded?16:n,query.value)&unsigned(b::detail::low_bits(n));
    const auto next=mask?unsigned(std::countr_zero(mask)):n;
    const auto g=next?next-1:0,first=g*16;
    const auto base=b::detail::read_element(source.checkpoints,g);
    if(query.value<base || (E==b::edge::at_or_after && query.value==base))return result{nullptr,first,0};
    const O relative=query.value-base;
    if(relative>16u*std::numeric_limits<W>::max())return result{nullptr,source.count,0};
    return result{source.group(g),first,unsigned(relative)};
}
}
