#include "mm-grow.h"
#include <absl/container/flat_hash_map.h>
#include <absl/container/inlined_vector.h>
#include <absl/container/internal/hashtable_debug.h>
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>

namespace study {
namespace ht=ikea::hashtable;
using ht::word;
using ht::byte;
constexpr unsigned K=REBUILD_K,b=REBUILD_B,W=K+b,V=REBUILD_V;
using Value=ht::bytes<V>;
using Key=std::conditional_t<W<=32,std::uint32_t,std::uint64_t>;
using Source=write_arch::multimap<W,b,V,false>;
using Next=write_arch::multimap<W,b+1,V,false>;
using Abseil=absl::flat_hash_map<Key,std::vector<Value>>;
using AbseilInline=absl::flat_hash_map<Key,absl::InlinedVector<Value,1>>;
template<class Owner> constexpr bool is_abseil=std::is_same_v<Owner,Abseil>||std::is_same_v<Owner,AbseilInline>;
using Build=write_arch::multimap_rebuild;
using clock=std::chrono::steady_clock;
word bits(word x){x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);}
word key(word rank,word seed){word x=(rank^seed)&ht::mask<W>;x^=x>>(W/2);x=x*0xbf58476d1ce4e5b9ULL&ht::mask<W>;
    x^=x>>(W/3);return x*0x94d049bb133111ebULL&ht::mask<W>;}
Value payload(word k,unsigned rank){Value out{};for(unsigned i=0;i<V;++i)out[i]=byte(bits(k+rank*65537+i*131));return out;}
unsigned length(word key){constexpr unsigned n[]={1,1,2,3,7,8,9,17,33,65};return n[bits(key)%10];}
word digest(const Value& v){word sum=0;for(unsigned i=0;i<V;i+=8)sum+=ht::load<8>(v.data()+i);return sum;}
template<class Owner> void insert(Owner& owner,word key,const Value& value){
    if constexpr(is_abseil<Owner>)owner[Key(key)].push_back(value);else owner.insert(key,value);
}
template<class Owner,class F> bool find(const Owner& owner,word key,F&& consume){
    if constexpr(is_abseil<Owner>){const auto it=owner.find(Key(key));if(it==owner.end())return false;
        for(const auto& value:it->second)consume(value);return true;}
    else return owner.find(key,std::forward<F>(consume));
}
template<class Owner> std::size_t bytes(const Owner& owner){
    if constexpr(is_abseil<Owner>){auto total=absl::container_internal::AllocatedByteSize(owner);
        for(const auto& entry:owner){const auto p=reinterpret_cast<std::uintptr_t>(entry.second.data()),base=reinterpret_cast<std::uintptr_t>(&entry.second);
            if(p<base||p>=base+sizeof(entry.second))total+=entry.second.capacity()*V;}return total;}
    else return owner.bytes_used();
}
template<class Owner> std::size_t spill(const Owner& owner){if constexpr(is_abseil<Owner>)return 0;else return owner.spill_size();}
struct fixture {
    word seed;unsigned rep;std::vector<word> keys;std::vector<unsigned> order;word expected=0;std::size_t values=0;
    fixture(word s,unsigned r):seed(s),rep(r){
        const auto n=Source::bucket_count*Source::N*85/100;
        for(std::size_t i=0;i<n;++i){keys.push_back(key(i,s));const auto k=keys.back();values+=length(k);
            for(unsigned rank=0;rank<length(k);++rank)expected+=digest(payload(k,rank));}
        order.resize(n);std::iota(order.begin(),order.end(),0);std::mt19937_64 rng(s);std::shuffle(order.begin(),order.end(),rng);
    }
    template<class Owner> void populate(Owner& owner)const{
        for(const auto key:keys)for(unsigned rank=0;rank<length(key);++rank)insert(owner,key,payload(key,rank));
    }
    template<class Owner> void verify(const Owner& owner)const{
        ht::require(owner.size()==keys.size(),"MM growth distinct count");
        if constexpr(!is_abseil<Owner>)owner.validate();
        for(const auto key:keys){unsigned rank=0;ht::require(find(owner,key,[&](const Value& v){
            ht::require(rank<length(key)&&v==payload(key,rank),"MM growth complete ordered value");++rank;}),"MM growth membership");
            ht::require(rank==length(key),"MM growth ordered length");}
    }
    template<class Owner> void report(const char* mode,const char* phase,const Owner& owner,double ns,std::size_t operations,std::size_t source_bytes)const{
        std::cout<<mode<<','<<phase<<','<<W<<','<<b<<','<<K<<','<<V<<','<<seed<<','<<rep<<','<<keys.size()<<','<<values<<','<<operations<<','<<ns<<','<<ns/operations<<','<<bytes(owner)<<','<<source_bytes<<','<<spill(owner)<<'\n';
    }
    template<class Owner> void reads(const char* mode,const Owner& owner,std::size_t source_bytes)const{
        word sum=0,hits=0;std::size_t count=0;const auto begin=clock::now();
        for(unsigned pass=0;pass<2;++pass)for(auto row:order)hits+=find(owner,keys[row],[&](const Value& v){sum+=digest(v);++count;});
        const auto ns=std::chrono::duration<double,std::nano>(clock::now()-begin).count();
        ht::require(hits==keys.size()*2&&sum==expected*2&&count==values*2,"MM growth timed ordered consumer");
        report(mode,"read_after",owner,ns,keys.size()*2,source_bytes);
    }
};
void candidate(const char* mode,const fixture& in){
    auto source=std::make_unique<Source>(bits(in.seed));in.populate(*source);in.verify(*source);
    const auto source_bytes=bytes(*source);const auto begin=clock::now();std::unique_ptr<Next> target;
    auto finish=[&](const auto& result){
        const auto ns=std::chrono::duration<double,std::nano>(clock::now()-begin).count();
        in.report(mode,"grow",*result,ns,in.keys.size(),source_bytes);in.verify(*result);if(source)in.verify(*source);
        in.reads(mode,*result,source?source_bytes:0);
    };
    if(std::string_view(mode)=="image_clone"){auto result=Build::grow<true>(*source);finish(result);return;}
    if(std::string_view(mode)=="image_consume"){auto result=Build::grow_consuming<true>(source);finish(result);return;}
    if(std::string_view(mode)=="split_clone")target=Build::grow(*source);
    else if(std::string_view(mode)=="split_consume")target=Build::grow_consuming(source);
    else if(std::string_view(mode)=="enumerate_index")target=Build::enumerate<false>(*source);
    else target=Build::enumerate<true>(*source);
    finish(target);
}
template<class Owner=Abseil> void abseil(const char* mode,const fixture& in){
    auto source=std::make_unique<Owner>();source->reserve(in.keys.size());in.populate(*source);in.verify(*source);
    const auto source_bytes=bytes(*source);const auto begin=clock::now();std::unique_ptr<Owner> target;
    if(std::string_view(mode).ends_with("_rehash")){target=std::move(source);target->reserve(in.keys.size()*2);}
    else if(std::string_view(mode).ends_with("_clone")){target=std::make_unique<Owner>(*source);target->reserve(in.keys.size()*2);}
    else {target=std::make_unique<Owner>();target->reserve(in.keys.size()*2);for(const auto& entry:*source)target->emplace(entry.first,entry.second);}
    const auto ns=std::chrono::duration<double,std::nano>(clock::now()-begin).count();
    in.report(mode,"grow",*target,ns,in.keys.size(),source_bytes);in.verify(*target);if(source)in.verify(*source);
    in.reads(mode,*target,source?source_bytes:0);
}
}
int main(int argc,char** argv){using namespace study;ht::require(argc==4,"mode seed rep");
    const std::string_view mode=argv[1];fixture in(std::stoull(argv[2]),unsigned(std::stoul(argv[3])));
    std::cout<<"mode,phase,W,b,K,V,seed,rep,n,values,operations,ns,ns_per_op,bytes,source_bytes,spill\n";
    if(mode=="split_clone"||mode=="split_consume"||mode=="image_clone"||mode=="image_consume"||mode=="enumerate_index"||mode=="enumerate_values")candidate(argv[1],in);
    else if(mode=="abseil_clone"||mode=="abseil_rehash"||mode=="abseil_insert")abseil(argv[1],in);
    else if(mode=="abseil_inline_clone"||mode=="abseil_inline_rehash"||mode=="abseil_inline_insert")abseil<AbseilInline>(argv[1],in);
    else ht::require(false,"known MM growth mode");
}
