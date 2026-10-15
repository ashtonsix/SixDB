// One concrete owner transition, measured with publication from its first run.
// Both physical families use the same Orbital catalog and retained read scope.
#include "single-plans.h"
#include "support.h"
#include "packed_key_baseline.h"
#include <absl/container/internal/hashtable_debug.h>
#include <absl/container/internal/hashtablez_sampler.h>
#include <orbital/object.h>
#include <object/store_impl.h>
#include <chrono>
#include <memory_resource>
#include <optional>
#include <source_location>

namespace lifecycle {
using namespace hashtable_spike;
using orbital::Result;
using orbital::Error;
using Clock=std::chrono::steady_clock;
void check(bool ok,std::source_location at=std::source_location::current()) {
    if(!ok){std::fprintf(stderr,"single lifecycle invariant: %s:%u\n",at.file_name(),at.line());std::abort();}
}
constexpr auto small=single::k40v16_rows16,large=single::k40v16_rows32;
constexpr word key(unsigned i) {return (word(i)*0x1f32117+91)&mask(40);}
constexpr word value(unsigned i) {return (word(i)*31991+177)&65535;}
constexpr word changed_value(unsigned mutation) {return value(0)^(mutation==1?65535:0x5a5a);}
struct Memory final:std::pmr::memory_resource {
    std::size_t live{},peak{},calls{},fail_at=SIZE_MAX;
    void* do_allocate(std::size_t n,std::size_t alignment) override {
        if(calls++==fail_at)throw std::bad_alloc{};
        auto* p=std::pmr::new_delete_resource()->allocate(n,alignment);live+=n;peak=std::max(live,peak);return p;
    }
    void do_deallocate(void* p,std::size_t n,std::size_t alignment) override {
        check(live>=n);live-=n;std::pmr::new_delete_resource()->deallocate(p,n,alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other)const noexcept override {return this==&other;}
};
template<bool Direct> struct Packed {
    oa::Allocation allocation;
    bool grown{};
    inline static const single::binding<small> small_binding{};
    inline static const single::binding<large> large_binding{};
    template<class F> decltype(auto) visit(F&& f)const {
        auto* p=reinterpret_cast<byte*>(allocation.address);
        if(grown)return f(single::kernel<large,Direct>{p,large_binding});
        return f(single::kernel<small,Direct>{p,small_binding});
    }
    static Result<Packed> make(oa::Pool& pool,const Packed* source,unsigned mutation) {
        // The requested transition is 16->17 entries. Later versions replace a
        // value without changing capacity; every version owns immutable bytes.
        Packed result;result.grown=source!=nullptr;
        const auto bytes=result.grown?large.bytes:small.bytes;
        auto allocation=oa::allocate(pool,{.bytes=bytes,.alignment=16,.initialization=oa::Initialization::uninitialized});
        if(!allocation)return std::unexpected(allocation.error());
        result.allocation=std::move(*allocation);
        if(source&&source->grown)std::memcpy(result.allocation.address,source->allocation.address,bytes);
        else result.visit([&](auto destination) {
            destination.initialize();
            if(source)source->visit([&](auto old){old.each([&](word k,word v){check(destination.set(k,v).result==single::status::inserted);});});
            else for(unsigned i=0;i<16;++i)check(destination.set(key(i),value(i)).result==single::status::inserted);
        });
        if(source)result.visit([&](auto destination) {
            const auto changed=destination.set(key(mutation?0:16),mutation?changed_value(mutation):value(16));
            check(changed.distinct_delta==(mutation?0:1));
        });
        return result;
    }
    bool get(word k,word& out)const {return visit([&](auto view){return view.get(k,out);});}
    std::size_t bytes()const {return allocation.size;}
    std::size_t backing()const {return oa::capacity(allocation);}
};
struct Abseil {
    absl::flat_hash_map<exact_unsigned_key<5>,std::uint16_t> table;
    static Result<Abseil> make(oa::Pool&,const Abseil* source,unsigned mutation) {
        try {
            Abseil result;
            if(source) {
                result.table=source->table;
                result.table.insert_or_assign(exact_unsigned_key<5>(key(mutation?0:16)),std::uint16_t(mutation?changed_value(mutation):value(16)));
            } else {
                result.table.reserve(16);
                for(unsigned i=0;i<16;++i)result.table.emplace(exact_unsigned_key<5>(key(i)),std::uint16_t(value(i)));
            }
            return result;
        }catch(const std::bad_alloc&){return std::unexpected(Error::capacity);}
    }
    bool get(word k,word& out)const {
        const auto it=table.find(exact_unsigned_key<5>(k));if(it==table.end())return false;out=it->second;return true;
    }
    std::size_t bytes()const {return absl::container_internal::AllocatedByteSize(table);}
    std::size_t backing()const {return 0;}
};

// Minimal material for this comparison. PMR accounts only for Orbital catalog
// and physical-owner metadata; packed payload is an ordinary Orbital Allocation.
// Private construction finishes before capture returns. Published bytes are
// never mutated, so old scopes see their actual original contents.
template<class Body> struct Physical {
    struct Value {std::pmr::memory_resource* memory;std::size_t references;Body body;};
    // Diagnostic owner counters, not fields in every table or version.
    inline static std::size_t payload_live{},payload_peak{},backing_live{},backing_peak{};
    static void charge(const Body& body) {
        payload_live+=body.bytes();backing_live+=body.backing();
        payload_peak=std::max(payload_peak,payload_live);backing_peak=std::max(backing_peak,backing_live);
    }
    static void refund(const Body& body) {
        check(payload_live>=body.bytes()&&backing_live>=body.backing());
        payload_live-=body.bytes();backing_live-=body.backing();
    }
    struct Selection {
        Value* value{};explicit operator bool()const{return value;}
        bool get(word k,word& out)const{return value->body.get(k,out);}
    };
    struct Specification {oa::Pool* pool;const Body* source{};unsigned mutation{};};
    using PartSource=std::byte;
    enum class ExportPolicy {owned};
    struct Footprint {std::size_t requested_bytes,copied_metadata;};
    static void retain(Value* p)noexcept{if(p)++p->references;}
    static void release(Value* p)noexcept {
        if(!p||--p->references)return;auto* resource=p->memory;
        refund(p->body);std::destroy_at(p);resource->deallocate(p,sizeof(Value),alignof(Value));
    }
    static Value* get(Selection s)noexcept{return s.value;}
    static Selection selection(Value* p)noexcept{return {p};}
    static Result<Value*> capture(std::pmr::memory_resource& memory,const Specification& spec,std::span<const PartSource> parts)noexcept {
        if(!parts.empty()||!spec.pool)return std::unexpected(Error::invalid_argument);
        auto body=Body::make(*spec.pool,spec.source,spec.mutation);
        if(!body)return std::unexpected(body.error());
        try {
            static_assert(std::is_nothrow_move_constructible_v<Body>);
            auto* record=static_cast<Value*>(memory.allocate(sizeof(Value),alignof(Value)));
            auto* result=std::construct_at(record,Value{&memory,1,std::move(*body)});
            // Count the completed owner. Abseil's sanitizer move can rehash;
            // accounting before that move does not describe the retained body.
            charge(result->body);
            return result;
        }catch(const std::bad_alloc&){return std::unexpected(Error::capacity);}
    }
    static bool accepts(Value*,orbital::ObjectId,std::uint64_t)noexcept{return true;}
    static bool accepts_resource(Value* p,std::pmr::memory_resource& r)noexcept{return p->memory==&r;}
    // This publication-only material does not implement representation conversion.
    static bool compatible(Value*,Value*)noexcept{return false;}
    static Footprint footprint(Value*)noexcept{return {sizeof(Value),0};}
    struct Export {
        Value* value;orbital::ObjectBinding binding;
        Export(Value* p,orbital::ObjectBinding b):value(p),binding(b){retain(p);}
        Export(Export&& x)noexcept:value(std::exchange(x.value,nullptr)),binding(x.binding){}
        ~Export(){release(value);}
    };
    static Result<Export> export_part(Selection s,std::size_t i,orbital::ObjectBinding b,ExportPolicy)noexcept {
        if(!s||i)return std::unexpected(Error::absent);return Export(s.value,b);
    }
};
template<class Body> using Store=orbital::ObjectStore<Physical<Body>>;
constexpr orbital::ObjectId id(unsigned i){return {7,i+1};}
template<class T>T take(Result<T> r){check(bool(r));return std::move(*r);}
template<class Body>void verify(typename Physical<Body>::Selection s,unsigned stage) {
    check(bool(s));
    for(unsigned i=0;i<17;++i) {
        word got=~word{};const bool found=s.get(key(i),got);check(found==(i<16||stage));
        check(got==(found?((stage==2&&i==0)?value(i)^65535:value(i)):~word{}));
    }
}

template<class Body>void run(unsigned count,unsigned rep,const char* name,bool output,bool faults=false) {
    check(Physical<Body>::payload_live==0&&Physical<Body>::backing_live==0);
    Physical<Body>::payload_peak=Physical<Body>::backing_peak=0;
    pool_owner pool;Memory memory;std::size_t retained_payload{},retained_backing{},active_payload{},active_backing{};
    std::size_t metadata_live{},metadata_peak{};double growth_ns{},update_ns{},retire_ns{};
    {
        Store<Body> store(memory);
        auto publish=[&](typename Store<Body>::Scope& basis,typename Store<Body>::Owner owner,unsigned i) {
            const std::array<typename Store<Body>::Change,1> changes{{{id(i),std::move(owner)}}};
            typename Store<Body>::Prepared prepared(store);check(bool(store.prepare(basis,changes,prepared)));check(bool(store.install(prepared)));
        };
        for(unsigned i=0;i<count;++i) {
            typename Store<Body>::Scope basis(store);
            publish(basis,take(store.capture({pool.pool},{})),i);
        }
        {
            typename Store<Body>::Scope original(store);
            for(unsigned stage=0;stage<2;++stage) {
                const auto start=Clock::now();
                for(unsigned i=0;i<count;++i) {
                    typename Store<Body>::Scope basis(store);
                    const auto selected=basis.bind(id(i));
                    auto replacement=take(store.capture({pool.pool,&selected.value->body,stage},{}));
                    publish(basis,std::move(replacement),i);
                }
                const double elapsed=std::chrono::duration<double,std::nano>(Clock::now()-start).count()/count;
                if(stage==0)growth_ns=elapsed;else update_ns=elapsed;
                for(unsigned i=0;i<count;++i)verify<Body>(store.borrow().bind(id(i)),stage+1);
            }
            // Discover the old representation after both publications. This
            // catches merely retaining an address while overwriting its bytes.
            for(unsigned i=0;i<count;++i) {
                const auto old=original.bind(id(i));verify<Body>(old,0);
                retained_payload+=old.value->body.bytes();retained_backing+=old.value->body.backing();
                active_payload+=store.borrow().bind(id(i)).value->body.bytes();
                active_backing+=store.borrow().bind(id(i)).value->body.backing();
            }
            if(faults) {
                typename Store<Body>::Scope basis(store);
                const auto source=basis.bind(id(0));const auto before=memory.live;
                auto unchanged=[&] {
                    typename Store<Body>::Scope current(store);
                    check(current.cut()==basis.cut()&&current.bind(id(0)).value==source.value);
                    verify<Body>(current.bind(id(0)),2);verify<Body>(original.bind(id(0)),0);
                };
                memory.fail_at=memory.calls;
                auto failed=store.capture({pool.pool,&source.value->body,2},{});
                memory.fail_at=SIZE_MAX;check(!failed&&failed.error()==Error::capacity&&memory.live==before);
                unchanged();
                auto replacement=take(store.capture({pool.pool,&source.value->body,2},{}));
                const std::array<typename Store<Body>::Change,1> changes{{{id(0),std::move(replacement)}}};
                typename Store<Body>::Prepared prepared(store);memory.fail_at=memory.calls;
                auto refused=store.prepare(basis,changes,prepared);memory.fail_at=SIZE_MAX;
                check(!refused&&refused.error()==Error::capacity);unchanged();
                check(bool(store.prepare(basis,changes,prepared)));prepared.abort();unchanged();
            }
            metadata_live=memory.live;
        }
        const auto start=Clock::now();store.drain();retire_ns=std::chrono::duration<double,std::nano>(Clock::now()-start).count()/count;
        for(unsigned i=0;i<count;++i)verify<Body>(store.borrow().bind(id(i)),2);
        check(Physical<Body>::payload_live==active_payload&&Physical<Body>::backing_live==active_backing);
    }
    metadata_peak=memory.peak;
    if(memory.live||Physical<Body>::payload_live||Physical<Body>::backing_live)
        std::fprintf(stderr,"remaining %s: metadata=%zu payload=%zu capacity=%zu\n",name,memory.live,Physical<Body>::payload_live,Physical<Body>::backing_live);
    check(memory.live==0&&Physical<Body>::payload_live==0&&Physical<Body>::backing_live==0);
    if(output)std::printf("%s,%u,%u,%.3f,%.3f,%.3f,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",name,count,rep,growth_ns,update_ns,retire_ns,
                         retained_payload,retained_backing,active_payload,metadata_live,metadata_peak,Physical<Body>::payload_peak,Physical<Body>::backing_peak);
}
} // namespace lifecycle
int main(int argc,char** argv) {
    using namespace lifecycle;absl::container_internal::SetHashtablezEnabled(false);
    const bool checking=argc==2&&std::string_view(argv[1])=="check";
    if(checking){run<Packed<false>>(4,0,"composed",false,true);run<Packed<true>>(4,0,"direct",false,true);run<Abseil>(4,0,"abseil",false,true);std::puts("single lifecycle: real Orbital publication,16->17 growth, retained late readers, replacement, capture/prepare refusal and retirement passed");return 0;}
    check(argc==3);const unsigned count=std::stoul(argv[1]),reps=std::stoul(argv[2]);check(count&&reps);
    std::puts("implementation,objects,rep,growth_publish_ns,replace_publish_ns,catalog_reclaim_ns,retained_original_payload,retained_original_capacity,current_payload,metadata_live_before_reclaim,metadata_peak,completed_version_payload_peak,completed_version_capacity_peak");
    for(unsigned r=0;r<reps;++r)for(unsigned j=0;j<3;++j)switch((r+j)%3){case 0:run<Packed<false>>(count,r,"composed",true);break;case 1:run<Packed<true>>(count,r,"direct",true);break;case 2:run<Abseil>(count,r,"abseil_exact_key",true);break;}
}
