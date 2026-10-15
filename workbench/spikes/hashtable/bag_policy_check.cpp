#include "promoted.h"
#include <map>

// Independent, untimed policy history. Splitting by value width keeps a check
// build small; zero exercises all 18 budget/width/block-cap combinations.
#ifndef HT_BAG_POLICY_VALUE_BYTES
#define HT_BAG_POLICY_VALUE_BYTES 0
#endif

using namespace ht;
namespace {
template<unsigned W> using value = std::array<byte,W>;
template<unsigned W> using oracle = std::map<word,std::vector<value<W>>>;

template<unsigned W> value<W> pattern(word key,unsigned ordinal) {
    value<W> out{};
    // Repeated zeros and repeated nonzero values are deliberate. Every byte,
    // including the upper bytes of W16, participates in the independent oracle.
    if(ordinal%7==0)return out;
    const auto n=ordinal%5==0?1u:ordinal;
    for(unsigned j=0;j<W;++j)out[j]=byte((key*131+n*29+j*97)^(0xa5u+j*11));
    return out;
}
template<unsigned W,class Body> oracle<W> contents(const Body& body) {
    oracle<W> out;
    body.each_bytes([&](word key,auto bytes) {
        require(bytes.size()==W,"policy complete value width");
        value<W> v;std::copy(bytes.begin(),bytes.end(),v.begin());out[key].push_back(v);
    });
    return out;
}
template<unsigned W,class Body> void check(const Body& body,const oracle<W>& expected) {
    require(contents<W>(body)==expected,"policy independent complete byte history");
    word total=0;for(const auto& [key,values]:expected) {
        std::vector<value<W>> got;
        require(body.get_bytes(key,[&](auto bytes) {
            require(bytes.size()==W,"policy point value width");
            value<W> v;std::copy(bytes.begin(),bytes.end(),v.begin());got.push_back(v);
        }),"policy expected key present");
        require(got==values,"policy independent point bytes and multiplicity");total+=values.size();
    }
    require(body.size()==expected.size()&&body.value_count()==total,"policy key/value cardinality");
    bool called=false;require(!body.get_bytes(0xffffffffffULL,[&](auto){called=true;})&&!called,"policy absent key");
}
template<unsigned W,class Bags> std::vector<value<W>> bag_contents(const Bags& bags,std::uint32_t head) {
    std::vector<value<W>> out;
    bags.each_bytes(head,[&](auto bytes) {
        require(bytes.size()==W,"policy link entry width");
        value<W> v;std::copy(bytes.begin(),bytes.end(),v.begin());out.push_back(v);
    });
    return out;
}

struct observed_links {
    const void* owner=nullptr;
    const byte* watch=nullptr;
    std::array<byte,6> image{};
    unsigned events=0,grown=0,moved=0;
    bool seen=false;
    void reset()noexcept {watch=nullptr;seen=false;events=0;}
    void watch_header(const byte* p)noexcept {watch=p;std::memcpy(image.data(),p,image.size());seen=false;}
    void operator()(const link_event& e)noexcept {
        ++events;require(e.owner==owner,"policy observed owner identity");
        grown+=e.kind==link_event_kind::allocation_grown;
        moved+=e.kind==link_event_kind::directory_relocated;
        if(e.kind!=link_event_kind::before_block_write&&e.kind!=link_event_kind::allocation_grown)return;
        require(e.block&&e.allocation&&e.address==e.allocation->address&&e.bytes==e.allocation->size,
                "policy observed current allocation extent");
        const auto begin=reinterpret_cast<std::uintptr_t>(e.address),at=reinterpret_cast<std::uintptr_t>(watch);
        if(e.kind==link_event_kind::before_block_write&&watch&&!seen&&at>=begin&&at+image.size()<=begin+e.bytes) {
            require(std::memcmp(watch,image.data(),image.size())==0,"policy observation before header/count/edge write");
            seen=true;
        }
    }
};

template<unsigned W,class Bags> void append(Bags& bags,std::uint32_t& head,
        std::span<const value<W>> values,observed_links& seen) {
    const auto old=bag_contents<W>(bags,head);seen.reset();
    auto pending=bags.reserve_values(head,unsigned(values.size()),seen);require(bool(pending),"policy link reservation");
    require(bag_contents<W>(bags,head)==old,"policy reservation is not logical publication");
    // Resolve only after reserve: Orbital is allowed to move a block there.
    const auto insertion=pending->insertion_after();seen.watch_header(bags.data(insertion));
    pending->commit_bytes({reinterpret_cast<const byte*>(values.data()),values.size()*W});
    head=pending->head_after();require(seen.seen,"policy commit has actual before-write observation");
    require(seen.events<=Bags::reservation_event_bound,"policy reservation event capacity");
    auto expected=old;expected.insert(expected.end(),values.begin(),values.end());
    require(bag_contents<W>(bags,head)==expected,"policy link atomic group bytes/order");
}

template<unsigned W,unsigned Budget,unsigned Slots> void physical_owner() {
    using Bags=linked_bags<W,true,Slots>;
    constexpr unsigned count=4*Slots+3,first=Budget/W+1;
    pool_owner pool;std::optional<Bags> copy;
    std::vector<std::uint32_t> heads(count);
    std::vector<std::vector<value<W>>> expected(count);
    {
        Bags source(*pool.pool);observed_links seen;seen.owner=&source;
        for(unsigned i=0;i<count;++i) {
            std::array<value<W>,first> group;
            for(unsigned j=0;j<first;++j)group[j]=pattern<W>(i,j);
            append<W>(source,heads[i],group,seen);expected[i].assign(group.begin(),group.end());
            require((heads[i]&255u)==i%Slots,"policy representation-local rank reaches requested block cap");
            if(i%Slots)require(heads[i]>>8==heads[i-1]>>8,"policy neighboring heads share physical block");
            else if(i)require(heads[i]>>8!=heads[i-1]>>8,"policy next block after exact cap");
        }
        require(seen.grown&&seen.moved>=2,"policy real payload growth and directory relocation");
        if constexpr(Slots==256)require((heads[255]&255u)==255&&heads[256]>>8!=heads[255]>>8,
                                       "policy rank255 and 256th assigned-slot boundary");
        unsigned blocks=0;source.allocations([&](const oa::Allocation& allocation) {
            const unsigned assigned=unsigned(allocation.address[0])|unsigned(allocation.address[1])<<8;
            require(assigned==(blocks<4?Slots:3),"policy independent LE16 assigned count");++blocks;
        });
        require(blocks==5,"policy five real same-family blocks");

        // Extend one chain through multiple geometric link families, without
        // confusing link value capacity with the number of links in a block.
        for(unsigned j=first;j<2*std::bit_ceil(first)+83;++j) {
            const std::array<value<W>,1> v{pattern<W>(0,j)};
            append<W>(source,heads[0],v,seen);expected[0].push_back(v[0]);
        }
        require(source.next(heads[0])&&source.next(source.next(heads[0])),"policy several link families");
        for(unsigned i=1;i<count;i+=3) {
            seen.reset();seen.watch_header(source.data(heads[i]));
            const auto bound=source.release_event_bound(heads[i]);
            require(source.release_values(heads[i],seen)==expected[i].size()&&seen.seen&&seen.events==bound,
                    "policy shared-block release observes old bytes and exact count");
            heads[i]=0;expected[i].clear();
        }
        for(unsigned i=0;i<count;++i)require(bag_contents<W>(source,heads[i])==expected[i],"policy release preserves neighboring bags");

        const auto before=source.footprint();seen.reset();
        auto cancelled=source.reserve_values(0,first,seen);require(bool(cancelled),"policy cancelled free-list reservation");
        const auto reused=cancelled->head_after();cancelled.reset();
        require(seen.events<=Bags::reservation_event_bound,"policy cancel event capacity");
        seen.reset();cancelled=source.reserve_values(0,first,seen);
        require(cancelled&&cancelled->head_after()==reused,"policy cancellation returns identical local ref");cancelled.reset();
        require(source.footprint().requested==before.requested,"policy cancel/reuse consumes no new payload storage");

        copy=source.clone(*pool.pool);require(bool(copy),"policy physical clone");
        require(copy->footprint().requested==source.footprint().requested,"policy clone preserves visible reservations");
        source.allocations([&](const oa::Allocation& old) {copy->allocations([&](const oa::Allocation& fresh) {
            require(old.storage!=fresh.storage&&old.address!=fresh.address,"policy clone has independently owned blocks");
        });});
        for(unsigned i=0;i<count;++i)require(bag_contents<W>(*copy,heads[i])==expected[i],"policy cloned refs and free/active metadata");
        observed_links copied;copied.owner=&*copy;
        auto a=source.reserve_values(0,first,seen),b=copy->reserve_values(0,first,copied);
        require(a&&b&&a->head_after()==b->head_after(),"policy clone retains free-list ordering");a.reset();b.reset();
        // Mutate and retire the original; its free-list links share blocks with
        // still-live bags, so a shallow copy would corrupt the retained owner.
        for(unsigned i=0;i<count;++i)if(heads[i]) {
            seen.reset();require(source.release_values(heads[i],seen)==expected[i].size(),"policy retire source bag");
        }
        std::uint32_t fresh=0;std::array<value<W>,first> values;
        for(unsigned i=0;i<first;++i)values[i]=pattern<W>(9999,i);
        append<W>(source,fresh,values,seen);source.validate();
        for(unsigned i=0;i<count;++i)require(bag_contents<W>(*copy,heads[i])==expected[i],"policy source reuse preserves retained bytes");
    }
    copy->validate();
    for(unsigned i=0;i<count;++i)require(bag_contents<W>(*copy,heads[i])==expected[i],"policy physical copy survives complete source retirement");
    observed_links seen;seen.owner=&*copy;
    const std::array<value<W>,1> extra{pattern<W>(10001,1)};
    append<W>(*copy,heads[0],extra,seen);expected[0].push_back(extra[0]);copy->validate();
    require(bag_contents<W>(*copy,heads[0])==expected[0],"policy cloned active owner grows after retirement");
}

template<unsigned W,unsigned Budget,unsigned Slots> void integrated_history() {
    using Bags=linked_bags<W,true,Slots>;
    using Small=small_table<40,container_kind::multimap,W,true,Bags,Budget>;
    using Body=promoted_table<40,container_kind::multimap,W,1,0,128,128,
        record_format::tag_bits,false,record_access::both,true,Bags,Budget>;
    constexpr unsigned capacity=Budget/W,initial=64,total=std::max(128u,Slots+17);
    pool_owner pool;std::optional<Small> retained;oracle<W> old;
    {
        Small source(*pool.pool,19);oracle<W> expected;
        for(unsigned k=0;k<initial;++k) {const auto v=pattern<W>(k,0);source.set_bytes(k,v);expected[k].push_back(v);}
        // Round-robin updates put different counts in adjacent list records.
        // Lists currently use row-byte tags; packed tags are exercised in the
        // promoted tag_bits format below, not claimed for this list layout.
        for(unsigned n=1;n<capacity;++n) {
            const auto allocations=oa::statistics(*pool.pool).allocations;
            for(unsigned k=0;k<initial;++k)if(k%3!=0) {
                const auto v=pattern<W>(k,n);const auto d=source.set_bytes(k,v);
                require(d.keys==0&&d.values==1,"policy inline append delta");expected[k].push_back(v);
            }
            require(oa::statistics(*pool.pool).allocations==allocations,"policy admitted inline budget needs no bag allocation");
            check<W>(source,expected);
            source.each_record([&](word k,const auto& r) {
                const auto n=expected.at(k).size();require(r.tag==(n==1?0:n),"policy exact inline tag count and neighbor preservation");
            });
        }
        // Fill and spill every key, exercising the newly enlarged first-spill
        // group (17 values for B32/W2) as one fallible admission.
        for(unsigned k=0;k<initial;++k) {
            while(expected[k].size()<capacity) {
                const auto v=pattern<W>(k,expected[k].size());source.set_bytes(k,v);expected[k].push_back(v);
            }
            std::array<small_event,0> none;small_journal denied{none};
            const auto v=pattern<W>(k,capacity);
            require(!source.set_bytes(k,v,denied)&&denied.used==0,"policy first-spill effects refusal emits nothing");
            check<W>(source,expected);
            std::array<small_event,32> events;small_journal admitted{events};
            auto d=source.set_bytes(k,v,admitted);require(d&&d->keys==0&&d->values==1,"policy atomic first-spill delta");
            bool bag=false,record=false;unsigned deltas=0;
            for(unsigned i=0;i<admitted.used;++i) {
                const auto& e=events[i];bag|=e.type==small_event::kind::bag_write;
                if(e.type==small_event::kind::list_write){require(bag,"policy first-spill bag before head publication");record=true;}
                if(e.type==small_event::kind::caller_delta){require(record&&e.delta.keys==0&&e.delta.values==1,"policy spill caller publication");++deltas;}
            }
            require(bag&&record&&deltas==1,"policy first-spill publication includes bag, record, one delta");expected[k].push_back(v);
        }
        source.each_record([](word,const auto& r){require(r.tag==1,"policy first spill changes every tag to external");});
        check<W>(source,expected);retained=source.clone();require(bool(retained),"policy retained physical small copy");old=expected;
        source.allocations([&](const oa::Allocation& a){retained->allocations([&](const oa::Allocation& b){
            require(a.storage!=b.storage&&a.address!=b.address,"policy small clone independent list and bag allocations");
        });});
        auto body=Body::promote(source,*pool.pool,41,73);require(body&&source.size()==0&&source.value_count()==0,"policy promotion consumes small owner");
        check<W>(*body,expected);
        for(unsigned k=initial;k<total;++k)for(unsigned n=0;n<=capacity;++n) {
            const auto v=pattern<W>(k,n);body->set_bytes(k,v);expected[k].push_back(v);
        }
        require(body->population()[0]&&body->population()[1]&&body->population()[2],"policy primary, secondary and key overflow");
        check<W>(*body,expected);
        for(unsigned k=0;k<total;k+=2) {
            const auto d=body->erase(k);require(d.keys==-1&&d.values==-std::int64_t(expected[k].size()),"policy hierarchy erase whole bag");expected.erase(k);
        }
        // Reinsert mixed inline counts beside external records; then transition
        // every reinserted key through its own first spill.
        for(unsigned n=0;n<=capacity;++n) {
            for(unsigned k=0;k<total;k+=2) {
                const auto v=pattern<W>(k+10000,n);body->set_bytes(k,v);expected[k].push_back(v);
            }
            check<W>(*body,expected);body->validate();
        }
        // Empty the index without resetting its bag owner, then refill all
        // tiers. Unlike relying on the even-key distribution above, this proves
        // inline/tag transitions actually reached primary, secondary AND key
        // overflow. B24/W8 and B32/W8 tags share bytes; B32/W2 tags do not.
        for(unsigned k=0;k<total;++k)body->erase(k);
        expected.clear();check<W>(*body,expected);
        std::array<bool,3> writes{};
        for(unsigned n=0;n<=capacity;++n) {
            for(unsigned k=0;k<total;++k) {
                const auto v=pattern<W>(k+20000,n);
                std::array<small_event,32> events;small_journal journal{events};
                auto d=body->try_set_bytes(k,v,journal);require(bool(d),"policy observed all-tier refill");
                expected[k].push_back(v);
                if(n)for(unsigned e=0;e<journal.used;++e) {
                    writes[0]|=events[e].type==small_event::kind::primary_write;
                    writes[1]|=events[e].type==small_event::kind::secondary_write;
                    writes[2]|=events[e].type==small_event::kind::overflow_write;
                }
            }
            check<W>(*body,expected);
        }
        require(writes[0]&&writes[1]&&writes[2],"policy inline/spill updates observed in every key tier");
        const auto before=expected;std::array<small_event,0> none;small_journal denied{none};
        require(!body->try_set_bytes(0,pattern<W>(77,3),denied)&&denied.used==0,"policy hierarchy effects refusal before external append");
        check<W>(*body,before);
        for(unsigned n=0;n<129;++n) {
            const auto v=pattern<W>(0,n+capacity+1);body->set_bytes(0,v);expected[0].push_back(v);
        }
        check<W>(*body,expected);body->validate();check<W>(*retained,old);
    }
    check<W>(*retained,old); // Both moved-to hierarchy and original list retired.
}

template<unsigned W,unsigned Budget,unsigned Slots> void allocation_refusal() {
    using Bags=linked_bags<W,true,Slots>;
    using Small=small_table<40,container_kind::multimap,W,true,Bags,Budget>;
    constexpr unsigned capacity=Budget/W;
    pool_owner pool(1u<<20);Small body(*pool.pool,19);oracle<W> expected;
    for(unsigned n=0;n<capacity;++n) {const auto v=pattern<W>(7,n);body.set_bytes(7,v);expected[7].push_back(v);}
    std::vector<oa::Allocation> pressure;
    for(std::size_t bytes=4096;bytes>=8;bytes/=2)
        while(auto a=oa::allocate(*pool.pool,{.bytes=bytes,.alignment=8}))pressure.push_back(std::move(*a));
    const auto v=pattern<W>(7,capacity);no_small_effects effects;
    // Existing list storage is already present. Refusal is the bag's first
    // external allocation, not initial key/list admission. Structural outcomes
    // may be published on allocation refusal; only logical bytes are invariant.
    require(!body.set_bytes(7,v,effects),"policy first-spill allocation refusal");check<W>(body,expected);
    require(!body.set_bytes(7,v,effects),"policy repeated allocation refusal");check<W>(body,expected);
    pressure.clear();const auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*pool.pool,0).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"policy refusal pressure trim");orbital::worker::poll(pool.lane,1024);
    }
    const auto d=body.set_bytes(7,v);require(d.keys==0&&d.values==1,"policy retry delta exactly once");
    expected[7].push_back(v);check<W>(body,expected);
}

template<unsigned W,unsigned Budget,unsigned Slots> void policy() {
    physical_owner<W,Budget,Slots>();integrated_history<W,Budget,Slots>();allocation_refusal<W,Budget,Slots>();
    std::printf("policy passed: value_bytes=%u inline_bytes=%u links_per_block=%u tag_bits=%u\n",
                W,Budget,Slots,inline_bag<W,Budget>::tag_bits);
}
template<unsigned W> void width() {
    policy<W,24,16>();policy<W,24,64>();policy<W,24,256>();
    policy<W,32,16>();policy<W,32,64>();policy<W,32,256>();
}
}
int main() {
#if HT_BAG_POLICY_VALUE_BYTES == 0 || HT_BAG_POLICY_VALUE_BYTES == 2
    width<2>();
#endif
#if HT_BAG_POLICY_VALUE_BYTES == 0 || HT_BAG_POLICY_VALUE_BYTES == 8
    width<8>();
#endif
#if HT_BAG_POLICY_VALUE_BYTES == 0 || HT_BAG_POLICY_VALUE_BYTES == 16
    width<16>();
#endif
    static_assert(HT_BAG_POLICY_VALUE_BYTES==0||HT_BAG_POLICY_VALUE_BYTES==2||
                  HT_BAG_POLICY_VALUE_BYTES==8||HT_BAG_POLICY_VALUE_BYTES==16);
    std::puts("coupled bag policy: block/rank growth, shared-block reuse, independent physical clones, packed tags, promotion, first-spill refusal and before-write observations passed");
}
