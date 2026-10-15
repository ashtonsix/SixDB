#include "single-plans.h"
#include "table-plans.h"
#include "table.h"
#include "support.h"
#include <absl/container/flat_hash_map.h>
#include <absl/container/internal/hashtable_debug.h>
#include <absl/container/internal/hashtablez_sampler.h>
#include <orbital/object.h>
#include <object/store_impl.h>
#include <map>
#include <memory_resource>
#include <random>
#include <source_location>
#include <unordered_set>
#include <variant>

namespace growth {
using namespace hashtable_spike;
namespace t=hashtable_spike::tiered;
using orbital::Result;using orbital::Error;
void check(bool condition,std::source_location at=std::source_location::current()) {
    if(!condition){std::fprintf(stderr,"growth invariant: %s:%u\n",at.file_name(),at.line());std::abort();}
}
template<class T>T take(Result<T> result){check(bool(result));return std::move(*result);}
struct memory {
    std::size_t payload{},backing{},metadata{},preparation{};
    std::unordered_set<const void*> payloads,owners,controls;
    void add(const void* p,std::size_t requested,std::size_t capacity) {
        if(requested&&payloads.insert(p).second){payload+=requested;backing+=capacity;}
    }
    void add(const oa::Allocation& a){add(a.address,a.size,oa::capacity(a));}
    void owner(const void* p,std::size_t n){if(n&&owners.insert(p).second)metadata+=n;}
    void control(const void* p,std::size_t n){if(n&&controls.insert(p).second)preparation+=n;}
};
// Catalog/material metadata only. Payload remains an ordinary Orbital Allocation.
struct metadata_resource:std::pmr::memory_resource {
    std::size_t live{},peak{},calls{},fail_at=SIZE_MAX;
    void* do_allocate(std::size_t n,std::size_t alignment)override {
        if(calls++==fail_at)throw std::bad_alloc{};
        void* p=std::pmr::new_delete_resource()->allocate(n,alignment);live+=n;peak=std::max(live,peak);return p;
    }
    void do_deallocate(void* p,std::size_t n,std::size_t alignment)override {
        check(live>=n);live-=n;std::pmr::new_delete_resource()->deallocate(p,n,alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& r)const noexcept override{return this==&r;}
};
struct entropy {std::array<word,6> words;};
struct no_observation {template<class... T>void operator()(const T&...)const{}};
constexpr auto list_plan=single::k40v64_rows32;
template<bool Direct>struct list {
    std::shared_ptr<oa::Allocation> storage;
    inline static const single::binding<list_plan> binding{};
    auto view()const{return single::kernel<list_plan,Direct>{reinterpret_cast<byte*>(storage->address),binding};}
    Result<void> prepare(oa::Pool& pool) {
        if(storage&&storage.unique())return {};
        auto allocation=oa::allocate(pool,{.bytes=list_plan.bytes,.alignment=16});
        if(!allocation)return std::unexpected(allocation.error());
        if(storage)std::memcpy(allocation->address,storage->address,list_plan.bytes);
        try {
            const bool empty=!storage;storage=std::make_shared<oa::Allocation>(std::move(*allocation));
            if(empty)view().initialize();return {};
        }catch(const std::bad_alloc&){return std::unexpected(Error::capacity);}
    }
    [[gnu::always_inline]] bool get(word key,word& out)const{return storage&&view().get(key,out);}
    std::size_t size()const{return storage?view().size():0;}
    template<class F>void each(F&& f)const{if(storage)view().each(std::forward<F>(f));}
    Result<bool> set(oa::Pool& pool,word key,word value) {
        auto ready=prepare(pool);if(!ready)return std::unexpected(ready.error());
        auto changed=view().set(key,value);check(changed.result!=single::status::grow_required);return changed.distinct_delta==1;
    }
    Result<bool> erase(oa::Pool& pool,word key) {
        if(!storage||view().find(key)<0)return false;
        auto ready=prepare(pool);if(!ready)return std::unexpected(ready.error());
        return view().erase(key).distinct_delta==-1;
    }
    void account(memory& m)const {
        m.owner(this,sizeof(*this));m.control(&binding,sizeof(binding));
        if(storage){m.add(*storage);m.owner(storage.get(),sizeof(oa::Allocation));}
    }
};
template<unsigned N>struct geometry;
template<>struct geometry<16> {
    static constexpr auto p1=t::k40v64_a2_n16_primary_rows_byte,q1=t::k40v64_a0_n16_secondary_rows_byte;
    static constexpr auto p2=t::k40v64_a3_n16_primary_rows_byte,q2=q1;
};
template<>struct geometry<8> {
    static constexpr auto p1=t::k40v64_a3_n8_primary_rows_byte,q1=t::k40v64_a0_n8_secondary_rows_byte;
    static constexpr auto p2=t::k40v64_a4_n8_primary_rows_byte,q2=t::k40v64_a1_n8_secondary_rows_byte;
};
template<unsigned N,bool Direct=false>class packed {
    using G=geometry<N>;
    using One=t::table<G::p1,G::q1,t::sharing::flat,true,Direct>;
    using Two=t::table<G::p2,G::q2,t::sharing::flat,true,Direct>;
    std::variant<list<Direct>,One,Two> body_;
    oa::Pool* pool_;
    entropy seeds_;
    template<class Target,class Observe>Result<bool> grow(word key,word value,unsigned stage,Observe&& observe) {
        auto made=Target::make(*pool_,seeds_.words[2*stage],seeds_.words[2*stage+1]);
        if(!made)return std::unexpected(made.error());
        Result<void> status;
        each([&](word k,word v) {
            if(!status)return;auto r=made->set(k,v);
            if(!r)status=std::unexpected(r.error());else check(r->distinct_delta==1);
        });
        if(!status)return std::unexpected(status.error());
        const auto added=made->set(key,value);if(!added)return std::unexpected(added.error());
        check(added->distinct_delta==1);
        // Untimed accounting can observe real old/new coexistence before swap.
        observe(*this,*made);body_.template emplace<Target>(std::move(*made));return true;
    }
  public:
    packed(oa::Pool& pool,entropy seeds):pool_(&pool),seeds_(seeds){}
    template<class F>[[gnu::always_inline]] decltype(auto) with_read(F&& f)const {
        // Keep the selected kernel fusible into a point consumer. std::visit's
        // invoke wrapper otherwise survived as a hot call in the measured build.
        switch(body_.index()) {
        case 0:return std::forward<F>(f)(std::get<0>(body_));
        case 1:return std::forward<F>(f)(std::get<1>(body_));
        case 2:return std::forward<F>(f)(std::get<2>(body_));
        default:check(false);std::unreachable();
        }
    }
    [[gnu::always_inline]] bool get(word key,word& out)const {
        return with_read([&] [[gnu::always_inline]] (const auto& body){return body.get(key,out);});
    }
    std::size_t size()const{return with_read([](const auto& body){return body.size();});}
    template<class F>void each(F&& f)const{with_read([&](const auto& body){body.each(f);});}
    template<class Observe=no_observation>Result<bool> set(word key,word value,Observe observe={}) {
        const unsigned stage=unsigned(body_.index());
        const std::size_t limit=stage==0?32:stage==1?51:102;
        if(size()>=limit) {
            word existing=0;
            if(!get(key,existing)) {
                if(stage==0)return grow<One>(key,value,1,observe);
                if(stage==1)return grow<Two>(key,value,2,observe);
                return std::unexpected(Error::capacity); // This experiment has two transitions.
            }
        }
        return std::visit([&](auto& body)->Result<bool> {
            using B=std::decay_t<decltype(body)>;
            if constexpr(std::is_same_v<B,list<Direct>>)return body.set(*pool_,key,value);
            else {auto r=body.set(key,value);if(!r)return std::unexpected(r.error());check(r->result!=t::outcome::grow_required);return r->distinct_delta==1;}
        },body_);
    }
    Result<bool> erase(word key) {
        return std::visit([&](auto& body)->Result<bool> {
            using B=std::decay_t<decltype(body)>;
            if constexpr(std::is_same_v<B,list<Direct>>)return body.erase(*pool_,key);
            else {auto r=body.erase(key);if(!r)return std::unexpected(r.error());return r->distinct_delta==-1;}
        },body_);
    }
    void account(memory& m)const {
        m.owner(this,sizeof(*this));
        // Charge all initialized shared bindings in this two-transition owner,
        // including representations which are currently inactive.
        auto control=[&](const void* p,std::size_t n){m.control(p,n);};
        One::preparation(control);Two::preparation(control);
        m.control(&list<Direct>::binding,sizeof(list<Direct>::binding));
        with_read([&](const auto& body) {
            if constexpr(requires{body.account(m);})body.account(m);
            else account_table(body,m);
        });
    }
    template<class T>static void account_table(const T& table,memory& m) {
        table.allocations([&](const auto& a){m.add(a);});
        table.metadata([&](const void* p,std::size_t n){m.owner(p,n);});
        table.preparation([&](const void* p,std::size_t n){m.control(p,n);});
    }
};
struct abseil {
    absl::flat_hash_map<word,word> body;
    abseil(oa::Pool&,entropy){} // No reserve or advance knowledge of final size.
    [[gnu::always_inline]] bool get(word key,word& out)const {
        const auto i=body.find(key);if(i==body.end())return false;out=i->second;return true;
    }
    template<class F>[[gnu::always_inline]] decltype(auto) with_read(F&& f)const{return f(*this);}
    std::size_t size()const{return body.size();}
    template<class F>void each(F&& f)const{for(const auto& [k,v]:body)f(k,v);}
    template<class Observe=no_observation>Result<bool> set(word key,word value,Observe={}) {
        try{return body.insert_or_assign(key,value).second;}catch(const std::bad_alloc&){return std::unexpected(Error::capacity);}
    }
    Result<bool> erase(word key){return body.erase(key)!=0;}
    void account(memory& m)const {
        // Each ordinary Abseil copy owns distinct storage. Backing is unknown.
        m.add(this,absl::container_internal::AllocatedByteSize(body),0);m.owner(this,sizeof(*this));
    }
};

// A publication-only material. Private construction is completed before capture;
// admission allocates the owner record before consuming that private body.
template<class Body,bool Observe>struct physical {
    struct Value {std::pmr::memory_resource* resource;std::size_t references;Body body;Value* next;};
    inline static Value* live{};
    struct Selection {Value* value{};explicit operator bool()const{return value;}};
    struct Specification {Body* pending;};
    using PartSource=std::byte;enum class ExportPolicy {owned};
    struct Footprint {std::size_t requested_bytes,copied_metadata;};
    static Value* get(Selection s)noexcept{return s.value;}
    static Selection selection(Value* p)noexcept{return {p};}
    static void retain(Value* p)noexcept{if(p)++p->references;}
    static void release(Value* p)noexcept {
        if(!p||--p->references)return;
        if constexpr(Observe){auto** at=&live;while(*at!=p)at=&(*at)->next;*at=p->next;}
        auto* resource=p->resource;std::destroy_at(p);resource->deallocate(p,sizeof(Value),alignof(Value));
    }
    static Result<Value*> capture(std::pmr::memory_resource& r,const Specification& spec,std::span<const PartSource> parts)noexcept {
        if(!spec.pending||!parts.empty())return std::unexpected(Error::invalid_argument);
        try {
            static_assert(std::is_nothrow_move_constructible_v<Body>);
            auto* p=static_cast<Value*>(r.allocate(sizeof(Value),alignof(Value)));
            std::construct_at(p,Value{&r,1,std::move(*spec.pending),live});
            if constexpr(Observe)live=p;return p;
        }catch(const std::bad_alloc&){return std::unexpected(Error::capacity);}
    }
    static bool accepts(Value*,orbital::ObjectId,std::uint64_t)noexcept{return true;}
    static bool accepts_resource(Value* p,std::pmr::memory_resource& r)noexcept{return p->resource==&r;}
    static bool compatible(Value*,Value*)noexcept{return false;}
    static Footprint footprint(Value*)noexcept{return {sizeof(Value),0};}
    struct Export {
        Value* value;orbital::ObjectBinding binding;
        Export(Value* p,orbital::ObjectBinding b):value(p),binding(b){retain(p);}
        Export(Export&& e)noexcept:value(std::exchange(e.value,nullptr)),binding(e.binding){}
        ~Export(){release(value);}
    };
    static Result<Export> export_part(Selection s,std::size_t i,orbital::ObjectBinding b,ExportPolicy)noexcept {
        if(!s||i)return std::unexpected(Error::absent);return Export(s.value,b);
    }
    static memory account() {
        memory m;for(auto* p=live;p;p=p->next){p->body.account(m);m.metadata-=sizeof(Body);}return m;
    }
};
template<class Body,bool Observe>using Store=orbital::ObjectStore<physical<Body,Observe>>;
constexpr orbital::ObjectId id{91,17};
word key(unsigned i,unsigned generation) {
    static const permutation<40,false> permutation(8173);return permutation(i+257*generation);
}
template<class Body>void verify(const Body& body,unsigned n,unsigned generation) {
    check(body.size()==n);std::map<word,word> expected,actual;
    for(unsigned i=0;i<n;++i)expected.emplace(key(i,generation),key(i?i-1:n-1,generation));
    body.each([&](word k,word v){check(actual.emplace(k,v).second);});check(actual==expected);
    for(const auto& [k,v]:expected){word out=~word{};check(body.get(k,out)&&out==v);}
    word untouched=9173;check(!body.get(key(199,generation+1),untouched)&&untouched==9173);
}
struct answer {word sum{},last{};friend bool operator==(answer,answer)=default;};
template<class View>[[gnu::noinline]] answer dependent(const View& view,word first,unsigned n,unsigned rounds) {
    word at=first,sum=0;
    for(std::size_t i=0;i<std::size_t(n)*rounds;++i) {
        word value=0;check(view.get(at,value));at=value;sum+=value;asm volatile("" : "+r"(at));
    }
    return {sum,at};
}
struct sample {const char* phase;double ns;std::size_t operations;word checksum;memory bytes;};
template<class Body,bool Observe>std::vector<sample> trial(oa::Pool& pool,const std::array<entropy,2>& seeds,bool faults=false) {
    using Catalog=Store<Body,Observe>;using Material=physical<Body,Observe>;
    metadata_resource resource;std::vector<sample> result;
    {
        const auto creation_begin=clock_type::now();
        std::optional<Catalog> holder(std::in_place,resource);auto& store=*holder;
        const double creation_ns=std::chrono::duration<double,std::nano>(clock_type::now()-creation_begin).count();
        auto publish=[&](Body&& pending,typename Catalog::Scope& basis) {
            auto owner=take(store.capture({&pending},{}));
            const std::array<typename Catalog::Change,1> changes{{{id,std::move(owner)}}};
            typename Catalog::Prepared prepared(store);check(bool(store.prepare(basis,changes,prepared)));check(bool(store.install(prepared)));
        };
        auto observed=[&] {memory m;if constexpr(Observe){m=Material::account();m.metadata+=resource.live;}return m;};
        result.push_back({"create_catalog",creation_ns,1,1,observed()});
        auto phase=[&](const char* name,std::size_t operations,auto&& f) {
            const auto begin=clock_type::now();const word checksum=f();asm volatile(""::"r"(checksum):"memory");
            const double elapsed=std::chrono::duration<double,std::nano>(clock_type::now()-begin).count();
            result.push_back({name,elapsed,operations,checksum,observed()});
        };
        auto insert=[&](Body& body,unsigned first,unsigned end,unsigned generation) {
            auto observer=[&](const auto& old,const auto& next) {
                if constexpr(Observe&&!std::is_same_v<Body,abseil>) {
                    auto m=observed();old.account(m);Body::account_table(next,m);
                    result.push_back({"migration_overlap",0,0,word(next.size()),std::move(m)});
                }
            };
            for(unsigned i=first;i<end;++i)check(take(body.set(key(i,generation),key(i?i-1:0,generation),observer)));
            check(!take(body.set(key(0,generation),key(end-1,generation))));
        };
        auto current=[&]()->const Body&{return store.borrow().bind(id).value->body;};
        phase("initial_build_publish",31,[&] {
            Body body(pool,seeds[0]);insert(body,0,31,0);typename Catalog::Scope basis(store);publish(std::move(body),basis);return word{31};
        });
        verify(current(),31,0);
        {
            typename Catalog::Scope original(store);
            auto read=[&](const char* point,const char* cohort,unsigned n,unsigned generation) {
                const auto& body=current();constexpr unsigned rounds=64;word expected_sum=0;
                for(unsigned i=0;i<n;++i)expected_sum+=key(i,generation);expected_sum*=rounds;
                const answer expected{expected_sum,key(0,generation)};
                phase(point,n*rounds,[&]{const auto a=dependent(body,key(0,generation),n,rounds);check(a==expected);return a.sum;});
                phase(cohort,n*rounds,[&]{const auto a=body.with_read([&](const auto& view){return dependent(view,key(0,generation),n,rounds);});check(a==expected);return a.sum;});
            };
            read("list_point","list_cohort",31,0);
            phase("first_growth_publish",19,[&] {
                typename Catalog::Scope basis(store);Body body=basis.bind(id).value->body;
                insert(body,31,50,0);publish(std::move(body),basis);return word{19};
            });
            verify(current(),50,0);read("middle_point","middle_cohort",50,0);
            phase("second_growth_publish",47,[&] {
                typename Catalog::Scope basis(store);Body body=basis.bind(id).value->body;
                insert(body,50,97,0);publish(std::move(body),basis);return word{47};
            });
            verify(current(),97,0);read("final_point","final_cohort",97,0);
            phase("turnover_publish",97,[&] {
                typename Catalog::Scope basis(store);Body body=basis.bind(id).value->body;
                for(unsigned i=0;i<97;++i){check(take(body.erase(key(i,0))));check(take(body.set(key(i,1),key(i?i-1:0,1))));}
                check(!take(body.set(key(0,1),key(96,1))));publish(std::move(body),basis);return word{194};
            });
            verify(current(),97,1);read("aged_point","aged_cohort",97,1);
            // Clearing a published object can capture a new empty realization;
            // neither contender needlessly copies the old contents first.
            phase("clear_release_publish",1,[&] {
                typename Catalog::Scope basis(store);Body empty(pool,seeds[1]);publish(std::move(empty),basis);return word{97};
            });
            verify(current(),0,1);
            phase("refill_publish",97,[&] {
                typename Catalog::Scope basis(store);Body body=basis.bind(id).value->body;
                insert(body,0,97,2);publish(std::move(body),basis);return word{97};
            });
            verify(current(),97,2);verify(original.bind(id).value->body,31,0);
            if(faults) {
                typename Catalog::Scope basis(store);const auto source=basis.bind(id);
                auto unchanged=[&] {
                    typename Catalog::Scope now(store);
                    check(now.cut()==basis.cut()&&now.bind(id).value==source.value);
                    verify(current(),97,2);verify(original.bind(id).value->body,31,0);
                };
                Body pending=source.value->body;
                const word altered=key(96,2)^(word{1}<<61);
                check(!take(pending.set(key(0,2),altered)));resource.fail_at=resource.calls;
                auto failure=store.capture({&pending},{});resource.fail_at=SIZE_MAX;check(!failure&&failure.error()==Error::capacity);
                word value=0;check(pending.get(key(0,2),value)&&value==altered);unchanged();
                auto owner=take(store.capture({&pending},{}));
                const std::array<typename Catalog::Change,1> changes{{{id,std::move(owner)}}};
                typename Catalog::Prepared prepared(store);resource.fail_at=resource.calls;
                auto refused=store.prepare(basis,changes,prepared);resource.fail_at=SIZE_MAX;
                check(!refused&&refused.error()==Error::capacity);unchanged();
                check(bool(store.prepare(basis,changes,prepared)));prepared.abort();
                unchanged();
            }
        }
        phase("reclaim_history",1,[&]{store.drain();return word{1};});verify(current(),97,2);
        if constexpr(Observe)check(Material::live==store.borrow().bind(id).value&&Material::live->next==nullptr);
        phase("destroy_current",1,[&]{holder.reset();return word{97};});
    }
    check(resource.live==0&&Material::live==nullptr);return result;
}
template<class Body>[[gnu::noinline]] void run(unsigned trials,unsigned rep,const char* name,bool output,bool checking=false) {
    pool_owner pool;std::mt19937_64 random(17291+rep);std::vector<sample> total;
    for(unsigned i=0;i<trials;++i) {
        std::array<entropy,2> seeds;for(auto& bundle:seeds)for(auto& seed:bundle.words)seed=random();
        auto rows=trial<Body,false>(*pool.pool,seeds,checking);
        if(total.empty())total=std::move(rows);
        else {check(total.size()==rows.size());for(unsigned j=0;j<rows.size();++j){check(std::string_view(total[j].phase)==rows[j].phase);total[j].ns+=rows[j].ns;total[j].operations+=rows[j].operations;total[j].checksum+=rows[j].checksum;}}
    }
    std::array<entropy,2> seeds;std::mt19937_64 accounting_random(17291+rep);
    for(auto& bundle:seeds)for(auto& seed:bundle.words)seed=accounting_random();
    const auto accounting=trial<Body,true>(*pool.pool,seeds,checking);
    if(output) {
        for(const auto& row:total) {
            const auto it=std::find_if(accounting.begin(),accounting.end(),[&](const auto& r){return std::string_view(r.phase)==row.phase;});check(it!=accounting.end());const auto& m=it->bytes;
            std::printf("%s,%u,%u,%s,%zu,%.6f,%llu,%zu,%zu,%zu,%zu\n",name,rep,trials,row.phase,row.operations,row.ns/row.operations,(unsigned long long)row.checksum,m.payload,m.backing,m.metadata,m.preparation);
        }
        for(const auto& row:accounting)if(std::string_view(row.phase)=="migration_overlap") {
            const auto& m=row.bytes;std::printf("%s,%u,0,migration_overlap,0,0,%llu,%zu,%zu,%zu,%zu\n",name,rep,(unsigned long long)row.checksum,m.payload,m.backing,m.metadata,m.preparation);
        }
    }
}
void check_zero_address() {
    auto test=[]<unsigned K>() {
        for(word seed:{word{0},word{1},~word{0},word{371}}) {
            t::quotient_hash<K,0> h(seed);t::quotient_hash<K,0,false> raw(seed);
            constexpr word count=[] {if constexpr(K<=16)return word{1}<<K;else return word{4096};}();
            for(word i=0;i<count;++i) {
                const word k=K<=16?i:mix(i)&mask(K);check(h.invert(h(k))==k&&raw.invert(raw(k))==k);
            }
        }
    };
    test.template operator()<8>();test.template operator()<16>();test.template operator()<40>();test.template operator()<64>();
    // A0 at K64 must reconstruct a complete secondary key without a shift by
    // 64. Force that bucket and real StreamPack links, then edit a shared copy.
    using Full=t::table<t::k64v64_a3_n8_primary_rows_byte,t::k64v64_a0_n8_secondary_rows_byte,t::sharing::flat,true>;
    pool_owner pool;auto body=take(Full::make(*pool.pool,~word{0},371));
    std::map<word,word> expected;
    for(word i=0;i<200;++i){const word k=mix(i),v=mix(i+7919);expected.emplace(k,v);check(take(body.set(k,v)).distinct_delta==1);}
    auto verify_full=[&](const auto& table,const auto& oracle) {
        std::map<word,word> actual;table.each([&](word k,word v){check(actual.emplace(k,v).second);});check(actual==oracle);
        for(const auto& [k,v]:oracle){word out=0;check(table.get(k,out)&&out==v);}table.audit();
    };
    check(body.counts().secondary&&body.counts().chain);verify_full(body,expected);
    const auto original=body;auto changed=expected;
    for(word i=0;i<200;i+=3){check(take(body.erase(mix(i))).distinct_delta==-1);changed.erase(mix(i));}
    verify_full(body,changed);verify_full(original,expected);
}
void check_allocation_refusal() {
    // Retained successful targets exhaust an actual, fixed Orbital Arena budget.
    // The next geometry migration must preserve both its source and its private
    // candidate on refusal; no substitute allocation/fault layer is involved.
    for(unsigned count:{32u,51u}) {
        pool_owner pool(1u<<20);const entropy seeds{{11,37,97,181,337,571}};
        packed<16> source(*pool.pool,seeds);
        for(unsigned i=0;i<count;++i)check(take(source.set(key(i,0),key(i?i-1:count-1,0))));
        verify(source,count,0);std::vector<packed<16>> retained;bool refused=false;
        for(unsigned attempt=0;attempt<4096;++attempt) {
            auto candidate=source;const auto added=candidate.set(key(count,0),key(count-1,0));
            if(!added) {
                check(added.error()==Error::capacity);verify(candidate,count,0);verify(source,count,0);refused=true;break;
            }
            check(*added);check(!take(candidate.set(key(0,0),key(count,0))));verify(candidate,count+1,0);
            retained.push_back(std::move(candidate));
        }
        check(refused&&!retained.empty());verify(retained.front(),count+1,0);
    }
}
} // namespace growth
int main(int argc,char** argv) {
    using namespace growth;absl::container_internal::SetHashtablezEnabled(false);
    const bool checking=argc==2&&std::string_view(argv[1])=="check";
    if(checking) {
        check_zero_address();check_allocation_refusal();run<abseil>(2,0,"abseil",false,true);run<packed<8>>(2,0,"n8",false,true);
        run<packed<16>>(2,0,"n16",false,true);run<packed<16,true>>(2,0,"n16_direct",false,true);
        std::puts("growth: A0 inverse and K64 secondary/chains, two geometry transitions, answer-dependent reads, real publication, retained late readers, churn, reset/refill, payload/capture/prepare refusal and retirement passed");return 0;
    }
    check(argc==3);const unsigned trials=std::stoul(argv[1]),reps=std::stoul(argv[2]);check(trials&&reps);
    std::puts("arm,repetition,trials,phase,operations,ns_per_operation,checksum,live_payload_bytes,live_known_backing_bytes,live_known_owner_metadata_bytes,shared_preparation_bytes");
    for(unsigned r=0;r<reps;++r)for(unsigned j=0;j<4;++j)switch((r+j)%4) {
    case 0:run<abseil>(trials,r,"abseil",true);break;
    case 1:run<packed<8>>(trials,r,"n8",true);break;
    case 2:run<packed<16>>(trials,r,"n16",true);break;
    case 3:run<packed<16,true>>(trials,r,"n16_direct",true);break;
    }
}
