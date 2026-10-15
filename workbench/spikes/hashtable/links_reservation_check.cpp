#include "links.h"

using namespace ht;
namespace {
template<bool Admitted> struct suite {
using bags=links<64,false,0,Admitted>;
struct observations {
    struct item {link_event_kind kind;std::uint32_t block;unsigned family;std::size_t bytes;};
    std::array<item,128> events{};
    unsigned count=0;
    const void* owner=nullptr;
    const byte* watch=nullptr;
    std::array<byte,6> before{};
    bool seen=false;
    void reset() noexcept {count=0;watch=nullptr;seen=false;}
    void watch_header(const byte* p) noexcept {watch=p;std::memcpy(before.data(),p,6);seen=false;}
    void operator()(const link_event& e) noexcept {
        require(count<events.size(),"observer preadmitted storage");
        require(e.owner==owner,"observer owner identity");
        events[count++]={e.kind,e.block,e.family,e.bytes};
        if(e.kind==link_event_kind::before_block_write || e.kind==link_event_kind::allocation_grown) {
            require(e.block && e.allocation && e.address==e.allocation->address &&
                    e.bytes==e.allocation->size,"actual current allocation observation");
            auto begin=reinterpret_cast<std::uintptr_t>(e.address);
            auto wanted=reinterpret_cast<std::uintptr_t>(watch);
            if(e.kind==link_event_kind::before_block_write && watch && !seen &&
               wanted>=begin && wanted+6<=begin+e.bytes) {
                require(std::memcmp(before.data(),watch,6)==0,"observation precedes header store");seen=true;
            }
        } else if(e.kind==link_event_kind::directory_inserted) {
            require(e.block && e.allocation && e.address && e.bytes,"inserted directory entry");
        } else {
            require(!e.block && !e.allocation,"owner metadata observation");
        }
    }
    void bounded()const {require(count<=bags::reservation_event_bound,"reservation event upper bound");}
};
std::vector<word> contents(const bags& b,std::uint32_t head) {
    std::vector<word> v;b.each(head,[&](word key,word value){require(!value,"bag has no map payload");v.push_back(key);});return v;
}
void add(bags& b,std::uint32_t& head,std::span<const word> values,observations& seen) {
    seen.reset();auto old=contents(b,head);
    auto p=b.reserve_values(head,values.size(),seen);require(bool(p),"bag reserve");
    require(contents(b,head)==old,"reserve preserves logical bag");
    if(head)seen.watch_header(b.data(head));
    p->commit(values);head=p->head_after();seen.bounded();
    old.insert(old.end(),values.begin(),values.end());
    require(contents(b,head)==old,"commit preserves duplicates and order");
}
void reservations() {
    pool_owner pool;bags b(*pool.pool);observations seen;seen.owner=&b;
    std::uint32_t head=0;
    seen.reset();auto p=b.reserve_values(head,2,seen);require(bool(p),"promotion reserve");
    auto reused=p->head_after();require(head==0 && contents(b,head).empty(),"promotion is not published");
    auto after_reserve=b.footprint();p.reset();seen.bounded();
    for(unsigned i=0;i<64;++i) {
        seen.reset();auto cancelled=b.reserve_values(0,2,seen);require(bool(cancelled),"cancel reuse reserve");
        require(cancelled->head_after()==reused,"cancel returns assigned StreamPack rank");
        cancelled.reset();seen.bounded();
        require(b.footprint().requested==after_reserve.requested &&
                b.footprint().metadata==after_reserve.metadata,"cancel does not consume slots");
    }
    seen.reset();p=b.reserve_values(head,2,seen);require(bool(p),"promotion retry");
    {auto moved=std::move(*p);p.reset();const std::array<word,2> values{0,~word(0)};
     moved.commit(values);head=moved.head_after();}
    seen.bounded();require(contents(b,head)==std::vector<word>({0,~word(0)}),"moved reservation commits once");
    seen.reset();{auto cancelled=b.reserve_values(head,1,seen);require(bool(cancelled),"in-place cancellation");}
    seen.bounded();require(contents(b,head)==std::vector<word>({0,~word(0)}),"cancel leaves existing count");
    const std::array<word,2> duplicates{7,7};add(b,head,duplicates,seen);
    // Full first link: cancelled extension is recycled; commit observes the old
    // edge before linking the initialized successor.
    seen.reset();auto extension=b.reserve_values(head,1,seen);require(bool(extension),"extension reserve");
    require(b.next(head)==0,"reserved extension is unreachable");extension.reset();seen.bounded();
    require(b.next(head)==0,"cancel leaves old edge");
    const std::array<word,1> one{11};add(b,head,one,seen);require(seen.seen,"old edge observed before publication");
    for(word i=0;i<300;++i) {const std::array<word,1> v{mix(i)};add(b,head,v,seen);}
    b.validate();const auto count=contents(b,head).size();
    const auto bound=b.release_event_bound(head);seen.reset();
    require(b.release_values(head,seen)==count && seen.count==bound,"whole-bag release count and event bound");
    head=0;const auto released=b.footprint();
    add(b,head,duplicates,seen);require(b.footprint().requested==released.requested,"release reuses assigned links");
    add(b,head,one,seen); // One spare row cannot satisfy an atomic two-value reservation.
    seen.reset();auto pair=b.reserve_values(head,2,seen);require(bool(pair),"pair extends partial link");
    require(b.count(head)==3 && !b.next(head),"pair reserve preserves partial predecessor");
    pair->commit(duplicates);seen.bounded();
    require(contents(b,head)==std::vector<word>({7,7,11,7,7}),"pair commits into one successor");
    b.validate();
    // Move assignment cancels a reservation belonging to a different owner.
    bags other(*pool.pool);observations other_seen;other_seen.owner=&other;
    auto a=b.reserve_values(0,1,seen);auto c=other.reserve_values(0,1,other_seen);
    require(bool(a)&&bool(c),"two independent owner reservations");
    const auto other_ref=c->head_after();*c=std::move(*a);a.reset();c.reset();
    other_seen.reset();auto again=other.reserve_values(0,1,other_seen);
    require(bool(again)&&again->head_after()==other_ref,"move assignment cancels previous owner");
}
void relocating_directory() {
    pool_owner pool;bags b(*pool.pool);observations seen;seen.owner=&b;
    std::vector<std::uint32_t> heads(180);bool grew=false,moved=false;
    for(unsigned i=0;i<heads.size();++i) {
        const std::array<word,2> values{word(i),~word(i)};add(b,heads[i],values,seen);
        for(unsigned j=0;j<seen.count;++j) {
            grew|=seen.events[j].kind==link_event_kind::allocation_grown;
            moved|=seen.events[j].kind==link_event_kind::directory_relocated;
        }
        for(unsigned j=0;j<=i;++j)
            require(contents(b,heads[j])==std::vector<word>({word(j),~word(j)}),"refs survive payload/directory growth");
    }
    require(grew&&moved,"exercise payload and directory growth");b.validate();
    for(auto h:heads) {seen.reset();require(b.release_values(h,seen)==2,"release independent head");}
}
void allocation_refusal() {
    pool_owner pool(1u<<20);bags b(*pool.pool);observations seen;seen.owner=&b;
    std::uint32_t head=0;const std::array<word,2> seed{41,41};add(b,head,seed,seen);
    std::vector<oa::Allocation> pressure;
    while(auto a=oa::allocate(*pool.pool,{.bytes=4096,.alignment=64}))pressure.push_back(std::move(*a));
    std::vector<word> expected(seed.begin(),seed.end());bool refused=false;
    for(word i=0;i<100000;++i) {
        seen.reset();auto pending=b.reserve_values(head,1,seen);
        if(!pending) {refused=true;seen.bounded();break;}
        const std::array<word,1> value{i};pending->commit(value);head=pending->head_after();
        expected.push_back(i);seen.bounded();
    }
    require(refused,"exercise payload refusal");require(contents(b,head)==expected,"refusal preserves bag contents");
    seen.reset();require(!b.reserve_values(head,1,seen),"repeat refusal does not reserve logical space");seen.bounded();
    require(contents(b,head)==expected,"repeated refusal preserves contents");
    pressure.clear();auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*pool.pool,0).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"reservation trim");orbital::worker::poll(pool.lane,1024);
    }
    const std::array<word,1> last{~word(0)};add(b,head,last,seen);b.validate();
}
void private_clone() {
    pool_owner pool;no_link_observer observer;
    std::optional<bags> copy;std::array<std::uint32_t,73> heads{};
    std::array<std::vector<word>,73> expected;
    {
        bags source(*pool.pool);
        for(unsigned i=0;i<heads.size();++i)for(unsigned j=0;j<2+(i%9)*7;++j) {
            auto pending=source.reserve_values(heads[i],1,observer);require(bool(pending),"clone source reserve");
            const std::array<word,1> v{mix(i*91+j)};pending->commit(v);heads[i]=pending->head_after();
            expected[i].push_back(v[0]);
        }
        // Leave both reusable assigned slots and partially occupied blocks.
        for(unsigned i=0;i<heads.size();i+=3) {
            require(source.release_values(heads[i],observer)==expected[i].size(),"clone source release");
            heads[i]=0;expected[i].clear();
        }
        copy=source.clone(*pool.pool);require(bool(copy),"private block clone");
        require(copy->footprint().requested==source.footprint().requested,"clone retains requested reservations");
        source.allocations([&](const oa::Allocation& old) {
            copy->allocations([&](const oa::Allocation& fresh) {
                require(old.storage!=fresh.storage && old.address!=fresh.address,"clone has independent storage");
            });
        });
        for(unsigned i=0;i<heads.size();++i)require(contents(*copy,heads[i])==expected[i],"clone local refs and contents");
        auto old_pending=source.reserve_values(0,1,observer);
        auto new_pending=copy->reserve_values(0,1,observer);
        require(bool(old_pending)&&bool(new_pending)&&old_pending->head_after()==new_pending->head_after(),
                "cloned free-list preserves local refs");
        const auto fresh_head=new_pending->head_after();const std::array<word,1> value{~word(0)};
        new_pending->commit(value);new_pending.reset();old_pending.reset();
        require(contents(*copy,fresh_head)==std::vector<word>{~word(0)},"clone independent free-slot mutation");
        require(copy->release_values(fresh_head,observer)==1,"clone independent slot reuse");
        for(unsigned i=0;i<heads.size();++i)require(contents(source,heads[i])==expected[i],"source unchanged after clone mutation");
        // Retire the complete source directory/payload before reading the copy.
    }
    for(unsigned i=0;i<heads.size();++i)require(contents(*copy,heads[i])==expected[i],"copy survives source retirement");
    for(unsigned i=0;i<heads.size();++i) {
        auto pending=copy->reserve_values(heads[i],1,observer);require(bool(pending),"clone grows after retirement");
        const std::array<word,1> v{word(i)};pending->commit(v);heads[i]=pending->head_after();expected[i].push_back(i);
        require(contents(*copy,heads[i])==expected[i],"clone independent growth");
    }
    copy->validate();
}
void clone_refusal() {
    pool_owner pool(1u<<20);bags source(*pool.pool);no_link_observer observer;std::uint32_t head=0;
    std::vector<word> expected;
    for(word i=0;i<1100;++i) {
        auto pending=source.reserve_values(head,1,observer);require(bool(pending),"clone pressure source");
        const std::array<word,1> v{mix(i)};pending->commit(v);head=pending->head_after();expected.push_back(v[0]);
    }
    std::size_t first_bytes=0;source.allocations([&](const oa::Allocation& a){if(!first_bytes)first_bytes=a.size;});
    auto prefix_slot=oa::allocate(*pool.pool,{.bytes=first_bytes,.alignment=8});require(bool(prefix_slot),"retain one clone prefix slot");
    std::vector<oa::Allocation> pressure;
    for(std::size_t bytes=4096;bytes>=8;bytes/=2)
        while(auto a=oa::allocate(*pool.pool,{.bytes=bytes,.alignment=8}))pressure.push_back(std::move(*a));
    require(!source.clone(*pool.pool)&&!source.clone(*pool.pool),"private clone allocation refusal");
    require(contents(source,head)==expected,"clone refusal leaves source unchanged");
    *prefix_slot=oa::Allocation{};
    // Release exactly one eligible payload while the rest of the budget remains
    // occupied. Successful-allocation counters prove a prefix was copied before
    // refusal; the repeated attempt proves its private storage was returned.
    for(unsigned attempt=0;attempt<2;++attempt) {
        const auto before=oa::statistics(*pool.pool).allocations;
        auto failed=source.clone(*pool.pool);
        require(!failed&&oa::statistics(*pool.pool).allocations>before,"partial clone refusal releases copied prefix");
        require(contents(source,head)==expected,"partial clone refusal preserves source");
    }
    pressure.clear();auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*pool.pool,0).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"clone pressure trim");orbital::worker::poll(pool.lane,1024);
    }
    auto copy=source.clone(*pool.pool);require(bool(copy)&&contents(*copy,head)==expected,"clone retries after pressure");
}
template<unsigned K,bool Direct,unsigned V> void old_api() {
    pool_owner pool;links<K,Direct,V> b(*pool.pool);std::uint32_t head=0;
    for(word i=0;i<100;++i)require(b.append(head,i,(i+17)&mask(V*8)),"unchanged append API");
    const auto copied_head=head;auto copy=b.clone(*pool.pool);require(bool(copy),"key overflow clone");
    for(word i=0;i<100;++i) {
        auto p=b.find(head,i);require(bool(p)&&b.value(p)==((i+17)&mask(V*8)),"old API value");
        b.erase(head,p);require(!b.find(head,i),"old API erase");
    }
    for(word i=0;i<100;++i) {
        auto p=copy->find(copied_head,i);require(bool(p)&&copy->value(p)==((i+17)&mask(V*8)),"cloned key overflow survives erase");
    }
    require(!head,"old API frees all links");b.validate();
}
void run() {
    reservations();relocating_directory();allocation_refusal();private_clone();clone_refusal();
    if constexpr(!Admitted) {
        old_api<8,false,8>();old_api<40,false,8>();old_api<64,false,0>();old_api<64,true,8>();
    }
}
};

struct physical_block {
    std::size_t requested;
    std::vector<byte> initialized;
    bool operator==(const physical_block&)const=default;
};
template<class Bags> std::vector<physical_block> physical(
        const Bags& b,const std::vector<unsigned>& families) {
    std::vector<physical_block> result;
    b.allocations([&](const oa::Allocation& allocation) {
        require(result.size()<families.size(),"physical oracle knows block family");
        const auto* p=reinterpret_cast<const byte*>(allocation.address);
        const unsigned assigned=unsigned(p[0])+(unsigned(p[1])<<8);
        require(assigned<=16,"independent assigned-count bound");
        // Count is always LE16. Each fixed slot contains a four-byte next ref,
        // a two-byte count and 4*2^family full64 values. Do not inspect unused
        // allocation slack, whose initialization is not part of the contract.
        const auto used=2+assigned*(6+(4u<<families[result.size()])*8);
        require(used<=allocation.size,"independent assigned payload bound");
        result.push_back({allocation.size,{p,p+used}});
    });
    require(result.size()==families.size(),"physical oracle covers every block");
    return result;
}
void all_family_parity() {
    pool_owner pool;
    suite<false>::bags checked(*pool.pool);suite<true>::bags admitted(*pool.pool);
    suite<false>::observations cs;suite<true>::observations as;
    cs.owner=&checked;as.owner=&admitted;
    std::vector<unsigned> families;
    std::array<bool,10> reached{},rank15{},rollover{};
    std::array<unsigned,10> blocks{};
    auto events=[&] {
        require(cs.count==as.count,"checked/admitted observer count parity");
        for(unsigned i=0;i<cs.count;++i) {
            const auto& c=cs.events[i];const auto& a=as.events[i];
            require(c.kind==a.kind && c.block==a.block && c.family==a.family &&
                    c.bytes==a.bytes,"checked/admitted ordered observer parity");
            require(c.family<10,"known bag family");
            if(c.kind==link_event_kind::directory_inserted) {
                require(c.block==families.size()+1,"independent block ordinal");
                families.push_back(c.family);++blocks[c.family];
            }
        }
    };
    auto bytes=[&] {
        require(physical(checked,families)==physical(admitted,families),
                "checked/admitted exact initialized bytes and requested size parity");
        require(checked.footprint().metadata==admitted.footprint().metadata,
                "checked/admitted owner metadata extent parity");
        checked.validate();admitted.validate();
    };
    auto value=[](unsigned bag,unsigned index) -> word {
        return index%7==0?0:index%11==0?~word(0):mix(bag*8192+index);
    };
    std::array<std::uint32_t,17> ch{},ah{},ct{},at{};
    // 4+8+...+2048 = 4092. The next value creates another maximum-family
    // link. Seventeen bags force rank 15 and a new block in every family.
    for(unsigned bag=0;bag<ch.size();++bag)for(unsigned index=0;index<4093;++index) {
        cs.reset();as.reset();
        auto c=checked.reserve_values(ch[bag],1,cs,ct[bag]);
        auto a=admitted.reserve_values(ah[bag],1,as,at[bag]);
        require(bool(c)&&bool(a),"all-family reserve");events();
        const auto ref=c->insertion_after();
        require(ref==a->insertion_after() && c->head_after()==a->head_after(),
                "checked/admitted logical ref parity");
        const bool fresh=ref!=ct[bag];
        if(fresh) {
            const auto f=checked.family(ref);reached[f]=true;
            rank15[f]|=(ref&255)==15;rollover[f]|=(ref&255)==0 && blocks[f]>1;
            const auto old=ct[bag];
            require(!old || (!checked.next(old)&&!admitted.next(old)),
                    "reserved all-family successor remains unreachable");
            const auto width=6+(4u<<f)*8;
            for(unsigned i=0;i<width;++i)
                require(checked.data(ref)[i]==0 && admitted.data(ref)[i]==0,
                        "new fixed slot is independently zero initialized");
            bytes();
            // Cancellation retains assigned ranks but publishes no bag edge.
            cs.reset();as.reset();c.reset();a.reset();events();cs.bounded();as.bounded();bytes();
            require(!old || (!checked.next(old)&&!admitted.next(old)),"cancel preserves old edge");
            cs.reset();as.reset();
            c=checked.reserve_values(ch[bag],1,cs,ct[bag]);
            a=admitted.reserve_values(ah[bag],1,as,at[bag]);
            require(bool(c)&&bool(a)&&c->insertion_after()==ref&&a->insertion_after()==ref,
                    "all-family cancellation reuses exact assigned rank");events();
        }
        cs.reset();as.reset();
        const std::array<word,1> input{value(bag,index)};
        c->commit(input);a->commit(input);
        ch[bag]=c->head_after();ah[bag]=a->head_after();ct[bag]=c->insertion_after();at[bag]=a->insertion_after();
        events();cs.bounded();as.bounded();
        if(fresh)bytes();
    }
    for(unsigned f=0;f<10;++f)
        require(reached[f]&&rank15[f]&&rollover[f],"every family fills and rolls its 16-slot block");
    for(unsigned bag=0;bag<ch.size();++bag) {
        std::vector<word> expected;for(unsigned i=0;i<4093;++i)expected.push_back(value(bag,i));
        require(suite<false>{}.contents(checked,ch[bag])==expected &&
                suite<true>{}.contents(admitted,ah[bag])==expected,"all-family independent ordered values");
        cs.reset();as.reset();
        require(checked.release_values(ch[bag],cs)==4093 && admitted.release_values(ah[bag],as)==4093,
                "all-family release independent count");events();
        require(cs.count==22,"eleven links each publish block and free-list writes");
    }
    bytes();const auto before=checked.footprint().requested;
    // Reuse after release checks the owner free lists as well as payload bytes.
    for(unsigned bag=0;bag<ch.size();++bag) {
        cs.reset();as.reset();auto c=checked.reserve_values(0,2,cs);auto a=admitted.reserve_values(0,2,as);
        require(bool(c)&&bool(a)&&c->head_after()==a->head_after(),"released rank reuse parity");events();
        const std::array<word,2> input{0,~word(0)};cs.reset();as.reset();c->commit(input);a->commit(input);events();
    }
    require(checked.footprint().requested==before,"released ranks require no new payload");bytes();
    // Although the typed access uses uint16/uint32, the actual schema width
    // lanes are one byte in families 0..2 and offsets are two bytes in 0..6.
    // Fixed/full/immortal removes every typed sidecar access: only the exact
    // payload copy and LE16 assigned count execute. This is not lane matching.
}
}
int main() {
    suite<false>{}.run();suite<true>{}.run();
    all_family_parity();
    std::puts("checked/admitted reservations, refusal, relocation, clones and all-family physical/event parity passed");
}
