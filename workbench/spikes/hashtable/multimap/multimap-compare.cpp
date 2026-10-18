#include "multimap.h"
#include "legacy/bags.h"
#include "../reference/iceberg/bags.h"
#include <absl/container/flat_hash_map.h>
#include <absl/container/inlined_vector.h>
#include <absl/container/internal/hashtable_debug.h>
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>

#ifndef WRITE_ARCH_W
#define WRITE_ARCH_W 24
#endif
#ifndef WRITE_ARCH_MM_FOCUSED
#define WRITE_ARCH_MM_FOCUSED 0
#endif

namespace mm_study {
namespace ht=ikea::hashtable;
using ht::word;using ht::byte;
constexpr unsigned V=WRITE_ARCH_V,W=WRITE_ARCH_W,b=10,K=W-b;
static_assert(W==24||W==40);
using Key=std::conditional_t<(W<=32),std::uint32_t,word>;
using Value=ht::bytes<V>;
#if defined(__aarch64__)
constexpr auto kernel=ht::search_kernel::body_small_neon;
constexpr unsigned prior_kernel=22;
#else
constexpr auto kernel=ht::search_kernel::exact_small512;
constexpr unsigned prior_kernel=20;
#endif
using Maintained=ht::multimap<W,b,V,K,kernel>;
using Prior=iceberg::multimap<W,b,V,K,prior_kernel,iceberg::heap_storage_policy>;
using Packed=write_arch::multimap<W,b,V,false>;
using Whole=write_arch::multimap<W,b,V,true>;
using PackedCompact=write_arch::multimap<W,b,V,false,0>;
using WholeCompact=write_arch::multimap<W,b,V,true,0>;
using WholePrefix=write_arch::multimap<W,b,V,true,0,write_arch::buffer_representation::mutable_prefix_diagnostic>;
using WholeDirect=write_arch::multimap<W,b,V,true,(V==8?2:1),write_arch::buffer_representation::streampack_capacity,true>;
using CompactDirect=write_arch::multimap<W,b,V,true,0,write_arch::buffer_representation::streampack_capacity,true>;
using WholeLegacy=write_arch::multimap<W,b,V,true,(V==8?2:1),write_arch::buffer_representation::streampack_capacity,false,false>;
constexpr unsigned owner_count=V==64?11:6;
constexpr std::array<std::string_view,11> owner_names={"maintained","prior_v74","packed_inline","whole_inline","abseil","whole_direct","packed_compact","whole_compact","whole_prefix_diagnostic","compact_direct","whole_legacy_writer"};
void check_owner(std::string_view name){
#if WRITE_ARCH_W == 24 && !WRITE_ARCH_MM_FOCUSED
    ht::require(name.empty()||std::find(owner_names.begin(),owner_names.begin()+owner_count,name)!=owner_names.begin()+owner_count,"selected multimap owner");
#else
    ht::require(name.empty()||name=="packed_inline"||name=="abseil","wide-key multimap owner");
#endif
}
using Bag=absl::InlinedVector<Value,1>;
using Abseil=absl::flat_hash_map<Key,Bag>;
word bits(word x){x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);}
word key(word rank,word seed){word x=(rank^seed)&ht::mask<W>;x^=x>>(W/2);x=x*0xbf58476d1ce4e5b9ULL&ht::mask<W>;x^=x>>(W/3);x=x*0x94d049bb133111ebULL&ht::mask<W>;return x^(x>>(W/2));}
Value payload(word k,unsigned ordinal){Value out{};for(unsigned i=0;i<V;i+=8)ht::store<8>(out.data()+i,bits(k+word(ordinal/2)*65537+i*131));return out;}
word digest(const Value& value){word sum=0;for(unsigned i=0;i<V;i+=8)sum+=ht::load<8>(value.data()+i);return sum;}
unsigned fanout(std::size_t rank,unsigned fixed){if(fixed)return fixed;const auto x=bits(rank+91)%100;return x<75?1:x<90?2:x<96?4:x<99?16:64;}
struct operation {word key;Value value;};
struct input {
    std::size_t n;word seed;unsigned fixed;std::string order;
    std::vector<word> keys,next_keys;
    std::vector<std::vector<Value>> values,next_values;
    std::vector<operation> append,refill;
    std::vector<unsigned> shuffled;
    input(std::size_t count,word s,unsigned f,std::string o):n(count),seed(s),fixed(f),order(std::move(o)),keys(n),next_keys(n),values(n),next_values(n),shuffled(n){
        ht::require(n<=Packed::bucket_count*Packed::N&&n>=2&&fixed<=64&&(order=="key"||order=="round"),"multimap benchmark input");
        for(unsigned i=0;i<n;++i){keys[i]=key(i,seed);next_keys[i]=i%2?keys[i]:key(n+i,seed);
            const auto count=fanout(i,fixed);for(unsigned j=0;j<=count;++j)values[i].push_back(payload(keys[i],j));
            for(unsigned j=0;j<count;++j)next_values[i].push_back(payload(next_keys[i],j));
        }
        std::iota(shuffled.begin(),shuffled.end(),0);std::mt19937_64 rng(seed);std::shuffle(shuffled.begin(),shuffled.end(),rng);
        if(order=="key")for(auto i:shuffled)for(unsigned j=1;j+1<values[i].size();++j)append.push_back({keys[i],values[i][j]});
        else for(unsigned j=1;j<64;++j)for(auto i:shuffled)if(j+1<values[i].size())append.push_back({keys[i],values[i][j]});
        // Refills are scalar calls too: each value pays its real lookup.
        for(auto i:shuffled)if(i%2==0)for(const auto& v:next_values[i])refill.push_back({next_keys[i],v});
    }
};
template<class Owner> [[gnu::always_inline]] void append(Owner& owner,word k,const Value& value){
    if constexpr(std::is_same_v<Owner,Abseil>)owner[Key(k)].push_back(value);
    else owner.insert(k,value);
}
template<class Owner,class Consume> [[gnu::always_inline]] bool find(const Owner& owner,word k,Consume&& consume){
    if constexpr(std::is_same_v<Owner,Abseil>){const auto it=owner.find(Key(k));if(it==owner.end())return false;for(const auto& v:it->second)consume(v);return true;}
    else return owner.find(k,std::forward<Consume>(consume));
}
template<class Owner> std::size_t bytes(const Owner& owner){
    if constexpr(std::is_same_v<Owner,Abseil>){std::size_t total=absl::container_internal::AllocatedByteSize(owner);
        for(const auto& [k,bag]:owner){const auto p=reinterpret_cast<std::uintptr_t>(bag.data()),base=reinterpret_cast<std::uintptr_t>(&bag);
            if(p<base||p>=base+sizeof(bag))total+=bag.capacity()*V;}
        return total;
    }else return owner.bytes_used();
}
template<class Owner> void verify(const Owner& owner,const input& in,unsigned stage){
    ht::require(owner.size()==in.n,"multimap history key count");
    for(unsigned i=0;i<in.n;++i){const bool changed=stage==3&&i%2==0;const auto k=changed?in.next_keys[i]:in.keys[i];
        const auto& expected=changed?in.next_values[i]:in.values[i];
        const auto wanted=changed?expected.size():stage==0?1:stage==1?expected.size()-1:expected.size();unsigned count=0;
        const bool hit=find(owner,k,[&](const Value& value){ht::require(count<wanted&&value==expected[count++],"multimap full ordered payload");});
        ht::require(hit&&count==wanted,"multimap full multiplicity");
        if(changed)ht::require(!find(owner,in.keys[i],[](const Value&){}),"multimap erased key absent");
    }
}
template<class Owner> [[gnu::noinline]] void run(const char* name,const input& in,unsigned rep){
    using clock=std::chrono::steady_clock;auto elapsed=[](auto start){return std::chrono::duration<double,std::nano>(clock::now()-start).count();};
    std::unique_ptr<Owner> owner;
    auto report=[&](const char* phase,double ns,std::size_t operations){
        std::cout<<name<<','<<phase<<','<<V<<','<<in.n<<','<<in.seed<<','<<rep<<','<<in.fixed<<','<<in.order<<','<<operations<<','<<ns<<','<<ns/operations<<','<<(owner?bytes(*owner):0)<<','<<W<<','<<b<<','<<K<<'\n';
    };
    auto start=clock::now();
    if constexpr(std::is_same_v<Owner,Abseil>){owner=std::make_unique<Owner>();owner->reserve(in.n);}
    else owner=std::make_unique<Owner>(bits(in.seed));
    for(unsigned i=0;i<in.n;++i)append(*owner,in.keys[i],in.values[i][0]);
    report("build_singletons",elapsed(start),in.n);verify(*owner,in,0);
    start=clock::now();for(const auto& op:in.append)append(*owner,op.key,op.value);
    report("append",elapsed(start),in.append.size());verify(*owner,in,1);
    auto reads=[&](const char* label,unsigned stage){word expected=0;for(unsigned i=0;i<in.n;++i){const auto count=in.values[i].size()-(stage==1);for(unsigned j=0;j<count;++j)expected+=digest(in.values[i][j]);}
        word sum=0,hits=0;start=clock::now();for(auto i:in.shuffled)hits+=find(*owner,in.keys[i],[&](const Value& value){sum+=digest(value);});
        report(label,elapsed(start),in.n);ht::require(hits==in.n&&sum==expected,"multimap timed complete consumer");};
    reads("read_before_boundary",1);
    start=clock::now();for(auto i:in.shuffled)append(*owner,in.keys[i],in.values[i].back());
    report("append_boundary",elapsed(start),in.n);verify(*owner,in,2);reads("read_after_boundary",2);
    start=clock::now();std::size_t erased=0;for(auto i:in.shuffled)if(i%2==0)erased+=owner->erase(Key(in.keys[i]));
    report("erase",elapsed(start),(in.n+1)/2);ht::require(erased==(in.n+1)/2,"multimap erase count");
    start=clock::now();for(const auto& op:in.refill)append(*owner,op.key,op.value);
    report("refill",elapsed(start),in.refill.size());verify(*owner,in,3);
    if constexpr(!std::is_same_v<Owner,Abseil>)owner->validate();
    start=clock::now();owner.reset();report("destroy",elapsed(start),in.n);
}
extern "C" [[gnu::noinline]] void packed_mm_append(Packed* p,word key,const Value* value){p->insert(key,*value);}
extern "C" [[gnu::noinline]] void whole_mm_append(Whole* p,word key,const Value* value){p->insert(key,*value);}
extern "C" [[gnu::noinline]] void abseil_mm_append(Abseil* p,word key,const Value* value){append(*p,key,*value);}
}
#include "multimap-lifetime.h"
int main(int argc,char** argv){
    using namespace mm_study;
    try {
        if((argc==3||argc==5)&&std::string_view(argv[1])=="lifetime")return lifetimes(std::stoul(argv[2]),argc==5?argv[3]:"",argc==5?unsigned(std::stoul(argv[4])):0);
        ht::require(argc==6||argc==8,"usage: write_arch_mm_V N SEED FANOUT(0=mixed) ORDER REPEATS [OWNER REP_BASE]");
        input in(std::stoull(argv[1]),std::stoull(argv[2]),std::stoul(argv[3]),argv[4]);const unsigned reps=std::stoul(argv[5]);
        const std::string_view only=argc==8?argv[6]:"";check_owner(only);
        const auto rep_base=argc==8?unsigned(std::stoul(argv[7])):0;
        ht::require(reps>0&&!in.append.empty(),"nonempty history");
        std::cout<<"implementation,phase,V,n,seed,rep,fanout,order,operations,total_ns,ns,bytes,W,b,K\n";
        for(unsigned trial=0;trial<reps;++trial){const auto rep=rep_base+trial;
          for(unsigned i=0;i<owner_count;++i){const auto which=(i+rep+in.seed)%owner_count;
          if(!only.empty()&&owner_names[which]!=only)continue;
          switch(which){
#if WRITE_ARCH_W == 24 && !WRITE_ARCH_MM_FOCUSED
          case 0:run<Maintained>("maintained",in,rep);break;
          case 1:run<Prior>("prior_v74",in,rep);break;
#endif
          case 2:run<Packed>("packed_inline",in,rep);break;
#if WRITE_ARCH_W == 24 && !WRITE_ARCH_MM_FOCUSED
          case 3:run<Whole>("whole_inline",in,rep);break;
#endif
          case 4:run<Abseil>("abseil",in,rep);break;
#if WRITE_ARCH_W == 24 && !WRITE_ARCH_MM_FOCUSED
          case 5:run<WholeDirect>("whole_direct",in,rep);break;
          case 6:if constexpr(V==64)run<PackedCompact>("packed_compact",in,rep);break;
          case 7:if constexpr(V==64)run<WholeCompact>("whole_compact",in,rep);break;
          case 8:if constexpr(V==64)run<WholePrefix>("whole_prefix_diagnostic",in,rep);break;
          case 9:if constexpr(V==64)run<CompactDirect>("compact_direct",in,rep);break;
          case 10:if constexpr(V==64)run<WholeLegacy>("whole_legacy_writer",in,rep);break;
#endif
        }}}
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
