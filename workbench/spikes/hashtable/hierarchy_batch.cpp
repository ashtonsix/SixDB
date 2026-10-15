#include "promoted.h"
#include "small_baseline.h"
#include <absl/container/internal/hashtable_debug.h>
#include <map>
#include <numeric>
#include <random>
#include <string>
#ifndef HT_PACKET_KEY
#define HT_PACKET_KEY 40
#endif
#ifndef HT_PACKET_BYTES
#define HT_PACKET_BYTES 2
#endif
#ifndef HT_PACKET_KIND
#define HT_PACKET_KIND 1
#endif
#ifndef HT_PACKET_CONSUMERS
#define HT_PACKET_CONSUMERS 0
#endif
namespace ht::hierarchy_packet {
constexpr unsigned K=HT_PACKET_KEY,W=HT_PACKET_BYTES;
constexpr auto Kind=container_kind(HT_PACKET_KIND);
static_assert((Kind==container_kind::set)==(W==0));
using value=std::array<byte,W>;
using oracle=std::map<word,std::vector<value>>;
using Bags=linked_bags<(W?W:8),true>;
using Small=small_table<K,Kind,W,true,Bags,16>;
template<unsigned A,unsigned B,bool Direct=false> using Candidate=promoted_table<K,Kind,W,A,B,128,128,
    record_format::exact,Direct,record_access::both,true,Bags,16>;
word key(unsigned i){return hash_mask<K>(word(i)*0xd6e8feb86659fd93ULL);}
value pattern(unsigned i){value v;for(unsigned j=0;j<W;++j)v[j]=byte(mix(i+word(j)*719));return v;}
struct packet {std::array<word,8> keys;std::array<value,8> values;unsigned active=255;};
using trace=std::vector<packet>;
trace make_trace(unsigned n,unsigned mode,unsigned first_key=0) {
    trace t;
    for(unsigned i=0;i<n;i+=8) {
        packet p;
        for(unsigned j=0;j<8;++j){unsigned rank=(i+j)%n;
            // Later modes deliberately repeat keys within a packet and vary all
            // value bytes. Odd multiplication makes the domain mapping injective.
            if(mode==1)rank=(i+j/2)%n;
            p.keys[j]=key(rank+first_key);p.values[j]=pattern(i+j+mode*30001);}
        p.active=mode==1?std::array{255u,219u,1u,128u,0u,85u,170u,240u}[(i/8)%8]:255;
        t.push_back(p);
    }
    std::mt19937 rng(912+mode);std::shuffle(t.begin(),t.end(),rng);return t;
}
void remember(oracle& expected,const trace& input) {
    for(const auto& p:input)for(unsigned i=0;i<8;++i)if(p.active&(1u<<i)) {
        auto& row=expected[p.keys[i]];
        if constexpr(Kind==container_kind::multimap)row.push_back(p.values[i]);
        else if constexpr(Kind==container_kind::map)row={p.values[i]};else row={value{}};
    }
    for(auto& [k,v]:expected)std::sort(v.begin(),v.end());
}
template<class Body> oracle contents(const Body& body) {
    oracle out;body.each_bytes([&](word k,auto bytes){value v;std::copy(bytes.begin(),bytes.end(),v.begin());out[k].push_back(v);});
    for(auto& [k,v]:out)std::sort(v.begin(),v.end());return out;
}
template<bool Exact> struct baseline {
    using key_type=std::conditional_t<Exact,hashtable_spike::exact_unsigned_key<K/8>,typename abseil<K>::key_type>;
    using scalar=std::conditional_t<W==1,std::uint8_t,std::conditional_t<W==2,std::uint16_t,
        std::conditional_t<W==4,std::uint32_t,std::conditional_t<W==8,word,value>>>>;
    using bag=absl::InlinedVector<scalar,1>;
    using mapped=std::conditional_t<Kind==container_kind::multimap,bag,scalar>;
    using table=std::conditional_t<Kind==container_kind::set,absl::flat_hash_set<key_type>,absl::flat_hash_map<key_type,mapped>>;
    table data;
    explicit baseline(unsigned n){data.reserve(n);}
    [[gnu::always_inline]] void set_bytes(word k,const value& v) {
        if constexpr(Kind==container_kind::set){[[clang::always_inline]] data.insert(key_type(k));}
        else if constexpr(Kind==container_kind::map){[[clang::always_inline]] data.insert_or_assign(key_type(k),std::bit_cast<scalar>(v));}
        else {std::pair<typename table::iterator,bool> r;[[clang::always_inline]] r=data.try_emplace(key_type(k));r.first->second.push_back(std::bit_cast<scalar>(v));}
    }
    template<class F> [[gnu::always_inline]] bool get_bytes(word k,F&& f)const {
        typename table::iterator at;[[clang::always_inline]] at=const_cast<table&>(data).find(key_type(k));
        if(at==data.end())return false;
        if constexpr(Kind==container_kind::set)f(value{});
        else if constexpr(Kind==container_kind::map)f(std::span<const byte,W>(reinterpret_cast<const byte*>(&at->second),W));
        else for(const auto& v:at->second)f(std::span<const byte,W>(reinterpret_cast<const byte*>(&v),W));return true;
    }
    template<unsigned N> [[gnu::always_inline]] unsigned setN_bytes(std::span<const word,N> keys,std::span<const value,N> values,unsigned active) {
        lanes<N>([&](auto i){if(active&(1u<<i))set_bytes(keys[i],values[i]);});return active;
    }
    template<unsigned N,class F> [[gnu::always_inline]] unsigned getN_bytes(std::span<const word,N> keys,unsigned active,F&& f)const {
        unsigned result=0;lanes<N>([&](auto i){if(active&(1u<<i))if(get_bytes(keys[i],[&] [[gnu::always_inline]] (auto v){f(i,v);}))result|=1u<<i;});return result;
    }
    void erase(word k){typename table::iterator at;[[clang::always_inline]] at=data.find(key_type(k));if(at!=data.end())data.erase(at);}
    template<class F> void each_bytes(F&& f)const {
        auto decode=[](const auto& k)->word{if constexpr(Exact)return k.value();else return k;};
        if constexpr(Kind==container_kind::set)for(auto k:data)f(decode(k),value{});
        else for(const auto& [k,v]:data) {
            if constexpr(Kind==container_kind::map)f(decode(k),std::bit_cast<value>(v));
            else for(auto x:v)f(decode(k),std::bit_cast<value>(x));
        }
    }
    memory footprint()const {
        word bytes=absl::container_internal::AllocatedByteSize(data);
        if constexpr(Kind==container_kind::multimap)for(const auto& [k,v]:data){auto a=reinterpret_cast<std::uintptr_t>(v.data()),b=reinterpret_cast<std::uintptr_t>(&v);
            if(a<b||a>=b+sizeof(v))bytes+=v.capacity()*W;}
        return {bytes,0,sizeof(*this)};
    }
};
struct answer {word hits=0,values=0,sum=0;bool operator==(const answer&)const=default;};
template<bool Tail=false> [[gnu::always_inline]] word digest(std::span<const byte,W> bytes) {
    if constexpr(Tail){static_assert(W>=8);word d;std::memcpy(&d,bytes.data()+W-8,8);return d;}
    else {word d=0;for(unsigned j=0;j<W;++j)d+=word(bytes[j])*(j+1);return d;}
}
// One audited root per complete trace; packet operations must fuse into it.
template<unsigned N,class Body> [[gnu::noinline]] unsigned write(Body& body,const trace& t) {
    unsigned count=0;for(const auto& p:t)for(unsigned first=0;first<8;first+=N) {
        const auto active=(p.active>>first)&unsigned(mask(N));
        if constexpr(N==1){if(active){[[clang::always_inline]] (void)body.set_bytes(p.keys[first],p.values[first]);}}
        else {unsigned done;[[clang::always_inline]] done=body.template setN_bytes<N>(
            std::span<const word,N>(p.keys.data()+first,N),std::span<const value,N>(p.values.data()+first,N),active);
            require(done==active,"complete hierarchy batch write");}
        count+=std::popcount(active);
    }
    return count;
}
template<unsigned N,bool Tail=false,class Body> [[gnu::noinline]] answer read(const Body& body,const trace& t,unsigned rounds) {
    answer out;for(unsigned r=0;r<rounds;++r)for(const auto& p:t)for(unsigned first=0;first<8;first+=N) {
        const auto active=(p.active>>first)&unsigned(mask(N));
        auto consume=[&] [[gnu::always_inline]] (unsigned lane,auto bytes) {
            ++out.values;out.sum+=digest<Tail>(std::span<const byte,W>(bytes.data(),W))^p.keys[first+lane];
        };
        if constexpr(N==1){if(active){bool found;[[clang::always_inline]] found=body.get_bytes(p.keys[first],[&] [[gnu::always_inline]] (auto b){consume(0,b);});out.hits+=found;}}
        else {unsigned found;[[clang::always_inline]] found=body.template getN_bytes<N>(std::span<const word,N>(p.keys.data()+first,N),active,consume);out.hits+=std::popcount(found);}
    }
    return out;
}
template<bool Tail=false> answer expected_read(const oracle& expected,const trace& t,unsigned rounds) {
    answer out;for(const auto& p:t)for(unsigned i=0;i<8;++i)if(p.active&(1u<<i))if(auto it=expected.find(p.keys[i]);it!=expected.end()) {
        ++out.hits;for(auto& v:it->second){++out.values;word d=0;
            if constexpr(Tail){static_assert(W>=8&&std::endian::native==std::endian::little);
                for(unsigned j=0;j<8;++j)d|=word(v[W-8+j])<<(8*j);}
            else for(unsigned j=0;j<W;++j)d+=word(v[j])*(j+1);
            out.sum+=d^p.keys[i];}}
    out.hits*=rounds;out.values*=rounds;out.sum*=rounds;return out;
}
template<unsigned N,class Body> void lane_oracle(const Body& body,const oracle& expected,const trace& t) {
    for(const auto& p:t)for(unsigned first=0;first<8;first+=N) {
        unsigned active=(p.active>>first)&unsigned(mask(N)),want=0;std::array<std::vector<value>,N> actual;unsigned previous=0;bool started=false;
        auto consume=[&](unsigned lane,auto bytes){require(lane<N&&(!started||lane>=previous),"ordered lane callbacks");started=true;previous=lane;
            value v;std::copy(bytes.begin(),bytes.end(),v.begin());actual[lane].push_back(v);};
        unsigned found;
        if constexpr(N==1)found=active&&body.get_bytes(p.keys[first],[&](auto b){consume(0,b);});
        else found=body.template getN_bytes<N>(std::span<const word,N>(p.keys.data()+first,N),active,consume);
        for(unsigned i=0;i<N;++i){std::vector<value> values;if(active&(1u<<i))if(auto it=expected.find(p.keys[first+i]);it!=expected.end()){want|=1u<<i;values=it->second;}
            std::sort(actual[i].begin(),actual[i].end());require(actual[i]==values,"independent per-lane full-byte bag oracle");}
        require(found==want,"exact per-lane hit mask");
    }
}
// One lane may commit, then an effects sink refuses the next active lane.
// This tests logical prefix/deltas, not an absence of reservation side effects.
struct prefix_effects {
    unsigned left;mutation_delta delta{};
    std::size_t remaining()const{return left?SIZE_MAX:0;}
    void before(const small_event& e)noexcept {if(e.type==small_event::kind::caller_delta){--left;delta.keys+=e.delta.keys;delta.values+=e.delta.values;}}
};
template<unsigned N,bool Direct=false> void check() {
    pool_owner pool;Small small(*pool.pool,23);using Body=Candidate<1,0,Direct>;
    auto body=Body::promote(small,*pool.pool,41,73);require(bool(body),"packet check owner");
    auto build=make_trace(240,0),update=make_trace(240,1);oracle expected;remember(expected,build);
    write<N>(*body,build);require(contents(*body)==expected,"all-tier build oracle");
    require(body->population()[0]&&body->population()[1]&&body->population()[2],"packet hits all hierarchy tiers");
    write<N>(*body,update);remember(expected,update);require(contents(*body)==expected,"ordered duplicate writes and inline/spill");
    lane_oracle<N>(*body,expected,update);
#if HT_PACKET_CONSUMERS
    require(read<N,true>(*body,update,3)==expected_read<true>(expected,update,3),"selected-field all-tier independent oracle");
    require(read<N,false>(*body,update,3)==expected_read<false>(expected,update,3),"whole-value all-tier independent oracle");
#endif
    trace probes=make_trace(16,2,240);lane_oracle<N>(*body,expected,probes);
    for(unsigned i=0;i<240;i+=2){body->erase(key(i));expected.erase(key(i));}
    lane_oracle<N>(*body,expected,build);write<N>(*body,update);remember(expected,update);
    require(contents(*body)==expected,"batch reuse after swap-last removal");
    if constexpr(N>1) {
        std::array<word,N> keys;std::array<value,N> values;for(unsigned i=0;i<N;++i){keys[i]=key(251);values[i]=pattern(i+713);}
        constexpr unsigned active=N==4?13u:181u;
        for(unsigned limit=0;limit<=std::popcount(active);++limit) {
            prefix_effects effects{limit};unsigned want=0,left=limit;
            for(unsigned i=0;i<N;++i)if((active&(1u<<i))&&left){want|=1u<<i;--left;}
            const auto old_keys=body->size();const auto old_values=body->value_count();
            const auto done=body->template setN_bytes<N>(keys,values,active,effects);
            require(done==want,"refusal preserves sparse active prefix");
            trace prefix;packet p{};p.active=done;
            for(unsigned i=0;i<N;++i){p.keys[i]=keys[i];p.values[i]=values[i];}prefix.push_back(p);remember(expected,prefix);
            require(contents(*body)==expected,"refused later duplicate is absent");
            require(body->size()==old_keys+effects.delta.keys&&body->value_count()==old_values+effects.delta.values,"prefix caller deltas");
            require(body->template setN_bytes<N>(keys,values,0,effects)==0,"empty packet has no effects");
            const auto retry=active&~done;require(body->template setN_bytes<N>(keys,values,retry)==retry,"retry uncommitted lanes");
            prefix[0].active=retry;remember(expected,prefix);require(contents(*body)==expected,"retry appends exactly once");
        }
    }
    body->validate();auto clone=body->clone();require(clone&&contents(*clone)==expected,"packet clone ownership");
    body.reset();lane_oracle<N>(*clone,expected,update);
}
template<class F> double timed(F&& f){const auto a=clock_type::now();f();return std::chrono::duration<double,std::nano>(clock_type::now()-a).count();}
template<unsigned N,unsigned Arm> void trial(unsigned n,unsigned rep,unsigned consumer=0) {
    pool_owner pool;constexpr unsigned A=K==8?3:6,B=K==8?1:2;
    using Body=std::conditional_t<Arm==0||Arm==3,Candidate<A,B,Arm==3>,baseline<Arm==2>>;
    const unsigned miss_count=[](unsigned count) {
        if constexpr(K<=16)return std::min(count,(1u<<K)-count);else return count;
    }(n);
    const auto build=make_trace(n,0),update=make_trace(n,1),misses=make_trace(miss_count,2,n);
    oracle expected;remember(expected,build);std::optional<Body> body;double life=0;unsigned writes=0;answer got;
    memory mem{};std::array<word,3> population{};
    auto read_body=&read<N,false,Body>;
    auto reference=&expected_read<false>;
#if HT_PACKET_CONSUMERS
    if(consumer){read_body=&read<N,true,Body>;reference=&expected_read<true>;}
#endif
    auto inventory=[&]{mem=body->footprint();if constexpr(Arm==0||Arm==3)population=body->population();};
    auto emit=[&](const char* phase,double ns,word ops){std::printf("%u,%u,%u,%u,%u,%u,%u,%s,%llu,%.3f,%llu,%llu,%llu,%llu,%llu,%u\n",
        K,W,unsigned(Kind),N,Arm,n,rep,phase,(unsigned long long)ops,ns,(unsigned long long)mem.requested,(unsigned long long)mem.metadata,
        (unsigned long long)population[0],(unsigned long long)population[1],(unsigned long long)population[2],consumer);};
    auto ns=timed([&]{if constexpr(Arm==0||Arm==3){Small small(*pool.pool,23);body=Body::promote(small,*pool.pool,41,73);require(bool(body),"timed packet promotion");}
        else body.emplace(n);});life+=ns;mem=body->footprint();emit("construct",ns,1);
    ns=timed([&]{writes=write<N>(*body,build);});life+=ns;
    inventory();emit("build",ns,writes);
    require(contents(*body)==expected,"timed initial independent oracle");lane_oracle<N>(*body,expected,update);
    const unsigned rounds=std::max(8u,65536u/n);
    ns=timed([&]{got=read_body(*body,build,rounds);});require(got==reference(expected,build,rounds),"timed read byte oracle");emit("hit",ns,word(n)*rounds);
    ns=timed([&]{got=read_body(*body,misses,rounds);});require(got==answer{},"timed missing oracle");emit("miss",ns,word(misses.size())*8*rounds);
    ns=timed([&]{writes=write<N>(*body,update);});life+=ns;inventory();emit("update",ns,writes);remember(expected,update);
    require(contents(*body)==expected,"timed duplicate/opaque update oracle");lane_oracle<N>(*body,expected,update);
    ns=timed([&]{got=read_body(*body,update,rounds);});require(got==reference(expected,update,rounds),"aged read oracle");emit("aged_hit",ns,word(writes)*rounds);
    ns=timed([&]{for(unsigned i=0;i<n;i+=2)body->erase(key(i));});life+=ns;inventory();emit("erase",ns,n/2);
    for(unsigned i=0;i<n;i+=2)expected.erase(key(i));require(contents(*body)==expected,"erase full-byte oracle");
    ns=timed([&]{writes=write<N>(*body,update);});life+=ns;inventory();emit("reuse",ns,writes);remember(expected,update);
    require(contents(*body)==expected,"reuse full-byte oracle");lane_oracle<N>(*body,expected,build);
    mem=body->footprint();if constexpr(Arm==0||Arm==3)population=body->population();
    ns=timed([&]{body.reset();});life+=ns;emit("release",ns,1);emit("lifecycle",life,n);
}
} // namespace ht::hierarchy_packet
int main(int argc,char** argv) {
    using namespace ht;using namespace ht::hierarchy_packet;
    if(argc==2&&std::string_view(argv[1])=="check"){check<1>();check<4>();check<8>();
#if HT_PACKET_CONSUMERS
        check<1,true>();check<4,true>();check<8,true>();
#endif
        std::puts("hierarchy scalar/4/8: all tiers, byte bags, duplicates, refusal prefix, removal/reuse and clone passed");return 0;}
    require(argc==5||(HT_PACKET_CONSUMERS&&argc==6),"usage: hierarchy_batch {1|4|8} {0|1|2|3-direct} n reps [0-full|1-tail] | check");
    unsigned width=std::stoul(argv[1]),arm=std::stoul(argv[2]),n=std::stoul(argv[3]),reps=std::stoul(argv[4]);
    unsigned consumer=argc==6?std::stoul(argv[5]):0;
    require((width==1||width==4||width==8)&&arm<(HT_PACKET_CONSUMERS?4:3)&&consumer<=1&&n>=16&&n%8==0&&n+16<=(K==8?256u:65536u),"packet driver arguments");
    std::puts("K,W,kind,N,arm,n,rep,phase,operations,ns,requested,metadata,primary,secondary,overflow,consumer");
    for(unsigned rep=0;rep<reps;++rep)lanes<HT_PACKET_CONSUMERS?4:3>([&](auto a){if(arm==a){
        if(width==1)trial<1,a>(n,rep,consumer);else if(width==4)trial<4,a>(n,rep,consumer);else trial<8,a>(n,rep,consumer);
    }});
}
