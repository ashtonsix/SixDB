#include "codec.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sched.h>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" unsigned prior_encode(const unsigned char*,unsigned,unsigned char*);
extern "C" void prior_decode(const unsigned char*,unsigned,unsigned char*);
#if defined(__AVX512VBMI__)
extern "C" unsigned prior_encode512(const unsigned char*,unsigned,unsigned char*);
extern "C" void prior_decode512x2(const unsigned char*,unsigned,const unsigned char*,unsigned,unsigned char*);
#endif
struct Cell { std::array<unsigned char,32> plain{}; std::array<unsigned char,96> body{}; unsigned pop{},size{}; const Cell* other{}; };
using Clock=std::chrono::steady_clock;
std::uint64_t observed{};
template<class Operation> void measure(const std::string& corpus,const char* name,
                                     std::vector<Cell>& cells,Operation operation,unsigned cells_per_call=1) {
    unsigned rounds=std::max(1u,unsigned(1000000/cells.size()));
    auto pass=[&] {
        std::uint64_t sum=0;
        for(unsigned r=0;r<rounds;++r) for(auto& cell:cells) sum+=operation(cell);
        observed+=sum;
    };
    pass();
    for(unsigned rep=0;rep<5;++rep) {
        auto start=Clock::now(); pass(); auto end=Clock::now();
        auto ns=std::chrono::duration<double,std::nano>(end-start).count();
        std::printf("%s,%s,%zu,%u,%u,%u,%.9f\n",corpus.c_str(),name,cells.size(),rounds,rep,cells_per_call,ns/(cells.size()*rounds*cells_per_call));
    }
}
void benchmark(const std::string& corpus,std::vector<Cell> cells) {
    for(std::size_t i=0;i<cells.size();++i) cells[i].other=&cells[(i*17+13)%cells.size()];
    for(auto& c:cells) {
        c.pop=0;
        for(auto v:c.plain) c.pop+=std::popcount(v);
        c.size=ikea_probe::encode_native(c.plain.data(),c.pop,c.body.data());
        std::array<unsigned char,96> prior{};
        if(prior_encode(c.plain.data(),c.pop,prior.data())!=c.size || !std::equal(c.body.begin(),c.body.begin()+c.size,prior.begin()))
            throw std::runtime_error("corpus encode disagreement");
        std::array<unsigned char,32> decoded{};
        ikea_probe::decode_native(prior.data(),c.pop,decoded.data());
        if(decoded!=c.plain) throw std::runtime_error("corpus decode disagreement");
    }
    for(auto& c:cells) {
        const auto& d=*c.other;
        std::array<unsigned char,64> decoded{};
        ikea_probe::decode_native2(c.body.data(),c.pop,d.body.data(),d.pop,decoded.data());
        if(!std::equal(c.plain.begin(),c.plain.end(),decoded.begin()) || !std::equal(d.plain.begin(),d.plain.end(),decoded.begin()+32))
            throw std::runtime_error("corpus two-stream decode disagreement");
    }
    std::array<unsigned char,96> out{};
    measure(corpus,"bec_encode",cells,[&](auto& c){return ikea_probe::encode_native(c.plain.data(),c.pop,out.data());});
    measure(corpus,"calico_encode",cells,[&](auto& c){return prior_encode(c.plain.data(),c.pop,out.data());});
#if defined(__AVX512VBMI__)
    measure(corpus,"calico_p2_encode",cells,[&](auto& c){return prior_encode512(c.plain.data(),c.pop,out.data());});
#endif
    measure(corpus,"bec_decode",cells,[&](auto& c){ikea_probe::decode_native(c.body.data(),c.pop,out.data());return out[0];});
    measure(corpus,"calico_decode",cells,[&](auto& c){prior_decode(c.body.data(),c.pop,out.data());return out[0];});
#if defined(__AVX512VBMI__)
    // One call consumes two independent cells; the CSV reports ns per cell.
    measure(corpus,"bec_decode_x2",cells,[&](auto& c){auto& d=*c.other;ikea_probe::decode_native2(c.body.data(),c.pop,d.body.data(),d.pop,out.data());return out[0]+out[32];},2);
    measure(corpus,"calico_decode_x2",cells,[&](auto& c){auto& d=*c.other;prior_decode512x2(c.body.data(),c.pop,d.body.data(),d.pop,out.data());return out[0]+out[32];},2);
#endif
}
int main(int argc,char** argv) {
    cpu_set_t affinity;
    if(sched_getaffinity(0,sizeof(affinity),&affinity)!=0 || CPU_COUNT(&affinity)!=1) {
        std::fprintf(stderr,"Benchmark requires affinity to exactly one CPU\n"); return 1;
    }
    std::printf("corpus,operation,cells,rounds,repetition,cells_per_call,ns_per_cell\n");
    std::mt19937_64 rng(0xbec25609);
    for(auto shape:{"uniform","run"}) {
        std::vector<Cell> cells(1028);
        for(unsigned j=0;j<cells.size();++j) {
            unsigned p=j%257;
            std::array<unsigned,256> positions{};
            for(unsigned i=0;i<256;++i) positions[i]=i;
            if(std::string(shape)=="uniform") for(unsigned i=255;i>0;--i) std::swap(positions[i],positions[rng()%(i+1)]);
            else std::rotate(positions.begin(),positions.begin()+rng()%256,positions.end());
            for(unsigned i=0;i<p;++i) cells[j].plain[positions[i]/8]|=1u<<(positions[i]%8);
        }
        benchmark(shape,std::move(cells));
    }
    if(argc>1) {
        std::vector<std::filesystem::path> paths;
        for(auto& f:std::filesystem::directory_iterator(argv[1])) if(f.path().extension()==".bits32") paths.push_back(f.path());
        std::sort(paths.begin(),paths.end());
        for(auto& path:paths) {
            std::ifstream in(path,std::ios::binary);
            std::vector<Cell> cells;
            Cell c;
            while(in.read(reinterpret_cast<char*>(c.plain.data()),32)) cells.push_back(c);
            if(in.gcount()!=0) throw std::runtime_error("truncated benchmark input");
            if(!cells.empty()) {
                auto partial=cells;
                std::erase_if(partial,[](const auto& cell){
                    return std::all_of(cell.plain.begin(),cell.plain.end(),[](auto x){return x==0;}) ||
                           std::all_of(cell.plain.begin(),cell.plain.end(),[](auto x){return x==255;});
                });
                if(!partial.empty()) benchmark(path.stem().string()+"/partial",std::move(partial));
                benchmark(path.stem().string()+"/all",std::move(cells));
            }
        }
    }
    std::fprintf(stderr,"checksum=%llu cpu=%d\n",static_cast<unsigned long long>(observed),sched_getcpu());
}
