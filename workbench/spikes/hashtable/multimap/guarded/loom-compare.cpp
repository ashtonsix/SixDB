#include "multimap.h"
#include <loom/ring.h>
#include <loom/stream.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/inlined_vector.h>
#include <absl/container/internal/hashtable_debug.h>
#include <chrono>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <thread>

namespace writes {
namespace ht=ikea::hashtable;
using ht::word;using ht::byte;
constexpr unsigned b=WRITE_ARCH_LOOM_WRITE_B,K=WRITE_ARCH_LOOM_WRITE_K,V=WRITE_ARCH_V,W=b+K;
using MM=write_arch::multimap<W,b,V,false>;
constexpr unsigned encoding=write_arch::read_profile<K,V,true>().width;
using Session=write_arch::prepared_multimap<MM>;
using Value=ht::bytes<V>;
using Key=std::conditional_t<(W<=32),std::uint32_t,word>;
using VectorMap=absl::flat_hash_map<Key,std::vector<Value>>;
using InlineMap=absl::flat_hash_map<Key,absl::InlinedVector<Value,1>>;
using Ring=loom::Ring<void*>;
constexpr unsigned bank=32,lookahead=32,quantum=128;
struct Context {
    bool enter(const MM&,word,ht::access_intent)const noexcept{return true;}
    bool prepare(std::span<const ht::mutation_damage>,std::span<const ht::mutation_source>)const noexcept{return true;}
    void before(ht::mutation_source,ikea::byte_write)const noexcept{}
};
word mix(word x){x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);}
word key(word rank,word seed){word x=(rank^seed)&ht::mask<W>;x^=x>>(W/2);x=x*0xbf58476d1ce4e5b9ULL&ht::mask<W>;x^=x>>(W/3);return x*0x94d049bb133111ebULL&ht::mask<W>;}
Value value(word k,unsigned rank){Value out{};for(unsigned i=0;i<V;i+=8)ht::store<8>(out.data()+i,mix(k+rank*65537+i*131));return out;}
template<class Owner> constexpr bool abseil=std::is_same_v<Owner,VectorMap>||std::is_same_v<Owner,InlineMap>;
template<class Owner> std::size_t bytes(const Owner& owner){
    if constexpr(abseil<Owner>){auto n=absl::container_internal::AllocatedByteSize(owner);
        for(const auto& [k,bag]:owner){(void)k;const auto p=reinterpret_cast<std::uintptr_t>(bag.data()),base=reinterpret_cast<std::uintptr_t>(&bag);
            if(p<base||p>=base+sizeof(bag))n+=bag.capacity()*V;}return n;
    }else return owner.bytes_used();
}
template<class Owner> void put(Owner& owner,word k,const Value& v){if constexpr(abseil<Owner>)owner[Key(k)].push_back(v);else owner.insert(k,v);}
template<class Owner,class Consume> bool find(Owner& owner,word k,Consume&& consume){
    if constexpr(abseil<Owner>){auto it=owner.find(Key(k));if(it==owner.end())return false;for(const auto& v:it->second)consume(v);return true;}
    else return owner.find(k,std::forward<Consume>(consume));
}

// Same dependent-line conditioning protocol as loom-composition/fixture.h.
// It conditions requests as well as owners; it does not identify cache misses.
struct Conditioning {
    struct alignas(64) Line {std::uintptr_t next{};std::array<byte,56> padding{};};
    std::vector<Line> lines;
    explicit Conditioning(std::size_t bytes):lines((bytes+63)/64){
        std::vector<std::size_t> order(lines.size());std::iota(order.begin(),order.end(),0);
        std::mt19937_64 rng(0x87a14411);std::shuffle(order.begin(),order.end(),rng);
        for(std::size_t i=0;i<order.size();++i)lines[order[i]].next=reinterpret_cast<std::uintptr_t>(&lines[order[(i+1)%order.size()]]);
    }
    void apply()const {if(lines.empty())return;auto p=reinterpret_cast<std::uintptr_t>(lines.data());
        for(std::size_t i=0;i<lines.size();++i)p=*reinterpret_cast<volatile std::uintptr_t*>(p);
        asm volatile(""::"r"(p):"memory");}
};
struct alignas(64) Row {word value{};std::array<word,7> padding{};};
struct Companion {
    std::vector<Row> rows;std::vector<unsigned> requests;
    loom::IndependentStream<unsigned,quantum,lookahead> stream;
    loom::DemandClass demand;word sum=0,expected=0;
    Companion(std::size_t n,word seed,bool small,loom::DemandClass d):rows(n?(small?256:1u<<19):0),requests(n),stream(requests),demand(d){
        for(std::size_t i=0;i<rows.size();++i)rows[i].value=mix(i+71);
        for(std::size_t i=0;i<n;++i){requests[i]=unsigned(mix(i+seed)&(rows.size()-1));expected+=rows[requests[i]].value;}
    }
};
LOOM_NATIVE_CC void companion(Ring::Cursor ring,Ring::Slot* slot,void* opaque){
    auto& f=*static_cast<Companion*>(opaque);auto sum=f.sum;
    (void)f.stream.advance([&](unsigned i)noexcept{ring->prefetch(f.demand,&f.rows[i]);},
        [&](unsigned i)noexcept{sum+=f.rows[i].value;});
    f.sum=sum;
    // One quantum per admission. The owner admits the next only at the next
    // common write watermark, keeping reader pressure paced in both procedures.
    [[clang::musttail]] return Ring::finish(ring,slot,opaque);
}
struct Frame {
    Session* owner=nullptr;Session::observation head;loom::DemandClass demand;
    word key=0;std::size_t request=0;bool started=false,ready=false;
};
LOOM_NATIVE_CC void observe(Ring::Cursor ring,Ring::Slot* slot,void* opaque){
    auto& f=*static_cast<Frame*>(opaque);
    {
        // This concrete context has no journal and no retained grant state.
        // Its lifetime still ends before tail transfer, as a real grant must.
        Context context;
        auto emit=[&](const byte* p,std::size_t)noexcept{ring->prefetch(f.demand,p);};
        const auto p=f.started?f.owner->advance_observation(f.head,context,emit):
            f.owner->begin_observation(f.key,f.head,context,emit);
        f.started=true;f.ready=p.status!=Session::observation_status::pending;
    }
    if(f.ready)[[clang::musttail]] return Ring::finish(ring,slot,opaque);
    [[clang::musttail]] return Ring::yield_unchanged(ring,slot,opaque);
}
struct Runtime {
    loom::Worker worker;orbital::Ref<orbital::allocation::Arena> arena;Ring ring;
    Runtime(){
        ht::require(bool(loom::adopt(worker)),"write worker adoption");
        auto result=orbital::allocation::open_arena(8*1024*1024);ht::require(bool(result),"write worker arena");arena=std::move(*result);
        ht::require(bool(loom::configure(worker,arena,0)),"write worker configure");
        ht::require(bool(ring.configure(worker,{bank+1,0})),"write Ring configure");
    }
    ~Runtime(){
        // Error cleanup is outside native execution and keeps borrowed frames
        // alive until the caller's enclosing scope exits.
        if(ring.pending())ring.discard();
        ht::require(bool(ring.close()),"write Ring close");
        for(;;){auto p=loom::shutdown(worker);ht::require(p.kind()!=orbital::Progress::Kind::failed,"write worker shutdown");
            if(p.kind()==orbital::Progress::Kind::done)break;loom::poll(worker,{64,0});std::this_thread::yield();}
        loom::relinquish(worker);
    }
};
constexpr auto unfinished=std::numeric_limits<std::size_t>::max();
struct Result {std::size_t completed=0,companion_finished_after=unfinished;};
void service_companion(Ring& ring,Companion& other,std::size_t committed){
    const auto target=std::min(other.requests.size(),committed);
    ht::require(other.stream.processed()<target,"companion watermark advances");
    ht::require(ring.push(companion,{&other}),"paced companion admission");
    // Other observation frames may progress, but no more writes commit until
    // this exact reader prefix completes. Direct uses the same service call.
    while(other.stream.processed()<target)ring.spin({bank+1});
    ht::require(other.stream.processed()==target,"matched reader prefix at writer watermark");
}
template<bool Prefetch,class Owner> Result direct(Owner& owner,const std::vector<word>& keys,
    const std::vector<Value>& incoming,Ring& ring,Companion& other,bool mixed){
    Result result;
    auto hint=[&](word k){
        if constexpr(abseil<Owner>)owner.prefetch(Key(k));
        else {Context context;ht::require(owner.prefetch_append(k,context,[](const byte* p,std::size_t)noexcept{__builtin_prefetch(p,0,3);}),"plain hint admitted");}
    };
    if constexpr(Prefetch)for(std::size_t i=0;i<std::min(std::size_t(lookahead),keys.size());++i)hint(keys[i]);
    for(std::size_t i=0;i<keys.size();++i){
        if constexpr(Prefetch)if(i+lookahead<keys.size())hint(keys[i+lookahead]);
        if constexpr(abseil<Owner>)put(owner,keys[i],incoming[i]);
        else {Context context;ht::require(owner.append(keys[i],std::span<const byte,V>(incoming[i].data(),V),context)==ht::mutation_status::updated,"direct complete append");}
        ++result.completed;
        if(mixed&&result.completed%quantum==0){service_companion(ring,other,result.completed);if(other.stream.empty())result.companion_finished_after=result.completed;}
    }
    if(mixed&&!other.stream.empty())service_companion(ring,other,result.completed);
    while(ring.pending())ring.spin({1});
    if(mixed&&result.companion_finished_after==unfinished)result.companion_finished_after=result.completed;
    return result;
}
Result staged(Session& owner,const std::vector<word>& keys,const std::vector<Value>& incoming,
    std::array<Frame,bank>& frames,Ring& ring,Companion& other,bool mixed,loom::DemandClass demand){
    std::size_t admitted=0;Result result;
    auto admit=[&](Frame& f){f.owner=&owner;f.demand=demand;f.key=keys[admitted];f.request=admitted++;
        f.started=f.ready=false;ht::require(ring.push(observe,{&f}),"observation frame admission");};
    for(unsigned i=0;i<bank&&admitted<keys.size();++i)admit(frames[i]);
    while(result.completed<keys.size()){
        ring.spin({quantum});
        // Commit in input order even if heads finish out of order. This is also
        // the repeated-key ordering rule; observations reserve no slot or rank.
        while(result.completed<keys.size()){
            auto& f=frames[result.completed%bank];if(!f.ready)break;
            ht::require(f.request==result.completed,"ordered observed commit");
            const auto& value=incoming[result.completed];
            {Context context;ht::require(owner.commit_observed(f.head,std::span<const byte,V>(value.data(),V),context).status==ht::mutation_status::updated,"observed complete append");}
            ++result.completed;f.ready=false;
            // Its finish already retired the physical borrower before spin
            // returned. Unrelated frames may remain while this head is reused.
            if(admitted<keys.size())admit(f);
            if(mixed&&result.completed%quantum==0){service_companion(ring,other,result.completed);if(other.stream.empty())result.companion_finished_after=result.completed;}
        }
    }
    if(mixed&&!other.stream.empty())service_companion(ring,other,result.completed);
    while(ring.pending())ring.spin({quantum});
    if(mixed&&result.companion_finished_after==unfinished)result.companion_finished_after=result.completed;
    return result;
}

template<class Owner> void run(std::string_view name,std::string_view path,unsigned count,unsigned repeats,
    bool mixed,word seed,unsigned rep,bool small,std::size_t eviction){
    const std::size_t n=small?127:MM::bucket_count*MM::N*85/100;
    std::vector<word> distinct;distinct.reserve(n);for(std::size_t i=0;i<n;++i)distinct.push_back(key(i,seed));
    std::mt19937_64 rng(seed);std::shuffle(distinct.begin(),distinct.end(),rng);
    std::vector<word> keys;std::vector<Value> incoming;keys.reserve(n*repeats);incoming.reserve(n*repeats);
    for(auto k:distinct)for(unsigned r=0;r<repeats;++r){keys.push_back(k);incoming.push_back(value(k,count+r));}
    std::unique_ptr<Owner> owner;
    if constexpr(abseil<Owner>){owner=std::make_unique<Owner>();owner->reserve(n);}else owner=std::make_unique<Owner>(mix(seed));
    for(auto k:distinct)for(unsigned r=0;r<count;++r)put(*owner,k,value(k,r));
    auto verify=[&](auto& target,unsigned length){ht::require(target.size()==n,"write distinct count");
        for(auto k:distinct){unsigned rank=0;ht::require(find(target,k,[&](const auto& v){
            ht::require(rank<length&&v==value(k,rank),"complete ordered write payload");++rank;}),"write membership");
            ht::require(rank==length,"write bag length");}
    };
    verify(*owner,count);if constexpr(!abseil<Owner>)owner->validate();
    const auto before=bytes(*owner);std::size_t after=0;
    auto demand=loom::bind_demand({loom::MemoryPattern::irregular,1},1);ht::require(bool(demand),"write demand bind");
    Companion other(mixed?keys.size():0,seed,small,*demand);
    Conditioning conditioning(eviction);Result result;double elapsed=0;loom::RingCounters counters{};
    if constexpr(abseil<Owner>){
        ht::require(path!="observed","no staged Abseil facade");Runtime runtime;conditioning.apply();
        const auto begin=std::chrono::steady_clock::now();
        if(path=="scalar")result=direct<false>(*owner,keys,incoming,runtime.ring,other,mixed);
        else result=direct<true>(*owner,keys,incoming,runtime.ring,other,mixed);
        elapsed=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count();
        counters=runtime.ring.counters();verify(*owner,count+repeats);after=bytes(*owner);
    }else {
        Session session(std::move(owner));std::array<Frame,bank> frames;
        Runtime runtime;conditioning.apply();const auto begin=std::chrono::steady_clock::now();
        if(path=="observed")result=staged(session,keys,incoming,frames,runtime.ring,other,mixed,*demand);
        else if(path=="scalar")result=direct<false>(session,keys,incoming,runtime.ring,other,mixed);
        else result=direct<true>(session,keys,incoming,runtime.ring,other,mixed);
        elapsed=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count();
        counters=runtime.ring.counters();verify(session,count+repeats);owner=session.release_owner();owner->validate();after=bytes(*owner);
    }
    ht::require(result.completed==keys.size()&&(!mixed||(other.stream.empty()&&other.sum==other.expected)),"complete write and companion accounting");
    const auto admitted=(path=="observed"?keys.size():0)+(mixed?(keys.size()+quantum-1)/quantum:0);
    ht::require(!counters.refused&&counters.accepted==admitted&&counters.completed==admitted,"physical frame accounting");
    if(!mixed)result.companion_finished_after=0;
#ifdef WRITE_ARCH_OBSERVED_STUDY
    std::cout<<WRITE_ARCH_OBSERVED_TYPED<<',';
#endif
    std::cout<<name<<','<<path<<','<<WRITE_ARCH_FITTING_PLAN<<','<<W<<','<<b<<','<<K<<','<<encoding<<','<<MM::N<<','<<V<<','<<count<<','<<repeats<<','<<mixed<<','<<seed<<','<<rep<<','<<n<<','<<keys.size()<<','<<elapsed<<','<<elapsed/keys.size()<<','<<before<<','<<after<<','<<eviction<<','<<other.requests.size()<<','<<other.sum<<','<<result.companion_finished_after<<','<<counters.accepted<<','<<counters.completed<<','<<counters.visits<<','<<counters.refused<<'\n';
}
}
int main(int argc,char** argv){using namespace writes;
    ht::require(argc==10,"owner path count repeats mixed seed rep small evictionMiB");
    std::string_view owner=argv[1],path=argv[2];const unsigned count=std::stoul(argv[3]),repeats=std::stoul(argv[4]);
    const bool mixed=std::stoul(argv[5]),small=std::stoul(argv[8]);const word seed=std::stoull(argv[6]);const unsigned rep=std::stoul(argv[7]);const auto eviction=std::stoull(argv[9])<<20;
    ht::require((path=="scalar"||path=="prefetch32"||path=="observed")&&(count==3||count==4)&&(repeats==1||repeats==2),"write composition cohort");
#ifdef WRITE_ARCH_OBSERVED_STUDY
    std::cout<<"arm,";
#endif
    std::cout<<"owner,path,fitting_plan,W,b,K,encoding,N,V,count,repeats,mixed,seed,rep,distinct,operations,elapsed_ns,ns,before_bytes,after_bytes,eviction_bytes,companion_requests,companion_sum,companion_finished_after,ring_accepted,ring_completed,ring_visits,ring_refused\n";
    if(owner=="prepared")run<MM>(owner,path,count,repeats,mixed,seed,rep,small,eviction);
    else if(owner=="abseil_vector")run<VectorMap>(owner,path,count,repeats,mixed,seed,rep,small,eviction);
    else if(owner=="abseil_inline")run<InlineMap>(owner,path,count,repeats,mixed,seed,rep,small,eviction);
    else ht::require(false,"known write owner");
}
