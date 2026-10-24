#include "history.h"

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_set>

namespace frame_histories {
namespace {
bool valid(field f) { return f == field::a || f == field::b; }
result<void> domains(std::size_t a, std::size_t b) {
    if (a != b) return std::unexpected(refusal::unequal_domains);
    if (a % block_rows) return std::unexpected(refusal::incomplete_block);
    return {};
}
result<void> validate_read(std::size_t rows, const read& r) {
    if (!valid(r.column)) return std::unexpected(refusal::bad_field);
    if (r.op != operation::sum && r.op != operation::count_ge &&
        r.op != operation::pair && r.op != operation::points)
        return std::unexpected(refusal::bad_operation);
    const std::size_t blocks = rows / block_rows;
    if (!r.coarse.empty()) {
        if (r.coarse.size() != (blocks + 63) / 64)
            return std::unexpected(refusal::bad_mask);
        if (blocks % 64 && (r.coarse.back() >> (blocks % 64)))
            return std::unexpected(refusal::bad_mask);
    }
    for (std::size_t row : r.point_rows)
        if (row >= rows) return std::unexpected(refusal::bad_row);
    if (r.op != operation::points && !r.point_rows.empty())
        return std::unexpected(refusal::bad_row);
    return {};
}
bool active(const read& r, std::size_t block) {
    return r.coarse.empty() || ((r.coarse[block / 64] >> (block % 64)) & 1);
}
result<void> validate_batch(std::size_t rows, const batch& b) {
    for (const edit& e : b.edits) {
        if (!valid(e.column)) return std::unexpected(refusal::bad_field);
        if (e.row >= rows) return std::unexpected(refusal::bad_row);
    }
    return {};
}
u64 next(u64& state) {
    state += 0x9e3779b97f4a7c15ull;
    u64 x = state;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}
} // namespace

result<answer> evaluate(std::span<const u64> a, std::span<const u64> b, const read& r) {
    if (auto d = domains(a.size(), b.size()); !d) return std::unexpected(d.error());
    if (auto v = validate_read(a.size(), r); !v) return std::unexpected(v.error());
    answer out;
    const auto column = r.column == field::a ? a : b;
    if (r.op == operation::points) {
        for (std::size_t row : r.point_rows) {
            if (!active(r, row / block_rows)) continue;
            out.sum += column[row];
            ++out.count;
        }
        return out;
    }
    for (std::size_t block = 0; block < a.size() / block_rows; ++block) {
        if (!active(r, block)) continue;
        for (std::size_t row = block * block_rows; row < (block + 1) * block_rows; ++row) {
            switch (r.op) {
            case operation::sum: out.sum += column[row]; ++out.count; break;
            case operation::count_ge: out.count += column[row] >= r.threshold; break;
            case operation::pair:
                if (a[row] >= r.threshold) { out.sum += b[row]; ++out.count; }
                break;
            case operation::points: break;
            }
        }
    }
    return out;
}

result<void> apply(columns& v, const batch& b) {
    if (auto d = domains(v.a.size(), v.b.size()); !d) return d;
    if (auto q = validate_batch(v.a.size(), b); !q) return q;
    for (const edit& e : b.edits)
        (e.column == field::a ? v.a : v.b)[e.row] = e.value;
    return {};
}

result<std::vector<expected_read>> prepare_expected(const columns& original, const trace& t) {
    if (auto d = domains(original.a.size(), original.b.size()); !d) return std::unexpected(d.error());
    // Admit all structure before copying or reading original value storage.
    for (const auto& b : t.batches)
        if (auto q = validate_batch(original.a.size(), b); !q) return std::unexpected(q.error());
    for (const auto& r : t.reads)
        if (auto q = validate_read(original.a.size(), r); !q) return std::unexpected(q.error());
    for (const auto& e : t.events)
        if ((e.kind == event_kind::edits && e.index >= t.batches.size()) ||
            (e.kind == event_kind::query && e.index >= t.reads.size()) ||
            (e.kind != event_kind::edits && e.kind != event_kind::query))
            return std::unexpected(refusal::bad_event);
    std::size_t previous_fold = 0;
    for (std::size_t at : t.fold_after_batches) {
        if (!at || at > t.batches.size() || at <= previous_fold)
            return std::unexpected(refusal::bad_event);
        previous_fold = at;
    }
    columns current = original;
    std::vector<answer> retained;
    for (const auto& r : t.reads) retained.push_back(*evaluate(original.a, original.b, r));
    std::vector<expected_read> out;
    for (std::size_t i = 0; i < t.events.size(); ++i) {
        const auto& e = t.events[i];
        if (e.kind == event_kind::edits) {
            if (auto applied = apply(current, t.batches[e.index]); !applied)
                return std::unexpected(applied.error());
        } else {
            out.push_back({i, e.index, *evaluate(current.a, current.b, t.reads[e.index]), retained[e.index]});
        }
    }
    return out;
}

result<std::vector<batch>> make_batches(const columns& v, edit_pattern pattern, edit_values values) {
    if (auto d = domains(v.a.size(), v.b.size()); !d) return std::unexpected(d.error());
    if (v.a.empty()) return std::unexpected(refusal::bad_row);
    const std::size_t blocks = v.a.size() / block_rows;
    std::array<std::unordered_set<u64>, 2> seen;
    if (values == edit_values::dictionary_novelty) {
        seen[0].insert(v.a.begin(), v.a.end());
        seen[1].insert(v.b.begin(), v.b.end());
    }
    std::array<u64, 2> absent{};
    std::vector<batch> out(8);
    for (std::size_t publication = 0; publication < out.size(); ++publication) {
        auto& edits = out[publication].edits;
        for (std::size_t command = 0; command < 64; ++command) {
            const std::size_t f = command % 2;
            const std::size_t ordinal = publication * 32 + command / 2;
            const std::size_t block = pattern == edit_pattern::repeated_hot ? 0 : ordinal % blocks;
            const std::size_t lane = pattern == edit_pattern::repeated_hot
                ? (command / 2) % 4 : (ordinal / blocks * 37 + 11) % block_rows;
            const std::size_t row = block * block_rows + lane;
            const auto& column = f ? v.b : v.a;
            u64 replacement;
            if (values == edit_values::local_replacement) {
                replacement = column[block * block_rows + (lane + publication + 1) % block_rows];
            } else {
                while (seen[f].contains(absent[f])) ++absent[f];
                replacement = absent[f];
                seen[f].insert(replacement);
            }
            edits.push_back({f ? field::b : field::a, row, replacement});
        }
    }
    return out;
}

result<std::vector<read>> make_reads(const columns& v, read_mix mix, thresholds mode,
                                     std::span<const u64> coarse) {
    if (auto d = domains(v.a.size(), v.b.size()); !d) return std::unexpected(d.error());
    if (v.a.empty()) return std::unexpected(refusal::bad_row);
    read admission;
    admission.coarse.assign(coarse.begin(), coarse.end());
    if (auto q = validate_read(v.a.size(), admission); !q) return std::unexpected(q.error());
    // Query acquisition is explicitly a preparation over accessible original
    // input, outside the oracle's inactive-block no-access guarantee.
    std::vector<u64> sorted = v.a;
    std::sort(sorted.begin(), sorted.end());
    const std::array<u64, 8> query_values{
        sorted[sorted.size() / 2], sorted[sorted.size() * 9 / 10], 0,
        std::numeric_limits<u64>::max(), sorted.front(), sorted.back(),
        u64{1} << 63, sorted[sorted.size() / 4]};
    std::vector<read> out(16);
    u64 state = 0x6672616d652d4132ull;
    for (std::size_t i = 0; i < out.size(); ++i) {
        auto& r = out[i];
        r.coarse = admission.coarse;
        r.threshold = query_values[mode == thresholds::repeated ? 0 : i % query_values.size()];
        if (mix == read_mix::reduction) {
            r.op = i % 2 ? operation::count_ge : operation::sum;
            r.column = i % 2 ? field::a : field::b;
        } else if (mix == read_mix::selection) {
            r.op = i % 4 < 2 ? operation::pair : (i % 4 == 2 ? operation::count_ge : operation::sum);
            r.column = r.op == operation::count_ge ? field::a : field::b;
        } else {
            r.op = i < 14 ? operation::points : (i == 14 ? operation::sum : operation::pair);
            r.column = field::b;
        }
        if (r.op == operation::points) {
            for (std::size_t p = 0; p < 256; ++p)
                r.point_rows.push_back(p && p % 16 == 0 ? r.point_rows.back() : next(state) % v.a.size());
        }
    }
    return out;
}

trace make_trace(std::vector<batch> batches, std::vector<read> reads, chronology order) {
    trace out;
    out.batches = std::move(batches);
    out.reads = std::move(reads);
    if (out.batches.size() >= 4) out.fold_after_batches.push_back(4);
    if (out.batches.size() >= 8) out.fold_after_batches.push_back(8);
    auto emit_reads = [&] {
        for (std::size_t i = 0; i < out.reads.size(); ++i) out.events.push_back({event_kind::query, i});
    };
    for (std::size_t i = 0; i < out.batches.size(); ++i) {
        out.events.push_back({event_kind::edits, i});
        if (order == chronology::interleaved) emit_reads();
    }
    if (order == chronology::burst_then_reads)
        for (std::size_t i = 0; i < out.batches.size(); ++i) emit_reads();
    return out;
}

fixture tiny_witness() {
    fixture f;
    f.original.a.resize(512);
    f.original.b.resize(512);
    const std::array<u64, 8> a{4, 6, 9, 3, 6, 1, 7, 2};
    const std::array<u64, 8> b{10, 20, 30, 40, 50, 60, 70, 80};
    std::copy(a.begin(), a.end(), f.original.a.begin());
    std::copy(b.begin(), b.end(), f.original.b.begin());
    // The second block is excluded by the first-block query variants.
    f.original.a[256] = 9;
    f.original.b[256] = 1000;
    f.commands.batches = {
        {{{field::a, 0, 8}, {field::b, 0, 100}, {field::b, 0, 101},
          {field::a, 1, 2}, {field::b, 2, 999}, {field::b, 2, 30},
          {field::a, 3, 13}, {field::b, 3, 103}}},
        {{{field::a, 0, 4}, {field::b, 0, 10}, {field::a, 1, 6},
          {field::a, 3, 3}, {field::b, 3, 40}}}
    };
    f.commands.reads = {
        {operation::sum, field::b, 0, {1}, {}},
        {operation::count_ge, field::a, 6, {1}, {}},
        {operation::pair, field::b, 6, {1}, {}},
        {operation::points, field::b, 0, {1}, {0, 0, 1, 2, 3, 256}},
        {operation::pair, field::b, 6, {0}, {}}
    };
    for (std::size_t stage = 0; stage < 3; ++stage) {
        if (stage) f.commands.events.push_back({event_kind::edits, stage - 1});
        for (std::size_t r = 0; r < f.commands.reads.size(); ++r)
            f.commands.events.push_back({event_kind::query, r});
    }
    f.commands.fold_after_batches = {1, 2};
    return f;
}
} // namespace frame_histories
