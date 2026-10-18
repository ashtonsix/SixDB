#define FILTER_X86_NO_MAIN
#include "x86-index.cpp"

// Keep packed data and query words hot while breaking the old 64-state/salt
// trace. Query words are plain values, never prepared predicates. Every row can
// be queried; the cursor recurrence consumes the actual returned rank.
namespace x86_entropy {
using namespace tiles;
template<unsigned K,unsigned N,unsigned Arm>
[[gnu::noinline]] void measure(const index_probe::Data<K,N>& data) {
    static_assert(std::has_single_bit(N));
    constexpr unsigned bits=std::countr_zero(64*N), row_bits=std::countr_zero(N);
    const auto& f=data.f;
    auto invoke=[&](unsigned cursor) __attribute__((always_inline)) {
        asm("" : "+r"(cursor));
        const unsigned w=cursor>>row_bits, row=cursor&(N-1);
        return x86_index::lookup<K,N,Arm>(f.storage.data()+w*f.stride+1,f.truth[w][row],(word{1}<<N)-1);
    };
    for(unsigned c=0;c<64*N;++c) {
        unsigned expected=N;
        const word q=f.truth[c>>row_bits][c&(N-1)];
        for(unsigned i=0;i<N;++i) if(f.truth[c>>row_bits][i]==q) {expected=i;break;}
        assert(invoke(c)==expected);
    }
    auto next=[](unsigned rank,unsigned salt) __attribute__((always_inline)) {
        const word noise=word(salt)*0xd1342543de82ef95ULL;
        return unsigned(((word(rank)^noise)*0x9e3779b97f4a7c15ULL)>>(64-bits));
    };
    for(bool serial:{true,false}) for(unsigned rep=0;rep<repeats;++rep) {
        std::array<unsigned,8> cursor{0,7,13,21,32,41,54,63};
        for(unsigned i=0;i<256;++i) for(unsigned j=0;j<8;++j) cursor[j]=next(invoke(cursor[j]),i+j*0x9e37u);
        const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<iterations;++i) {
            if(serial) cursor[0]=next(invoke(cursor[0]),i);
            else sp::detail::each<8>([&](auto j) {cursor[j]=next(invoke(cursor[j]),i+j*0x9e37u);});
        }
        const auto stop=std::chrono::steady_clock::now();
        for(auto c:cursor) observed+=c;
        const auto ns=std::chrono::duration<double,std::nano>(stop-start).count()/(double(iterations)*(serial?1:8));
        std::printf("%u,%u,14,%llu,%u,%s,%u,%.6f\n",K,N,(unsigned long long)((word{1}<<N)-1),Arm,
            serial?"serial":"throughput",rep,ns);
    }
}
template<unsigned K,unsigned N> void run() {
    index_probe::Data<K,N> data(8);
    if(reverse_order) {
        x86_entropy::measure<K,N,12>(data);x86_entropy::measure<K,N,11>(data);x86_entropy::measure<K,N,10>(data);x86_entropy::measure<K,N,8>(data);x86_entropy::measure<K,N,7>(data);x86_entropy::measure<K,N,2>(data);x86_entropy::measure<K,N,0>(data);
    } else {
        x86_entropy::measure<K,N,0>(data);x86_entropy::measure<K,N,2>(data);x86_entropy::measure<K,N,7>(data);x86_entropy::measure<K,N,8>(data);x86_entropy::measure<K,N,10>(data);x86_entropy::measure<K,N,11>(data);x86_entropy::measure<K,N,12>(data);
    }
}
}
int main(int argc,char** argv) {
    if(argc>1) tiles::iterations=std::stoul(argv[1]);
    if(argc>2) tiles::repeats=std::stoul(argv[2]);
    if(argc>3) tiles::reverse_order=std::stoul(argv[3]);
    std::puts("width,count,pattern,active,arm,metric,rep,ns");
    x86_entropy::run<12,32>();x86_entropy::run<14,32>();x86_entropy::run<30,16>();x86_entropy::run<38,16>();
    std::fprintf(stderr,"x86 varied-query truth checks passed; observation=%llu\n",(unsigned long long)tiles::observed);
}
