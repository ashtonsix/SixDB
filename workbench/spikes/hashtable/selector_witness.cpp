#include "record_table.h"
#include "small_baseline.h"
#include <absl/container/internal/hashtable_debug.h>
#include <numeric>
#include <random>

namespace {
using namespace ht;
template<unsigned K,unsigned A> struct candidate {
    using table=record_table<K,container_kind::map,2,A,(A>4?A-4:1),128,128,
        record_format::exact,false,record_access::both>;
    table data;
    candidate(oa::Pool& pool,word seed,unsigned):data(pool,seed,mix(seed^0x91817731eafULL)) {}
    void set(word key,std::uint16_t value) {
        typename table::record r;std::memcpy(r.payload.data(),&value,2);
        auto done=data.mutate(key,[&](const auto&)->std::optional<typename table::record>{return r;});
        require(bool(done),"witness map insertion");
    }
    [[gnu::always_inline]] std::optional<std::uint16_t> get(word key)const {
        std::uint16_t value=0;
        const bool found=data.get_record(key,[&](const auto& r){std::memcpy(&value,r.payload.data(),2);});
        return found?std::optional(value):std::nullopt;
    }
    memory footprint()const{return data.footprint();}
    auto population()const{return data.population();}
};
template<unsigned K,bool Exact> struct baseline {
    using key_type=std::conditional_t<Exact,hashtable_spike::exact_unsigned_key<K/8>,typename abseil<K>::key_type>;
    absl::flat_hash_map<key_type,std::uint16_t> data;
    baseline(oa::Pool&,word,unsigned n){data.reserve(n);}
    void set(word key,std::uint16_t value){[[clang::always_inline]] data.insert_or_assign(key_type(key),value);}
    [[gnu::always_inline]] std::optional<std::uint16_t> get(word key)const {
        typename decltype(data)::const_iterator at;[[clang::always_inline]] at=data.find(key_type(key));
        return at==data.end()?std::nullopt:std::optional(at->second);
    }
    memory footprint()const{return {absl::container_internal::AllocatedByteSize(data),0,sizeof(*this)};}
    std::array<word,3> population()const{return {};}
};
template<class F> double timed(F&& f) {
    const auto start=clock_type::now();f();
    return std::chrono::duration<double,std::nano>(clock_type::now()-start).count();
}

// These fixed geometries replay identified selector-quality weaknesses through
// real table owners. They do not select the compact-list transition or presets.
template<unsigned K,unsigned A,unsigned Arm> void trial(oa::Pool& pool,unsigned seed_index,unsigned rep) {
    constexpr unsigned C=K-A-8,n=12u<<A,rounds=std::max(8u,262144u/n);
    static_assert(C>=5 && n<65536);
    using body_type=std::conditional_t<Arm==0,candidate<K,A>,baseline<K,Arm==2>>;
    std::vector<word> keys(2*n);
    for(unsigned i=0;i<2*n;++i) {
        const word group=i>>A,low=i&mask(A);
        if constexpr(K==40)keys[i]=((group*0x44ULL)<<A)|low;
        else keys[i]=((group<<std::min(C-5,8u))<<(A+8))|low;
        require(keys[i]<=mask(K),"witness input domain");
    }
    auto sorted=keys;std::sort(sorted.begin(),sorted.end());
    require(std::adjacent_find(sorted.begin(),sorted.end())==sorted.end(),"independent witness input uniqueness");
    std::vector<unsigned> order(n),next(n);std::iota(order.begin(),order.end(),0);
    std::mt19937 random(0x219317);std::shuffle(order.begin(),order.end(),random);
    for(unsigned i=0;i<n;++i)next[order[i]]=order[(i+1)%n];
    std::optional<body_type> body;
    const auto seed=mix(seed_index+0x66129453812ULL);
    const auto construction=timed([&]{body.emplace(pool,seed,n);});
    // Sequential admission matches the quality witness; query order is shuffled.
    const auto build=timed([&]{for(unsigned i=0;i<n;++i)body->set(keys[i],std::uint16_t(next[i]));});
    for(unsigned i=0;i<2*n;++i) {
        auto result=body->get(keys[i]);
        require(bool(result)==(i<n) && (!result || *result==next[i]),"independent complete point/value oracle");
    }
    auto population=body->population();
    if constexpr(Arm==0)require(population==std::array<word,3>{n,0,0},"twelve-per-primary witness placement");
    const auto memory=body->footprint();
    word hit_sum=0,miss_sum=0,serial_sum=0;
    auto read=[&](bool miss) {
        word sum=0;
        for(unsigned r=0;r<rounds;++r)for(auto i:order) {
            auto value=body->get(keys[i+(miss?n:0)]);if(value)sum+=word(*value)+1;
        }
        return sum;
    };
    const auto hit=timed([&]{[[clang::always_inline]] hit_sum=read(false);});
    const auto miss=timed([&]{[[clang::always_inline]] miss_sum=read(true);});
    unsigned rank=order[0];
    const auto serial=timed([&]{
        for(unsigned i=0;i<n*rounds;++i) {
            auto value=body->get(keys[rank]);require(bool(value),"witness dependent hit");
            rank=*value;serial_sum+=word(rank)+1;
        }
    });
    const word expected=word(n)*(n+1)/2*rounds;
    require(hit_sum==expected && miss_sum==0 && serial_sum==expected && rank==order[0],"complete reductions and dependent cycle");
    const auto release=timed([&]{body.reset();});
    auto emit=[&](const char* phase,double ns,word operations) {
        std::printf("%u,%u,%u,%u,%u,%u,%s,%llu,%.3f,%llu,%llu,%llu,%llu,%llu\n",K,A,Arm,seed_index,rep,HT_SELECTOR_HASH,
            phase,(unsigned long long)operations,ns,(unsigned long long)memory.requested,
            (unsigned long long)memory.metadata,(unsigned long long)population[0],
            (unsigned long long)population[1],(unsigned long long)population[2]);
    };
    emit("construct",construction,1);emit("build",build,n);emit("hit",hit,word(n)*rounds);
    emit("miss",miss,word(n)*rounds);emit("serial",serial,word(n)*rounds);
    emit("release",release,1);emit("build_lifetime",construction+build+release,n);
}
template<unsigned K,unsigned A> void select(unsigned arm,unsigned seed,unsigned reps) {
    for(unsigned rep=0;rep<reps;++rep) {
        pool_owner pool;
        if(arm==0)trial<K,A,0>(*pool.pool,seed,rep);
        else if(arm==1)trial<K,A,1>(*pool.pool,seed,rep);
        else if(arm==2)trial<K,A,2>(*pool.pool,seed,rep);
        else require(false,"witness arm");
    }
}
}
int main(int argc,char** argv) {
    require(argc==5,"selector witness: K24/32/40 arm0/1/2 seed_index reps");
    const unsigned k=std::strtoul(argv[1],nullptr,10),arm=std::strtoul(argv[2],nullptr,10),
        seed=std::strtoul(argv[3],nullptr,10),reps=std::strtoul(argv[4],nullptr,10);
    require(reps>0,"witness repetitions");
    std::puts("K,A,arm,seed,rep,selector,phase,operations,ns,requested,metadata,primary,secondary,overflow");
    if(k==24)select<24,3>(arm,seed,reps);
    else if(k==32)select<32,6>(arm,seed,reps);
    else if(k==40)select<40,8>(arm,seed,reps);
    else require(false,"witness domain");
}
