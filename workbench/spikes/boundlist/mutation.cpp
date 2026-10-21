#include "mutation-fixture.h"
#include "legacy-read.h"
#include <ikea/streampack.h>
#include <ikea/effects.h>
#include "legacy-write.h"
#include <array>
#include <bit>
#include <cassert>
#include <cstdlib>
#include <string>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

struct effects {
    std::array<ikea::owner_write,8> entries;
    unsigned used=0;
    template<class S> [[gnu::always_inline]] void before(const S& s,ikea::byte_write w) { entries[used++]={&s,w}; }
    unsigned remaining() const { return entries.size()-used; }
};
template<unsigned Chains,bool Legacy,bool Append,class Access>
[[gnu::noinline]] word consume(fixture& f,unsigned rounds,Access prototype) {
    (void)prototype;
    std::array<word,Chains> salts;
    std::array<std::array<std::byte,32>,Chains> inputs;
    std::array<effects,Chains> logs;
    for(unsigned c=0;c<Chains;++c) salts[c]=mix(713+c*971);
    for(unsigned step=0;step<rounds;++step) {
        [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
            auto one=[&](auto index) __attribute__((always_inline)) {
                constexpr unsigned c=index;
                auto& input=inputs[c];
                const auto node=Append?c:unsigned(salts[c]>>32)&(f.nodes-1);
                auto& v=f.views[node];
                using A=std::conditional_t<Legacy,boundlist_before::author::access<Access::family,
                    typename Access::width_type,typename Access::offset_type,std::byte>,Access>;
                const A a(v); auto& log=logs[c];log.used=0;
                if constexpr(Append) if(!(step%256)) {
                    if constexpr(Legacy) [[clang::always_inline]] boundlist_legacy::initialize_unchecked(v,log);
                    else [[clang::always_inline]] sp::author::initialize_unchecked(v,log);
                    log.used=0;
                }
                const unsigned rank=Append?step%256:unsigned(salts[c])%256;
                word old=0;
                if constexpr(!Append) std::memcpy(&old,v.windows().payload.data()+a.start(rank)+8,8);
                const auto word0=salts[c]+old,word1=mix(salts[c]^old^step);
                std::memcpy(input.data(),&word0,8);std::memcpy(input.data()+8,&word1,8);
                std::memcpy(input.data()+16,&word0,8);std::memcpy(input.data()+24,&word1,8);
                const auto length=a.format().has_used()?16u+8u*(step%2):32u;
                const auto bytes=sp::bytes(input).first(length);
                if constexpr(Append) {
                    if constexpr(Legacy) [[clang::always_inline]] boundlist_legacy::append_unchecked(a,bytes,32,log);
                    else [[clang::always_inline]] sp::author::append_unchecked(a,bytes,32,log);
                } else {
                    if constexpr(Legacy) [[clang::always_inline]] boundlist_legacy::replace_unchecked(a,rank,bytes,log);
                    else [[clang::always_inline]] sp::author::replace_unchecked(a,rank,bytes,log);
                }
                word damage=log.used;
                for(unsigned i=0;i<log.used;++i) {
                    assert(log.entries[i].source==&v);
                    const auto w=log.entries[i].bytes;damage^=std::rotl(word(w.offset+w.size+w.plane),i*7);
                }
                salts[c]=mix(word1^damage^a.length(rank));
            };
            (one(std::integral_constant<unsigned,I>{}),...);
        }(std::make_index_sequence<Chains>{});
    }
    word result=0;for(auto x:salts)result^=x;
    return result;
}
volatile word observed;
template<unsigned Chains,bool Legacy,bool Append,class A>
void trial(fixture& f,unsigned rounds,unsigned rep,A a,word expected) {
    f.reset();
    const auto start=std::chrono::steady_clock::now();
    const auto result=consume<Chains,Legacy,Append>(f,rounds,a);
    const auto end=std::chrono::steady_clock::now();observed=result;
    assert(result==expected);
    for(const auto& v:f.views)assert(v.validate());
    std::printf("%s,%u,%u,%u,%s,%s,%u,%u,%.6f,%llu,%zu,%zu,%zu,%zu\n",
        A::family==sp::geometry::width?"width":"direct",f.format.schema().maximum,f.nodes,unsigned(f.format.has_used()),
        Legacy?"before":"production",Append?"append_with_reset_effects":"replace_with_effects",Chains,rep,
        double(std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count())/(rounds*Chains),
        (unsigned long long)result,std::size_t(f.nodes)*f.stride,f.payload.size(),f.views.size()*sizeof(sp::view),
        Chains*(sizeof(effects)+32+sizeof(word)));
}
template<bool Append,class A>void panel(fixture&f,unsigned rounds,unsigned reps,A a) {
    f.reset();const auto one=consume<1,true,Append>(f,rounds,a);
    f.reset();const auto eight=consume<8,true,Append>(f,rounds,a);
    for(unsigned rep=0;rep<reps;++rep) {
        trial<1,true,Append>(f,rounds,rep,a,one);trial<1,false,Append>(f,rounds,rep,a,one);
        trial<8,true,Append>(f,rounds,rep,a,eight);trial<8,false,Append>(f,rounds,rep,a,eight);
    }
}
int main(int argc,char**argv) {
    const unsigned nodes=argc>1?std::stoul(argv[1]):8,rounds=argc>2?std::stoul(argv[2]):32768,reps=argc>3?std::stoul(argv[3]):5;
    if(nodes<8||!std::has_single_bit(nodes))return 2;
    std::puts("layout,maximum,nodes,used,implementation,metric,chains,rep,ns,checksum,metadata_bytes,payload_capacity_bytes,binding_bytes,scratch_bytes");
    for(auto g:{sp::geometry::direct,sp::geometry::width})for(unsigned max:{255u,4095u})for(bool used:{false,true}) {
        fixture f(nodes,{g,sp::placement::grouped,std::uint16_t(max),8192,{},!used,false});
        sp::author::dispatch(f.views[0],[&](auto a){
            // Frozen controls predate Relative Ends; fixture selects only the
            // original Absolute Ends/Sizes formats.
            if constexpr(decltype(a)::family != sp::geometry::relative_ends) {
                panel<false>(f,rounds,reps,a);panel<true>(f,rounds,reps,a);
            }
        });
    }
}
