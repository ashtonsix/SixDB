// Private assignment-batch programs, not a Frame mutation interface.
#pragma once
#include "phase_construction.h"
#include <algorithm>
#include <numeric>
#include <vector>

namespace frame_lifecycle::phase {
struct Edit { unsigned row; LogicalRow next; };

// Row order groups work; ordinal order preserves all assignments to each row.
// Keeping intermediates makes block-fold physical NULL fill identical to the
// original writer. The override merge alone discards intermediate assignments.
struct IndexedBatch {
    std::vector<Edit> ordered;
    std::array<unsigned,groups*blocks_per_group+1> offsets{};
    std::array<unsigned,groups> distinct{};
    explicit IndexedBatch(std::span<const Edit> input,BlockSelection mask) {
        if (!mask.bits) return; // Even coordinates may be inaccessible.
        std::vector<unsigned> order;order.reserve(input.size());
        for (unsigned i=0;i<input.size();++i) if (mask.row(input[i].row)) order.push_back(i);
        std::sort(order.begin(),order.end(),[&](unsigned a,unsigned b) {
            return input[a].row!=input[b].row?input[a].row<input[b].row:a<b;
        });
        ordered.reserve(order.size());
        for (auto i:order) {
            if (ordered.empty() || ordered.back().row!=input[i].row) ++distinct[input[i].row/rows_per_group];
            ordered.push_back(input[i]);
        }
        unsigned next=0;
        for (unsigned b=0;b<groups*blocks_per_group;++b) {
            offsets[b]=next;
            while (next<ordered.size() && ordered[next].row/256==b) ++next;
        }
        offsets.back()=next;
    }
    std::span<const Edit> block(unsigned b) const {
        return std::span(ordered).subspan(offsets[b],offsets[b+1]-offsets[b]);
    }
    std::span<const Edit> group(unsigned g) const {
        const auto first=offsets[g*blocks_per_group],last=offsets[(g+1)*blocks_per_group];
        return std::span(ordered).subspan(first,last-first);
    }
};
inline bool same_logical(LogicalRow a,LogicalRow b) {
    return flags(a)==flags(b) && (!a.contributes() || a.value==b.value);
}
// Source has already been admitted under its immutable physical base identity.
inline OverrideRow read_override(Overlay source,Bytes body,unsigned i) {
    const unsigned n=source.count;
    const auto load=[&]<class T>(unsigned offset) {T value;std::memcpy(&value,body.data+offset,sizeof value);return value;};
    const auto old=load.template operator()<std::uint8_t>(8+n*2+i);
    const auto now=load.template operator()<std::uint8_t>(8+n*3+i);
    return {load.template operator()<std::uint16_t>(8+i*2),
        {load.template operator()<u64>(8+n*4+i*8),bool(old&1),bool(old&2)},
        {load.template operator()<u64>(8+n*12+i*8),bool(now&1),bool(now&2)}};
}
template<class BasePoint>
std::vector<OverrideRow> merge_overrides(Overlay source,Bytes body,std::span<const Edit> commands,
                                       unsigned origin,unsigned distinct,BasePoint base_point) {
    std::vector<OverrideRow> out;out.reserve(std::min(unsigned(rows_per_group),source.count+distinct));
    unsigned old=0,next=0;
    const auto append=[&](OverrideRow row) {if (!same_logical(row.original,row.current)) out.push_back(row);};
    while (old<source.count || next<commands.size()) {
        const auto position=next<commands.size()?commands[next].row-origin:rows_per_group;
        // Only the position column is needed to choose the next source.
        std::uint16_t previous=rows_per_group;
        if (old<source.count) std::memcpy(&previous,body.data+8+old*2,2);
        if (previous<position) {append(read_override(source,body,old++));continue;}
        auto end=next+1;
        while (end<commands.size() && commands[end].row==commands[next].row) ++end;
        const auto original=previous==position?read_override(source,body,old++).original:base_point(position);
        append({std::uint16_t(position),original,commands[end-1].next});
        next=end;
    }
    return out;
}
} // namespace frame_lifecycle::phase
