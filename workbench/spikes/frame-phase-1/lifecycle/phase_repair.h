#pragma once
#include "phase_material.h"
#include "phase_plane.h"
#include <utility>
#include <vector>
#include <cstring>

namespace frame_lifecycle::phase {
enum class RepairReason { success, no_value_change, coded, constant, runs, other_kind, packed_basis, delta_bound, geometry };
// Preflight over a whole logical block. Stores happen only after all new
// residuals and checkpoint updates are staged and the group has fresh backing.
// Common sparse plans have one or two stores. Keep four inline and preserve
// heap fallback for dense/anchor repairs; this is not a general Ikea container.
class SmallPlanEdits {
    std::array<PlaneEdit,4> small_{};
    std::vector<PlaneEdit> large_;
    unsigned count_{};
public:
    SmallPlanEdits()=default;
    SmallPlanEdits(const SmallPlanEdits&)=default;
    SmallPlanEdits& operator=(const SmallPlanEdits&)=default;
    SmallPlanEdits(SmallPlanEdits&& other) noexcept
        :small_(other.small_),large_(std::move(other.large_)),count_(std::exchange(other.count_,0)) {other.large_.clear();}
    SmallPlanEdits& operator=(SmallPlanEdits&& other) noexcept {
        if (this!=&other) {small_=other.small_;large_=std::move(other.large_);count_=std::exchange(other.count_,0);other.large_.clear();}
        return *this;
    }
    void push_back(PlaneEdit value) {
        if (count_<small_.size()) small_[count_++]=value;
        else {
            if (count_==small_.size()) {large_.reserve(8);large_.insert(large_.end(),small_.begin(),small_.end());}
            large_.push_back(value);++count_;
        }
    }
    bool empty() const {return !count_;}
    std::size_t size() const {return count_;}
    bool uses_heap() const {return count_>small_.size();}
    const PlaneEdit* data() const {return uses_heap()?large_.data():small_.data();}
    const PlaneEdit* begin() const {return data();}
    const PlaneEdit* end() const {return data()+count_;}
};
template<class Edits> struct RepairPlan {
    u64 anchor{};
    Edits plane, checkpoints;
    unsigned checkpoint_offset{}, checkpoint_rows{}, checkpoint_width{}, checkpoint_bytes{};
    RepairReason reason=RepairReason::success;
    unsigned examined_positions{};
};
using BlockRepair=RepairPlan<std::vector<PlaneEdit>>;
using InlineBlockRepair=RepairPlan<SmallPlanEdits>;
inline bool reject_repair(auto& out,RepairReason reason) {out.reason=reason;return false;}
inline bool repair_kind(const fe::borrowed_block& source,auto& out) {
    if (source.coded) return reject_repair(out,RepairReason::coded);
    if (source.d.type==fe::kind::constant) return reject_repair(out,RepairReason::constant);
    if (source.d.type==fe::kind::runs) return reject_repair(out,RepairReason::runs);
    if (source.d.type!=fe::kind::packed && source.d.type!=fe::kind::delta)
        return reject_repair(out,RepairReason::other_kind);
    if (source.rows!=256 || !source.d.width || source.d.width>64) return reject_repair(out,RepairReason::geometry);
    return true;
}
template<class Change,class Plan>
inline void stage_checkpoints(const fe::borrowed_block& source,Change change,Plan& out) {
    const unsigned width=source.d.width;
    if (source.delta_checkpoints==fe::checkpoints::none) return;
    const unsigned spacing=source.delta_checkpoints==fe::checkpoints::every64?64:16;
    out.checkpoint_rows=256/spacing-1;
    out.checkpoint_offset=fe::plane_bytes(width,256)+8;
    const auto* marks=source.body+out.checkpoint_offset;
    if (source.delta_checkpoints==fe::checkpoints::packed && width+8<=64) out.checkpoint_width=width+8;
    else out.checkpoint_bytes=width+8<=32?4:8;
    for (unsigned mark=0;mark<out.checkpoint_rows;++mark) {
        const __int128 adjustment=change((mark+1)*spacing-1)-change(0);
        if (!adjustment) continue;
        u64 previous=0;
        if (out.checkpoint_width) previous=fe::plane_kernels(out.checkpoint_width).point(marks,mark);
        else std::memcpy(&previous,marks+mark*out.checkpoint_bytes,out.checkpoint_bytes);
        // Wide stored prefixes wrap modulo u64. Fitting residuals prove that
        // smaller checkpoint widths still fit; this is not logical wrapping.
        const auto next=previous+u64(adjustment);
        out.checkpoints.push_back({mark,next});
    }
}
inline bool plan_repair(const fe::borrowed_block& source, std::span<const u64,256> before,
                        std::span<const u64,256> after, auto& out) {
    if (!repair_kind(source,out)) return false;
    out.examined_positions=256;
    const auto width=source.d.width;
    const u64 maximum=width==64?UINT64_MAX:(u64{1}<<width)-1;
    out.anchor=source.d.base;
    if (source.d.type==fe::kind::packed) {
        for (unsigned i=0;i<256;++i) if (before[i]!=after[i]) {
            if (after[i]<source.d.base || after[i]-source.d.base>maximum) return reject_repair(out,RepairReason::packed_basis);
            out.plane.push_back({i,after[i]-source.d.base});
        }
        return true;
    }
    using i128=__int128;
    const auto change=[&](unsigned i) {return i128(after[i])-i128(before[i]);};
    out.anchor=after[0];
    for (unsigned i=1;i<256;++i) {
        const i128 adjustment=change(i)-change(i-1);
        if (!adjustment) continue;
        const auto original=fe::plane_kernels(width).point(source.body,i);
        const i128 next=i128(original)+adjustment;
        if (next<0 || next>i128(maximum)) return reject_repair(out,RepairReason::delta_bound);
        out.plane.push_back({i,u64(next)});
    }
    stage_checkpoints(source,change,out);
    return true;
}
// Sparse assignment payload is readable only where its bit is set.
struct BlockAssignments {
    std::array<u64,256> values;
    std::array<u64,4> assigned{};
    void set(unsigned row,u64 value) {values[row]=value;assigned[row/64]|=u64{1}<<(row%64);}
    void clear(unsigned row) {assigned[row/64]&=~(u64{1}<<(row%64));}
};
inline bool plan_sparse_repair(const fe::borrowed_block& source,const BlockAssignments& input,auto& out) {
    out.anchor=source.d.base;
    if (!(input.assigned[0]|input.assigned[1]|input.assigned[2]|input.assigned[3])) {out.reason=RepairReason::no_value_change;return true;}
    if (!repair_kind(source,out)) return false;
    const unsigned width=source.d.width;
    const u64 maximum=width==64?UINT64_MAX:(u64{1}<<width)-1;
    std::array<__int128,256> deltas;
    std::array<u64,4> changed{},events{};
    for (unsigned word=0;word<4;++word) {
        auto bits=input.assigned[word];
        while (bits) {
            const auto row=word*64+std::countr_zero(bits);bits&=bits-1;
            ++out.examined_positions;
            const auto next=input.values[row];
            if (source.d.type==fe::kind::packed) {
                if (next<source.d.base || next-source.d.base>maximum) return reject_repair(out,RepairReason::packed_basis);
                const auto residual=next-source.d.base;
                if (residual!=fe::plane_kernels(width).point(source.body,row)) out.plane.push_back({row,residual});
            } else {
                const auto delta=__int128(next)-__int128(fe::value_at(source,row));
                if (!delta) continue;
                deltas[row]=delta;changed[word]|=u64{1}<<(row%64);
                if (row) events[word]|=u64{1}<<(row%64);
                if (row<255) events[(row+1)/64]|=u64{1}<<((row+1)%64);
            }
        }
    }
    if (source.d.type==fe::kind::packed) {
        if (out.plane.empty()) out.reason=RepairReason::no_value_change;
        return true;
    }
    if (!(changed[0]|changed[1]|changed[2]|changed[3])) {out.reason=RepairReason::no_value_change;return true;}
    const auto change=[&](unsigned row)->__int128 {return changed[row/64]>>(row%64)&1?deltas[row]:0;};
    if (changed[0]&1) out.anchor=input.values[0];
    for (unsigned word=0;word<4;++word) {
        auto bits=events[word];
        while (bits) {
            const auto row=word*64+std::countr_zero(bits);bits&=bits-1;
            const auto adjustment=change(row)-change(row-1);
            if (!adjustment) continue;
            const auto next=__int128(fe::plane_kernels(width).point(source.body,row))+adjustment;
            if (next<0 || next>__int128(maximum)) return reject_repair(out,RepairReason::delta_bound);
            out.plane.push_back({row,u64(next)});
        }
    }
    stage_checkpoints(source,change,out);return true;
}
inline void apply_repair(const auto& plan,unsigned width,std::uint8_t* body) {
    replace_private_plane(width,256,body,plan.plane);
    if (plan.checkpoint_width)
        replace_private_plane(plan.checkpoint_width,plan.checkpoint_rows,body+plan.checkpoint_offset,plan.checkpoints);
    else for (const auto& e:plan.checkpoints)
        std::memcpy(body+plan.checkpoint_offset+e.row*plan.checkpoint_bytes,&e.value,plan.checkpoint_bytes);
}
} // namespace frame_lifecycle::phase
