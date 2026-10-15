#include "promoted.h"
#include <map>

using namespace ht;
namespace {
// Observations resolve live allocations synchronously. These before images
// neither retain storage nor permit another mutation while a borrow is live.
struct index_journal {
    const byte* address=nullptr;
    std::vector<byte> before;
    unsigned tier=0,events=0;
    void remember(const oa::Allocation& a,std::size_t offset,std::size_t bytes,unsigned t)noexcept {
        require(!events++,"one index damage event");
        require(offset<=a.size && bytes<=a.size-offset,"observed allocation bounds");
        address=reinterpret_cast<const byte*>(a.address)+offset;
        before.assign(address,address+bytes);tier=t;
    }
    void bucket(const void*,const oa::Allocation& a,std::size_t offset,std::size_t bytes,bool secondary)noexcept {
        remember(a,offset,bytes,secondary?2:1);
    }
    void operator()(const link_event& e)noexcept {
        require(e.kind==link_event_kind::before_block_write && e.allocation,"key-overflow damage");
        remember(*e.allocation,0,e.bytes,3);
    }
};

template<unsigned K> void exact_prefix() {
    using Index=record_table<K,container_kind::multimap,64,1,0,128,128,
        record_format::tag_bits,false,record_access::both,4>;
    pool_owner pool;Index body(*pool.pool,41,73);
    std::map<word,typename Index::record> expected;
    for(word k=0;k<32;++k) {
        typename Index::record row;
        for(unsigned i=0;i<64;++i)row.payload[i]=byte(k*37+i*19);
        row.tag=1;require(body.insert_record(k,row),"poisoned external row insertion");expected[k]=row;
    }
    unsigned tiers=0;
    for(word k=0;k<32;++k) {
        auto next=expected[k];for(unsigned i=0;i<8;++i)next.payload[i]^=0xff;
        index_journal observed;bool prepared=false;
        auto r=body.mutate(k,[&](const auto&)->std::optional<typename Index::record>{prepared=true;return {};},observed,
            [&](auto& edit)->existing_record_result {
                require(edit.tag()==1,"external tag precedes copied descriptor");
                const auto prefix=edit.template prefix<8>();
                require(std::equal(prefix.begin(),prefix.end(),expected[k].payload.begin()),"exact copied descriptor");
                edit.template patch<8>(0,std::span<const byte,8>(next.payload.data(),8));
                return mutation_delta{0,1};
            });
        require(r && !prepared && r->keys==0 && r->values==1 && observed.events==1,"handled edit publishes once without fallback");
        tiers|=1u<<observed.tier;
        unsigned changed=0;std::size_t first=observed.before.size(),last=0;
        for(std::size_t i=0;i<observed.before.size();++i)if(observed.before[i]!=observed.address[i]) {
            ++changed;first=std::min(first,i);last=i;
        }
        require(changed==8 && last-first==7,"before-image precedes exactly eight contiguous descriptor stores");
        expected[k]=next;
        unsigned seen=0;
        body.each_record([&](word key,const auto& row) {
            ++seen;require(row.payload==expected.at(key).payload && row.tag==1,"inactive poison and neighboring records/tags preserved");
        });
        require(seen==expected.size(),"every physical row checked");
        index_journal refused;
        r=body.mutate(k,[&](const auto&)->std::optional<typename Index::record>{prepared=true;return {};},refused,
            [](auto&)->existing_record_result {return std::unexpected(mutation_error::preparation);});
        require(!r && r.error()==mutation_error::preparation && !prepared && !refused.events,
            "handled refusal cannot fall through or publish index damage");
    }
    require(tiers==14,"prefix and patch cover primary, secondary and key overflow");body.validate();
}

template<unsigned W> using value=std::array<byte,W>;
template<unsigned W> value<W> pattern(word key,unsigned ordinal) {
    value<W> result;for(unsigned i=0;i<W;++i)result[i]=byte(key*37+ordinal*71+i*19);return result;
}
template<unsigned W,class Body> auto contents(const Body& body) {
    std::map<word,std::vector<value<W>>> result;
    body.each_bytes([&](word k,auto bytes){value<W> v;std::copy(bytes.begin(),bytes.end(),v.begin());result[k].push_back(v);});
    for(auto& [k,v]:result)std::sort(v.begin(),v.end());return result;
}
struct events {
    unsigned deltas=0,index_writes=0,bag_writes=0,tiers=0;
    void before(const small_event& e)noexcept {
        using E=small_event::kind;
        if(e.type==E::caller_delta) {
            require(index_writes<=1 && bag_writes && e.delta.keys==0 && e.delta.values==1,"delta follows bag and optional index commits");++deltas;
        }else if(e.type==E::primary_write || e.type==E::secondary_write || e.type==E::overflow_write) {
            require(bag_writes && !deltas,"index damage follows bag commit and precedes delta");++index_writes;
            tiers|=1u<<(e.type==E::primary_write?1:e.type==E::secondary_write?2:3);
        }else if(e.type==E::bag_write) {require(!index_writes && !deltas,"bag write before index commit");++bag_writes;}
    }
};
struct one_operation:events {
    std::size_t remaining()const{return deltas?0:SIZE_MAX;}
};

// Fill external bags in every key tier, force real allocation refusal, then
// retry after pressure is released. Exercise the same path from ordered packets.
template<unsigned W,class Bags> void external_growth() {
    using Small=small_table<64,container_kind::multimap,W,true,Bags,64>;
    using Body=promoted_table<64,container_kind::multimap,W,1,0,128,128,
        record_format::tag_bits,false,record_access::both,true,Bags,64>;
    pool_owner pool(4u<<20);Small small(*pool.pool,19);
    auto body=Body::promote(small,*pool.pool,41,73);require(bool(body),"private hierarchy allocation");
    std::map<word,std::vector<value<W>>> expected;
    auto remember=[&](word key,value<W> v){expected[key].push_back(v);std::sort(expected[key].begin(),expected[key].end());};
    for(word k=0;k<16;++k)for(unsigned i=0;i<4;++i){auto v=pattern<W>(k,i);body->set_bytes(k,v);remember(k,v);}
    require(body->population()[0] && body->population()[1] && body->population()[2],"external growth spans all key tiers");
    std::vector<oa::Allocation> pressure;
    for(std::size_t bytes=4096;bytes>=8;bytes/=2)while(auto a=oa::allocate(*pool.pool,{.bytes=bytes,.alignment=8}))pressure.push_back(std::move(*a));
    unsigned tiers=0;
    for(word k=0;k<16;++k) {
        bool refused=false;
        for(unsigned attempt=0;attempt<4096;++attempt) {
            const auto before=body->value_count();auto v=pattern<W>(k,100+attempt);events observed;
            auto r=body->try_set_bytes(k,v,observed);
            if(!r) {
                require(r.error()==mutation_error::preparation && !observed.deltas && !observed.index_writes &&
                    body->value_count()==before && contents<W>(*body)==expected,"external refusal keeps index, reachable values and counts");
                refused=true;break;
            }
            require(observed.deltas==1,"successful external append publishes one delta");tiers|=observed.tiers;remember(k,v);
        }
        require(refused,"pressure reaches external capacity refusal");
    }
    // Key zero has just refused; duplicate active lanes must not retry or skip
    // past that lane. Nothing may be published after the failed first lane.
    std::array<word,4> keys{0,0,1,0};std::array<value<W>,4> values;
    for(unsigned i=0;i<4;++i)values[i]=pattern<W>(0,500+i);
    events denied;
    require(body->template setN_bytes<4>(keys,values,15,denied)==0 && !denied.deltas && !denied.index_writes &&
        contents<W>(*body)==expected,"packet stops at external allocation refusal");
    pressure.clear();auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*pool.pool,0).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"external pressure trim");orbital::worker::poll(pool.lane,1024);
    }
    for(word k=0;k<16;++k) {
        auto v=pattern<W>(k,700);events observed;auto r=body->try_set_bytes(k,v,observed);
        require(r && observed.deltas==1,"external retry commits exactly once");tiers|=observed.tiers;remember(k,v);
    }
    if constexpr(std::is_same_v<Bags,buffer_bags<W>>)require(!tiers,"stable buffer descriptor causes no index writes");
    else require(tiers==14,"changed link descriptors publish damage in all tiers");
    one_operation prefix;
    require(body->template setN_bytes<4>(keys,values,15,prefix)==1 && prefix.deltas==1,
        "duplicate packet commits first lane and stops on observer refusal");
    remember(keys[0],values[0]);
    require(contents<W>(*body)==expected,"no repeated-key lane after refused prefix is replayed");
    require(body->template setN_bytes<4>(keys,values,11)==11,"ordered duplicate packet after retry");
    for(unsigned i: {0u,1u,3u})remember(keys[i],values[i]);
    body->validate();require(contents<W>(*body)==expected,"full external-growth and retry contents");
}
#if HT_BAG_SPILL_PATCH
// No external owner exists when pressure begins. Failure may create an empty
// owner, but must leave every inline row and every caller count unchanged.
template<unsigned W,class Bags> void first_spill() {
    using Small=small_table<64,container_kind::multimap,W,true,Bags,64>;
    using Body=promoted_table<64,container_kind::multimap,W,1,0,128,128,
        record_format::tag_bits,false,record_access::both,true,Bags,64>;
    constexpr unsigned C=64/W;
    pool_owner pool(4u<<20);Small small(*pool.pool,19);
    auto body=Body::promote(small,*pool.pool,41,73);require(bool(body),"first-spill hierarchy");
    std::map<word,std::vector<value<W>>> expected;
    auto remember=[&](word k,value<W> v){expected[k].push_back(v);std::sort(expected[k].begin(),expected[k].end());};
    for(word k=0;k<16;++k)for(unsigned i=0;i<C;++i){auto v=pattern<W>(k,i);body->set_bytes(k,v);remember(k,v);}
    require(body->population()[0] && body->population()[1] && body->population()[2],"full inline rows span three tiers");
    const auto before_count=body->value_count();std::vector<oa::Allocation> pressure;
    for(std::size_t bytes=4096;bytes>=8;bytes/=2)while(auto a=oa::allocate(*pool.pool,{.bytes=bytes,.alignment=8}))pressure.push_back(std::move(*a));
    for(word k=0;k<16;++k) {
        events observed;auto r=body->try_set_bytes(k,pattern<W>(k,C),observed);
        require(!r && r.error()==mutation_error::preparation && !observed.deltas && !observed.index_writes &&
            body->value_count()==before_count && contents<W>(*body)==expected,"first-spill refusal preserves every full inline row");
    }
    pressure.clear();auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*pool.pool,0).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"first-spill pressure trim");orbital::worker::poll(pool.lane,1024);
    }
    unsigned tiers=0;
    for(word k=0;k<16;++k) {
        auto v=pattern<W>(k,C);events observed;auto r=body->try_set_bytes(k,v,observed);
        require(r && r->keys==0 && r->values==1 && observed.deltas==1 && observed.index_writes==1,
            "retried first spill publishes one index patch and one value delta");
        tiers|=observed.tiers;remember(k,v);
        require(contents<W>(*body)==expected,"split-input first spill keeps all old values and the new value");
    }
    require(tiers==14 && body->value_count()==before_count+16,"first spill commits once in every tier");body->validate();
}
#endif
}
int main() {
    exact_prefix<8>();exact_prefix<16>();exact_prefix<24>();exact_prefix<32>();
    exact_prefix<40>();exact_prefix<48>();exact_prefix<56>();exact_prefix<64>();
    external_growth<32,linked_bags<32,true>>();external_growth<64,linked_bags<64,true>>();
    external_growth<32,buffer_bags<32>>();external_growth<64,buffer_bags<64>>();
#if HT_BAG_SPILL_PATCH
    first_spill<1,linked_bags<1,true>>();first_spill<2,linked_bags<2,true>>();
    first_spill<3,linked_bags<3,true>>();
    first_spill<32,linked_bags<32,true>>();first_spill<57,linked_bags<57,true>>();first_spill<64,linked_bags<64,true>>();
    first_spill<1,buffer_bags<1>>();first_spill<2,buffer_bags<2>>();
    first_spill<3,buffer_bags<3>>();
    first_spill<32,buffer_bags<32>>();first_spill<57,buffer_bags<57>>();first_spill<64,buffer_bags<64>>();
#endif
    std::puts("external descriptor: exact writes/all domains/tiers, allocation refusal/retry and ordered packets passed");
}
