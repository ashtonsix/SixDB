#include "model.h"

#include <iostream>
#include <string>

using namespace aggregate_deltas;

int main(int argc, char **argv) {
    std::string profile = argc == 2 ? argv[1] : "smoke";
    if (argc > 2 || (profile != "smoke" && profile != "screen")) {
        std::cerr << "usage: aggregate_deltas_check [smoke|screen]\n";
        return 2;
    }
    uint64_t checked = 0;
    // Small universes: request every half-open range, including empty and
    // cross-partition ranges, after every batch. Expected values come from
    // the generator's row oracle, with exhaustive requests substituted below.
    for (uint64_t seed = 0; seed < 12; ++seed) {
        Config c;
        c.leaf_bits = 4;
        c.fanout_bits = seed % 2 ? 1 : 2;
        c.partition_bits = seed % 5;
        c.batch = seed % 2 ? 1 : 4;
        c.updates = 128;
        c.max_runs = 1 + seed % 5;
        c.seed = seed;
        Trace trace = make_trace(c);
        // Replay deltas into flat cells only to make exhaustive range oracles;
        // independently generated afterimage oracles remain exercised below.
        std::vector<Value> rows(16);
        auto hash = [](uint64_t h, Value v) {
            h = (h ^ static_cast<uint64_t>(v.count)) * 0x100000001b3ULL;
            return (h ^ static_cast<uint64_t>(v.sum)) * 0x100000001b3ULL;
        };
        trace.checksum = 0;
        for (Batch &b : trace.batches) {
            for (const auto &m : b.mutations) rows[m.key] += m.delta;
            b.queries.clear();
            for (uint32_t lo = 0; lo <= 16; ++lo) {
                Value sum;
                for (uint32_t hi = lo; hi <= 16; ++hi) {
                    if (hi > lo) sum += rows[hi - 1];
                    b.queries.push_back({lo, hi, sum});
                    trace.checksum = hash(trace.checksum, sum);
                    ++checked;
                }
            }
        }
        trace.checksum = hash(trace.checksum, trace.final);
        for (Method method : methods) {
            const auto accounted = execute(trace, method, true);
            const auto unaccounted = execute(trace, method, false);
            if (!accounted.correct || !unaccounted.correct ||
                accounted.checksum != unaccounted.checksum ||
                accounted.stats.pending_entries_at_end != 0) {
                std::cerr << "FAIL exhaustive seed=" << seed << " method=" << name(method) << '\n';
                return 1;
            }
        }
    }
    write_csv_header();
    const uint64_t uniform_digest = make_trace(cases(profile).front()).mutation_checksum;
    for (const auto &c : cases(profile)) {
        const Trace trace = make_trace(c);
        if (c.locality == "uniform" && c.label != "large_base" && trace.mutation_checksum != uniform_digest) {
            std::cerr << "FAIL: comparison changed mutation stream: " << c.label << '\n';
            return 1;
        }
        for (Method method : methods) {
            const auto outcome = execute(trace, method, true);
            if (!outcome.correct || outcome.stats.pending_entries_at_end) {
                std::cerr << "FAIL " << c.label << '/' << name(method) << '\n';
                return 1;
            }
            write_csv_row(c, method, outcome);
        }
    }
    std::cerr << "PASS: " << checked << " exhaustive range/batch-boundary expectations across 4 methods"
        << " in both accounting modes; " << cases(profile).size()
        << " afterimage-oracle scenarios; pending runs drained.\n";
}
