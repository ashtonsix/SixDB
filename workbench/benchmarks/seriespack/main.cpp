#include <benchmark/benchmark.h>
#include <cstring>
#include <iostream>
#include <string>
void register_ordinary();
void register_pipelines();
void register_packet_pipelines();
void register_placed_low();
void register_placed_mid();
void register_placed_wide();
int main(int argc, char** argv) {
    std::string suite = "quick";
    for (int i = 1; i < argc; ++i)
        if (std::strncmp(argv[i], "--suite=", 8) == 0) {
            suite = argv[i] + 8;
            for (int j = i; j + 1 < argc; ++j)
                argv[j] = argv[j + 1];
            --argc;
            --i;
        }
    std::string filter;
    if (suite == "quick")
        filter = "(^ordinary/k(12|64)/|pipeline-packet/n32/.*(source0-map1|source1-map0).*/"
                 "(inline|cps)$|placed/k31h8/"
                 "substituted/partial/)";
    else if (suite == "ordinary")
        filter = "^ordinary/";
    else if (suite == "placement")
        filter = "^placed/";
    else if (suite == "pipelines")
        filter = "^pipeline-packet/";
    else if (suite == "stress")
        filter = "^pipeline-mutation/";
    else if (suite == "all")
        filter = ".*";
    else {
        std::cerr << "Expected --suite=quick|ordinary|placement|pipelines|stress|all\n";
        return 2;
    }
    register_ordinary();
    register_pipelines();
    register_packet_pipelines();
    register_placed_low();
    register_placed_mid();
    register_placed_wide();
    benchmark::SetBenchmarkFilter(filter);
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
        return 2;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
