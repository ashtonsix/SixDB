#include "placement.h"
#include <algorithm>
namespace directory_spike {
OrderedPlacement::OrderedPlacement(std::vector<Row> rows,unsigned j,PlacementPolicy p,unsigned bits)
    :j_(j),suffix_bits_(bits?bits:48-8*j),policy_(p),rows_(std::move(rows)) {
    require(j<=6 && suffix_bits_<=48-8*j && suffix_bits_<64);
    std::ranges::sort(rows_,{},&Row::physical);verify();
}
void OrderedPlacement::verify()const {
    for(unsigned i=0;i<rows_.size();++i) {
        const auto& row=rows_[i];require(row.physical<(u64(1)<<48));
        if(j_)require((row.physical>>(48-8*j_))==(row.logical>>(64-8*j_)));
        if(i)require(rows_[i-1].physical<row.physical && rows_[i-1].logical<=row.logical);
    }
}
Row OrderedPlacement::erase(u64 id) {
    auto it=std::ranges::find(rows_,id,&Row::id);require(it!=rows_.end());auto row=*it;rows_.erase(it);return row;
}
std::expected<PlacementEdit,bool> OrderedPlacement::insert(Row row) {
    const u64 base=j_ ? (row.logical>>(64-8*j_))<<(48-8*j_):0;
    const u64 capacity=u64(1)<<suffix_bits_,end=base+capacity;
    auto begin=std::lower_bound(rows_.begin(),rows_.end(),base,[](auto r,u64 p){return r.physical<p;});
    auto finish=std::lower_bound(begin,rows_.end(),end,[](auto r,u64 p){return r.physical<p;});
    auto lower=std::lower_bound(begin,finish,row.logical,[](auto r,u64 q){return r.logical<q;});
    auto upper=std::upper_bound(lower,finish,row.logical,[](u64 q,auto r){return q<r.logical;});
    if(u64(finish-begin)==capacity)return std::unexpected(true);
    PlacementEdit edit;
    auto commit=[&](u64 p) {
        row.physical=p;rows_.push_back(row);std::ranges::sort(rows_,{},&Row::physical);
        edit.moved=edit.changed.size();edit.changed.push_back(row);
        return std::expected<PlacementEdit,bool>{std::move(edit)};
    };
    // Every gap in the equal-key run is legal, including both outer fences.
    u64 cursor=lower==begin?base:std::prev(lower)->physical+1;
    const u64 limit=upper==finish?end:upper->physical;
    for(auto it=lower;;++it) {
        const auto next=it==upper?limit:it->physical;
        if(cursor<next) {
            const auto gap=next-cursor;
            const auto p=policy_==PlacementPolicy::midpoint ? cursor+gap/2:
                (it==finish ? cursor+std::min<u64>(15,(gap-1)/4):cursor+(gap-1)/2);
            return commit(p);
        }
        if(it==upper)break;
        cursor=it->physical+1;
    }
    // Move the boundary duplicate across its own run before redistributing a
    // larger interval. No internal order among duplicates needs preservation.
    if(upper!=finish) {
        auto run_end=std::upper_bound(upper,finish,upper->logical,[](u64 q,auto r){return q<r.logical;});
        u64 hole=upper->physical+1;
        for(auto it=std::next(upper);; ++it) {
            const auto next=it==run_end?(run_end==finish?end:run_end->physical):it->physical;
            if(hole<next) {const auto slot=upper->physical;upper->physical=hole;edit.changed.push_back(*upper);++edit.duplicate_moves;return commit(slot);}
            if(it==run_end)break;hole=it->physical+1;
        }
    }
    if(policy_==PlacementPolicy::hierarchical) {
        const auto anchor=upper==finish ? std::prev(finish)->physical:upper->physical;
        for(unsigned bits=std::min(8u,suffix_bits_);bits<=suffix_bits_;bits=std::min(bits+8,suffix_bits_)) {
            const u64 width=u64(1)<<bits,start=base+((anchor-base)/width)*width;
            auto a=std::lower_bound(begin,finish,start,[](auto r,u64 p){return r.physical<p;});
            auto b=std::lower_bound(a,finish,start+width,[](auto r,u64 p){return r.physical<p;});
            const u64 n=b-a+1;
            if(n<=width && (n*4<=width*3 || bits==suffix_bits_)) {
                std::vector<Row> folded(a,b);folded.push_back(row);std::ranges::stable_sort(folded,{},&Row::logical);
                u64 slot=0;
                for(unsigned i=0;i<folded.size();++i) {
                    folded[i].physical=start+(u64(i+1)*width)/(n+1);
                    if(folded[i].id==row.id)slot=folded[i].physical;
                }
                std::copy_if(folded.begin(),folded.end(),a,[&](auto r){return r.id!=row.id;});
                for(auto r:folded)if(r.id!=row.id)edit.changed.push_back(r);
                edit.window=width;return commit(slot);
            }
            if(bits==suffix_bits_)break;
        }
        return std::unexpected(true);
    }
    // Simple packed control: find right slack and shift to it; if unavailable,
    // shift left. Vector/model movement is included in caller timing.
    auto hole=upper;
    while(hole!=finish && std::next(hole)!=finish && std::next(hole)->physical==hole->physical+1)++hole;
    if(hole!=finish && hole->physical+1<end) {
        const auto slot=upper->physical;
        for(auto it=hole;;--it){++it->physical;edit.changed.push_back(*it);if(it==upper)break;}
        edit.window=hole-upper+1;return commit(slot);
    }
    auto left=lower;
    if(left==begin)return std::unexpected(true);
    --left;
    while(left!=begin && std::prev(left)->physical+1==left->physical)--left;
    require(left->physical>base);
    const auto slot=std::prev(lower)->physical;
    for(auto it=left;it!=lower;++it){--it->physical;edit.changed.push_back(*it);}
    edit.window=lower-left;return commit(slot);
}
}
