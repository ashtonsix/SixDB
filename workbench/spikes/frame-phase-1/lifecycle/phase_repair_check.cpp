#include "phase_repair.h"
#include "phase_owner.h"
#include <random>
#include <cstdio>
namespace p=frame_lifecycle::phase;
namespace {
struct Image {p::fe::descriptor d;std::vector<std::uint8_t> bytes;std::array<p::u64,256> values;};
Image make(unsigned width,p::fe::checkpoints cp,bool delta) {
    Image out;const auto maximum=width==64?UINT64_MAX:(p::u64{1}<<width)-1;
    out.d={delta?maximum/2+128:UINT64_MAX-maximum,13,256,delta?p::fe::kind::delta:p::fe::kind::packed,std::uint8_t(width)};
    const auto plane=p::fe::plane_bytes(width,256);
    const auto size=plane+(delta?8+p::fe::checkpoint_cost(cp,width):0);
    out.bytes.resize(13+size+17,0xa5);std::array<p::u64,256> residual{};
    std::vector<p::u64> marks;const unsigned spacing=cp==p::fe::checkpoints::every64?64:16;
    const p::fe::i64 rbase=-p::fe::i64(maximum/2);p::u64 sum=0;std::mt19937_64 random(713);
    out.values[0]=out.d.base;
    for (unsigned i=delta?1:0;i<256;++i) {
        residual[i]=delta?(i%2?0:maximum):(random()&maximum);
        if (delta) {
            const __int128 value=__int128(out.values[i-1])+rbase+residual[i];
            p::require(value>=0 && value<=__int128(UINT64_MAX),"repair fixture escaped u64");out.values[i]=p::u64(value);
            sum+=residual[i];
            if (cp!=p::fe::checkpoints::none && (i+1)%spacing==0 && i!=255) marks.push_back(sum);
        } else out.values[i]=out.d.base+residual[i];
    }
    auto* body=out.bytes.data()+13;p::fe::pack(width,residual,body);
    if (delta) {
        std::memcpy(body+plane,&rbase,8);
        if (cp==p::fe::checkpoints::packed && width+8<=64) p::fe::pack(width+8,marks,body+plane+8);
        else for (unsigned i=0;i<marks.size();++i) {
            const auto bytes=width+8<=32?4:8;std::memcpy(body+plane+8+i*bytes,&marks[i],bytes);
        }
    }
    return out;
}
p::fe::borrowed_block borrow(const Image& image,p::fe::checkpoints cp) {
    p::fe::borrowed_block out;out.d=image.d;out.body=image.bytes.data()+13;out.grant=image.bytes.size()-30;out.delta_checkpoints=cp;return out;
}
unsigned verify(const Image& image,p::fe::checkpoints cp,const std::array<p::u64,256>& expected) {
    const auto k=borrow(image,cp);p::require(p::fe::admits(k),"repaired body lost admission");
    p::u128 sum=0;
    for (unsigned i=0;i<256;++i) {p::require(p::fe::value_at(k,i)==expected[i],"repair point differs");sum+=expected[i];}
    p::require(p::fe::stored_sum(k)==sum,"repair exact sum differs");
    for (auto rhs:{p::u64{0},expected[0],expected[15],expected[16],expected[63],expected[64],UINT64_MAX}) {
        unsigned wanted=0;for (auto x:expected) wanted+=x>=rhs;
        p::require(p::fe::stored_count_ge(k,rhs)==wanted,"repair count differs");
    }
    p::require(std::all_of(image.bytes.begin(),image.bytes.begin()+13,[](auto x){return x==0xa5;}) &&
               std::all_of(image.bytes.end()-17,image.bytes.end(),[](auto x){return x==0xa5;}),"repair escaped private extent");
    return 264;
}
}
int main() {
    unsigned checks=0,plans=0;
    p::SmallPlanEdits edges;
    for (unsigned i=0;i<9;++i) {
        edges.push_back({i,1729+i});
        p::require(edges.size()==i+1 && edges.uses_heap()==(i>=4),"inline plan changed its spill boundary");
    }
    const auto copied=edges;auto moved=std::move(edges);
    p::require(edges.empty() && copied.size()==9 && moved.size()==9,"inline plan move lost its value");
    for (unsigned i=0;i<9;++i) p::require(copied.data()[i].value==1729+i && moved.data()[i].row==i,"inline plan copy/move differs");
    edges.push_back({3,5});p::require(edges.size()==1 && !edges.uses_heap(),"moved plan cannot be reused");
    for (unsigned width=1;width<=64;++width) for (unsigned mode=0;mode<4;++mode) for (bool delta:{false,true}) {
        const auto cp=p::fe::checkpoints(mode);const auto original=make(width,cp,delta);checks+=verify(original,cp,original.values);
        for (unsigned row:{0u,1u,15u,16u,63u,64u,239u,240u,254u,255u,256u}) {
            auto current=original;auto after=original.values;
            if (row==256) {for (auto& x:after) x=delta?x-1:UINT64_MAX;} // Descriptor-anchor shift, unchanged delta residuals.
            else if (delta) after[row]+=row%2?1:p::u64(-1);
            else after[row]=row%2?original.d.base:UINT64_MAX;
            p::BlockRepair plan;
            p::require(p::plan_repair(borrow(original,cp),original.values,after,plan),"fitting block repair refused");
            p::apply_repair(plan,width,current.bytes.data()+13);current.d.base=plan.anchor;
            checks+=verify(current,cp,after);++plans;
            auto sparse=original;p::BlockAssignments assigned;
            for (unsigned i=0;i<256;++i) if (after[i]!=original.values[i]) assigned.set(i,after[i]);
            p::BlockRepair sparse_plan;
            p::require(p::plan_sparse_repair(borrow(original,cp),assigned,sparse_plan),"fitting sparse repair refused");
            p::apply_repair(sparse_plan,width,sparse.bytes.data()+13);sparse.d.base=sparse_plan.anchor;
            checks+=verify(sparse,cp,after);++plans;
            auto inline_dense=original,inline_sparse=original;
            p::InlineBlockRepair dense_plan,inline_plan;
            p::require(p::plan_repair(borrow(original,cp),original.values,after,dense_plan),"inline dense repair refused");
            p::apply_repair(dense_plan,width,inline_dense.bytes.data()+13);inline_dense.d.base=dense_plan.anchor;
            checks+=verify(inline_dense,cp,after);++plans;
            p::require(p::plan_sparse_repair(borrow(original,cp),assigned,inline_plan),"inline sparse repair refused");
            p::apply_repair(inline_plan,width,inline_sparse.bytes.data()+13);inline_sparse.d.base=inline_plan.anchor;
            checks+=verify(inline_sparse,cp,after);++plans;
            p::require(original.bytes==make(width,cp,delta).bytes,"planning mutated the source");
        }
        auto impossible=original.values;
        if (delta) --impossible[1];
        else if (original.d.base) impossible[1]=original.d.base-1;
        else continue;
        p::BlockRepair refused;
        p::require(!p::plan_repair(borrow(original,cp),original.values,impossible,refused),"nonfitting repair accepted");
        p::InlineBlockRepair refused_inline;
        p::require(!p::plan_repair(borrow(original,cp),original.values,impossible,refused_inline),"nonfitting inline repair accepted");
    }
    std::printf("%u repair plans / %u independent points+aggregates pass: heap/inline/spill, widths 1..64, all CP modes, anchors/interiors/last, descriptor shifts, refusals and unaligned guards.\n",plans,checks);
}
