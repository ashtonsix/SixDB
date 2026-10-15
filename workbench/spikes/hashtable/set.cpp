#include "table.h"
#include "set_baseline.h"
#include "input.h"
#include <unordered_set>
#include <string_view>

namespace ht {
struct question {word key,next[2];};
struct set_trace {
    input in;
    std::vector<question> graph;
    word steps;
    template<unsigned K> static set_trace make(unsigned n) {
        set_trace out{make_input<K>(n,95813),{},0};
        std::vector<word> order(2*n);std::iota(order.begin(),order.end(),0);
        std::mt19937_64 random(414);std::shuffle(order.begin(),order.end(),random);
        out.graph.resize(2*n);
        for(word i=0;i<order.size();++i) {
            auto at=order[i],next=order[(i+1)%order.size()];bool present=at<n;
            out.graph[at].key=present?out.in.keys[at]:out.in.absent[at-n];
            out.graph[at].next[present]=next;out.graph[at].next[!present]=(next+1)%order.size();
        }
        out.steps=((262144+2*n-1)/(2*n))*(2*n);return out;
    }
};
template<class Set> [[gnu::noinline]] word fill(Set& set,std::span<const word> keys) {
    word added=0;for(auto k:keys) {auto d=set.insert(k);require(d>=0,"set admission");added+=d;}return added;
}
template<class Set> [[gnu::noinline]] word contains_independent(const Set& set,std::span<const word> keys,unsigned repeats) {
    word found=0;for(unsigned r=0;r<repeats;++r)for(auto k:keys)found+=set.contains(k);
    asm volatile("" : "+r"(found) :: "memory");return found;
}
template<class Set> [[gnu::noinline]] word contains_serial(const Set& set,std::span<const question> graph,word steps) {
    word index=0,found_count=0;
    for(word i=0;i<steps;++i) {
        auto& q=graph[index];bool found=set.contains(q.key);index=q.next[found];found_count+=found;
        asm volatile("" : "+r"(index) : "r"(found));
    }
    require(index==0,"serial membership returns around complete cycles");return found_count;
}
template<class Set> [[gnu::noinline]] word turnover(Set& set,std::span<const word> old,std::span<const word> fresh) {
    word changed=0;
    for(unsigned i=0;i<old.size();++i) {
        changed+=set.erase(old[i]);auto d=set.insert(fresh[i]);require(d>=0,"turnover admission");changed+=d;
    }
    return changed;
}
template<class Set> void verify(const Set& set,std::span<const word> present,std::span<const word> absent) {
    set.validate();std::unordered_set<word> expected(present.begin(),present.end()),actual;
    set.each([&](word k){require(actual.insert(k).second,"set unique iteration");});
    require(actual==expected,"set independent oracle");
    for(auto k:present)require(set.contains(k),"set presence");
    for(auto k:absent)require(!set.contains(k),"set absence");
}
template<unsigned K,class Set> void trial(oa::Pool& pool,const set_trace& input,const char* arm,unsigned rep) {
    auto ns=[](auto at){return std::chrono::duration<double,std::nano>(clock_type::now()-at).count();};
    auto begin=clock_type::now();std::optional<Set> storage;
    storage.emplace(pool,mix(133+rep*2),mix(134+rep*2));auto& set=*storage;
    if constexpr(requires{set.reserve(1);})set.reserve(input.in.keys.size());
    auto setup=ns(begin);begin=clock_type::now();auto added=fill(set,input.in.keys);auto build=ns(begin);
    auto n=input.in.keys.size();require(added==n,"set fill count");verify(set,input.in.keys,input.in.absent);
    struct census {memory mem;word p=0,q=0,c=0;unsigned a=0,b=0,slots=0,unit=0;};
    auto inspect=[&] {
        census out{set.footprint()};
        if constexpr(requires{set.map.distribution();}) {
            auto d=set.map.distribution();out.p=d.primary;out.q=d.secondary;out.c=d.chain;
            out.a=Set::map_type::primary_bits;out.b=Set::map_type::secondary_bits;
            out.slots=Set::map_type::primary.slots;out.unit=Set::map_type::primary.unit;
        }
        return out;
    };
    // Census precedes matched read warmups, never the mutation timers. Retain
    // fresh/aged samples and print only after the complete timing sequence.
    auto fresh=inspect();census aged{};
    struct row {const char* op;double ns;word operations,checksum;bool aged;};
    std::array<row,10> rows;unsigned used=0;
    auto output=[&](const char* op,double duration,word operations,word checksum,bool after=false) {
        rows[used++]={op,duration,operations,checksum,after};
    };
    output("setup",setup,1,0);output("fill",build,n,n);
    auto repeats=unsigned((262144+n-1)/n);auto operations=n*repeats;
    for(bool miss:{false,true}) {
        auto& keys=miss?input.in.absent:input.in.keys;auto expected=miss?0:operations;
        require(contains_independent(set,keys,repeats)==expected,"membership warmup");
        begin=clock_type::now();auto sum=contains_independent(set,keys,repeats);auto t=ns(begin);
        require(sum==expected,"membership checksum");output(miss?"miss":"hit",t,operations,sum);
    }
    // Membership's Boolean selects the next query-source address. This includes
    // that source load and cannot be described as isolated kernel latency.
    require(contains_serial(set,input.graph,input.steps)==input.steps/2,"serial membership warmup");
    begin=clock_type::now();auto sum=contains_serial(set,input.graph,input.steps);auto t=ns(begin);
    require(sum==input.steps/2,"serial membership checksum");output("serial_mixed",t,input.steps,sum);
    begin=clock_type::now();sum=fill(set,input.in.keys);t=ns(begin);
    require(sum==0,"duplicate set insertion");output("duplicate",t,n,sum);
    begin=clock_type::now();sum=turnover(set,input.in.keys,input.in.absent);t=ns(begin);
    require(sum==2*n,"set turnover count");output("turnover",t,n,sum,true);
    verify(set,input.in.absent,input.in.keys);
    aged=inspect();
    require(contains_independent(set,input.in.absent,repeats)==operations,"aged warmup");
    begin=clock_type::now();sum=contains_independent(set,input.in.absent,repeats);t=ns(begin);
    require(sum==operations,"aged membership checksum");output("aged_hit",t,operations,sum,true);
    require(contains_independent(set,input.in.keys,repeats)==0,"aged miss warmup");
    begin=clock_type::now();sum=contains_independent(set,input.in.keys,repeats);t=ns(begin);
    require(sum==0,"aged miss checksum");output("aged_miss",t,operations,sum,true);
    begin=clock_type::now();storage.reset();t=ns(begin);
    output("release",t,1,0,true);require(used==rows.size(),"set panel coverage");
    for(auto r:rows) {
        auto s=r.aged?aged:fresh;auto mem=s.mem;
        std::printf("%s,%u,%zu,%u,%s,%llu,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u\n",arm,K,n,rep,r.op,
            (unsigned long long)r.operations,r.ns,(unsigned long long)r.checksum,(unsigned long long)mem.requested,
            (unsigned long long)mem.backing,(unsigned long long)mem.metadata,(unsigned long long)s.p,(unsigned long long)s.q,
            (unsigned long long)s.c,s.a,s.b,s.slots,s.unit);
    }
    std::fflush(stdout);
}
template<unsigned K> void check_set(oa::Pool& pool) {
    packed_set<K,2,0,64,64> set(pool,771,881);std::unordered_set<word> expected;
    std::mt19937_64 random(611);std::vector<word> keys{0,mask(K)};
    while(keys.size()<200){auto k=random()&mask(K);if(std::find(keys.begin(),keys.end(),k)==keys.end())keys.push_back(k);}
    for(unsigned i=0;i<5000;++i) {
        auto k=keys[random()%keys.size()];
        if(i%3)require(set.insert(k)==int(expected.insert(k).second),"set insert oracle");
        else require(set.erase(k)==(expected.erase(k)!=0),"set erase oracle");
        if(i%127==0) {
            set.validate();std::unordered_set<word> actual;set.each([&](word k){require(actual.insert(k).second,"unique set iteration");});
            require(actual==expected,"set mutation oracle");
        }
        require(set.contains(k)==expected.contains(k),"set presence oracle");
    }
    for(auto k:expected)require(set.erase(k),"set final erase");
    for(auto k:keys)require(!set.contains(k),"set final empty");
    set.validate();require(set.insert(0)==1&&set.insert(mask(K))==1,"set refill extremes");
}
template<unsigned K,unsigned A> void campaign(oa::Pool& pool,unsigned n,unsigned reps) {
    // Short remainders have no use for a half-empty 128B/N32 unit. Compare
    // 48/64B units there; wide remainders compare more 64B units against fewer
    // 96B units. These are executable physical candidates, not chosen presets.
    auto input=set_trace::make<K>(n);
    using compact=packed_set<K,A,A-4,(K==16?48:64),(K==16?48:64)>;
    using fuller=packed_set<K,(K==16?A:A-1),(K==16?A-4:A-5),(K==16?64:96),(K==16?64:96)>;
    for(unsigned rep=0;rep<reps;++rep)for(unsigned j=0;j<3;++j)switch((rep+j)%3) {
        case 0:trial<K,abseil_set<K>>(pool,input,"abseil_set",rep);break;
        case 1:trial<K,compact>(pool,input,"compact",rep);break;
        case 2:trial<K,fuller>(pool,input,"fuller",rep);break;
    }
}
} // namespace ht
int main(int argc,char** argv) {
    using namespace ht;auto mode=argc>1?std::string_view(argv[1]):"check";
    auto reps=argc>2?std::stoul(argv[2]):9;require(reps>0,"set repetitions");pool_owner pool;
    std::puts("arm,K,n,rep,operation,operations,ns,checksum,requested,owned_capacity,metadata,primary,secondary,chain,A,B,slots,unit");
    if(mode=="check") {
        check_set<8>(*pool.pool);check_set<16>(*pool.pool);check_set<24>(*pool.pool);check_set<32>(*pool.pool);
        check_set<40>(*pool.pool);check_set<48>(*pool.pool);check_set<56>(*pool.pool);check_set<64>(*pool.pool);
    } else if(mode=="hot16")campaign<16,6>(*pool.pool,900,reps);
    else if(mode=="warm16")campaign<16,10>(*pool.pool,24000,reps);
    else if(mode=="hot40")campaign<40,8>(*pool.pool,1800,reps);
    else if(mode=="large40")campaign<40,14>(*pool.pool,110000,reps);
    else {require(mode=="wide40","set mode");campaign<40,19>(*pool.pool,3600000,reps);}
}
