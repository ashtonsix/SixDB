#include "single-plans.h"
#include <random>
#include <unordered_map>
#include <sys/mman.h>
#include <unistd.h>
using namespace hashtable_spike;
namespace one=hashtable_spike::single;
struct coverage {
    std::vector<one::extent> spans;
    bool allow=true;
    unsigned bucket_bytes{};
    bool prepare(std::span<const one::extent> s) {
        if(!allow)return false;
        if(bucket_bytes)spans.assign(1,one::extent{0,bucket_bytes});else spans.assign(s.begin(),s.end());
        return true;
    }
};
template<one::plan P,bool Direct> void check() {
    one::binding<P> shared;
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));require(P.bytes<page);
    auto* area=static_cast<byte*>(mmap(nullptr,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    require(area!=MAP_FAILED&&mprotect(area+page,page,PROT_NONE)==0);
    auto* data=area+page-P.bytes;std::memset(area,0xa7,page);
    one::kernel<P,Direct> table{data,shared};table.initialize();
    std::unordered_map<word,word> oracle;
    auto verify=[&] {
        require(table.size()==oracle.size());
        std::unordered_map<word,word> visited;
        table.each([&](word key,word value){require(visited.emplace(key,value).second);});
        require(visited==oracle);
        for(const auto& [key,value]:oracle){word out=~word{};require(table.get(key,out)&&out==value);}
        for(std::size_t i=0;i<page-P.bytes;++i)require(area[i]==0xa7);
    };
    unsigned mutations=0;
    auto mutate=[&](auto&& operation,bool allow=true) {
        std::vector<byte> before(data,data+P.bytes);coverage damage;damage.allow=allow;
        damage.bucket_bytes=(mutations++&1)?P.bytes:0;
        if(!allow)damage.spans.push_back({P.bytes+17,0});
        auto result=operation(damage);
        for(unsigned i=0;i<P.bytes;++i)if(before[i]!=data[i]) {
            require(allow);bool covered=false;
            for(const auto& span:damage.spans)covered|=i>=span.offset&&i<span.offset+span.bytes;
            require(covered);
        }
        if(!allow)require(damage.spans.size()==1&&damage.spans[0].offset==P.bytes+17&&damage.spans[0].bytes==0);
        if(!allow||result.result==one::status::grow_required) require(std::equal(before.begin(),before.end(),data));
        if(result.result==one::status::grow_required)require(damage.spans.empty());
        return result;
    };
    word miss=12345;require(!table.get(mask(P.key_bits),miss)&&miss==12345);verify();
    // All wide-key entries deliberately have the same255 selector, exercising
    // dense-prefix masking as well as full suffix verification.
    for(unsigned i=0;i<P.slots;++i) {
        const word key=P.key_bits==8?i:(word(i)<<8)|word(255^one::selector(i));
        const word value=(i&1)?mask(P.value_bits):0;
        auto r=mutate([&](auto& effects){return table.set(key,value,effects);});
        require(r.result==one::status::inserted&&r.distinct_delta==1);oracle[key]=value;
    }
    verify();
    const word existing=oracle.begin()->first;
    auto replaced=mutate([&](auto& effects){return table.set(existing,mask(P.value_bits),effects);});
    require(replaced.result==one::status::replaced&&replaced.distinct_delta==0);oracle[existing]=mask(P.value_bits);verify();
    const word extra=mask(P.key_bits);require(!oracle.contains(extra));
    auto full=mutate([&](auto& effects){return table.set(extra,0,effects);});
    require(full.result==one::status::grow_required&&full.distinct_delta==0);verify();
    if constexpr(P.value_bits) {
        auto failed=mutate([&](auto& effects){return table.set(existing,0,effects);},false);
        require(failed.result==one::status::refused&&failed.distinct_delta==0);verify();
    }
    auto failed=mutate([&](auto& effects){return table.erase(existing,effects);},false);
    require(failed.result==one::status::refused);verify();
    auto removed=mutate([&](auto& effects){return table.erase(existing,effects);});
    require(removed.distinct_delta==-1);oracle.erase(existing);verify();
    auto added=mutate([&](auto& effects){return table.set(extra,0,effects);});
    require(added.distinct_delta==1);oracle[extra]=0;verify();
    auto clear=mutate([&](auto& effects){return table.clear(effects);});
    require(clear.distinct_delta==-int(oracle.size()));oracle.clear();verify();
    std::mt19937_64 random(19181+P.key_bits);
    for(unsigned step=0;step<1200;++step) {
        const word key=mix(random()%64)&mask(P.key_bits),value=random()&mask(P.value_bits);
        if(random()%3) {
            const bool present=oracle.contains(key);const bool room=oracle.size()<P.slots;
            auto r=mutate([&](auto& effects){return table.set(key,value,effects);});
            require(r.result==(present?one::status::replaced:room?one::status::inserted:one::status::grow_required));
            require(r.distinct_delta==int(!present&&room));if(present||room)oracle[key]=value;
        } else {
            const bool present=oracle.erase(key);
            auto r=mutate([&](auto& effects){return table.erase(key,effects);});
            require(r.result==(present?one::status::erased:one::status::missing));require(r.distinct_delta==-int(present));
        }
        verify();
    }
    require(munmap(area,2*page)==0);
}
#define PAIR(K,V,S) check<one::k##K##v##V##_##S,false>();check<one::k##K##v##V##_##S,true>()
#define CHECK(K,V) PAIR(K,V,rows16);PAIR(K,V,planes16);PAIR(K,V,rows32);PAIR(K,V,planes32)
int main() {
    CHECK(8,64);CHECK(16,64);CHECK(24,0);CHECK(32,64);CHECK(40,16);CHECK(48,64);CHECK(56,64);CHECK(64,64);
    std::puts("single-bucket: all key widths, independent oracle, full/duplicate/refusal, selector255, guard boundary, swap deletion, clear, precise/bucket damage admission passed");
}
