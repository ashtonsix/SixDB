#include "model.h"

#include <benchmark/benchmark.h>
#include <sched.h>

#include <iostream>
#include <memory>
#include <string>

using namespace aggregate_deltas;

int main(int argc, char **argv) {
    std::string profile = "smoke";
    int kept = 1;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.starts_with("--probe_profile=")) profile = arg.substr(16);
        else argv[kept++] = argv[i];
    }
    argc = kept;
    if (profile != "smoke" && profile != "screen") {
        std::cerr << "probe profile must be smoke or screen\n";
        return 2;
    }
    cpu_set_t affinity;
    CPU_ZERO(&affinity);
    if (sched_getaffinity(0, sizeof(affinity), &affinity) != 0 || CPU_COUNT(&affinity) != 1) {
        std::cerr << "Pin this process to one CPU (use run.py or taskset).\n";
        return 2;
    }
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 2;
    benchmark::AddCustomContext("probe", "aggregate-deltas-v1");
    benchmark::AddCustomContext("profile", profile);
    benchmark::AddCustomContext("cpu_affinity", std::to_string(sched_getcpu()));
    benchmark::AddCustomContext("visibility", "current state after each batch; no retained history");
    benchmark::AddCustomContext("timed_scope", "apply+queries+folds+final drain; empty-base setup excluded");
    for (const Config &c : cases(profile)) {
        auto trace = std::make_shared<Trace>(make_trace(c));
        for (Method method : methods) {
            std::string label = c.label + "/" + name(method);
            benchmark::RegisterBenchmark(label.c_str(), [trace, method](benchmark::State &state) {
                for (auto _ : state) {
                    Outcome result = execute(*trace, method, false);
                    if (!result.correct) { state.SkipWithError("oracle mismatch"); break; }
                    benchmark::DoNotOptimize(result.checksum);
                    state.SetIterationTime(result.seconds);
                }
                state.counters["mutations_per_trace"] = trace->config.updates;
                state.counters["queries_per_trace"] =
                    trace->batches.size() * trace->config.queries_per_batch;
            })->UseManualTime()->Unit(benchmark::kNanosecond);
        }
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
