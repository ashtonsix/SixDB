#include <benchmark/benchmark.h>
void register_tuple_packets_narrow();
int main(int argc,char** argv){ register_tuple_packets_narrow(); benchmark::Initialize(&argc,argv); if(benchmark::ReportUnrecognizedArguments(argc,argv))return 1; benchmark::RunSpecifiedBenchmarks(); benchmark::Shutdown(); }
