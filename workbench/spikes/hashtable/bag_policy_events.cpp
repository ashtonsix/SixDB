#include "promoted.h"
#include <map>
#include <numeric>
#include <random>
#include <string>

// Untimed logical attribution for value_width_bench.cpp's mixed bag controls
// 1/5/6. This observer deliberately changes the consumer: none of its execution
// time is a benchmark result. No Storage internals or kernel hooks are used.
// Zero builds all three widths; set the macro to 2, 8 or 16 to split this TU.
#ifndef HT_BAG_POLICY_VALUE_BYTES
#define HT_BAG_POLICY_VALUE_BYTES 0
#endif

using namespace ht;
namespace {
constexpr unsigned K=40,A=10,B=6,N=6144,Budget=16,Unit=192;
constexpr word PrimarySeed=41,SecondarySeed=73;
using event_kind=small_event::kind;
template<unsigned W> using value_type=std::array<byte,W>;
template<unsigned W> using oracle=std::map<word,std::vector<value_type<W>>>;

unsigned fanout(unsigned i) {
    const auto x=mix(i+317)%100;
    return x<75?1:x<90?2:x<96?4:x<99?16:64;
}
word key(unsigned i) {return word(i)*0xd6e8feb86659fd93ULL&mask(K);}

// Kept byte-for-byte equivalent to the workload's value generator, including
// the complete-bag XOR successor correction. Expected storage comes from these
// input arrays, never from a candidate read or encoded record.
template<unsigned W> value_type<W> value(unsigned i,unsigned j,unsigned count,unsigned step) {
    value_type<W> result{};
    auto part=[&](unsigned n) {
        value_type<W> out;
        for(unsigned b=0;b<W;++b)out[b]=byte(mix(word(i)+(word(n)<<32)+719*b));
        return out;
    };
    if(j+1<count)return part(j);
    result=part(j);word correction=step;
    for(unsigned b=0;b<W;++b)correction^=word(result[b])<<((b%8)*8);
    for(unsigned n=0;n<j;++n) {
        auto x=part(n);for(unsigned b=0;b<W;++b)correction^=word(x[b])<<((b%8)*8);
    }
    for(unsigned b=0;b<std::min(W,8u);++b)result[b]^=byte(correction>>(b*8));
    return result;
}

struct link_counts {
    word block_writes=0,issued_damage_bytes=0,new_blocks=0,growths=0;
    word directory_relocations=0,directory_insertions=0,directory_reported_bytes=0;
    word free_list_writes=0,active_writes=0,owner_metadata_write_bytes=0;
    std::array<word,10> blocks_by_family{},growths_by_family{},writes_by_family{},damage_by_family{};
    void event(const small_event& e)noexcept {
        require(e.link_kind.has_value()&&e.family<10,"bag attribution link kind/family");
        switch(*e.link_kind) {
        case link_event_kind::before_block_write:
            require(e.storage&&e.block&&e.size,"bag attribution real block extent");
            ++block_writes;issued_damage_bytes+=e.size;
            ++writes_by_family[e.family];damage_by_family[e.family]+=e.size;break;
        case link_event_kind::directory_inserted:
            ++new_blocks;++directory_insertions;directory_reported_bytes+=e.size;
            ++blocks_by_family[e.family];break;
        case link_event_kind::directory_relocated:
            ++directory_relocations;directory_reported_bytes+=e.size;break;
        case link_event_kind::allocation_grown:
            require(e.storage&&e.block&&e.size,"bag attribution successful growth extent");
            ++growths;++growths_by_family[e.family];break;
        case link_event_kind::before_free_list_write:
            ++free_list_writes;owner_metadata_write_bytes+=e.size;break;
        case link_event_kind::before_active_write:
            ++active_writes;owner_metadata_write_bytes+=e.size;break;
        }
    }
};

struct observed {
    word events=0,bag_owners=0,primary_writes=0,primary_bytes=0,secondary_writes=0,secondary_bytes=0;
    word caller_events=0;std::int64_t keys_delta=0,values_delta=0;
    link_counts bags,overflow;
    std::size_t remaining()const noexcept {return SIZE_MAX;}
    void before(const small_event& e)noexcept {
        ++events;
        switch(e.type) {
        case event_kind::bag_owner_created:++bag_owners;break;
        case event_kind::primary_write:++primary_writes;primary_bytes+=e.size;break;
        case event_kind::secondary_write:++secondary_writes;secondary_bytes+=e.size;break;
        case event_kind::bag_write:case event_kind::bag_structure:bags.event(e);break;
        case event_kind::overflow_write:case event_kind::overflow_structure:overflow.event(e);break;
        case event_kind::caller_delta:++caller_events;keys_delta+=e.delta.keys;values_delta+=e.delta.values;break;
        default:require(false,"bag attribution unexpected small-list/reset event in hierarchy mutation");
        }
    }
};

template<unsigned W,class Body> void check(const Body& body,const oracle<W>& expected) {
    oracle<W> actual;
    body.each_bytes([&](word k,auto bytes) {
        require(bytes.size()==W,"bag attribution full value width");
        value_type<W> v;std::copy(bytes.begin(),bytes.end(),v.begin());actual[k].push_back(v);
    });
    for(auto& [k,values]:actual)std::sort(values.begin(),values.end());
    require(actual==expected,"bag attribution independent complete byte oracle");
    word count=0;for(const auto& [k,values]:expected)count+=values.size();
    require(body.size()==expected.size()&&body.value_count()==count,"bag attribution caller counts");
    for(unsigned i=0;i<2*N;++i) {
        std::vector<value_type<W>> actual;
        const auto found=body.get_bytes(key(i),[&](auto bytes) {
            require(bytes.size()==W,"bag attribution point value width");
            value_type<W> v;std::copy(bytes.begin(),bytes.end(),v.begin());actual.push_back(v);
        });
        std::sort(actual.begin(),actual.end());const auto at=expected.find(key(i));
        require(found==(at!=expected.end())&&(found?actual==at->second:actual.empty()),"bag attribution independent hit/miss bytes");
    }
    body.validate();
}

void emit_array(const std::array<word,10>& values) {
    std::putchar('[');
    for(unsigned i=0;i<values.size();++i)std::printf("%s%llu",i?",":"",(unsigned long long)values[i]);
    std::putchar(']');
}
void emit_links(const char* name,const link_counts& c) {
    std::printf("\"%s\":{\"block_writes\":%llu,\"issued_damage_bytes\":%llu,\"new_blocks\":%llu,\"growths\":%llu,"
        "\"directory_relocations\":%llu,\"directory_insertions\":%llu,\"directory_reported_bytes\":%llu,"
        "\"free_list_writes\":%llu,\"active_writes\":%llu,\"owner_metadata_write_bytes\":%llu,\"blocks_by_family\":",
        name,(unsigned long long)c.block_writes,(unsigned long long)c.issued_damage_bytes,(unsigned long long)c.new_blocks,
        (unsigned long long)c.growths,(unsigned long long)c.directory_relocations,(unsigned long long)c.directory_insertions,
        (unsigned long long)c.directory_reported_bytes,(unsigned long long)c.free_list_writes,
        (unsigned long long)c.active_writes,(unsigned long long)c.owner_metadata_write_bytes);
    emit_array(c.blocks_by_family);std::printf(",\"growths_by_family\":");emit_array(c.growths_by_family);
    std::printf(",\"writes_by_family\":");emit_array(c.writes_by_family);
    std::printf(",\"damage_by_family\":");emit_array(c.damage_by_family);std::putchar('}');
}

template<unsigned W,unsigned Slots> void run() {
    constexpr unsigned Arm=Slots==16?1:Slots==64?5:6;
    using Bags=linked_bags<W,true,Slots>;
    using Small=small_table<K,container_kind::multimap,W,true,Bags,Budget>;
    using Body=promoted_table<K,container_kind::multimap,W,A,B,Unit,Unit,
        record_format::exact,false,record_access::both,true,Bags,Budget>;
    std::vector<unsigned> order(N),next(N),offsets(N+1);
    std::iota(order.begin(),order.end(),0);std::mt19937 random(91);std::shuffle(order.begin(),order.end(),random);
    for(unsigned i=0;i<N;++i)next[order[i]]=order[(i+1)%N];
    for(unsigned i=0;i<N;++i)offsets[i+1]=offsets[i]+fanout(i);
    std::vector<value_type<W>> input(offsets.back()),updates(N);oracle<W> expected;
    for(unsigned i=0;i<N;++i) {
        const unsigned count=offsets[i+1]-offsets[i];
        for(unsigned j=0;j<count;++j) {
            auto v=value<W>(i,j,count,(next[i]+N-i)%N);input[offsets[i]+j]=v;expected[key(i)].push_back(v);
        }
        updates[i]=value<W>(i,0,2,0);std::sort(expected[key(i)].begin(),expected[key(i)].end());
    }
    pool_owner pool;const auto before_construct=oa::statistics(*pool.pool);
    auto body=[&] {Small source(*pool.pool,19);return Body::promote(source,*pool.pool,PrimarySeed,SecondarySeed);}();
    require(bool(body),"bag attribution empty owner construction");
    const auto after_construct=oa::statistics(*pool.pool);
    std::printf("{\"record\":\"context\",\"arm\":%u,\"K\":%u,\"value_bytes\":%u,\"A\":%u,\"B\":%u,"
        "\"n\":%u,\"budget\":%u,\"unit\":%u,\"block_slots\":%u,\"primary_seed\":41,\"secondary_seed\":73,"
        "\"shuffle_seed\":91,\"fanout\":0,\"input_values\":%u,\"construct_pool_allocations\":%llu,"
        "\"qualification\":\"untimed observer; phase-local issued overlapping damage extents, not unique dirty bytes, traffic or MVCC copies; "
        "directory bytes are event extents, not copied/reserved bytes; constructor/destructor have no mutation observation; "
        "observer/oracle heap excluded from owner footprint; no payload relocation/address attribution\"}\n",
        Arm,K,W,A,B,N,Budget,Unit,Slots,offsets.back(),
        (unsigned long long)(after_construct.allocations-before_construct.allocations));

    auto emit=[&](const char* phase,unsigned operations,word mutation_calls,const observed& events,const oa::Statistics& before) {
        const auto memory=body->footprint();const auto population=body->population();const auto after=oa::statistics(*pool.pool);
        std::printf("{\"record\":\"phase\",\"arm\":%u,\"K\":%u,\"value_bytes\":%u,\"kind\":2,\"A\":%u,\"n\":%u,"
            "\"phase\":\"%s\",\"operations\":%u,\"mutation_calls\":%llu,\"keys\":%u,\"values\":%llu,"
            "\"requested\":%llu,\"owned_capacity\":%llu,\"metadata\":%llu,\"current_primary_rows\":%llu,"
            "\"current_secondary_rows\":%llu,\"current_overflow_rows\":%llu,\"pool_allocations\":%llu,\"pool_growths\":%llu,"
            "\"events\":%llu,\"bag_owners_created\":%llu,\"caller_events\":%llu,\"caller_keys_delta\":%lld,\"caller_values_delta\":%lld,"
            "\"primary_writes\":%llu,\"primary_issued_damage_bytes\":%llu,\"secondary_writes\":%llu,\"secondary_issued_damage_bytes\":%llu,"
            "\"all_issued_damage_bytes\":%llu,",
            Arm,K,W,A,N,phase,operations,(unsigned long long)mutation_calls,body->size(),(unsigned long long)body->value_count(),
            (unsigned long long)memory.requested,(unsigned long long)memory.backing,(unsigned long long)memory.metadata,
            (unsigned long long)population[0],(unsigned long long)population[1],(unsigned long long)population[2],
            (unsigned long long)(after.allocations-before.allocations),(unsigned long long)(after.growths-before.growths),
            (unsigned long long)events.events,(unsigned long long)events.bag_owners,(unsigned long long)events.caller_events,
            (long long)events.keys_delta,(long long)events.values_delta,(unsigned long long)events.primary_writes,
            (unsigned long long)events.primary_bytes,(unsigned long long)events.secondary_writes,(unsigned long long)events.secondary_bytes,
            (unsigned long long)(events.primary_bytes+events.secondary_bytes+events.bags.issued_damage_bytes+events.overflow.issued_damage_bytes));
        emit_links("bags",events.bags);std::putchar(',');emit_links("key_overflow",events.overflow);std::puts("}");
    };
    observed events;auto before=oa::statistics(*pool.pool);
    for(auto i:order)for(unsigned j=offsets[i];j<offsets[i+1];++j)
        require(bool(body->try_set_bytes(key(i),input[j],events)),"bag attribution observed build");
    require(events.caller_events==input.size()&&events.keys_delta==N&&events.values_delta==input.size(),"bag attribution build deltas");
    check<W>(*body,expected);emit("build",N,input.size(),events,before);

    events={};before=oa::statistics(*pool.pool);
    for(auto i:order)require(bool(body->try_set_bytes(key(i),updates[i],events)),"bag attribution observed append");
    for(unsigned i=0;i<N;++i) {
        auto& values=expected[key(i)];values.push_back(updates[i]);std::sort(values.begin(),values.end());
    }
    require(events.caller_events==N&&events.keys_delta==0&&events.values_delta==N,"bag attribution append deltas");
    check<W>(*body,expected);emit("update",N,N,events,before);

    events={};before=oa::statistics(*pool.pool);word removed=0;
    for(unsigned i=0;i<N;i+=2) {
        removed+=expected.at(key(i)).size();require(bool(body->erase(key(i),events)),"bag attribution observed erase");expected.erase(key(i));
    }
    require(events.caller_events==N/2&&events.keys_delta==-std::int64_t(N/2)&&events.values_delta==-std::int64_t(removed),
            "bag attribution erase deltas");
    check<W>(*body,expected);emit("erase",N/2,N/2,events,before);
}

template<unsigned W> void width(unsigned arm) {
    if(!arm||arm==1)run<W,16>();
    if(!arm||arm==5)run<W,64>();
    if(!arm||arm==6)run<W,256>();
}
}
int main(int argc,char** argv) {
    require(argc==1||argc==2,"bag events [arm: 1,5,6; omitted runs all]");
    const unsigned arm=argc==2?unsigned(std::stoul(argv[1])):0;
    require(!arm||arm==1||arm==5||arm==6,"bag event policy arm");
#if HT_BAG_POLICY_VALUE_BYTES == 0 || HT_BAG_POLICY_VALUE_BYTES == 2
    width<2>(arm);
#endif
#if HT_BAG_POLICY_VALUE_BYTES == 0 || HT_BAG_POLICY_VALUE_BYTES == 8
    width<8>(arm);
#endif
#if HT_BAG_POLICY_VALUE_BYTES == 0 || HT_BAG_POLICY_VALUE_BYTES == 16
    width<16>(arm);
#endif
    static_assert(HT_BAG_POLICY_VALUE_BYTES==0||HT_BAG_POLICY_VALUE_BYTES==2||HT_BAG_POLICY_VALUE_BYTES==8||HT_BAG_POLICY_VALUE_BYTES==16);
}
