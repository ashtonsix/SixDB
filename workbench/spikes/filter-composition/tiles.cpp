#include "tile-bench.h"
int main(int argc,char** argv) {
    if(argc>1) tiles::iterations=std::stoul(argv[1]);
    if(argc>2) tiles::repeats=std::stoul(argv[2]);
    if(argc>3) tiles::reverse_order=std::stoul(argv[3]);
    std::puts("width,count,op,offset,stride,active,arm,metric,rep,ns");
    tiles::width<7>(); tiles::width<10>(); tiles::width<12>(); tiles::width<16>();
    tiles::width<17>(); tiles::width<24>(); tiles::width<30>(); tiles::width<32>();
    tiles::width<40>(); tiles::width<48>(); tiles::width<56>(); tiles::width<64>();
    tiles::width<14>(); tiles::width<23>(); tiles::width<33>(); tiles::width<41>();
    tiles::width<47>(); tiles::width<63>();
    std::fprintf(stderr,"tile comparison oracles passed; observed=%llu\n",(unsigned long long)tiles::observed);
}
