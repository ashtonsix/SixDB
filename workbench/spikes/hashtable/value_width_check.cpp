#include "promoted.h"
#include "publication.h"
#include <object/store_impl.h>
#include <map>
#ifndef HT_VALUE_BYTES
#define HT_VALUE_BYTES 2
#endif
#ifndef HT_WIDE_INLINE_KEY
#define HT_WIDE_INLINE_KEY 0
#endif
using namespace ht;
namespace {
constexpr unsigned W=HT_VALUE_BYTES;
using value=std::array<byte,W>;
value pattern(unsigned i) {value v;for(unsigned j=0;j<W;++j)v[j]=byte(mix(i+word(j)*719));return v;}
template<class Body> auto contents(const Body& body) {
    std::map<word,std::vector<value>> out;
    body.each_bytes([&](word k,auto bytes){value v;std::copy(bytes.begin(),bytes.end(),v.begin());out[k].push_back(v);});
    for(auto& [k,v]:out)std::sort(v.begin(),v.end());return out;
}
template<unsigned K,container_kind Kind,unsigned Budget,record_format Format=record_format::exact,
         class Bags=linked_bags<W,true>> void history() {
    pool_owner pool;using Small=small_table<K,Kind,W,true,Bags,Budget>;
    using Body=promoted_table<K,Kind,W,1,0,128,128,Format,false,record_access::both,true,Bags,Budget>;
    Small source(*pool.pool,19);std::map<word,std::vector<value>> expected;
    auto remember=[&](word k,const value& v){auto& a=expected[k];if constexpr(Kind==container_kind::map)a={v};else a.push_back(v);std::sort(a.begin(),a.end());};
    constexpr unsigned C=Small::bag_policy::capacity;
    // Equal values, high bytes and all-zero values survive every inline/spill boundary.
    for(unsigned i=0;i<33;++i){auto v=i%3?pattern(i):value{};source.set_bytes(0,v);remember(0,v);}
    std::array<word,4> keys{1,1,1,1};std::array<value,4> values{pattern(91),pattern(92),pattern(93),pattern(94)};
    require(source.template setN_bytes<4>(keys,values,13)==13,"byte packet active writes");
    for(unsigned i:{0u,2u,3u})remember(1,values[i]);
    std::array<std::vector<value>,4> packet;
    require(source.template getN_bytes<4>(keys,5,[&](unsigned lane,auto bytes){
        require(lane<4,"byte packet lane range");value v;std::copy(bytes.begin(),bytes.end(),v.begin());packet[lane].push_back(v);
    })==5,"byte packet found mask");
    for(unsigned lane=0;lane<4;++lane){std::sort(packet[lane].begin(),packet[lane].end());
        require(packet[lane]==(lane==0||lane==2?expected[1]:std::vector<value>{}),"byte packet exact per-lane multiset");}
    auto old=source.clone();require(old&&contents(*old)==expected,"wide physical clone");const auto retained=expected;
    auto rebuilt=source.clone_rebuild();require(rebuilt&&contents(*rebuilt)==expected,"wide logical small clone");
    auto body=Body::promote(source,*pool.pool,41,73);require(bool(body)&&!source.size(),"wide promotion owner transfer");
    for(word k=2;k<240;++k)for(unsigned j=0;j<1+k%5;++j){auto v=pattern(k+j);body->set_bytes(k,v);remember(k,v);}
    for(unsigned j=0;j<C+2;++j){auto v=pattern(j+300);body->set_bytes(239,v);remember(239,v);}
    std::array<small_event,0> empty;small_journal denied{empty};
    require(!body->try_set_bytes(0,pattern(400),denied)&&contents(*body)==expected,"byte mutation refusal has no visible effect");
    require(body->population()[0]&&body->population()[1]&&body->population()[2],"wide rows occupy every key tier");
    for(word k=0;k<240;k+=2){auto d=body->erase(k);require(d.keys==-1&&d.values==-std::int64_t(expected[k].size()),"wide erase cardinality");expected.erase(k);}
    for(word k=0;k<240;k+=2){auto v=pattern(k+700);body->set_bytes(k,v);remember(k,v);}
    body->validate();require(contents(*body)==expected,"wide all-tier read/write/reuse");
    for(word k=0;k<256;++k){std::vector<value> got;auto found=body->get_bytes(k,[&](auto bytes){value v;std::copy(bytes.begin(),bytes.end(),v.begin());got.push_back(v);});
        std::sort(got.begin(),got.end());require(found==expected.contains(k)&&(found?got==expected[k]:got.empty()),"wide point hit/miss");}
    auto clone=body->clone();require(clone&&contents(*clone)==expected,"wide hierarchy logical clone");
    body.reset();require(contents(*clone)==expected&&contents(*old)==retained,"wide retained ownership after retirement");
}
template<unsigned Budget> void refusal_and_publication() {
    using Small=small_table<40,container_kind::multimap,W,true,linked_bags<W,true>,Budget>;
    constexpr unsigned C=Small::bag_policy::capacity;pool_owner pool(4u<<20);Small body(*pool.pool,19);
    for(unsigned i=0;i<C;++i)body.set_bytes(0,pattern(i));const auto saved=contents(body);
    std::vector<oa::Allocation> pressure;
    for(std::size_t bytes=4096;bytes>=8;bytes/=2)while(auto a=oa::allocate(*pool.pool,{.bytes=bytes,.alignment=8}))pressure.push_back(std::move(*a));
    no_small_effects effects;require(!body.set_bytes(0,pattern(C),effects)&&contents(body)==saved,"atomic first spill or initial list allocation refusal");
    // With C=0 the empty list can refuse first. Independently force refusal at
    // the external owner itself, then retry its complete group reservation.
    linked_bags<W,true> empty_bag(*pool.pool);no_link_observer observer;
    auto refused=empty_bag.reserve_values(0,C+1,observer);require(!refused,"first external allocation refusal");
    pressure.clear();auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*pool.pool,0).kind()!=orbital::Progress::Kind::done){require(clock_type::now()<deadline,"wide pressure trim");orbital::worker::poll(pool.lane,1024);}
    auto admitted=empty_bag.reserve_values(0,C+1,observer);require(bool(admitted),"first external allocation retry");
    std::array<byte,(C+1)*W> group;for(unsigned i=0;i<C+1;++i){auto v=pattern(i);std::copy(v.begin(),v.end(),group.begin()+i*W);}
    const auto head=admitted->head_after();admitted->commit_bytes(group);unsigned count=0;
    empty_bag.each_bytes(head,[&](auto bytes){require(std::equal(bytes.begin(),bytes.end(),pattern(count++).begin()),"complete retried group bytes");});
    require(count==C+1,"complete retried group multiplicity");
    body.set_bytes(0,pattern(C));require(body.value_count()==C+1,"complete first spill retry exactly once");
    using Material=publication_material<Small>;using Store=orbital::ObjectStore<Material>;catalog_resource resource;
    {
        Store store(resource);constexpr orbital::ObjectId id{19,31};
        auto publish=[&](Small& pending,typename Store::Scope& basis){auto owner=store.capture({&pending},{});require(bool(owner),"wide capture");
            const std::array<typename Store::Change,1> changes{{{id,std::move(*owner)}}};typename Store::Prepared prepared(store);
            require(bool(store.prepare(basis,changes,prepared))&&bool(store.install(prepared)),"wide install");};
        {typename Store::Scope basis(store);publish(body,basis);}
        typename Store::Scope old(store); // Retain before binding: publication must preserve discovery as well as bytes.
        const auto original=contents(store.borrow().bind(id).value->body);
        {typename Store::Scope basis(store);auto copy=basis.bind(id).value->body.clone();require(bool(copy),"wide retained clone");
            for(unsigned i=0;i<257;++i)copy->set_bytes(0,pattern(i+200));copy->erase(0);copy->set_bytes(0,value{});publish(*copy,basis);}
        require(contents(old.bind(id).value->body)==original,"wide retained old scope after links grow, erase and reuse");
    }
    require(!resource.live,"wide publication catalog balance");
}
template<unsigned K> void key() {history<K,container_kind::multimap,8>();history<K,container_kind::multimap,16>();history<K,container_kind::map,8>();}

// Enroll readers while every bag is full inline, before any external storage
// exists. Discover both old versions only after spilling, erasure and reuse.
// This tests actual ObjectStore publication, not a borrowed old Body pointer.
template<unsigned K,class Bags,bool Hierarchy> void inline_publication() {
    using Small=small_table<K,container_kind::multimap,W,true,Bags,64>;
    using Large=promoted_table<K,container_kind::multimap,W,1,0,128,128,
        record_format::exact,false,record_access::both,true,Bags,64>;
    using Body=std::conditional_t<Hierarchy,Large,Small>;
    constexpr unsigned C=Small::bag_policy::capacity;
    pool_owner pool;Small source(*pool.pool,19);
    std::map<word,std::vector<value>> expected;
    for(word k=0;k<16;++k)for(unsigned i=0;i<C;++i) {
        auto v=pattern(k*101+i);source.set_bytes(k,v);expected[k].push_back(v);
    }
    for(auto& [k,v]:expected)std::sort(v.begin(),v.end());
    auto body=[&]() -> Body {
        if constexpr(Hierarchy) {
            auto p=Large::promote(source,*pool.pool,41,73);require(bool(p),"inline publication hierarchy");
            require(p->population()[0]&&p->population()[1]&&p->population()[2],"inline publication all tiers");
            return std::move(*p);
        }else return std::move(source);
    }();
    using Material=publication_material<Body>;using Store=orbital::ObjectStore<Material>;
    catalog_resource resource;
    {
        Store store(resource);constexpr orbital::ObjectId id{37,91};
        auto publish=[&](Body& pending,typename Store::Scope& basis) {
            auto owner=store.capture({&pending},{});require(bool(owner),"inline publication capture");
            const std::array<typename Store::Change,1> changes{{{id,std::move(*owner)}}};
            typename Store::Prepared prepared(store);
            require(bool(store.prepare(basis,changes,prepared))&&bool(store.install(prepared)),"inline publication install");
        };
        {typename Store::Scope basis(store);publish(body,basis);}
        typename Store::Scope inline_reader(store);
        const auto inline_expected=expected;
        {typename Store::Scope basis(store);auto copy=basis.bind(id).value->body.clone();require(bool(copy),"inline version clone");
            for(word k=0;k<16;++k){auto v=pattern(k+900);copy->set_bytes(k,v);expected[k].push_back(v);std::sort(expected[k].begin(),expected[k].end());}
            require(contents(*copy)==expected,"all inline-full bags spilled exactly once");publish(*copy,basis);}
        typename Store::Scope spilled_reader(store);
        const auto spilled_expected=expected;
        {typename Store::Scope basis(store);auto copy=basis.bind(id).value->body.clone();require(bool(copy),"spilled version clone");
            for(word k=0;k<16;++k){copy->erase(k);copy->set_bytes(k,value{});expected[k]={value{}};}
            require(contents(*copy)==expected,"spilled version erase and reuse");publish(*copy,basis);}
        require(contents(inline_reader.bind(id).value->body)==inline_expected,"late reader discovers inline version after spill");
        require(contents(spilled_reader.bind(id).value->body)==spilled_expected,"late reader discovers spilled version after reuse");
        require(contents(store.borrow().bind(id).value->body)==expected,"current reused version");
    }
    require(!resource.live&&!Material::live,"inline publication complete retirement");
}

[[maybe_unused]] void overflow_row_boundary() {
    using Rows=links<64,false,65,true>;
    pool_owner pool;Rows rows(*pool.pool);no_link_observer observer;
    std::array<std::uint32_t,2> heads{};
    std::array<byte,65> payload{};
    // Two adjacent assigned slots make the last row's immediate successor a
    // live link header. Check complete serialized bytes with independent LE
    // encoding rather than decoding through the writer's own transfer helper.
    auto append=[&](std::uint32_t& head,word key,unsigned marker) {
        for(unsigned i=0;i<payload.size();++i)payload[i]=byte(marker+17*i);
        auto pending=rows.reserve_record(head,observer);require(bool(pending),"65-byte row reserve");
        const auto ref=pending->insertion_after();const unsigned slot=rows.count(ref);
        std::vector<byte> before(rows.payload_window(ref).begin(),rows.payload_window(ref).end());
        std::vector<byte> expected=before;const unsigned at=6+slot*73;
        expected[4]=byte(slot+1);expected[5]=byte((slot+1)>>8);
        for(unsigned i=0;i<8;++i)expected[at+i]=byte(key>>(8*i));
        std::copy(payload.begin(),payload.end(),expected.begin()+at+8);
        pending->commit_record(key,payload);head=pending->head_after();
        require(std::equal(expected.begin(),expected.end(),rows.payload_window(ref).begin()),"exact 73-byte entry and untouched reservation bytes");
    };
    append(heads[0],0,11);append(heads[1],~word(0),29);
    require((heads[0]>>8)==(heads[1]>>8)&&heads[1]==heads[0]+1&&
        rows.payload_window(heads[0]).data()+rows.payload_window(heads[0]).size()==rows.data(heads[1]),
        "wide row boundary has adjacent live StreamPack links");
    const std::vector<byte> neighbor(rows.payload_window(heads[1]).begin(),rows.payload_window(heads[1]).end());
    for(unsigned i=1;i<4;++i)append(heads[0],i,11+i);
    auto at=rows.find(heads[0],3);require(bool(at)&&at.slot==3,"last 65-byte overflow row");
    rows.payload(at)[64]=byte(1); // Inline -> external discriminator replacement.
    require(std::equal(neighbor.begin(),neighbor.end(),rows.payload_window(heads[1]).begin()),"last tag cannot touch next link");
    auto pending=rows.reserve_record(heads[0],observer);require(bool(pending),"wide extension reserve");
    const auto reusable=pending->insertion_after();pending.reset();
    require(!rows.next(heads[0])&&rows.count(heads[0])==4,"cancel leaves full old link intact");
    pending=rows.reserve_record(heads[0],observer);
    require(pending&&pending->insertion_after()==reusable,"cancelled wide link reused");
    payload.fill(0xf3);pending->commit_record(4,payload);
    require(rows.next(heads[0])==reusable,"wide extension published once");
    rows.erase(heads[0],rows.find(heads[0],0));
    at=rows.find(heads[0],3);require(bool(at)&&at.slot==0&&rows.payload(at)[64]==1,"swap deletion preserves final tag byte");
    require(std::equal(neighbor.begin(),neighbor.end(),rows.payload_window(heads[1]).begin()),"wide cancellation/reuse leaves adjacent link intact");
    rows.validate();
}
}
int main() {
#if HT_WIDE_INLINE_KEY
    history<HT_WIDE_INLINE_KEY,container_kind::multimap,64>();
    history<HT_WIDE_INLINE_KEY,container_kind::multimap,64,record_format::tag_bits>();
    history<HT_WIDE_INLINE_KEY,container_kind::multimap,64,record_format::tag_plane,buffer_bags<W>>();
    refusal_and_publication<64>();
    inline_publication<HT_WIDE_INLINE_KEY,linked_bags<W,true>,false>();
    inline_publication<HT_WIDE_INLINE_KEY,linked_bags<W,true>,true>();
    inline_publication<HT_WIDE_INLINE_KEY,buffer_bags<W>,true>();
#if HT_VALUE_BYTES == 64 && HT_WIDE_INLINE_KEY == 64
    overflow_row_boundary();
#endif
#else
    key<40>();
#if HT_VALUE_BYTES == 2
    key<8>();key<16>();key<24>();key<32>();key<48>();key<56>();key<64>();
#endif
    history<40,container_kind::multimap,16,record_format::tag_bits>();
    history<40,container_kind::multimap,8,record_format::tag_plane,buffer_bags<W>>();
    refusal_and_publication<8>();refusal_and_publication<16>();
#endif
    std::printf("value_bytes=%u: byte packets, all key tiers, inline/spill boundaries, wide mutation/clone, refusal/retry and retained publication passed\n",W);
}
