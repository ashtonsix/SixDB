#include "codec.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <random>
#include <sys/mman.h>
#include <unistd.h>

using namespace ikea::integers;
int main() {
    std::mt19937_64 rng(0x69238752);
    uint64_t cases=0, points=0;
    for(auto l:{Layout::local,Layout::scan}) for(unsigned k=1;k<=7;++k) {
        const auto& c=codecs(l)[k-1];
        for(unsigned trial=0;trial<1024;++trial) {
            std::array<uint8_t,256> in{},out{},part{};
            std::array<uint8_t,320> packed{},expected{};
            packed.fill(0xA7);expected.fill(0xA7);
            for(unsigned i=0;i<256;++i) in[i]=(trial<128?trial:trial<256?i:rng())&((1u<<k)-1);
            const unsigned offset=trial%64;
            c.encode(in.data(),packed.data()+offset);
            oracle_encode(l,k,in.data(),expected.data()+offset);
            assert(packed==expected);
            c.decode(packed.data()+offset,out.data()); assert(in==out);
            for(unsigned i=0;i<256;++i) { assert(c.get1(packed.data()+offset,i)==in[i]); ++points; }
            for(unsigned i=0;i<256;i+=16) c.get16(packed.data()+offset,i,part.data()+i);
            assert(in==part);
#if IP_HAS_PRIOR
            const auto& old=prior_codecs(l)[k-1];
            std::array<uint8_t,224> old_packed{};
            old.encode(in.data(),old_packed.data());
            old.decode(old_packed.data(),out.data()); assert(in==out);
            for(unsigned i=0;i<256;++i) assert(old.get1(old_packed.data(),i)==in[i]);
            if(l==Layout::local || (k!=5&&k!=7)) assert(std::equal(old_packed.begin(),old_packed.begin()+32*k,packed.begin()+offset));

#endif
            ++cases;
        }
        const size_t page=size_t(sysconf(_SC_PAGESIZE));
        auto* memory=static_cast<uint8_t*>(mmap(nullptr,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        assert(memory!=MAP_FAILED); assert(mprotect(memory+page,page,PROT_READ|PROT_WRITE)==0);
        for(unsigned end=0;end<2;++end) {
            auto* packed=memory+page+(end?page-32*k:0);
            std::array<uint8_t,256> in{},out{};
            for(auto& v:in) v=rng()&((1u<<k)-1);
            c.encode(in.data(),packed); c.decode(packed,out.data()); assert(in==out);
            for(unsigned i=0;i<256;++i) assert(c.get1(packed,i)==in[i]);
            for(unsigned i=0;i<256;i+=16) c.get16(packed,i,out.data()+i);
            assert(in==out);
        }
        munmap(memory,page*3);
    }
    std::printf("codec_cases,point_reads\n%llu,%llu\n",(unsigned long long)cases,(unsigned long long)points);
}
