#include "adapter.h"
#include "../ikea-composition/probes/ikea-heterogeneous/fixture.h"
#include <benchmark/benchmark.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace bec_metadata;
struct Workload {
    std::string name;
    std::vector<std::vector<h::PlainBlock>> plain;
    std::vector<Source> sources;
    std::uint64_t metadata_bytes = 0, body_bytes = 0, query_bytes = 0, checksum = 0;
};
static std::vector<std::shared_ptr<Workload>> load_workloads() {
    auto target = sp::execution_target::automatic;
    if (const char* value = std::getenv("BEC_METADATA_TARGET")) {
        const std::string name = value;
        if (name == "avx2") target = sp::execution_target::avx2;
        else if (name == "avx512") target = sp::execution_target::avx512;
        else if (name == "neon") target = sp::execution_target::neon;
        else throw std::runtime_error("unknown bound target");
    }
    std::vector<std::shared_ptr<Workload>> result;
    for (auto name : {"structural", "random_half", "structural_tail129"}) {
        auto w = std::make_shared<Workload>(); w->name = name;
        for (unsigned i = 0; i < 8; ++i) {
            auto blocks = h::structural_blocks(w->name == "structural_tail129" ? 129 : 256, i * 17);
            if (w->name == "random_half")
                for (unsigned j = 0; j < blocks.size(); ++j)
                    for (unsigned b = 0; b < 32; ++b) blocks[j][b] = h::mix(i * 8192 + j * 32 + b + 811);
            w->plain.push_back(std::move(blocks));
        }
        result.push_back(w);
    }
    if (const char* dir = std::getenv("BEC_METADATA_DATA")) {
        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::directory_iterator(dir))
            if (entry.path().extension() == ".windows") paths.push_back(entry.path());
        std::sort(paths.begin(), paths.end());
        if (paths.empty()) throw std::runtime_error("no real windows");
        for (const auto& path : paths) {
            auto w = std::make_shared<Workload>(); w->name = path.stem();
            const auto bytes = std::filesystem::file_size(path);
            if (!bytes || bytes % 8192) throw std::runtime_error("window extent");
            std::ifstream stream(path, std::ios::binary);
            for (unsigned i = 0; i < bytes / 8192; ++i) {
                std::vector<h::PlainBlock> blocks(256);
                if (!stream.read(reinterpret_cast<char*>(blocks.data()), 8192)) throw std::runtime_error("window read");
                w->plain.push_back(std::move(blocks));
            }
            result.push_back(w);
        }
    }
    for (auto& w : result) {
        for (unsigned i = 0; i < w->plain.size(); ++i) {
            const auto encoded = h::encode_source(w->plain[i]);
            auto metadata = std::make_shared<h::MetadataOwner>(encoded, h::MetadataKind::scan128);
            auto query = h::make_query(w->plain[i].size(), 1234 + i * 8192);
            w->metadata_bytes += metadata->bytes().size();
            w->body_bytes += encoded.body()->bytes.size();
            w->query_bytes += query->size();
            w->sources.emplace_back(metadata, encoded.body(), query, target);
            for (const auto& block : w->plain[i])
                for (auto b : block) w->checksum = (w->checksum ^ b) * 1099511628211ULL;
        }
    }
    return result;
}
static void counters(benchmark::State& state, const Workload& w) {
    state.counters["windows"] = w.sources.size();
    state.counters["logical_blocks"] = w.sources[0].lengths().size();
    state.counters["bound_target"] = unsigned(w.sources[0].reader().target());
    state.counters["metadata_bytes"] = w.metadata_bytes;
    state.counters["body_bytes_including_suffix"] = w.body_bytes;
    state.counters["query_bytes"] = w.query_bytes;
    state.counters["source_descriptor_bytes"] = w.sources.size() * sizeof(Source);
    state.counters["input_hash_lo"] = std::uint32_t(w.checksum);
    state.counters["input_hash_hi"] = std::uint32_t(w.checksum >> 32);
    const auto& s = w.sources[0];
    state.counters["metadata_mod4096"] = reinterpret_cast<std::uintptr_t>(s.metadata().bytes().data()) % 4096;
    state.counters["lengths_mod4096"] = reinterpret_cast<std::uintptr_t>(s.lengths().placement().payload.bytes.data()) % 4096;
    state.counters["body_mod4096"] = reinterpret_cast<std::uintptr_t>(s.body().bytes.data()) % 4096;
    state.counters["query_mod4096"] = reinterpret_cast<std::uintptr_t>(s.query().data()) % 4096;
}
int main(int argc, char** argv) {
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    const auto workloads = load_workloads();
    constexpr const char* names[] = {"specialized", "native", "materialized"};
    // One process retains exactly the same allocations for all six blocks.
    // Pairwise this is ABBA for specialized/native and native/materialized.
    constexpr Reader order[] = {Reader::specialized, Reader::native, Reader::materialized,
        Reader::materialized, Reader::native, Reader::specialized};
    for (unsigned block = 0; block < 6; ++block) {
        const auto reader = order[block];
        for (const auto& w : workloads) {
            const unsigned n = w->sources[0].lengths().size();
            const auto prefix = std::string("bec/") + std::to_string(block) + "-" + names[unsigned(reader)] + "/" + w->name;
            for (unsigned group : {0u, (n / 2) & ~15u, (n - 1) & ~15u}) {
                const auto kernel = refill_kernel(reader);
                for (const auto& s : w->sources) {
                    std::uint32_t output[16]; kernel(s, group, output);
                    for (unsigned lane = 0; lane < 16; ++lane) {
                        auto e = h::read_entry_reference(h::MetadataKind::scan128,
                            {s.metadata().bytes().data(), s.metadata().bytes().size()}, s.metadata().capacity(), group + lane);
                        if (output[lane] != (unsigned(e.offset) << 16 | e.population)) throw std::runtime_error("refill preflight");
                    }
                }
                const auto name = prefix + "/refill/g" + std::to_string(group);
                benchmark::RegisterBenchmark(name.c_str(), [w, group, kernel](benchmark::State& state) {
                    alignas(64) std::uint32_t output[16];
                    for (auto _ : state) {
                        for (unsigned i = 0; i < 32; ++i) for (const auto& s : w->sources) {
                            kernel(s, group, output); benchmark::DoNotOptimize(output); benchmark::ClobberMemory();
                        }
                    }
                    state.SetItemsProcessed(state.iterations() * 32 * w->sources.size());
                    state.counters["refills_per_operation"] = 1;
                    counters(state, *w);
                });
            }
            for (auto [first, count] : {std::pair{0u, n}, {3u, 37u}, {15u, 18u}, {15u, 2u}, {127u, 2u}, {n - 1, 1u}}) {
                for (auto execution : {h::Execution::inlined, h::Execution::split}) {
                    const auto kernel = count_kernel(reader, execution);
                    std::uint64_t expected_sum = 0;
                    for (unsigned i = 0; i < w->sources.size(); ++i) {
                        const auto& s = w->sources[i]; s.admit_range(first, count);
                        const auto expected = h::reference_count(w->plain[i], s.query(), first, count);
                        if (kernel(s, first, count) != expected) throw std::runtime_error("count preflight");
                        expected_sum += expected;
                    }
                    const auto name = prefix + (execution == h::Execution::inlined ? "/inline/" : "/split/")
                        + std::to_string(first) + "/" + std::to_string(count);
                    benchmark::RegisterBenchmark(name.c_str(), [w, first, count, kernel, expected_sum](benchmark::State& state) {
                        for (auto _ : state) {
                            std::uint64_t sum = 0;
                            for (unsigned i = 0; i < 32; ++i) for (const auto& s : w->sources) sum += kernel(s, first, count);
                            benchmark::DoNotOptimize(sum);
                        }
                        state.SetItemsProcessed(state.iterations() * 32 * w->sources.size());
                        state.counters["refills_per_operation"] = (first + count - 1) / 16 - first / 16 + 1;
                        state.counters["requested_blocks"] = count;
                        state.counters["expected_count_sum"] = expected_sum;
                        counters(state, *w);
                    });
                }
            }
        }
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
