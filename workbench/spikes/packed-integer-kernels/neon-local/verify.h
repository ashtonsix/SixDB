#pragma once
#include "coalescing.h"
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
namespace candidate {
struct page {
    std::size_t n=std::size_t(sysconf(_SC_PAGESIZE));
    std::uint8_t* p=static_cast<std::uint8_t*>(mmap(nullptr,3*n,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    page(){if(p==MAP_FAILED || mprotect(p+n,n,PROT_READ|PROT_WRITE))std::abort();}
    ~page(){munmap(p,3*n);}
};
template<unsigned W> void verify64(){
    std::array<std::uint8_t,64> input{},wanted{},decoded{};
    std::array<std::uint8_t,8*W> expected{},encoded{};
    const auto check=[&](const std::uint8_t* in,std::uint8_t* wire,std::uint8_t* out){
        expected.fill(0);
        for(unsigned i=0;i<64;++i){wanted[i]=in[i]&((1u<<W)-1);for(unsigned bit=0;bit<W;++bit)expected[i/8*W+bit]|=((in[i]>>bit)&1u)<<(i%8);}
        encode64<W>(in,wire);
        if(!std::equal(expected.begin(),expected.end(),wire)){std::fprintf(stderr,"coalesce encode W%u\n",W);std::abort();}
        decode64<W>(expected.data(),out);
        if(!std::equal(wanted.begin(),wanted.end(),out)){std::fprintf(stderr,"coalesce decode W%u\n",W);std::abort();}
        decode64<W>(wire,out);
        if(!std::equal(wanted.begin(),wanted.end(),out))std::abort();
    };
    check(input.data(),encoded.data(),decoded.data());
    input.fill(255);check(input.data(),encoded.data(),decoded.data());input.fill(0);
    for(unsigned i=0;i<64;++i)for(unsigned bit=0;bit<8;++bit){input[i]=1u<<bit;check(input.data(),encoded.data(),decoded.data());input[i]=0;}
    std::uint64_t s=0xc7482b131b22332fULL;
    for(unsigned trial=0;trial<32;++trial){for(auto&v:input){s^=s>>12;s^=s<<25;s^=s>>27;v=s*0x2545f4914f6cdd1dULL;}check(input.data(),encoded.data(),decoded.data());}
    auto exact_in=std::make_unique<std::uint8_t[]>(64),exact_out=std::make_unique<std::uint8_t[]>(64),exact_wire=std::make_unique<std::uint8_t[]>(8*W);
    std::copy(input.begin(),input.end(),exact_in.get());check(exact_in.get(),exact_wire.get(),exact_out.get());
    for(unsigned offset=0;offset<64;++offset){
        std::array<std::uint8_t,256> ins{},outs{},wire{};outs.fill(0xa5);wire.fill(0xa5);
        std::copy(input.begin(),input.end(),ins.data()+32+offset);
        check(ins.data()+32+offset,wire.data()+32+offset,outs.data()+32+offset);
        for(unsigned i=0;i<256;++i){if((i<32+offset||i>=32+offset+64)&&outs[i]!=0xa5)std::abort();if((i<32+offset||i>=32+offset+8*W)&&wire[i]!=0xa5)std::abort();}
    }
    page a,b,c;
    for(auto in:{a.p+a.n,a.p+2*a.n-64})for(auto wire:{b.p+b.n,b.p+2*b.n-8*W})for(auto out:{c.p+c.n,c.p+2*c.n-64}){
        std::copy(input.begin(),input.end(),in);check(in,wire,out);
    }
}
}
