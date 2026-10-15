#include "bag_buffers.h"
#include "promoted.h"
#include <map>
using namespace ht;
namespace {
struct observer {
    unsigned events=0;bool grown=false,moved=false;
    const void* owner=nullptr;std::size_t expected_count=0;
    void reset(){events=0;}
    void operator()(const buffer_event& e)noexcept {
        ++events;require(!owner || owner==e.owner,"buffer owner event identity");
        if(e.kind==buffer_event_kind::before_payload_write) {
            require(e.allocation && e.id && e.address==e.allocation->address,"payload event allocation");
            require(e.offset+e.bytes<=e.allocation->size,"exact payload event bounds");
            if(owner)require(static_cast<const buffer_bags<8>*>(owner)->value_count(e.id)==expected_count,
                              "payload observation precedes count publication");
        }
        grown|=e.kind==buffer_event_kind::allocation_grown;
        moved|=e.kind==buffer_event_kind::directory_relocated;
    }
};
template<unsigned W> auto contents(const buffer_bags<W>& bags,std::uint32_t head) {
    std::vector<std::array<byte,W>> result;
    bags.each_bytes(head,[&](auto bytes){std::array<byte,W> out;std::copy(bytes.begin(),bytes.end(),out.begin());result.push_back(out);});
    return result;
}
template<unsigned W> void geometry() {
    pool_owner pool;observer seen;std::optional<buffer_bags<W>> copy;
    std::array<std::uint32_t,70> heads{};
    std::array<std::vector<std::array<byte,W>>,70> expected;
    {
        buffer_bags<W> bags(*pool.pool);
        for(unsigned i=0;i<heads.size();++i) {
            const unsigned lengths[]{1,2,3,4,5,12,13,28,29,60,61,64,96,124,125,128,257};
            for(unsigned j=0;j<lengths[i%std::size(lengths)];++j) {
                std::array<byte,W> value;for(unsigned b=0;b<W;++b)value[b]=byte((i*23+j*11+b)%256);
                seen.reset();auto pending=bags.reserve_values(heads[i],1,seen);require(bool(pending),"byte-width reserve");
                require(contents(bags,heads[i])==expected[i],"reserve preserves old initialized prefix");
                pending->commit_bytes(value);heads[i]=pending->head_after();expected[i].push_back(value);
                require(seen.events<=buffer_bags<W>::reservation_event_bound,"commit event bound");
            }
        }
        require(seen.moved&&seen.grown,"exercise payload and directory growth");bags.validate();
        for(unsigned i=0;i<heads.size();i+=3){bags.release_values(heads[i],seen);heads[i]=0;expected[i].clear();}
        copy=bags.clone(*pool.pool);require(bool(copy),"physical buffer clone");
        bags.allocations([&](const oa::Allocation& a){copy->allocations([&](const oa::Allocation& b){
            require(a.address!=b.address && a.storage!=b.storage,"independent buffer clone storage");});});
        for(unsigned i=0;i<heads.size();++i)require(contents(*copy,heads[i])==expected[i],"clone IDs and initialized values");
        auto old=bags.reserve_values(0,1,seen),fresh=copy->reserve_values(0,1,seen);
        require(old&&fresh&&old->head_after()==fresh->head_after(),"clone free-list IDs preserved");
        std::array<byte,W> value{};value[0]=91;fresh->commit_bytes(value);fresh.reset();old.reset();
        for(unsigned i=0;i<heads.size();++i)require(contents(bags,heads[i])==expected[i],"clone mutation leaves source unchanged");
    }
    for(unsigned i=0;i<heads.size();++i)require(contents(*copy,heads[i])==expected[i],"clone survives complete source retirement");
    copy->validate();
}
void reservations() {
    pool_owner pool;buffer_bags<> bags(*pool.pool);observer seen;seen.owner=&bags;
    std::uint32_t head=0,reusable=0;
    for(unsigned i=0;i<30;++i) {
        seen.reset();auto pending=bags.reserve_values(0,2,seen);require(bool(pending),"fresh cancellation reserve");
        if(!reusable)reusable=pending->head_after();require(pending->head_after()==reusable,"cancel reuses directory ID");
        pending.reset();require(seen.events<=bags.reservation_event_bound,"cancel event bound");
        require(bags.footprint().requested==0,"canceled new buffer releases payload");bags.validate();
    }
    auto pending=bags.reserve_values(0,2,seen);require(bool(pending),"fresh reserve");
    {auto moved=std::move(*pending);pending.reset();std::array<word,2> values{0,~word(0)};
        moved.commit(values);head=moved.head_after();}
    const auto before=contents(bags,head);seen.expected_count=2;
    seen.reset();pending=bags.reserve_values(head,1,seen);require(bool(pending),"existing growth reserve");
    pending.reset();require(contents(bags,head)==before,"existing growth cancel preserves values/count");
    pending=bags.reserve_values(head,2,seen);require(bool(pending),"atomic pair reserve");
    const std::array<word,2> duplicates{7,7};pending->commit(duplicates);pending.reset();
    std::vector<word> values;bags.each(head,[&](word v,word){values.push_back(v);});
    require(values==std::vector<word>({0,~word(0),7,7}),"order, duplicates and full-word values");
    buffer_bags<> other(*pool.pool);observer other_seen;
    seen.expected_count=4;auto first=bags.reserve_values(0,1,seen),second=other.reserve_values(0,1,other_seen);
    const auto other_id=second->head_after();*second=std::move(*first);first.reset();second.reset();
    auto retry=other.reserve_values(0,1,other_seen);require(retry&&retry->head_after()==other_id,"move assignment cancels old owner");retry.reset();
    seen.reset();require(bags.release_values(head,seen)==4 && seen.events==bags.release_event_bound(head),"release exact values/event bound");bags.validate();
}
void refusal() {
    pool_owner pool(1u<<20);buffer_bags<> bags(*pool.pool);observer seen;std::uint32_t head=0;
    auto p=bags.reserve_values(0,2,seen);const std::array<word,2> initial{31,31};p->commit(initial);head=p->head_after();p.reset();
    std::vector<oa::Allocation> pressure;
    for(std::size_t bytes=4096;bytes>=8;bytes/=2)
        while(auto a=oa::allocate(*pool.pool,{.bytes=bytes,.alignment=8}))pressure.push_back(std::move(*a));
    require(!bags.reserve_values(0,2,seen),"fresh buffer refuses exhausted budget");
    bool refused=false;for(word i=0;i<100000;++i){auto pending=bags.reserve_values(head,1,seen);if(!pending){refused=true;break;}
        const std::array<word,1> value{i};pending->commit(value);}
    require(refused,"existing buffer growth refuses");const auto expected=contents(bags,head);
    require(!bags.reserve_values(head,1,seen)&&contents(bags,head)==expected,"repeat refusal preserves logical buffer");
    require(!bags.clone(*pool.pool)&&!bags.clone(*pool.pool),"clone refuses without changing source");
    require(contents(bags,head)==expected,"clone refusal preserves source");pressure.clear();
    auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*pool.pool,0).kind()!=orbital::Progress::Kind::done){require(clock_type::now()<deadline,"buffer pressure trim");orbital::worker::poll(pool.lane,1024);}
    auto copy=bags.clone(*pool.pool);require(copy&&contents(*copy,head)==expected,"clone retries after pressure");
}
void partial_clone_refusal() {
    pool_owner pool(1u<<20);buffer_bags<> bags(*pool.pool);observer seen;
    std::array<std::uint32_t,2> heads{};
    for(auto& head:heads)for(word i=0;i<1024;++i) {
        auto p=bags.reserve_values(head,1,seen);require(bool(p),"partial clone source");
        const std::array<word,1> value{mix(i)};p->commit(value);head=p->head_after();
    }
    auto prefix=oa::allocate(*pool.pool,{.bytes=8192,.alignment=8});require(bool(prefix),"one clone prefix allocation");
    std::vector<oa::Allocation> pressure;
    for(std::size_t bytes=8192;bytes>=8;bytes/=2)
        while(auto a=oa::allocate(*pool.pool,{.bytes=bytes,.alignment=8}))pressure.push_back(std::move(*a));
    require(!bags.clone(*pool.pool),"clone refuses under full pressure");
    *prefix=oa::Allocation{};
    for(unsigned i=0;i<2;++i) {
        const auto before=oa::statistics(*pool.pool).allocations;
        auto failed=bags.clone(*pool.pool);
        require(!failed && oa::statistics(*pool.pool).allocations>before,"partial clone releases copied prefix on failure");
    }
    for(auto head:heads) {word n=0;bags.each(head,[&](word v,word){require(v==mix(n++),"partial clone preserves source bytes");});require(n==1024,"partial clone preserves source count");}
}
template<unsigned K> void integration() {
    pool_owner pool;using Small=small_table<K,container_kind::multimap,8,true,buffer_bags<>>;
    using Table=promoted_table<K,container_kind::multimap,8,1,0,64,64,record_format::exact,false,record_access::exact,true,buffer_bags<>>;
    Small source(*pool.pool,19);std::map<word,std::vector<word>> expected;
    for(word i=0;i<30;++i)for(word j=0;j<1+i%5;++j){source.set_value(i,mix(i+j));expected[i].push_back(mix(i+j));}
    auto copy=source.clone();require(bool(copy),"buffer small retained private clone");
    auto table=Table::promote(source,*pool.pool,41,73);require(bool(table)&&source.size()==0,"transfer same bag directory through promotion");
    for(word i=30;i<240;++i){table->set_value(i,mix(i));expected[i].push_back(mix(i));}
    for(word i=0;i<240;i+=3)for(word j=0;j<65;++j){table->set_value(i,mix(j));expected[i].push_back(mix(j));}
    for(word i=0;i<240;i+=2){table->erase(i);expected.erase(i);}
    for(word i=0;i<240;i+=2){table->set_value(i,~mix(i));expected[i].push_back(~mix(i));}
    for(auto [key,values]:expected){std::vector<word> got;require(table->get(key,[&](word v){got.push_back(v);}),"integrated buffer present");require(got==values,"integrated all-tier bag values");}
    table->validate();for(word i=0;i<30;++i){std::vector<word> got;copy->get(i,[&](word v){got.push_back(v);});
        std::vector<word> old;for(word j=0;j<1+i%5;++j)old.push_back(mix(i+j));require(got==old,"old private clone unchanged through promotion/write/erase");}
    std::array<small_event,0> events;small_journal denied{events};
    require(!table->try_set(250,37,denied)&&!table->get(250,[](word){}),"observer refusal precedes buffer mutation");
}
}
int main(){reservations();geometry<1>();geometry<3>();geometry<8>();geometry<16>();geometry<64>();refusal();partial_clone_refusal();
    lanes<8>([](auto i){integration<8*(i+1)>();});
    std::puts("buffer widths, reservation/move/cancel/refusal, independent clones, all-key-width list/hierarchy integration passed");}
