#include "tile-bench.h"
void tile_widths_1();
void tile_widths_9();
void tile_widths_17();
void tile_widths_25();
void tile_widths_33();
void tile_widths_41();
void tile_widths_49();
void tile_widths_57();
int main(int argc,char** argv) {
    if(argc>1) tiles::iterations=std::stoul(argv[1]);
    if(argc>2) tiles::repeats=std::stoul(argv[2]);
    if(argc>3) tiles::reverse_order=std::stoul(argv[3]);
    std::puts("width,count,op,offset,stride,active,arm,metric,rep,ns");
    tile_widths_1();
    tile_widths_9();
    tile_widths_17();
    tile_widths_25();
    tile_widths_33();
    tile_widths_41();
    tile_widths_49();
    tile_widths_57();
    std::fprintf(stderr,"tile comparison oracles passed; observed=%llu\n",(unsigned long long)tiles::observed);
}
