#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aggregate_deltas {

struct Value {
    int64_t count = 0;
    int64_t sum = 0;
    bool operator==(const Value &) const = default;
    Value &operator+=(Value other);
};
Value operator+(Value a, Value b);
Value operator-(Value a, Value b);

enum class Method { eager, eager_batch, ancestor_runs, location_runs };
inline constexpr Method methods[] = {Method::eager, Method::eager_batch,
    Method::ancestor_runs, Method::location_runs};
const char *name(Method method);

struct Config {
    std::string label = "uniform";
    unsigned leaf_bits = 16;
    unsigned fanout_bits = 4;
    unsigned partition_bits = 4;
    uint32_t updates = 16384;
    uint32_t batch = 256;
    uint32_t queries_per_batch = 4;
    uint32_t max_runs = 4;
    std::string locality = "uniform";
    std::string query_shape = "mixed";
    uint64_t seed = 17;
};

struct Mutation {
    uint32_t key;
    Value delta;
};
struct Query {
    uint32_t lo, hi; // Half-open key interval.
    Value expected;
};
struct Batch {
    std::vector<Mutation> mutations;
    std::vector<Query> queries;
};
struct Trace {
    Config config;
    std::vector<Batch> batches;
    Value final;
    uint64_t checksum = 0;
    uint64_t mutation_checksum = 0;
};

struct Stats {
    uint64_t foreground_entries = 0;
    uint64_t sort_input_entries = 0;
    uint64_t base_updates = 0;
    uint64_t leaf_base_updates = 0;
    uint64_t drain_base_updates = 0;
    uint64_t run_entries_written = 0;
    uint64_t prefix_entries_written = 0;
    uint64_t partition_total_updates = 0;
    uint64_t base_query_nodes = 0;
    uint64_t partition_consults = 0;
    uint64_t partition_total_reads = 0;
    uint64_t run_probes = 0;
    uint64_t search_comparisons = 0;
    uint64_t folds = 0;
    uint64_t peak_pending_entries = 0;
    uint64_t peak_pending_runs = 0;
    uint64_t peak_pending_payload_bytes = 0;
    uint64_t largest_scratch_capacity_bytes = 0;
    uint64_t base_bytes = 0;
    uint64_t partition_metadata_bytes = 0;
    uint64_t pending_entries_at_end = 0;
};

struct Outcome {
    Stats stats;
    uint64_t checksum = 0;
    uint64_t mutation_checksum = 0;
    double seconds = 0;
    bool correct = true;
};

// Fixtures contain bounded integers; this probe does not define SQL overflow.
std::vector<Config> cases(const std::string &profile);
Trace make_trace(const Config &config);
// Setup of empty base/partitions is excluded. Batch processing, queries,
// allocation within those operations, and final draining are included.
// Accounting and timing use separate instantiations of the same algorithms.
Outcome execute(const Trace &trace, Method method, bool accounting);
void write_csv_header();
void write_csv_row(const Config &, Method, const Outcome &);

} // namespace aggregate_deltas
