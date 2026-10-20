#include <ikea/boundlist/search.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <string_view>
#include <vector>
namespace bl=ikea::boundlist;
using u64=std::uint64_t;
std::string key(u64 ordinal,unsigned bytes,bool late) {
    std::string s(bytes,late?'x':'\0');
    const unsigned offset=late?bytes-8:0;
    for(unsigned b=0;b<8;++b)s[offset+b]=char(ordinal>>(56-b*8));
    return s;
}
struct Keys {
    const std::vector<std::string>* values;
    unsigned size()const{return values->size();}
    std::string_view at(unsigned i)const{return (*values)[i];}
};
struct Virtual {
    unsigned count;u64 origin{},stride{1};
    unsigned size()const{return count;}
    u64 at(unsigned i)const{assert(i<count);return origin+u64(i)*stride;}
};
void check() {
    for(unsigned n:{0,1,17,256,4096})for(unsigned bytes:{8,32,256})for(bool late:{false,true}) {
        std::vector<std::string> storage;for(unsigned i=0;i<n;++i)storage.push_back(key((i/17)*2+1,bytes,late));Keys source{&storage};
        for(unsigned q=0;q<n+4;++q) {
            auto query=key(q,bytes,late);unsigned expected=0;while(expected<n && std::string_view(storage[expected])<query)++expected;
            assert(bl::lower_bound(source,std::string_view(query))==expected);
        }
    }
    const Virtual maximum{UINT_MAX};
    for(u64 q:std::array<u64,5>{0,1,u64(UINT_MAX)-1,u64(UINT_MAX),UINT64_MAX}) {
        assert(bl::lower_bound(maximum,q)==std::min<u64>(q,UINT_MAX));
    }
    // Entire 2^32-prefix layer, without allocating it or narrowing a global
    // count into one source. Outer pages and local ranks have distinct sentinels.
    const Virtual pages{4096,(u64(1)<<20)-1,u64(1)<<20};
    for(u64 q:std::array<u64,5>{0,(u64(1)<<20)-1,u64(1)<<20,(u64(1)<<32)-1,u64(1)<<32}) {
        const auto page=bl::lower_bound(pages,q);u64 global=u64(1)<<32;
        if(page<pages.size()) {
            const Virtual local{1u<<20,u64(page)<<20};const auto rank=bl::lower_bound(local,q);global=(u64(page)<<20)+rank;
        }
        assert(global==q);
    }
    std::puts("key oracle: full byte comparisons, binary zeros, long common prefixes, repeated bounds, UINT_MAX local sentinel and 2^32-segment outer coordinates passed");
}
int main(int argc,char** argv) {
    if(argc>1 && std::string_view(argv[1])=="--check"){check();return 0;}
    const unsigned ops=argc>1?std::stoul(argv[1]):32768,reps=argc>2?std::stoul(argv[2]):3;
    std::puts("phase,entries,key_bytes,duplicates,trace,projection,arm,trial,operations,ns_per_operation,checksum,key_content_bytes,key_object_bytes,payload_bytes");
    for(unsigned n:{256,4096,65536})for(unsigned width:{8,32,256})for(unsigned duplicates:{1,17})for(bool late:{false,true}) {
        if(width==8 && late)continue;
        std::vector<std::string> storage;storage.reserve(n);for(unsigned i=0;i<n;++i)storage.push_back(key((i/duplicates)*2+1,width,late));Keys source{&storage};
        std::vector<u64> values(n);for(unsigned i=0;i<n;++i)values[i]=i*31;
        std::vector<std::string> queries;queries.reserve(ops);std::mt19937_64 rng(159);
        for(unsigned i=0;i<ops;++i) {auto rank=rng()%n;if(i%10)rank%=std::min(n,256u);queries.push_back(key((rank/duplicates)*2+(i%4?1:2),width,late));}
        for(unsigned trial=0;trial<reps;++trial) {
            std::array<u64,2> results{};
            for(unsigned offset=0;offset<2;++offset) {
                const auto arm=(trial+offset)%2;u64 sum=0;const auto start=std::chrono::steady_clock::now();
                for(const auto& query:queries) {
                    const std::string_view q=query;
                    const unsigned rank=arm?bl::lower_bound(source,q):std::lower_bound(storage.begin(),storage.end(),q,[](const auto& a,auto b){return std::string_view(a)<b;})-storage.begin();
                    sum+=rank<n?values[rank]:97;
                }
                asm volatile("" : "+r"(sum) : : "memory");results[arm]=sum;
                const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/ops;
                std::printf("byte-keys,%u,%u,%u,%s,1,%s,%u,%u,%.3f,%llu,%zu,%zu,%zu\n",n,width,duplicates,late?"hot-late-prefix":"hot-binary-prefix",arm?"boundlist":"std-lower-bound",trial,ops,ns,
                    (unsigned long long)sum,std::size_t(n)*width,std::size_t(n)*sizeof(std::string),std::size_t(n)*8);
            }
            assert(results[0]==results[1]);
        }
    }
}
