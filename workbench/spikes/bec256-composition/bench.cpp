#include "workloads.h"
#include <benchmark/benchmark.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace bec_study;
void register_codec_cases();
void register_mutation_cases();
void register_analysis_cases();
std::vector<workload> workloads;
static void make_workloads() {
    std::mt19937_64 random(0xbec20260913);
    for (auto shape : {"random256", "runs256", "terminals256", "random129", "random4096"}) {
        workload w;
        w.label = shape;
        const unsigned n = w.label == "random129" ? 129 : w.label == "random4096" ? 4096 : 256;
        for (unsigned window = 0; window < 8; ++window) {
            std::vector<bc::byte> plain(n * 32), query(n * 32);
            for (unsigned i = 0; i < n; ++i)
                for (unsigned b = 0; b < 32; ++b) {
                    unsigned value = random();
                    if (w.label == "runs256")
                        value = b < ((i + window) % 33) ? 255 : 0;
                    if (w.label == "terminals256")
                        value = (i + window) % 2 ? 255 : 0;
                    plain[i * 32 + b] = bc::byte(value);
                    query[i * 32 + b] = bc::byte(random());
                }
            w.inputs.push_back(std::make_unique<bitset>(plain));
            w.query.push_back(std::move(query));
        }
        workloads.push_back(std::move(w));
    }
    // Fixed sampling's correlation failure is structural, not a random seed
    // accident: all 32 sampled ordinals differ from the unsampled population.
    for (bool sampled_dense : {false, true}) {
        workload w;
        w.label = sampled_dense ? "sampletrap_sparse" : "sampletrap_dense";
        for (unsigned window = 0; window < 8; ++window) {
            std::vector<bc::byte> plain(8192), query(8192);
            for (unsigned i = 0; i < 256; ++i)
                for (unsigned j = 0; j < 32; ++j) {
                    const bool dense = ((i % 8) == 0) == sampled_dense;
                    plain[i * 32 + j] = dense ? bc::byte(random()) : bc::byte{0};
                    query[i * 32 + j] = bc::byte(random());
                }
            w.inputs.push_back(std::make_unique<bitset>(plain));
            w.query.push_back(std::move(query));
        }
        workloads.push_back(std::move(w));
    }
    if (const auto *root = std::getenv("BEC_WINDOWS")) {
        // Existing prepared whole-window sample; no new corpus/sample recipe.
        for (const char *family : {"census-income", "weather_sept_85", "wikileaks-noquotes"}) {
            workload w;
            w.label = family;
            std::ifstream file(std::filesystem::path(root) / (w.label + ".windows"),
                               std::ios::binary);
            if (!file)
                throw std::runtime_error("missing retained windows");
            for (unsigned window = 0; window < 8; ++window) {
                std::vector<bc::byte> plain(8192), query(8192);
                file.read(reinterpret_cast<char *>(plain.data()), plain.size());
                if (!file)
                    throw std::runtime_error("truncated retained windows");
                // A synthetic query in the same ordinal space; not an observed
                // predicate or a falsely claimed pair of natural source bitmaps.
                for (auto &b : query)
                    b = bc::byte(random());
                w.inputs.push_back(std::make_unique<bitset>(plain));
                w.query.push_back(std::move(query));
            }
            workloads.push_back(std::move(w));
        }
    }
}
static void comparisons(benchmark::State &state, unsigned wi, layout kind, unsigned checkpoint,
                        resolution mode, execution execution, unsigned shape) {
    const auto &w = workloads[wi];
    std::vector<std::unique_ptr<directory>> directories;
    for (const auto &input : w.inputs) {
        directories.push_back(std::make_unique<directory>(kind, input->catalog, checkpoint));
        input->admit(*directories.back());
    }
    const auto n = w.inputs[0]->size();
    std::vector<std::uint64_t> active((n + 63) / 64);
    std::mt19937_64 random(0x25bec256);
    for (auto &word : active)
        word = random() & random();
    const request range = shape == 0   ? request{0, n, 1}
                          : shape == 1 ? request{0, n, 17}
                          : shape == 2 ? request{15, 2, 1}
                          : shape == 3 ? request{n - 1, 1, 1}
                                       : request{3, std::min(37u, n - 3), 1};
    request selected = shape == 5 ? request{0, n, 1, active.data()} : range;
    const auto call = bind_count(kind, mode, execution);
    for (unsigned i = 0; i < w.inputs.size(); ++i)
        if (call(*directories[i], *w.inputs[i], w.query[i].data(), selected) !=
            count_reference(*w.inputs[i], w.query[i].data(), selected))
            throw std::runtime_error("consumer preflight failed");
    unsigned index = 0;
    for (auto _ : state) {
        auto value = call(*directories[index], *w.inputs[index], w.query[index].data(), selected);
        benchmark::DoNotOptimize(value);
        index = (index + 1) % w.inputs.size();
    }
    state.counters["metadata_bytes"] = directories[0]->storage_bytes();
    unsigned selected_count = 0;
    for (unsigned i = selected.first; i < selected.first + selected.count; i += selected.every)
        selected_count += !selected.active || ((selected.active[i / 64] >> (i % 64)) & 1);
    state.counters["selected_blocks"] = selected_count;
    state.counters["original_blocks"] = n;
    state.counters["body_bytes"] = w.inputs[0]->body.size() - 64;
}
static void metadata_update(benchmark::State &state, layout kind) {
    auto &bits = *workloads[0].inputs[0];
    directory d(kind, bits.catalog, 16);
    unsigned row = 0;
    std::uint64_t issued = 0;
    for (auto _ : state) {
        // Existing metadata is rewritten: measures the full checked metadata
        // command and its issued coverage, not a complete bitset replacement.
        auto n = d.update(row, bits.catalog[row]);
        benchmark::DoNotOptimize(n);
        issued += n;
        row = (row + 17) % bits.size();
    }
    state.counters["issued_bytes"] = double(issued) / state.iterations();
}
int main(int argc, char **argv) {
    register_analysis_cases();
    make_workloads();
    register_whole_bitset_cases();
    register_codec_cases();
    register_mutation_cases();
    for (unsigned wi = 0; wi < workloads.size(); ++wi)
        if (!workloads[wi].label.starts_with("sampletrap_"))
            for (auto kind : layouts)
                for (unsigned cp : {16u, 64u})
                    for (auto mode :
                         {resolution::point, resolution::buffered16, resolution::native16})
                        for (auto e :
                             {execution::inline_body, execution::shared_body,
                              execution::materialized, execution::cps, execution::cps_fused}) {
                            if (absolute(kind) && (cp != 16 || mode != resolution::point))
                                continue;
                            if (kind == layout::direct32 &&
                                std::any_of(workloads[wi].inputs.begin(),
                                            workloads[wi].inputs.end(), [](const auto &input) {
                                                return input->catalog.back().offset >= (1u << 17);
                                            }))
                                continue;
                            for (unsigned shape = 0; shape < 6; ++shape) {
                                auto label = "count/" + workloads[wi].label + "/" + name(kind) +
                                             "/cp" + std::to_string(cp) + "/" + name(mode) + "/" +
                                             name(e) + "/" +
                                             std::array<const char *, 6>{
                                                 "scan", "sparse17", "crossing15",
                                                 "last", "range37",  "masked25"}[shape];
                                benchmark::RegisterBenchmark(label.c_str(), comparisons, wi, kind,
                                                             cp, mode, e, shape);
                            }
                        }
    for (auto kind : layouts)
        benchmark::RegisterBenchmark((std::string("metadata_update/") + name(kind)).c_str(),
                                     metadata_update, kind);
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
        return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
