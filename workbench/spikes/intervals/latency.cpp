#include "consumer_fixture.h"
#include <bit>

// One true dependent chain and four/eight genuinely independent request chains.
// Key scaling, checked geometry, useful payload/child load and next-state work are
// timed together. Prepared layout is bound once; no per-request std::function.
namespace {
using namespace intervals_probe;
volatile u64 latency_sink;
constexpr unsigned steps=4096;

template<unsigned Op,unsigned Chains,class Source>
[[gnu::noinline]] u64 chain(const Source &source,const fixture &f,bool clustered) {
    std::array<u64,Chains> state{};
    for(unsigned c=0;c<Chains;++c) state[c]=0x713987abcd025721ull+c*0x1311295;
    const unsigned domain=Op==0 ? (clustered?std::min(16u,f.count):f.count)
                                : (clustered?f.ends[std::min(15u,f.count-1)]:f.extent);
    need(domain!=0,"empty clustered geometry");
    for(unsigned step=0;step<steps;++step) {
        [&]<std::size_t... C>(std::index_sequence<C...>) __attribute__((always_inline)) {
            ([&] __attribute__((always_inline)) {
                const unsigned key=(u64(std::uint32_t(state[C]))*domain)>>32;
                u64 value;
                if constexpr(Op==0) {
                    const auto interval=ikea::intervals::locate(source,key);
                    if(!interval) std::abort();
                    value=f.use_interval(key,interval->first,interval->limit);
                } else {
                    const unsigned rank=ikea::intervals::reverse(source,key);
                    if(rank>=f.count) std::abort();
                    value=f.children[rank];
                }
                state[C]=std::rotl(state[C],13)^(value*0x9e3779b185ebca87ull);
            }(),...);
        }(std::make_index_sequence<Chains>{});
    }
    u64 result=0;for(auto value:state) result+=value;return result;
}

template<unsigned Op,unsigned Chains>
u64 oracle(const fixture &f,bool clustered) {
    std::array<u64,Chains> state{};
    for(unsigned c=0;c<Chains;++c) state[c]=0x713987abcd025721ull+c*0x1311295;
    const unsigned domain=Op==0?(clustered?std::min(16u,f.count):f.count)
                              :(clustered?f.ends[std::min(15u,f.count-1)]:f.extent);
    for(unsigned step=0;step<steps;++step) for(auto &s:state) {
        const unsigned key=(u64(std::uint32_t(s))*domain)>>32;
        u64 value;
        if constexpr(Op==0) value=f.use_interval(key,key?f.ends[key-1]:0,f.ends[key]);
        else {unsigned rank=0;while(f.ends[rank]<=key) ++rank;value=f.children[rank];}
        s=std::rotl(s,13)^(value*0x9e3779b185ebca87ull);
    }
    u64 result=0;for(auto value:state) result+=value;return result;
}
struct arm {const char *name;std::function<u64(unsigned,unsigned,bool)> run;};
template<class Source>
void add(std::vector<arm> &arms,const char *name,Source source,const fixture &f) {
    arms.push_back({name,[source,&f](unsigned op,unsigned chains,bool clustered) {
        if(op==0) {
            if(chains==1) return chain<0,1>(source,f,clustered);
            if(chains==4) return chain<0,4>(source,f,clustered);
            return chain<0,8>(source,f,clustered);
        }
        if(chains==1) return chain<1,1>(source,f,clustered);
        if(chains==4) return chain<1,4>(source,f,clustered);
        return chain<1,8>(source,f,clustered);
    }});
}
template<unsigned Maximum>
void experiment(unsigned n,unsigned trials) {
    using W=std::conditional_t<(Maximum<=255),std::uint8_t,std::uint16_t>;
    using P=std::conditional_t<(capacity*Maximum<=65535),std::uint16_t,std::uint32_t>;
    fixture f(n,Maximum);
    sparse_image<16,W,P,Maximum,false> g16(f);
    sparse_image<64,W,P,Maximum,false> g64(f);
    compact_image compact(f,ikea::boundlist::geometry::sizes);
    std::array<P,capacity> ends{};for(unsigned i=0;i<n;++i) ends[i]=f.ends[i];
    std::vector<arm> arms;
    add(arms,"before16",g16.before_source(),f);add(arms,"current16",g16.source(),f);
    add(arms,"before64",g64.before_source(),f);add(arms,"current64",g64.source(),f);
    add(arms,"batch64",g64.source().for_independent_reads(),f);
    add(arms,"direct",ikea::intervals::from_ends(ikea::intervals::direct_source<P>{ends.data(),n},P{0}),f);
    ikea::boundlist::author::dispatch(*compact.view,[&](auto access) {add(arms,"maintained16",access,f);});
    for(unsigned op:{0u,1u}) for(unsigned chains:{1u,4u,8u}) for(bool clustered:{false,true}) {
        u64 expected;
        if(op==0) expected=chains==1?oracle<0,1>(f,clustered):chains==4?oracle<0,4>(f,clustered):oracle<0,8>(f,clustered);
        else expected=chains==1?oracle<1,1>(f,clustered):chains==4?oracle<1,4>(f,clustered):oracle<1,8>(f,clustered);
        for(auto &a:arms) need(a.run(op,chains,clustered)==expected,"dependent consumer differs from oracle");
        for(unsigned trial=0;trial<trials;++trial) for(unsigned i=0;i<arms.size();++i) {
            auto &a=arms[(i+trial)%arms.size()];
            const auto begin=std::chrono::steady_clock::now();
            for(unsigned repeat=0;repeat<16;++repeat) {asm volatile("" ::: "memory");latency_sink=a.run(op,chains,clustered);}
            const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
            std::printf("{\"arm\":\"%s\",\"operation\":\"%s\",\"maximum\":%u,\"count\":%u,\"chains\":%u,\"clustered\":%s,\"trial\":%u,\"elapsed_ns\":%lld,\"operations\":%u}\n",
                a.name,op==0?"rank_payload":"inverse_child",Maximum,n,chains,clustered?"true":"false",trial,static_cast<long long>(ns),steps*chains*16);
        }
    }
}
}
int main(int argc,char **argv) {
    try {
        const unsigned m=argc>1?std::stoul(argv[1]):255,n=argc>2?std::stoul(argv[2]):256,trials=argc>3?std::stoul(argv[3]):9;
        need(n>=16&&n<=256&&trials>0,"invalid fixture size/trials");
        if(m==15) experiment<15>(n,trials);
        else if(m==255) experiment<255>(n,trials);
        else if(m==4095) experiment<4095>(n,trials);
        else throw std::runtime_error("maximum must be15/255/4095");
    }catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
