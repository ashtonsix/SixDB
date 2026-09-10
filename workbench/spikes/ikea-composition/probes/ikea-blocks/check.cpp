#include "codec.h"
#include "blocks.h"
#include "tiling.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__aarch64__)
#include "native_neon.h"
#else
#include "native_avx512.h"
#endif
#if IKEA_HAVE_PRIOR
extern "C" unsigned prior_encode(const unsigned char*,unsigned,unsigned char*);
extern "C" void prior_decode(const unsigned char*,unsigned,unsigned char*);
#if defined(__AVX512VBMI__)
extern "C" unsigned prior_encode512(const unsigned char*,unsigned,unsigned char*);
extern "C" void prior_decode512x2(const unsigned char*,unsigned,const unsigned char*,unsigned,unsigned char*);
#endif
#endif
using namespace ikea_probe;
std::uint64_t cases = 0;
void require(bool test, const char* message) { if (!test) throw std::runtime_error(message); }
void check(const std::array<std::uint8_t,32>& in) {
    unsigned population = 0;
    for (auto b:in) population += std::popcount(b);
    std::array<std::uint8_t,96> a{}, reference{};
    std::array<std::uint8_t,32> decoded{};
    auto bits = encode_reference(in.data(),reference.data());
    a.fill(0xa5);
    auto size = encode_native(in.data(),population,a.data());
    require(size == (bits+7)/8,"encoded size");
    require(std::equal(a.begin(),a.begin()+size,reference.begin()),"encoded bytes");
    require(std::all_of(a.begin()+64,a.end(),[](auto v){return v==0xa5;}),"write footprint");
    auto validation = validate(std::span(a).first(size),population);
    require(validation.error == Invalid::none && validation.bits == bits,"valid admission");
    require(decode_native(a.data(),population,decoded.data()) == bits,"decoded extent");
    require(in==decoded,"decoded value");
    if(cases%31==0) {
        std::array<std::uint8_t,32> other{};
        unsigned other_pop=0;
        for(unsigned j=0;j<32;++j) {
            other[j]=std::uint8_t(in[j]^std::uint8_t(17*j+cases));
            other_pop+=std::popcount(other[j]);
        }
        std::array<std::uint8_t,96> other_body{};
        encode_reference(other.data(),other_body.data());
        std::array<std::uint8_t,64> two{};
        decode_native2(a.data(),population,other_body.data(),other_pop,two.data());
        require(std::equal(in.begin(),in.end(),two.begin()) && std::equal(other.begin(),other.end(),two.begin()+32),"two-stream native decode");
    }
#if IKEA_HAVE_PRIOR
    std::array<std::uint8_t,96> b{};
    auto prior_size = prior_encode(in.data(),population,b.data());
    require(prior_size==size && std::equal(a.begin(),a.begin()+size,b.begin()),"Calico fast encode");
    prior_decode(a.data(),population,decoded.data());
    require(in==decoded,"Calico fast decode");
#if defined(__AVX512VBMI__)
    auto p2size = prior_encode512(in.data(),population,b.data());
    require(p2size==size && std::equal(a.begin(),a.begin()+size,b.begin()),"Calico p2 encode");
    std::array<std::uint8_t,64> two{};
    prior_decode512x2(a.data(),population,b.data(),population,two.data());
    require(std::equal(in.begin(),in.end(),two.begin()) && std::equal(in.begin(),in.end(),two.begin()+32),"Calico pair decode");
#endif
#endif
    // Exact admission reads no slack, and rejects each truncated prefix.
    if (cases < 512 || cases%127 == 0) {
        for (unsigned n=0;n<size;++n)
            require(validate(std::span(a).first(n),population).error != Invalid::none,"truncated admission");
        if (bits%8) {
            a[size-1] |= 0x80;
            require(validate(std::span(a).first(size),population).error==Invalid::padding,"padding admission");
        }
    }
    cases++;
}
int main() {
    try {
        std::mt19937_64 rng(0xbec25609);
        std::array<std::uint8_t,32> in{};
        for(unsigned position=0;position<256;++position) {
            in.fill(0); in[position/8]=std::uint8_t(1u<<(position%8));
            check(in);
            for(auto& byte:in) byte=std::uint8_t(~byte);
            check(in);
        }
        for (unsigned n=0;n<65536;++n) {
            in.fill(0); in[15]=n; in[16]=n>>8;
            check(in);
        }
        for (unsigned population=0;population<=256;++population) {
            for (unsigned repeat=0;repeat<16;++repeat) {
                std::array<unsigned,256> permutation{};
                for (unsigned i=0;i<256;++i) permutation[i]=i;
                for (unsigned i=255;i>0;--i) std::swap(permutation[i],permutation[rng()%(i+1)]);
                in.fill(0);
                for (unsigned i=0;i<population;++i) in[permutation[i]/8] |= 1u<<(permutation[i]%8);
                check(in);
            }
            in.fill(0);
            unsigned start=rng()%256;
            for(unsigned i=0;i<population;++i) in[((start+i)%256)/8] |= 1u<<((start+i)%8);
            check(in);
        }
        // Native compute is independent of PlainBits/Tiles. The same 512-position
        // body operates on a flat bitset and on two adjacent 256-position children.
        for (unsigned rep=0;rep<1024;++rep) {
            PlainBits<512> a,b;
            for(auto& w:a.words) w=rng(); for(auto& w:b.words) w=rng();
            PlainBits<512> both,either;
#if defined(__aarch64__)
            for(unsigned i=0;i<2;++i) {
                auto x=neon::load256(a.words.data()+4*i), y=neon::load256(b.words.data()+4*i);
                neon::store256(both.words.data()+4*i,neon::intersection(x,y));
                neon::store256(either.words.data()+4*i,neon::set_union(x,y));
            }
#else
            avx512::store512(both.words.data(),avx512::intersection512(avx512::load512(a.words.data()),avx512::load512(b.words.data())));
            avx512::store512(either.words.data(),avx512::union512(avx512::load512(a.words.data()),avx512::load512(b.words.data())));
#endif
            for(unsigned i=0;i<8;++i) require(both.words[i]==(a.words[i]&b.words[i]) && either.words[i]==(a.words[i]|b.words[i]),"native algebra");
        }
        auto tiled_check=[&]<std::size_t N>() {
            PlainBits<N> flat,mask,expected;
            Tiles<PlainBits<64>,N/64> tiled;
            for(std::size_t i=0;i<N/64;++i) {
                flat.words[i]=rng(); mask.words[i]=rng();
                tiled.children[i].words[0]=flat.words[i];
                expected.words[i]=flat.words[i]&mask.words[i];
            }
            bitwise_contiguous<Bitwise::intersection,N>(&tiled,&mask,&tiled);
            require(std::memcmp(&tiled,&expected,sizeof(expected))==0,"tiled intersection substitution");
            bitwise_contiguous<Bitwise::set_union,N>(&flat,&mask,&flat);
            for(std::size_t i=0;i<N/64;++i) require((flat.words[i]&mask.words[i])==mask.words[i],"tiled union");
        };
        tiled_check.template operator()<64>();
        tiled_check.template operator()<256>();
        tiled_check.template operator()<512>();
        tiled_check.template operator()<4096>();
        tiled_check.template operator()<65536>();
        // Deliberately invalid values, beyond truncation and padding checks.
        std::array<std::uint8_t,2> invalid{};
        require(validate({},257).error==Invalid::cardinality,"root bound admission");
        invalid[0]=3; // root population 2 has only 0,1,2 left-population choices
        require(validate(invalid,2).error==Invalid::split,"split bound admission");
        in.fill(0); in[0]=3;
        std::array<std::uint8_t,64> bad_rank{};
        auto rank_bits=encode_reference(in.data(),bad_rank.data());
        require(rank_bits==15,"two-bit oracle extent");
        bad_rank[1]|=0x7c; // five-bit byte rank 31 is outside C(8,2)=28
        require(validate(std::span(bad_rank).first(2),2).error==Invalid::rank,"rank bound admission");
        require(validate(std::span(bad_rank).first(1),0).error==Invalid::trailing,"trailing body admission");
        auto page=std::size_t(sysconf(_SC_PAGESIZE));
        auto* mapping=static_cast<std::uint8_t*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        require(mapping!=MAP_FAILED,"mmap");
        require(mprotect(mapping+page,page,PROT_NONE)==0,"mprotect");
        in.fill(0x0f);
        auto* body=mapping+page-64;
        encode_native(in.data(),128,body);
        std::array<std::uint8_t,32> out{};
        require(decode_native(body,128,out.data())==374 && out==in,"64 readable boundary");
        unsigned size=encode_native(in.data(),128,body);
        std::memmove(mapping+page-size,body,size);
        require(validate({mapping+page-size,size},128).error==Invalid::none,"exact admitted boundary");
        munmap(mapping,page*2);
        std::printf("metric,value\ncodec_cases,%llu\nalgebra_cases,1024\nstatus,pass\n",static_cast<unsigned long long>(cases));
    } catch(const std::exception& e) { std::fprintf(stderr,"case %llu: %s\n",static_cast<unsigned long long>(cases),e.what()); return 1; }
}
