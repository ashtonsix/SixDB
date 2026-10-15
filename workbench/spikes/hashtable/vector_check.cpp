#include "promoted.h"
#include <map>
using namespace ht;
namespace {
struct observed {
    unsigned events=0;
    void operator()(const link_event& e)noexcept {
        ++events;
        if(e.kind==link_event_kind::before_block_write)
            require(e.allocation && e.address==e.allocation->address && e.bytes==e.allocation->size,"vector physical effect");
    }
};
template<unsigned W> auto pattern(unsigned i) {
    std::array<byte,W> v;
    for(unsigned j=0;j<W;++j)v[j]=byte((i*37+j*71)^(i>>5));
    return v;
}
template<unsigned W,class Bags> auto read(const Bags& bags,std::uint32_t head) {
    std::vector<std::array<byte,W>> out;
    bags.each_bytes(head,[&](auto b){std::array<byte,W> v;std::copy(b.begin(),b.end(),v.begin());out.push_back(v);});
    return out;
}
template<unsigned W,unsigned Slots> void physical() {
    using Bags=pooled_vector_bags<W,true,Slots>;
    pool_owner pool;Bags bags(*pool.pool);observed events;
    std::uint32_t head=0,hint=0;
    std::vector<std::array<byte,W>> expected;
    // Cross the largest contiguous slot, preserving the unbounded segmented
    // continuation. Other bags force directory and shared-block growth.
    std::vector<std::uint32_t> neighbors;
    for(unsigned i=0;i<2300;++i) {
        auto v=pattern<W>(i);const auto before=head;
        events.events=0;auto pending=bags.reserve_values(head,1,events,hint);
        require(bool(pending),"vector reserve");
        if(head && pending->head_after()!=head) {
            require(read<W>(bags,head)==expected,"old vector remains readable during replacement");
            pending.reset();require(read<W>(bags,head)==expected,"replacement cancellation preserves old values");
            require(events.events<=Bags::reservation_event_bound,"vector cancel event bound");
            events.events=0;pending=bags.reserve_values(head,1,events,hint);require(bool(pending),"replacement retry");
        }
        head=pending->head_after();hint=pending->insertion_after();pending->commit_bytes(v);pending.reset();
        require(events.events<=Bags::reservation_event_bound,"vector commit event bound");expected.push_back(v);
        if(before && before!=head)require(bags.count(before)==0,"replacement retires old count");
        if(i<512)require(bags.next(head)==0,"ordinary fanout is a single contiguous slot");
        if(i%17==0) {
            auto other=bags.reserve_values(0,1,events);require(bool(other),"neighbor reserve");
            neighbors.push_back(other->head_after());other->commit_bytes(pattern<W>(9999));
        }
        if(i%127==0)require(read<W>(bags,head)==expected,"vector resize full byte oracle");
    }
    require(bags.next(head)!=0 && read<W>(bags,head)==expected,"largest-family segmented continuation");
    for(auto ref:neighbors)require(read<W>(bags,ref)==std::vector{pattern<W>(9999)},"neighbor values survive relocation");
    auto copy=bags.clone(*pool.pool);require(bool(copy),"vector independent clone");
    require(bags.release_values(head,events)==expected.size(),"vector release count");
    for(auto ref:neighbors)bags.release_values(ref,events);
    for(unsigned i=0;i<500;++i){auto p=bags.reserve_values(0,1,events);require(bool(p),"released-family reuse");p->commit_bytes(pattern<W>(i));}
    require(read<W>(*copy,head)==expected,"old physical clone survives retirement/reuse");bags.validate();copy->validate();
}
template<unsigned K,unsigned W> void integrated() {
    using Bags=pooled_vector_bags<W,true,16>;
    constexpr unsigned Budget=W<=8?16:64;
    using Small=small_table<K,container_kind::multimap,W,true,Bags,Budget>;
    using Body=promoted_table<K,container_kind::multimap,W,1,0,128,128,
        record_format::exact,false,record_access::both,true,Bags,Budget>;
    pool_owner pool;Small small(*pool.pool,19);
    std::map<word,std::vector<std::array<byte,W>>> expected;
    for(unsigned i=0;i<64;++i){auto v=pattern<W>(i);small.set_bytes(i,v);expected[i].push_back(v);}
    auto body=Body::promote(small,*pool.pool,41,73);require(bool(body),"vector hierarchy promotion");
    for(unsigned j=1;j<70;++j)for(unsigned i=0;i<64;++i){auto v=pattern<W>(i*71+j);body->set_bytes(i,v);expected[i].push_back(v);}
    require(body->population()[0]&&body->population()[1]&&body->population()[2],"vector all key tiers");
    auto check=[&] {for(const auto& [key,want]:expected){std::vector<std::array<byte,W>> got;
        require(body->get_bytes(key,[&](auto b){std::array<byte,W> v;std::copy(b.begin(),b.end(),v.begin());got.push_back(v);}),"vector lookup");
        require(got==want,"vector complete values/order/multiplicity");}};check();
    std::array<small_event,0> none;small_journal denied{none};
    require(!body->try_set_bytes(0,pattern<W>(1),denied),"vector effects refusal");check();
    for(unsigned i=0;i<64;i+=2){body->erase(i);expected.erase(i);}
    for(unsigned i=0;i<64;i+=2){auto v=pattern<W>(777);body->set_bytes(i,v);expected[i]={v};}check();body->validate();
}
void refusal() {
    pool_owner pool(1u<<20);pooled_vector_bags<8,true> bags(*pool.pool);observed events;
    std::uint32_t head=0;
    for(unsigned i=0;i<4;++i){auto p=bags.reserve_values(head,1,events);require(bool(p),"pressure source");head=p->head_after();p->commit_bytes(pattern<8>(i));}
    const auto old=read<8>(bags,head);std::vector<oa::Allocation> pressure;
    for(unsigned n=4096;n>=8;n/=2)while(auto p=oa::allocate(*pool.pool,{.bytes=n,.alignment=8}))pressure.push_back(std::move(*p));
    require(!bags.reserve_values(head,1,events) && read<8>(bags,head)==old,"resize allocation refusal preserves prefix");
    require(!bags.reserve_values(head,1,events) && read<8>(bags,head)==old,"repeat resize refusal");
}
}
int main() {
    physical<1,16>();physical<2,256>();physical<3,16>();physical<8,16>();physical<32,16>();physical<64,16>();refusal();
    integrated<8,1>();integrated<16,2>();integrated<24,3>();integrated<32,8>();
    integrated<40,16>();integrated<48,32>();integrated<56,57>();integrated<64,64>();
    std::puts("pooled vector copy/cancel/refusal/reuse/clone, neighbors, long continuation, all-key-width hierarchy and effects passed");
}
