#include <cstdio>
#include <cstdlib>
#include <cstdint>
unsigned repeats=3,iterations=100000;
std::uint64_t observed;
void run_0();
void run_1();
void run_2();
void run_3();
void run_4();
int main(int argc,char** argv) {
    if(argc>1)iterations=std::strtoul(argv[1],nullptr,10);
    if(argc>2)repeats=std::strtoul(argv[2],nullptr,10);
    puts("module,width,op,candidate,metric,repeat,ns_window,ns_value");
    run_0();
    run_1();
    run_2();
    run_3();
    run_4();
    std::fprintf(stderr,"observable=%llu\n",(unsigned long long)observed);
}
