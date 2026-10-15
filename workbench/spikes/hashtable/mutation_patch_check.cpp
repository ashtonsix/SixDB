#include "promoted.h"
#include "publication.h"
#include <allocation/internal.h>
#include <object/store_impl.h>
#include <map>

using namespace ht;
namespace {
constexpr unsigned K=40,A=1,B=0,Unit=128,Keys=96;
constexpr word PrimarySeed=41,SecondarySeed=73;
using event_kind=small_event::kind;

bool coarse(event_kind k) {
    return k==event_kind::primary_write || k==event_kind::secondary_write ||
        k==event_kind::overflow_write;
}
bool same_region(const small_event& a,const small_event& b) {
    return a.type==b.type && a.owner==b.owner && a.storage==b.storage &&
        a.offset==b.offset && a.size==b.size && a.block==b.block && a.family==b.family;
}

// Test-only resolution of an already live owner-qualified identity. No fake
// Allocation/Hold is constructed, and this neither retains nor mutates storage.
// The private owner remains live and is never polled, reentered or relocated
// while these images are used. This is not a proposed publication adapter API.
const byte* address(const small_event& e) {
    require(coarse(e.type) && e.storage && e.size,"coarse event has live storage");
    const auto& storage=*static_cast<const oa::Storage*>(e.storage);
    const auto& route=*oa::detail::route(storage);
    const std::byte* base;std::size_t capacity;
    if(route.finite) {
        const auto& geometry=*static_cast<const oa::detail::Group*>(route.context)->geometry;
        base=geometry.resolve({storage.identity()});capacity=geometry.bound_capacity({storage.identity()});
    }else {
        const auto& window=*static_cast<const oa::detail::Window*>(route.context);
        base=window.address;capacity=window.prepared;
    }
    require(base && e.offset<=capacity && e.size<=capacity-e.offset,"coarse event lies in live backing");
    return reinterpret_cast<const byte*>(base)+e.offset;
}
word little(const byte* p,unsigned bytes) {
    require(bytes<=8,"independent integer decoder width");word v=0;
    for(unsigned i=0;i<bytes;++i)v|=word(p[i])<<(8*i);return v;
}
struct region {small_event event;const byte* data;std::vector<byte> image;};
struct journal {
    std::size_t allowance=256,used=0;
    std::array<small_event,2> events{};
    const region* expected=nullptr;
    const std::vector<byte>* after=nullptr;
    std::size_t remaining()const {return allowance-used;}
    void before(const small_event& e)noexcept {
        require(used<events.size(),"one coarse event and one caller delta only");
        if(!used) {
            require(coarse(e.type),"first event is bucket/block damage");
            if(expected) {
                require(same_region(e,expected->event),"damage identifies the existing row's region");
                require(std::equal(expected->image.begin(),expected->image.end(),address(e)),
                    "before-image event precedes every payload and tag store");
            }
        }else {
            require(e.type==event_kind::caller_delta && !e.storage && !e.offset && !e.size,
                "second event is caller delta without an invented byte window");
            if(after)require(std::equal(after->begin(),after->end(),expected->data),
                "complete infallible patch precedes caller delta");
        }
        events[used++]=e;
    }
};

template<unsigned W> using value=std::array<byte,W>;
template<unsigned W> using oracle=std::map<word,std::vector<value<W>>>;
template<unsigned W> value<W> pattern(word key,unsigned round) {
    value<W> v{};
    for(unsigned i=0;i<W;++i)v[i]=byte(key*37+round*71+i*19+(key>>(i%5)));
    return v;
}
template<unsigned W,class Body> oracle<W> contents(const Body& body) {
    oracle<W> out;
    body.each_bytes([&](word key,auto bytes) {
        require(bytes.size()==W,"whole value returned");value<W> v;
        std::copy(bytes.begin(),bytes.end(),v.begin());out[key].push_back(v);
    });
    for(auto& [key,values]:out)std::sort(values.begin(),values.end());return out;
}
template<unsigned W> oracle<W> sorted(oracle<W> expected) {
    for(auto& [key,values]:expected)std::sort(values.begin(),values.end());return expected;
}
template<container_kind Kind,unsigned W,unsigned Budget=8> struct types {
    using Bags=linked_bags<W,true>;
    using Small=small_table<K,Kind,W,true,Bags,Budget>;
    using Body=promoted_table<K,Kind,W,A,B,Unit,Unit,record_format::tag_bits,
        false,record_access::both,true,Bags,Budget>;
    using Index=record_table<K,Kind,Small::stored_bytes,A,B,Unit,Unit,
        record_format::tag_bits,false,record_access::both,Small::tag_bits>;
};
template<container_kind Kind,unsigned W,unsigned Budget=8>
auto make_body(oa::Pool& pool,oracle<W>& expected) {
    using T=types<Kind,W,Budget>;
    typename T::Small source(pool,19);
    auto body=T::Body::promote(source,pool,PrimarySeed,SecondarySeed);
    require(bool(body),"empty promoted owner allocation");
    for(word key=0;key<Keys;++key) {
        auto v=pattern<W>(key,0);body->set_bytes(key,v);expected[key]={v};
    }
    const auto population=body->population();
    require(population[0] && population[1] && population[2],"fixture occupies primary, secondary and key overflow");
    require(contents<W>(*body)==sorted<W>(expected),"initial contents match independent insertion history");
    return std::move(*body);
}

struct row_location {std::size_t region,payload,tag=0;unsigned tag_shift=0,tag_bits=0;};
template<container_kind Kind,unsigned W,unsigned Budget=8> class audit {
    using T=types<Kind,W,Budget>;
    static constexpr bool MM=Kind==container_kind::multimap;
    static constexpr unsigned Stored=T::Small::stored_bytes;
    typename T::Body& body_;
    oracle<W>& expected_;
    std::vector<region> regions_;
    std::map<word,row_location> rows_;
    unsigned shared_tag_pairs_=0;

    void remember(const small_event& e) {
        require(coarse(e.type),"remember initialized coarse window");
        const auto* p=address(e);
        for(auto& r:regions_)if(same_region(r.event,e)) {
            require(r.data==p,"existing-key mutation cannot relocate storage");
            r.image.assign(p,p+e.size);return;
        }
        regions_.push_back({e,p,{p,p+e.size}});
    }
    void observe_result(const journal& j,const mutation_delta& d)const {
        require(j.used==2 && d.keys==0 && d.values==(MM?1:0),"eligible operation returns exact cardinality change");
        require(j.events[1].owner==&body_ && j.events[1].delta.keys==d.keys &&
            j.events[1].delta.values==d.values,"one caller-owned cardinality publication");
        if(j.events[0].type==event_kind::overflow_write)
            require(j.events[0].block && j.events[0].link_kind==link_event_kind::before_block_write,
                "key overflow carries block-qualified before-write event");
        else require(j.events[0].size==Unit && !j.events[0].block,
            "primary/secondary publishes exactly one complete bucket");
    }
    void add_row(word key,row_location at) {
        require(expected_.contains(key) && !rows_.contains(key),"physical keys independently cover the logical oracle once");
        const auto& r=regions_[at.region];
        require(at.payload<=r.image.size() && Stored<=r.image.size()-at.payload,"physical payload bounds");
        if constexpr(MM) {
            require(at.tag<r.image.size(),"physical tag bounds");
            const auto tag=(r.image[at.tag]>>at.tag_shift)&mask(at.tag_bits);
            require(tag==expected_.at(key).size() && tag>=2,"discovery leaves an inline two-value record");
        }
        const auto& values=expected_.at(key);
        for(unsigned i=0;i<values.size();++i)
            require(std::equal(values[i].begin(),values[i].end(),r.image.begin()+at.payload+i*W),
                "physical before-image values match independent history");
        rows_.emplace(key,at);
    }
    template<unsigned Bits> void bucket_rows(std::size_t index,const layout_solver::candidate& plan,word seed) {
        using id=layout_solver::field_id;
        const auto& r=regions_[index];const auto* p=r.image.data();
        require(r.event.offset%Unit==0 && r.image.size()==Unit && p[0]<=plan.shape.slots,"bucket count and extent");
        const auto fp=plan.get(id::fingerprints),suffix=plan.get(id::suffixes),payload=plan.get(id::values);
        const auto tags=plan.get(id::tag);permutation<K,Bits> hash(seed);
        for(unsigned rank=0;rank<p[0];++rank) {
            const auto f=(fp.offset_bits+rank*fp.stride_bits)/8;
            const auto s=(suffix.offset_bits+rank*suffix.stride_bits)/8;
            const auto key=hash.invert({r.event.offset/Unit,little(p+s,(suffix.logical_bits+7)/8),p[f]});
            row_location at{index,(payload.offset_bits+rank*payload.stride_bits)/8};
            if constexpr(MM) {
                const auto bit=tags.offset_bits+rank*tags.stride_bits;
                at.tag=bit/8;at.tag_shift=bit%8;at.tag_bits=tags.logical_bits;
                require(tags.width_bits<8 && tags.stride_bits==tags.width_bits,"exercise packed neighboring tags");
                if(rank && at.tag==(tags.offset_bits+(rank-1)*tags.stride_bits)/8)++shared_tag_pairs_;
            }
            add_row(key,at);
        }
    }
    void overflow_rows(std::size_t index) {
        const auto& r=regions_[index];const auto* p=r.image.data();
        constexpr unsigned Entry=K/8+Stored+MM;
        const auto capacity=4u<<r.event.family,width=6+capacity*Entry;
        require(r.image.size()>=2,"StreamPack control exists");
        const auto assigned=little(p,2);
        require(assigned<=16 && 2+assigned*width<=r.image.size(),"assigned fixed-slot StreamPack extent");
        for(unsigned slot=0;slot<assigned;++slot) {
            const auto base=2+slot*width;const auto n=little(p+base+4,2);
            require(n<=capacity,"key-link occupancy fits its family");
            for(unsigned i=0;i<n;++i) {
                const auto entry=base+6+i*Entry;
                row_location at{index,entry+K/8};
                if constexpr(MM){at.tag=at.payload+Stored;at.tag_bits=8;}
                add_row(little(p+entry,K/8),at);
            }
        }
    }
  public:
    // Discovery uses ordinary eligible operations, then snapshots their stable
    // initialized regions. It is not the before-image assertion. Every later
    // operation compares its callback with a snapshot taken before the call.
    audit(typename T::Body& body,oracle<W>& expected,unsigned discovery_round)
        :body_(body),expected_(expected) {
        if constexpr(MM)static_assert(T::Small::bag_policy::capacity>=4);
        for(auto& [key,values]:expected_) {
            const auto v=pattern<W>(key,discovery_round);journal j;
            auto changed=body_.try_set_bytes(key,v,j);require(bool(changed),"discovery operation admitted");
            observe_result(j,*changed);remember(j.events[0]);
            if constexpr(MM)values.push_back(v);else values={v};
        }
        require(contents<W>(body_)==sorted<W>(expected_),"discovery preserves complete logical contents");
        for(std::size_t i=0;i<regions_.size();++i) {
            switch(regions_[i].event.type) {
                case event_kind::primary_write:bucket_rows<A>(i,T::Index::primary,PrimarySeed);break;
                case event_kind::secondary_write:bucket_rows<B>(i,T::Index::secondary,SecondarySeed);break;
                case event_kind::overflow_write:overflow_rows(i);break;
                default:require(false,"unexpected discovery region");
            }
        }
        require(rows_.size()==Keys,"all logical keys covered by initialized region snapshots");
        if constexpr(MM)require(shared_tag_pairs_>=2,"neighbor preservation includes shared packed-tag bytes");
        unchanged();
    }
    void unchanged()const {
        for(const auto& r:regions_)
            require(std::equal(r.image.begin(),r.image.end(),r.data),"all other bytes and neighbor tags are unchanged");
    }
    void disjoint_from(const audit& other)const {
        for(const auto& r:regions_)for(const auto& s:other.regions_)
            require(r.event.storage!=s.event.storage,"private clone owns independent allocation identities");
    }
    void phase(unsigned round) {
        const auto population=body_.population();const auto keys=body_.size();
        const auto values_before=body_.value_count();unsigned changed_count=0;
        std::array<bool,3> refused_tiers{};
        for(const auto& [key,at]:rows_) {
            auto& r=regions_[at.region];const auto v=pattern<W>(key,round);
            const unsigned tier=r.event.type==event_kind::primary_write?0:r.event.type==event_kind::secondary_write?1:2;
            if(!refused_tiers[tier]) {
                for(std::size_t allowance:{0u,1u}) {
                    journal denied;denied.allowance=allowance;
                    const auto old_values=body_.value_count();
                    auto refused=body_.try_set_bytes(key,v,denied);
                    require(!refused && refused.error()==mutation_error::effects && denied.used==0,
                        "effects refusal publishes neither byte events nor caller delta");
                    require(body_.size()==keys && body_.value_count()==old_values,"refusal preserves caller cardinality");
                    unchanged();
                }
                refused_tiers[tier]=true;
            }
            auto after=r.image;const auto old_count=expected_.at(key).size();
            const auto offset=at.payload+(MM?old_count*W:0);
            require(offset<=after.size() && W<=after.size()-offset,"expected patch lies in snapshot");
            std::copy(v.begin(),v.end(),after.begin()+offset);
            if constexpr(MM) {
                require(old_count<T::Small::bag_policy::capacity,"this operation is an inline append");
                const auto bits=mask(at.tag_bits)<<at.tag_shift;
                after[at.tag]=byte((after[at.tag]&~bits)|((old_count+1)<<at.tag_shift));
            }
            journal j;j.expected=&r;j.after=&after;
            auto changed=body_.try_set_bytes(key,v,j);require(bool(changed),"eligible mutation succeeds");
            observe_result(j,*changed);r.image=std::move(after);unchanged();
            if constexpr(MM)expected_[key].push_back(v);else expected_[key]={v};
            ++changed_count;
        }
        require(std::all_of(refused_tiers.begin(),refused_tiers.end(),[](bool v){return v;}),"refusal exercised in all three tiers");
        require(body_.population()==population && body_.size()==keys &&
            body_.value_count()==values_before+(MM?changed_count:0),"eligible mutations preserve placement and publish exact aggregate change");
        require(contents<W>(body_)==sorted<W>(expected_),"complete logical contents match independent mutation history");
        body_.validate();
    }
};

template<container_kind Kind,unsigned W,unsigned Budget=8> void check() {
    pool_owner pool;oracle<W> expected;auto body=make_body<Kind,W,Budget>(*pool.pool,expected);
    audit<Kind,W,Budget> observed(body,expected,1);observed.phase(2);observed.phase(3);
}
void retained_promoted() {
    constexpr auto Kind=container_kind::map;constexpr unsigned W=2;
    using Body=types<Kind,W>::Body;using Material=publication_material<Body>;
    using Store=orbital::ObjectStore<Material>;
    pool_owner pool;catalog_resource resource;oracle<W> expected;
    auto body=make_body<Kind,W>(*pool.pool,expected);
    audit<Kind,W> original(body,expected,1);const auto old_expected=sorted<W>(expected);
    {
        Store store(resource);constexpr orbital::ObjectId id{67,109};
        auto publish=[&](Body& pending,Store::Scope& basis) {
            auto owner=store.capture({&pending},{});require(bool(owner),"capture private promoted body");
            const std::array<Store::Change,1> changes{{{id,std::move(*owner)}}};Store::Prepared prepared(store);
            require(bool(store.prepare(basis,changes,prepared)) && bool(store.install(prepared)),"publish promoted material");
        };
        {Store::Scope basis(store);publish(body,basis);}
        Store::Scope old(store); // Retain discovery before the first bind.
        {
            Store::Scope basis(store);auto copy=basis.bind(id).value->body.clone();
            require(bool(copy),"clone retained promoted body");auto next_expected=expected;
            audit<Kind,W> changed(*copy,next_expected,2);changed.disjoint_from(original);
            changed.phase(3);original.unchanged();publish(*copy,basis);
            require(contents<W>(store.borrow().bind(id).value->body)==sorted<W>(next_expected),"new version publishes patched values");
        }
        require(contents<W>(old.bind(id).value->body)==old_expected,"retained promoted version preserves old values");
        original.unchanged();
    }
    require(!resource.live,"retained promoted catalog storage is balanced");
    // ObjectStore owns whole materials here; it does not consume this test's
    // damage journal or establish a byte-journal MVCC implementation.
}
}
int main() {
    check<container_kind::map,2>();check<container_kind::map,17>();
    check<container_kind::multimap,2,8>();check<container_kind::multimap,3,16>();
    retained_promoted();
    std::puts("mutation patch: all-tier raw before-images, exact coarse/caller events, packed-tag neighbors, effects refusal and retained promoted material passed");
}
