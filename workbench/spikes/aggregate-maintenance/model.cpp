#include "model.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <span>
#include <utility>

namespace aggregate_deltas {

Value &Value::operator+=(Value b) { count += b.count; sum += b.sum; return *this; }
Value operator+(Value a, Value b) { return a += b; }
Value operator-(Value a, Value b) { return {a.count - b.count, a.sum - b.sum}; }

const char *name(Method method) {
    switch (method) {
    case Method::eager: return "eager";
    case Method::eager_batch: return "eager_batch";
    case Method::ancestor_runs: return "ancestor_runs";
    case Method::location_runs: return "location_runs";
    }
    return "invalid";
}

namespace {

uint64_t random_word(uint64_t &state) {
    uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

uint64_t digest(uint64_t h, Value v) {
    h = (h ^ static_cast<uint64_t>(v.count)) * 0x100000001b3ULL;
    return (h ^ static_cast<uint64_t>(v.sum)) * 0x100000001b3ULL;
}

struct Entry { uint32_t key; Value value; };

struct Geometry {
    uint32_t leaves;
    std::vector<uint32_t> spans, shifts, offsets;
    uint32_t nodes = 0;
    explicit Geometry(const Config &c) : leaves(1u << c.leaf_bits) {
        assert(c.leaf_bits <= 20 && c.fanout_bits > 0);
        assert(c.leaf_bits % c.fanout_bits == 0);
        for (unsigned bits = 0; bits <= c.leaf_bits; bits += c.fanout_bits) {
            spans.push_back(1u << bits);
            shifts.push_back(bits);
            offsets.push_back(nodes);
            nodes += leaves >> bits;
        }
    }
    void expand(uint32_t key, Value value, std::vector<Entry> &out) const {
        for (size_t l = 0; l < spans.size(); ++l)
            out.push_back({offsets[l] + (key >> shifts[l]), value});
    }
    std::vector<uint32_t> cover(uint32_t lo, uint32_t hi) const {
        std::vector<uint32_t> out;
        while (lo < hi) {
            size_t l = spans.size() - 1;
            while (spans[l] > hi - lo || (lo & (spans[l] - 1))) --l;
            out.push_back(offsets[l] + (lo >> shifts[l]));
            lo += spans[l];
        }
        return out;
    }
};

template <bool Account>
void compact(std::vector<Entry> &entries, Stats &stats) {
    if constexpr (Account) {
        stats.sort_input_entries += entries.size();
        stats.largest_scratch_capacity_bytes = std::max(
            stats.largest_scratch_capacity_bytes, entries.capacity() * sizeof(Entry));
    }
    std::sort(entries.begin(), entries.end(),
        [](const Entry &a, const Entry &b) { return a.key < b.key; });
    size_t write = 0;
    for (size_t read = 0; read < entries.size();) {
        Entry e = entries[read++];
        while (read < entries.size() && entries[read].key == e.key)
            e.value += entries[read++].value;
        if (e.value != Value{}) entries[write++] = e;
    }
    entries.resize(write);
}

struct Run {
    std::vector<Entry> entries;
    std::vector<Value> prefix;
};
struct Partition {
    std::vector<Run> runs;
    Value total;
};

template <bool Account>
size_t lower(const Run &run, uint32_t key, Stats &stats) {
    size_t first = 0, count = run.entries.size();
    while (count) {
        size_t step = count / 2, mid = first + step;
        if constexpr (Account) ++stats.search_comparisons;
        if (run.entries[mid].key < key) { first = mid + 1; count -= step + 1; }
        else count = step;
    }
    return first;
}

template <bool Account>
class Model {
    const Config &c;
    Method method;
    Geometry geometry;
    std::vector<Value> base;
    std::vector<Partition> partitions;
    uint32_t partition_span;
    unsigned partition_shift;
    uint64_t pending_entries = 0, pending_runs = 0, pending_bytes = 0;

    void apply_entries(std::span<const Entry> entries) {
        for (const Entry &e : entries) {
            base[e.key] += e.value;
            if constexpr (Account) stats.leaf_base_updates += e.key < geometry.leaves;
        }
        if constexpr (Account) stats.base_updates += entries.size();
    }
    void expand_and_apply(std::span<const Entry> leaves) {
        std::vector<Entry> expanded;
        expanded.reserve(leaves.size() * geometry.spans.size());
        for (const Entry &e : leaves) geometry.expand(e.key, e.value, expanded);
        compact<Account>(expanded, stats);
        apply_entries(expanded);
    }
    static uint64_t bytes(const Run &run) {
        return run.entries.capacity() * sizeof(Entry)
            + run.prefix.capacity() * sizeof(Value);
    }
    void fold(Partition &partition) {
        if (partition.runs.empty()) return;
        std::vector<Entry> merged;
        size_t size = 0;
        for (const Run &run : partition.runs) size += run.entries.size();
        merged.reserve(size);
        for (const Run &run : partition.runs) {
            merged.insert(merged.end(), run.entries.begin(), run.entries.end());
            if constexpr (Account) {
                pending_bytes -= bytes(run);
                pending_entries -= run.entries.size();
                --pending_runs;
            }
        }
        compact<Account>(merged, stats);
        if (method == Method::location_runs) expand_and_apply(merged);
        else apply_entries(merged);
        partition.runs.clear();
        partition.total = {};
        if constexpr (Account) ++stats.folds;
    }
    void append(uint32_t id, std::span<const Entry> leaves) {
        Partition &p = partitions[id];
        Run run;
        Value total;
        for (const Entry &e : leaves) total += e.value;
        if (method == Method::ancestor_runs) {
            run.entries.reserve(leaves.size() * geometry.spans.size());
            for (const Entry &e : leaves) geometry.expand(e.key, e.value, run.entries);
            if constexpr (Account) stats.foreground_entries += run.entries.size();
            compact<Account>(run.entries, stats);
        } else {
            run.entries.assign(leaves.begin(), leaves.end());
            run.prefix.reserve(run.entries.size() + 1);
            run.prefix.push_back({});
            for (const Entry &e : run.entries) run.prefix.push_back(run.prefix.back() + e.value);
            if constexpr (Account) stats.prefix_entries_written += run.prefix.size();
        }
        p.total += total;
        if constexpr (Account) {
            ++stats.partition_total_updates;
            stats.run_entries_written += run.entries.size();
        }
        if constexpr (Account) {
            pending_entries += run.entries.size();
            pending_bytes += bytes(run);
            ++pending_runs;
        }
        p.runs.push_back(std::move(run));
        if constexpr (Account) {
            stats.peak_pending_entries = std::max(stats.peak_pending_entries, pending_entries);
            stats.peak_pending_runs = std::max(stats.peak_pending_runs, pending_runs);
            stats.peak_pending_payload_bytes = std::max(stats.peak_pending_payload_bytes, pending_bytes);
        }
        if (p.runs.size() >= c.max_runs) fold(p);
    }

public:
    Stats stats;
    Model(const Config &config, Method selected)
        : c(config), method(selected), geometry(c), base(geometry.nodes),
          partitions(selected == Method::eager || selected == Method::eager_batch
              ? 0 : 1u << c.partition_bits),
          partition_span(geometry.leaves >> c.partition_bits),
          partition_shift(c.leaf_bits - c.partition_bits) {
        stats.base_bytes = base.size() * sizeof(Value);
        stats.partition_metadata_bytes = partitions.size() * sizeof(Partition);
    }
    void apply(const Batch &batch) {
        if (method == Method::eager) {
            for (const Mutation &m : batch.mutations) {
                for (size_t l = 0; l < geometry.spans.size(); ++l)
                    base[geometry.offsets[l] + (m.key >> geometry.shifts[l])] += m.delta;
            }
            if constexpr (Account) {
                stats.foreground_entries += batch.mutations.size() * geometry.spans.size();
                stats.base_updates += batch.mutations.size() * geometry.spans.size();
                stats.leaf_base_updates += batch.mutations.size();
            }
            return;
        }
        if (method == Method::eager_batch) {
            std::vector<Entry> leaves;
            leaves.reserve(batch.mutations.size());
            for (const Mutation &m : batch.mutations) leaves.push_back({m.key, m.delta});
            if constexpr (Account) stats.foreground_entries += leaves.size();
            compact<Account>(leaves, stats);
            std::vector<Entry> expanded;
            expanded.reserve(leaves.size() * geometry.spans.size());
            for (const Entry &e : leaves) geometry.expand(e.key, e.value, expanded);
            if constexpr (Account) stats.foreground_entries += expanded.size();
            compact<Account>(expanded, stats);
            apply_entries(expanded);
            return;
        }
        std::vector<Entry> leaves;
        leaves.reserve(batch.mutations.size());
        for (const Mutation &m : batch.mutations) leaves.push_back({m.key, m.delta});
        if constexpr (Account) {
            // Both buffered methods get the same initial leaf coalescing.
            stats.foreground_entries += leaves.size();
        }
        compact<Account>(leaves, stats);
        for (size_t i = 0; i < leaves.size();) {
            uint32_t id = leaves[i].key >> partition_shift;
            size_t end = i + 1;
            while (end < leaves.size() && (leaves[end].key >> partition_shift) == id) ++end;
            append(id, std::span(leaves).subspan(i, end - i));
            i = end;
        }
    }
    Value query(uint32_t lo, uint32_t hi) {
        if (lo == hi) return {};
        Value result;
        const auto nodes = geometry.cover(lo, hi);
        for (uint32_t node : nodes) result += base[node];
        if constexpr (Account) stats.base_query_nodes += nodes.size();
        if (partitions.empty()) return result;
        uint32_t first = lo >> partition_shift, last = (hi - 1) >> partition_shift;
        for (uint32_t id = first; id <= last; ++id) {
            if constexpr (Account) ++stats.partition_consults;
            const Partition &p = partitions[id];
            if (lo <= id * partition_span && hi >= (id + 1) * partition_span) {
                result += p.total;
                if constexpr (Account) ++stats.partition_total_reads;
                continue;
            }
            for (const Run &run : p.runs) {
                if constexpr (Account) ++stats.run_probes;
                if (method == Method::location_runs) {
                    size_t a = lower<Account>(run, lo, stats);
                    size_t b = lower<Account>(run, hi, stats);
                    result += run.prefix[b] - run.prefix[a];
                } else {
                    for (uint32_t node : nodes) {
                        size_t i = lower<Account>(run, node, stats);
                        if (i < run.entries.size() && run.entries[i].key == node)
                            result += run.entries[i].value;
                    }
                }
            }
        }
        return result;
    }
    void drain() {
        const auto before = stats.base_updates;
        for (auto &p : partitions) fold(p);
        stats.drain_base_updates = stats.base_updates - before;
        stats.pending_entries_at_end = pending_entries;
        assert(pending_entries == 0 && pending_runs == 0 && pending_bytes == 0);
    }
};

template <bool Account>
Outcome run(const Trace &trace, Method method) {
    Model<Account> model(trace.config, method);
    Outcome out;
    out.mutation_checksum = trace.mutation_checksum;
    auto start = std::chrono::steady_clock::now();
    for (const Batch &batch : trace.batches) {
        model.apply(batch);
        for (const Query &query : batch.queries) {
            Value got = model.query(query.lo, query.hi);
            out.correct &= got == query.expected;
            out.checksum = digest(out.checksum, got);
        }
    }
    model.drain();
    Value final = model.query(0, 1u << trace.config.leaf_bits);
    out.correct &= final == trace.final;
    out.checksum = digest(out.checksum, final);
    auto stop = std::chrono::steady_clock::now();
    out.seconds = std::chrono::duration<double>(stop - start).count();
    out.stats = model.stats;
    out.correct &= out.checksum == trace.checksum;
    return out;
}

} // namespace

std::vector<Config> cases(const std::string &profile) {
    std::vector<Config> out;
    Config base;
    if (profile == "smoke") { base.leaf_bits = 8; base.fanout_bits = 2; base.updates = 512; base.batch = 32; }
    else assert(profile == "screen");
    out.push_back(base);
    auto add = [&](const char *label) -> Config & { out.push_back(base); out.back().label = label; return out.back(); };
    add("hot").locality = "hot";
    add("single").locality = "single";
    add("monotonic").locality = "monotonic";
    add("root").query_shape = "root";
    add("narrow").query_shape = "narrow";
    add("read_heavy").queries_per_batch = 64;
    add("one_partition").partition_bits = 0;
    add("many_partitions").partition_bits = profile == "smoke" ? 6 : 8;
    add("deep").fanout_bits = 1;
    add("small_batch").batch = profile == "smoke" ? 4 : 16;
    add("large_batch").batch = profile == "smoke" ? 128 : 1024;
    add("long_buffer").max_runs = 16;
    if (profile == "screen") add("large_base").leaf_bits = 20;
    return out;
}

Trace make_trace(const Config &c) {
    assert(c.batch && c.updates % c.batch == 0 && c.max_runs > 0);
    assert(c.partition_bits <= c.leaf_bits);
    Trace trace;
    trace.config = c;
    const uint32_t n = 1u << c.leaf_bits;
    struct Row { bool present = false; int64_t value = 0; };
    std::vector<Row> rows(n);
    std::vector<Value> prefix(n + 1);
    uint64_t rng = c.seed;
    uint64_t query_rng = c.seed ^ 0xd1b54a32d192ed03ULL;
    for (uint32_t start = 0; start < c.updates; start += c.batch) {
        Batch batch;
        batch.mutations.reserve(c.batch);
        for (uint32_t i = 0; i < c.batch; ++i) {
            uint64_t word = random_word(rng);
            uint32_t key = static_cast<uint32_t>(word) & (n - 1);
            if (c.locality == "hot") key %= std::min(n, 64u);
            if (c.locality == "single") key = n / 2;
            if (c.locality == "monotonic") key = (start + i) % n;
            Row &row = rows[key];
            const Value old = row.present ? Value{1, row.value} : Value{};
            // 20% delete attempts, converted to inserts for absent rows.
            uint64_t operation = random_word(rng);
            bool present = !row.present || operation % 5 != 0;
            int64_t value = static_cast<int64_t>(random_word(rng) % 2001) - 1000;
            if (present && row.present && value == row.value) ++value;
            const Value now = present ? Value{1, value} : Value{};
            batch.mutations.push_back({key, now - old});
            trace.mutation_checksum = digest(trace.mutation_checksum, {key, 0});
            trace.mutation_checksum = digest(trace.mutation_checksum, now - old);
            row = {present, value};
        }
        // Oracle reads afterimages, independently of the maintained summaries.
        prefix[0] = {};
        for (uint32_t k = 0; k < n; ++k)
            prefix[k + 1] = prefix[k] + (rows[k].present ? Value{1, rows[k].value} : Value{});
        for (uint32_t i = 0; i < c.queries_per_batch; ++i) {
            uint32_t lo = 0, hi = n;
            if (c.query_shape == "narrow" || (c.query_shape == "mixed" && i % 4 != 0)) {
                lo = static_cast<uint32_t>(random_word(query_rng)) & (n - 1);
                uint32_t width = c.query_shape == "narrow" || i % 4 == 1
                    ? 7 : (i % 4 == 2 ? 64 : n / 8);
                if (i % 4 == 2 && c.query_shape != "narrow") lo = (lo / 64) * 64;
                hi = std::min(n, lo + width);
            }
            Value expected = prefix[hi] - prefix[lo];
            batch.queries.push_back({lo, hi, expected});
            trace.checksum = digest(trace.checksum, expected);
        }
        trace.batches.push_back(std::move(batch));
    }
    trace.final = prefix[n];
    trace.checksum = digest(trace.checksum, trace.final);
    return trace;
}

Outcome execute(const Trace &trace, Method method, bool accounting) {
    return accounting ? run<true>(trace, method) : run<false>(trace, method);
}

void write_csv_header() {
    std::cout << "case,method,leaf_bits,fanout_bits,partition_bits,updates,batch,queries_per_batch,max_runs,locality,query_shape,seed,mutation_checksum,checksum,correct";
#define FIELD(f) std::cout << "," #f;
    FIELD(foreground_entries) FIELD(sort_input_entries) FIELD(base_updates) FIELD(leaf_base_updates)
    FIELD(drain_base_updates) FIELD(run_entries_written) FIELD(prefix_entries_written)
    FIELD(partition_total_updates) FIELD(base_query_nodes) FIELD(partition_consults)
    FIELD(partition_total_reads) FIELD(run_probes) FIELD(search_comparisons)
    FIELD(folds) FIELD(peak_pending_entries) FIELD(peak_pending_runs)
    FIELD(peak_pending_payload_bytes) FIELD(largest_scratch_capacity_bytes)
    FIELD(base_bytes) FIELD(partition_metadata_bytes) FIELD(pending_entries_at_end)
#undef FIELD
    std::cout << '\n';
}
void write_csv_row(const Config &c, Method method, const Outcome &o) {
    std::cout << c.label << ',' << name(method) << ',' << c.leaf_bits << ',' << c.fanout_bits
        << ',' << c.partition_bits << ',' << c.updates << ',' << c.batch << ',' << c.queries_per_batch
        << ',' << c.max_runs << ',' << c.locality << ',' << c.query_shape << ',' << c.seed
        << ',' << o.mutation_checksum << ',' << o.checksum << ',' << o.correct;
#define FIELD(f) std::cout << ',' << o.stats.f;
    FIELD(foreground_entries) FIELD(sort_input_entries) FIELD(base_updates) FIELD(leaf_base_updates)
    FIELD(drain_base_updates) FIELD(run_entries_written) FIELD(prefix_entries_written)
    FIELD(partition_total_updates) FIELD(base_query_nodes) FIELD(partition_consults)
    FIELD(partition_total_reads) FIELD(run_probes) FIELD(search_comparisons)
    FIELD(folds) FIELD(peak_pending_entries) FIELD(peak_pending_runs)
    FIELD(peak_pending_payload_bytes) FIELD(largest_scratch_capacity_bytes)
    FIELD(base_bytes) FIELD(partition_metadata_bytes) FIELD(pending_entries_at_end)
#undef FIELD
    std::cout << '\n';
}

} // namespace aggregate_deltas
