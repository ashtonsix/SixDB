#include "model.h"
#include "point_cases.h"
#include <benchmark/benchmark.h>
#include <cstdio>
#include <cstring>
#include <bit>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace tuple_point_probe {
using namespace tuple_runtime;
struct candidate { std::string id; unsigned bytes; std::array<code, 4> codes; };
struct access { const char* id; bool write; unsigned count; std::array<byte, 4> ranks; };
constexpr std::array<access, 7> accesses{{
    {"R_AC", false, 2, {0,2}}, {"R_0", false, 4, {0,2,1,3}}, {"R_1", false, 4, {1,0,3,2}},
    {"W_A", true, 1, {0}}, {"W_AC", true, 2, {0,2}}, {"W_AB", true, 2, {0,1}}, {"W_all", true, 4, {0,2,1,3}}
}};
constexpr std::array<unsigned, 4> widths{1,7,3,5};
std::vector<candidate> candidates() {
    std::istringstream in(small_candidate_tsv);
    std::string line;
    std::vector<candidate> result;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line.starts_with("id\t")) continue;
        std::istringstream row(line);
        candidate c; row >> c.id >> c.bytes;
        for (unsigned i = 0; i < 4; ++i) {
            unsigned offset = 99, shift = 99; row >> offset >> shift;
            c.codes[i] = {byte(offset), byte(shift), byte(widths[i])};
        }
        if (!row || c.bytes < 2 || c.bytes > 3) { std::fprintf(stderr,"invalid candidate TSV: %s\n", line.c_str()); std::abort(); }
        mapping m; m.fill(255); m[0] = 0;
        if (!prepare_read({c.bytes, c.codes}, m)) std::abort();
        result.push_back(c);
    }
    if (result.empty()) std::abort();
    return result;
}
struct prepared {
    std::uint64_t invalid = 0;
    std::uint32_t changed = 0;
    std::array<byte, 4> offset{}, shift{}, mask{};
    byte count = 0, bytes = 0, coverage = 0;
};
prepared bind(const candidate& c, const access& a) {
    prepared p; p.count = a.count; p.bytes = c.bytes;
    for (unsigned j = 0; j < a.count; ++j) {
        const auto code = c.codes[a.ranks[j]];
        p.offset[j] = code.offset; p.shift[j] = code.shift; p.mask[j] = (1u << code.width) - 1;
        p.invalid |= std::uint64_t(byte(~p.mask[j])) << (8 * j);
        p.changed |= std::uint32_t(p.mask[j]) << (8 * code.offset + code.shift);
        p.coverage |= 1u << code.offset;
    }
    return p;
}
inline std::uint32_t load_word(const byte* row, unsigned bytes) {
    std::uint16_t first; std::memcpy(&first, row, 2);
    return first | (bytes == 3 ? std::uint32_t(row[2]) << 16 : 0);
}
using operation = std::uint64_t (*)(const prepared&, byte*, std::uint64_t);
template <unsigned N, bool Write, bool Word>
[[gnu::noinline]] std::uint64_t execute(const prepared& p, byte* row, std::uint64_t input) {
    if constexpr (!Write) {
        std::uint64_t result = 0;
        if constexpr (Word) {
            const auto word = load_word(row, p.bytes);
            for (unsigned j = 0; j < N; ++j)
                result |= std::uint64_t((word >> (8 * p.offset[j] + p.shift[j])) & p.mask[j]) << (8 * j);
        } else for (unsigned j = 0; j < N; ++j)
            result |= std::uint64_t((row[p.offset[j]] >> p.shift[j]) & p.mask[j]) << (8 * j);
        return result;
    } else {
        // Return zero on admission failure, otherwise issued-byte coverage.
        // Every writer here selects at least one code. Holes above N are ignored.
        if (input & p.invalid) return 0;
        if constexpr (N == 1) {
            // A single-code write has exactly one destination byte. There is
            // no physical-byte traversal or coalesced word load to perform.
            const unsigned offset = p.offset[0], shift = p.shift[0];
            row[offset] = (row[offset] & byte(~(p.mask[0] << shift))) | (byte(input) << shift);
        } else if constexpr (Word) {
            auto updated = load_word(row, p.bytes) & ~p.changed;
            for (unsigned j = 0; j < N; ++j)
                updated |= std::uint32_t(byte(input >> (8 * j))) << (8 * p.offset[j] + p.shift[j]);
            for (unsigned b = 0; b < 3; ++b)
                if (p.coverage & (1u << b)) row[b] = updated >> (8 * b);
        } else {
            for (unsigned b = 0; b < 3; ++b) {
                if (!(p.coverage & (1u << b))) continue;
                byte updated = row[b] & byte(~(p.changed >> (8 * b)));
                for (unsigned j = 0; j < N; ++j)
                    if (p.offset[j] == b) updated |= byte(input >> (8 * j)) << p.shift[j];
                row[b] = updated;
            }
        }
        return p.coverage;
    }
}
operation select(const access& a, bool word) {
    auto get = [&]<unsigned N>() -> operation {
        if (a.write) {
            if constexpr (N == 1) return execute<1,true,false>;
            return word ? execute<N,true,true> : execute<N,true,false>;
        }
        return word ? execute<N,false,true> : execute<N,false,false>;
    };
    if (a.count == 1) return get.template operator()<1>();
    if (a.count == 2) return get.template operator()<2>();
    return get.template operator()<4>();
}
std::uint64_t mix(std::uint64_t v) {
    v ^= v >> 30; v *= 0xbf58476d1ce4e5b9ULL; v ^= v >> 27; v *= 0x94d049bb133111ebULL; return v ^ (v >> 31);
}
bool owned_write(const prepared& p, operation call, byte* row, std::uint64_t input,
                 unsigned available_effects, byte& before_write_coverage) {
    // Binding owns the full readable tuple envelope. Here each selected byte
    // consumes one journal slot; real owners may coalesce adjacent spans.
    if (input & p.invalid || unsigned(std::popcount(p.coverage)) > available_effects) return false;
    before_write_coverage |= p.coverage; // no-fail owner hook before stores
    const auto issued = call(p,row,input);
    if (issued != p.coverage) std::abort();
    return true;
}
void check(const std::vector<candidate>& cs) {
    unsigned count = 0;
    for (unsigned width = 1; width <= 8; ++width) for (unsigned shift = 0; shift + width <= 8; ++shift)
    for (unsigned offset = 0; offset < 3; ++offset) for (unsigned trial = 0; trial < 256; ++trial) {
        prepared p; p.count = 1; p.bytes = offset+1; p.coverage = 1u << offset;
        p.offset[0] = offset; p.shift[0] = shift; p.mask[0] = (1u << width)-1; p.invalid = byte(~p.mask[0]);
        std::array<byte,5> row, expected;
        row.fill(byte(trial * 13 + 71)); expected = row;
        const auto input = trial & p.mask[0];
        for (unsigned b = 0; b < width; ++b)
            expected[1+offset] = (expected[1+offset] & ~(1u << (shift+b))) | (((input>>b)&1) << (shift+b));
        if (execute<1,true,false>(p,row.data()+1,input) != p.coverage || row != expected) std::abort();
        if (width < 8 && (execute<1,true,false>(p,row.data()+1,input|(1u<<width)) || row != expected)) std::abort();
    }
    std::puts("27648 single-code byte-write width/shift/offset cases passed");
    for (const auto& c : cs) for (const auto& a : accesses) for (bool word : {false,true}) {
        auto p = bind(c, a); auto op = select(a, word);
        mapping m; m.fill(255);
        for (unsigned j = 0; j < a.count; ++j) m[j] = a.ranks[j];
        for (unsigned trial = 0; trial < 256; ++trial) {
            // Exact 2/3-byte span plus external guards. Scalar reference uses
            // original schema, independent of the bound masks/shift recipe.
            std::array<byte, 5> data, expected;
            for (unsigned i = 0; i < 5; ++i) data[i] = mix(trial * 7 + i);
            expected = data;
            const auto input = mix(trial + 923) & ~p.invalid;
            const auto result = op(p, data.data() + 1, input);
            if (a.write) {
                std::array<byte,64> bytes{}; std::memcpy(bytes.data(), &input, 8);
                reference_write({c.bytes,c.codes}, m, expected.data() + 1, bytes);
                if (data != expected || result != p.coverage) std::abort();
                // The same logical map changes byte coverage across layouts.
                // Exhausted effect capacity rejects before stores/effects.
                byte effects = 0;
                data = expected;
                if (owned_write(p,op,data.data()+1,input,std::popcount(p.coverage)-1,effects) ||
                    effects || data != expected) std::abort();
                if (!owned_write(p,op,data.data()+1,input,std::popcount(p.coverage),effects) ||
                    effects != p.coverage || data != expected) std::abort();
                data = expected;
                const unsigned bit = widths[a.ranks[trial % a.count]];
                if (op(p, data.data() + 1, input | (1ULL << (8 * (trial % a.count) + bit))) || data != expected) std::abort();
            } else {
                auto ref = reference_read({c.bytes,c.codes}, m, data.data() + 1);
                std::uint64_t expected_result; std::memcpy(&expected_result, ref.data(), 8);
                if (result != expected_result || data != expected) std::abort();
            }
            ++count;
        }
    }
    std::printf("%u scalar8 candidate/map/recipe cases with checked writes and spare-bit preservation passed\n", count);
}
void bench(benchmark::State& state, const candidate& c, const access& a, bool word, bool preparing) {
    if (preparing) {
        for (auto _ : state) { auto p = bind(c,a); benchmark::DoNotOptimize(p); }
        state.SetItemsProcessed(state.iterations()); state.counters["items_per_iteration"] = 1; return;
    }
    const auto p = bind(c,a); auto op = select(a,word); asm volatile("" : "+r"(op));
    std::array<byte, 1024 * 16> data;
    for (unsigned i = 0; i < data.size(); ++i) data[i] = mix(i + 871);
    std::array<unsigned,8192> trace;
    for (unsigned i = 0; i < trace.size(); ++i) trace[i] = mix(i + 873) & 1023;
    unsigned cursor = 0, phase = 0;
    std::uint64_t input[2]{mix(170) & ~p.invalid, mix(371) & ~p.invalid};
    for (auto _ : state) {
        std::uint64_t checksum = 0;
        for (unsigned i = 0; i < 256; ++i) checksum += op(p, data.data() + trace[(cursor + i) & 8191] * 16, input[phase]);
        asm volatile("" : "+r"(checksum)); benchmark::ClobberMemory();
        cursor = (cursor + 256) & 8191; phase ^= 1;
    }
    state.SetItemsProcessed(state.iterations() * 256); state.counters["items_per_iteration"] = 256;
    state.counters["binding_bytes"] = sizeof(prepared); state.counters["unit_bytes"] = c.bytes;
}
void describe(std::ostream& out) {
    out << "{\"contract\":\"byte8-preserve-v1\",\"placement\":\"1024 rows at stride16; repeated 8192-entry random trace; two alternating inputs; output-sum or coverage-sum sink\",\"recipes\":[\"word\",\"bytes\"],\"candidates\":[";
    bool first = true;
    for (const auto& c : candidates()) {
        if (!first) out << ','; first = false;
        out << "{\"id\":\"" << c.id << "\",\"unit_bytes\":" << c.bytes << ",\"codes\":[";
        for (unsigned i = 0; i < 4; ++i) {
            if (i) out << ','; const auto code = c.codes[i];
            out << '[' << +code.offset << ',' << +code.shift << ',' << +code.width << ']';
        }
        out << "]}";
    }
    out << "],\"operations\":["; first = true;
    for (const auto& a : accesses) {
        if (!first) out << ','; first = false;
        out << "{\"id\":\"" << a.id << "\",\"write\":" << a.write << ",\"map\":[";
        for (unsigned i = 0; i < 8; ++i) { if (i) out << ','; out << (i < a.count ? +a.ranks[i] : 255); }
        out << "]}";
    }
    out << "]}";
}
template <unsigned N, bool Write>
[[gnu::noinline]] std::uint64_t unpacked(const prepared& p, byte* row, std::uint64_t input) {
    if constexpr (Write) {
        if (input & p.invalid) return 0;
        unsigned coverage = 0;
        for (unsigned j = 0; j < N; ++j) {
            const unsigned rank = p.offset[j], mask = p.mask[j];
            row[rank] = (row[rank] & byte(~mask)) | byte(input >> (8 * j));
            coverage |= 1u << rank;
        }
        return coverage;
    } else {
        std::uint64_t result = 0;
        for (unsigned j = 0; j < N; ++j)
            result |= std::uint64_t(row[p.offset[j]] & p.mask[j]) << (8 * j);
        return result;
    }
}
void unpacked_bench(benchmark::State& state, const access& a) {
    candidate plain{"unpacked",4,{}};
    for (unsigned i = 0; i < 4; ++i) plain.codes[i] = {byte(i),0,byte(widths[i])};
    const auto p = bind(plain,a);
    using call_type = std::uint64_t (*)(const prepared&, byte*, std::uint64_t);
    auto get = [&]<unsigned N>() -> call_type { return a.write ? unpacked<N,true> : unpacked<N,false>; };
    call_type call = a.count == 1 ? get.template operator()<1>() : a.count == 2 ? get.template operator()<2>() : get.template operator()<4>();
    asm volatile("" : "+r"(call));
    std::array<byte, 1024 * 16> data;
    for (unsigned i = 0; i < data.size(); ++i) data[i] = mix(i + 871);
    std::array<unsigned,8192> trace;
    for (unsigned i = 0; i < trace.size(); ++i) trace[i] = mix(i + 873) & 1023;
    std::uint64_t invalid = 0;
    for (unsigned j = 0; j < a.count; ++j) invalid |= std::uint64_t(byte(~((1u << widths[a.ranks[j]]) - 1))) << (8 * j);
    std::uint64_t input[2]{mix(170) & ~invalid, mix(371) & ~invalid};
    mapping map; map.fill(255);
    for (unsigned j = 0; j < a.count; ++j) map[j] = a.ranks[j];
    for (unsigned trial = 0; trial < 128; ++trial) {
        std::array<byte,4> row{}, expected{};
        for (unsigned i = 0; i < 4; ++i) row[i] = mix(trial * 4 + i);
        expected = row;
        const auto got = call(p,row.data(),input[trial & 1]);
        if (a.write) {
            std::array<byte,64> bytes{}; std::memcpy(bytes.data(), &input[trial & 1], 8);
            reference_write({4,plain.codes},map,expected.data(),bytes);
            if (row != expected || got != p.coverage) std::abort();
        } else {
            auto result = reference_read({4,plain.codes},map,row.data());
            std::uint64_t ref; std::memcpy(&ref,result.data(),8);
            if (got != ref || row != expected) std::abort();
        }
    }
    unsigned cursor = 0, phase = 0;
    for (auto _ : state) {
        std::uint64_t checksum = 0;
        for (unsigned i = 0; i < 256; ++i) checksum += call(p, data.data() + trace[(cursor + i) & 8191] * 16, input[phase]);
        asm volatile("" : "+r"(checksum)); benchmark::ClobberMemory();
        cursor = (cursor + 256) & 8191; phase ^= 1;
    }
    state.SetItemsProcessed(state.iterations() * 256); state.counters["items_per_iteration"] = 256;
    state.counters["unit_bytes"] = 4;
}
void mixed(benchmark::State& state, const candidate& c, unsigned write_percent, bool word) {
    std::array<prepared,7> plans;
    std::array<operation,7> calls;
    std::array<std::array<std::uint64_t,2>,7> inputs;
    for (unsigned j = 0; j < 7; ++j) {
        plans[j] = bind(c,accesses[j]); calls[j] = select(accesses[j],word);
        for (unsigned n = 0; n < 2; ++n) inputs[j][n] = mix(j * 2 + n + 17) & ~plans[j].invalid;
    }
    // All seven already-bound operations coexist, unlike isolated costs.
    // Observe the table once; individual calls carry no memory barrier.
    benchmark::DoNotOptimize(calls);
    std::array<byte,1024*16> data;
    for (unsigned i = 0; i < data.size(); ++i) data[i] = mix(i + 871);
    std::array<unsigned,8192> rows,ops;
    std::array<unsigned,7> counts{};
    for (unsigned i = 0; i < rows.size(); ++i) {
        rows[i] = mix(i + 873) & 1023;
        const auto pick = mix(i + 12871);
        const unsigned part = (pick >> 16) % 10;
        ops[i] = pick % 100 < write_percent ? (part < 4 ? 3 : part < 8 ? 4 : part < 9 ? 5 : 6) :
                                             (part < 5 ? 0 : ((pick >> 32) & 1) ? 1 : 2);
        ++counts[ops[i]];
    }
    unsigned cursor = 0, phase = 0;
    for (auto _ : state) {
        std::uint64_t checksum = 0;
        for (unsigned i = 0; i < 256; ++i) {
            const unsigned n = (cursor + i) & 8191, op = ops[n];
            checksum += calls[op](plans[op], data.data() + rows[n] * 16, inputs[op][phase]);
        }
        asm volatile("" : "+r"(checksum)); benchmark::ClobberMemory();
        cursor = (cursor + 256) & 8191; phase ^= 1;
    }
    state.SetItemsProcessed(state.iterations() * 256); state.counters["items_per_iteration"] = 256;
    for (unsigned j = 0; j < 7; ++j) state.counters[std::string("fraction_") + accesses[j].id] = double(counts[j]) / 8192;
}
void add() {
    static const auto all = candidates();
    check(all);
    for (const auto& a : accesses) {
        const auto label = std::string("small/unpacked/") + a.id;
        benchmark::RegisterBenchmark(label.c_str(), [&a](benchmark::State& state) { unpacked_bench(state, a); });
    }
    for (const auto& c : all) for (unsigned w : {5,50,90}) for (bool word : {false,true}) {
        const auto label = "mixture/" + c.id + '/' + std::to_string(w) + (word ? "/word" : "/bytes");
        benchmark::RegisterBenchmark(label.c_str(), [&c,w,word](benchmark::State& s) { mixed(s,c,w,word); });
    }
    for (const auto& c : all) for (const auto& a : accesses) {
        for (bool word : {false,true}) {
            const auto name = "small/" + c.id + '/' + a.id + (word ? "/word" : "/bytes");
            benchmark::RegisterBenchmark(name.c_str(), [&c,&a,word](benchmark::State& s) { bench(s,c,a,word,false); });
        }
        const auto name = "small/" + c.id + '/' + a.id + "/prepare";
        benchmark::RegisterBenchmark(name.c_str(), [&c,&a](benchmark::State& s) { bench(s,c,a,false,true); });
    }
}
}
void register_tuple_point() { tuple_point_probe::add(); }

void describe_tuple_point(std::ostream& out) { tuple_point_probe::describe(out); }
