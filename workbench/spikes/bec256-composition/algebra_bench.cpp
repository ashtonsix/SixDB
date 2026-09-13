#include "algebra.h"
#include "workloads.h"
#include <ikea/bec256/author/analysis.h>
#include <benchmark/benchmark.h>
#include <random>

using namespace bec_study;
namespace {
struct configuration {
    const char *name;
    bool left_plain, right_plain;
    layout left, right;
    resolution left_lookup = resolution::native16, right_lookup = resolution::native16;
};
constexpr std::array configurations{
    configuration{"plain_plain", true, true, layout::direct32, layout::direct32},
    configuration{"direct_direct", false, false, layout::direct32, layout::direct32},
    configuration{"tuple_series", false, false, layout::tuple_folded, layout::series_folded_scan},
    configuration{"series_tuple", false, false, layout::series_folded_local, layout::tuple_folded},
    configuration{"point_frame", false, false, layout::series_local, layout::series_scan,
                  resolution::point, resolution::native16},
    configuration{"series_plain", false, true, layout::series_folded_scan, layout::direct32},
    configuration{"plain_tuple", true, false, layout::direct32, layout::tuple_folded}};

void algebra_bench(benchmark::State &state, unsigned wi, unsigned ci, boolean_op op,
                   output_kind kind, unsigned grain, unsigned selection) {
    const auto &w = workloads[wi];
    const auto config = configurations[ci];
    const auto n = w.inputs.front()->size();
    std::vector<std::unique_ptr<directory>> da, db;
    for (const auto &input : w.inputs) {
        da.push_back(std::make_unique<directory>(config.left, input->catalog, 16));
        db.push_back(std::make_unique<directory>(config.right, input->catalog, 64));
        input->admit(*da.back());
        input->admit(*db.back());
    }
    std::mt19937_64 rng(0xabc25);
    std::vector<std::uint64_t> active((n + 63) / 64);
    unsigned selected = 0;
    for (unsigned i = 0; i < n; ++i)
        if (selection == 0 || (selection == 1 && i % 17 == 0) ||
            (selection == 2 && (rng() & 3) == 0)) {
            active[i / 64] |= std::uint64_t{1} << (i % 64);
            ++selected;
        }
    std::vector<bc::byte> storage(n * 32, bc::byte{0xa5});
    bc::destination output{storage};
    std::vector<ikea::owner_write> records(n);
    ikea::source_write_journal effects{records};
    const auto call = bind_algebra(op, kind, grain);
    auto invoke = [&](unsigned i) {
        const auto j = (i + 3) % w.inputs.size();
        effects.used = 0;
        call({*w.inputs[i], config.left_plain ? nullptr : da[i].get(), config.left_lookup},
             {*w.inputs[j], config.right_plain ? nullptr : db[j].get(), config.right_lookup},
             active.data(), output, effects);
    };
    for (unsigned i = 0; i < w.inputs.size(); ++i) {
        std::fill(storage.begin(), storage.end(), bc::byte{0xa5});
        invoke(i);
        const auto j = (i + 3) % w.inputs.size();
        for (unsigned k = 0; k < n * 32; ++k) {
            const bool chosen = (active[k / 32 / 64] >> ((k / 32) % 64)) & 1;
            const auto expected = chosen                          ? (op == boolean_op::intersection
                                                                         ? w.inputs[i]->plain[k] & w.inputs[j]->plain[k]
                                                                         : w.inputs[i]->plain[k] | w.inputs[j]->plain[k])
                                  : kind == output_kind::complete ? bc::byte{0}
                                                                  : bc::byte{0xa5};
            if (storage[k] != expected)
                throw std::runtime_error("whole-bitset preflight failed");
        }
    }
    benchmark::DoNotOptimize(storage.data());
    benchmark::DoNotOptimize(records.data());
    benchmark::DoNotOptimize(&effects);
    unsigned i = 0;
    for (auto _ : state) {
        invoke(i);
        benchmark::ClobberMemory();
        i = (i + 1) % w.inputs.size();
    }
    state.counters["original_blocks"] = n;
    state.counters["selected_blocks"] = selected;
    state.counters["issued_output_bytes"] = (kind == output_kind::complete ? n : selected) * 32;
    state.counters["left_metadata_bytes"] = config.left_plain ? 0 : da[0]->storage_bytes();
    state.counters["right_metadata_bytes"] = config.right_plain ? 0 : db[0]->storage_bytes();
}

enum class analysis_method { full, sample32, checked_encode };
template <analysis_method M>
unsigned body_estimate(const bitset &bits, bc::destination &output,
                       ikea::source_write_journal &effects) {
    unsigned total = 0;
    if constexpr (M == analysis_method::checked_encode) {
        for (unsigned i = 0; i < bits.size(); ++i) {
            effects.used = 0;
            total += *bc::encode(std::span<const bc::byte, 32>(bits.plain.data() + i * 32, 32),
                                 bits.catalog[i].population, output, 0, effects);
            benchmark::ClobberMemory(); // Each intermediate emitted body is observable.
        }
    } else if constexpr (M == analysis_method::sample32) {
        const unsigned samples = std::min(32u, bits.size());
        for (unsigned j = 0; j < samples; ++j) {
            const auto i = j * bits.size() / samples;
            total += bc::native::estimate_bytes(bc::native::load(bits.plain.data() + i * 32));
        }
        total = (std::uint64_t(total) * bits.size() + samples / 2) / samples;
    } else {
        unsigned i = 0;
        for (; i + 1 < bits.size(); i += 2) {
            const auto sizes =
                bc::native::estimate_bytes(bc::native::load_pair(bits.plain.data() + i * 32));
            total += sizes[0] + sizes[1];
        }
        if (i < bits.size())
            total += bc::native::estimate_bytes(bc::native::load(bits.plain.data() + i * 32));
    }
    return total;
}
template <analysis_method M>
void analysis_bench(benchmark::State &state, unsigned wi, layout kind, unsigned suffix) {
    const auto &w = workloads[wi];
    std::array<bc::byte, 64> storage{};
    bc::destination output{storage};
    std::array<ikea::owner_write, 1> records;
    ikea::source_write_journal effects{records};
    benchmark::DoNotOptimize(storage.data());
    benchmark::DoNotOptimize(records.data());
    benchmark::DoNotOptimize(&effects);
    double mae = 0, regret = 0, predicted = 0, actual = 0;
    unsigned max_regret = 0, wrong = 0;
    const directory d(kind, w.inputs[0]->catalog, 16);
    const auto metadata = d.storage_bytes();
    auto extent = [&](unsigned body) { return (body + metadata + suffix + 63) & ~std::size_t{63}; };
    const auto raw = (w.inputs[0]->plain.size() + 63) & ~std::size_t{63};
    for (const auto &bits : w.inputs) {
        const auto estimate = body_estimate<M>(*bits, output, effects);
        const auto encoded = bits->body.size() - 64;
        const auto chosen = extent(estimate) < raw ? extent(encoded) : raw;
        const auto lost = unsigned(chosen - std::min(raw, extent(encoded)));
        predicted += extent(estimate);
        actual += extent(encoded);
        mae += std::abs(double(estimate) - encoded);
        regret += lost;
        max_regret = std::max(max_regret, lost);
        wrong += (extent(estimate) < raw) != (extent(encoded) < raw);
    }
    unsigned i = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(body_estimate<M>(*w.inputs[i], output, effects));
        i = (i + 1) % w.inputs.size();
    }
    const double windows = w.inputs.size();
    state.counters["raw_allocation_bytes"] = raw;
    state.counters["metadata_bytes"] = metadata;
    state.counters["readable_suffix_bytes"] = suffix;
    state.counters["predicted_allocation_bytes"] = predicted / windows;
    state.counters["actual_allocation_bytes"] = actual / windows;
    state.counters["body_mae_bytes"] = mae / windows;
    state.counters["wrong_byte_choice_fraction"] = wrong / windows;
    state.counters["storage_regret_bytes"] = regret / windows;
    state.counters["max_storage_regret_bytes"] = max_regret;
}
} // namespace

void register_whole_bitset_cases() {
    for (unsigned wi = 0; wi < workloads.size(); ++wi) {
        if (workloads[wi].label == "random4096")
            continue; // Direct4 offset limit.
        if (!workloads[wi].label.starts_with("sampletrap_"))
            for (unsigned ci = 0; ci < configurations.size(); ++ci)
                for (auto op : {boolean_op::intersection, boolean_op::set_union})
                    for (auto out : {output_kind::complete, output_kind::selected_only})
                        for (unsigned grain : {1u, 2u})
                            for (unsigned selection = 0; selection < 3; ++selection)
                                benchmark::RegisterBenchmark(
                                    ("algebra/" + workloads[wi].label + "/" +
                                     configurations[ci].name +
                                     (op == boolean_op::intersection ? "/intersection/"
                                                                     : "/union/") +
                                     (out == output_kind::complete ? "complete/" : "selected/") +
                                     "grain" + std::to_string(grain) + "/" +
                                     std::array{"all", "sparse17", "masked25"}[selection])
                                        .c_str(),
                                    algebra_bench, wi, ci, op, out, grain, selection);
        for (auto kind : {layout::direct32, layout::tuple_folded, layout::series_folded_scan})
            for (unsigned suffix : {0u, 64u}) {
                const auto stem = "storage_analysis/" + workloads[wi].label + "/" + name(kind) +
                                  "/suffix" + std::to_string(suffix) + "/";
                benchmark::RegisterBenchmark((stem + "full").c_str(),
                                             analysis_bench<analysis_method::full>, wi, kind,
                                             suffix);
                benchmark::RegisterBenchmark((stem + "sample32").c_str(),
                                             analysis_bench<analysis_method::sample32>, wi, kind,
                                             suffix);
                benchmark::RegisterBenchmark((stem + "checked_encode").c_str(),
                                             analysis_bench<analysis_method::checked_encode>, wi,
                                             kind, suffix);
            }
    }
}
