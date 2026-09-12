#include <benchmark/benchmark.h>
void gpr_rows1();
void gpr_rows2();
void gpr_rows4();
void gpr_rows8();
int main(int argc, char** argv) {
    gpr_rows1(); gpr_rows2(); gpr_rows4(); gpr_rows8();
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
