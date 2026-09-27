#include "../../benchmarks/seriespack/ordinary.cpp"
int main(int argc, char** argv) {
    auto add = []<unsigned K>() {
        ordinary_case<K, sp::geometry::local, 0>();
        ordinary_case<K, sp::geometry::local, 2>();
    };
    add.template operator()<11>(); add.template operator()<12>(); add.template operator()<13>();
    add.template operator()<15>(); add.template operator()<19>(); add.template operator()<20>();
    add.template operator()<23>(); add.template operator()<28>(); add.template operator()<31>();
    add.template operator()<35>(); add.template operator()<36>(); add.template operator()<39>();
    add.template operator()<47>(); add.template operator()<55>(); add.template operator()<63>();
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
