// Predicate-mask emit timings (predicate.h) on one real column: every block
// selected, no fine input, one 256-bit mask per block written to a reused
// plane. Arms per predicate:
//   emit       predicate_masks, per-call dictionary translation
//   emit_dup   the same call again: an exact duplicate control for noise
//   cached     the same with a warm dictionary_cache (coded images only)
//   decode     decode-then-compare control (reference_predicate_masks)
//   plain      a native u64 array compared into the same mask plane
// Each trial runs every arm once in a fixed-seed shuffled order. Raw trial
// nanoseconds are printed. Local VM timings are direction only.
#include "material.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>

namespace {
using namespace frame_encoding;

struct arm {
    const char *name;
    std::function<u64()> call;
    std::vector<double> ns = {};
    u64 result = 0;
};

void time_arms(std::vector<arm> &arms, int trials) {
    for (auto &a : arms)
        a.result = a.call();  // warm, and the answer each arm must keep giving
    std::vector<std::size_t> order(arms.size());
    u64 seed = 0x2545f4914f6cdd1du;
    for (int t = 0; t < trials; ++t) {
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        for (std::size_t i = order.size(); i > 1; --i) {
            seed = seed * 6364136223846793005u + 1442695040888963407u;
            std::swap(order[i - 1], order[(seed >> 33) % i]);
        }
        for (std::size_t i : order) {
            auto &a = arms[i];
            const auto start = std::chrono::steady_clock::now();
            const u64 r = a.call();
            a.ns.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count());
            if (r != a.result)
                std::abort();
        }
    }
}

// Rows of the plain array satisfying the interval, as the same mask plane.
u64 plain_masks(const std::vector<u64> &v, const value_interval &in, std::span<u64> out) {
    const u64 span = in.last - in.lo, flip = in.negate ? ~u64{0} : 0;
    u64 total = 0;
    for (std::size_t w = 0; w < v.size() / 64; ++w) {
        u64 bits = 0;
        for (unsigned i = 0; i < 64; ++i)
            bits |= static_cast<u64>(v[w * 64 + i] - in.lo <= span) << i;
        bits = (in.empty ? 0 : bits) ^ flip;
        out[w] = bits;
        total += static_cast<u64>(std::popcount(bits));
    }
    return total;
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s NAME FILE.u64 [--checkpoints=native|packed|every64|none] [--delta-exceptions] "
                     "[--dictionary=none|sorted|frequency|arrival] [--trials=N]\n",
                     argv[0]);
        return 2;
    }
    encode_options options;
    options.bind_decoders = false;
    int trials = 21;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--delta-exceptions") options.delta_exceptions = true;
        else if (a == "--dictionary=none") options.dictionary = dictionary_order::none;
        else if (a == "--dictionary=sorted") options.dictionary = dictionary_order::sorted;
        else if (a == "--dictionary=frequency") options.dictionary = dictionary_order::frequency;
        else if (a == "--dictionary=arrival") options.dictionary = dictionary_order::arrival;
        else if (a == "--checkpoints=native") options.delta_checkpoints = checkpoints::native;
        else if (a == "--checkpoints=packed") options.delta_checkpoints = checkpoints::packed;
        else if (a == "--checkpoints=every64") options.delta_checkpoints = checkpoints::every64;
        else if (a == "--checkpoints=none") options.delta_checkpoints = checkpoints::none;
        else if (a.rfind("--trials=", 0) == 0) trials = std::stoi(a.substr(9));
        else return 2;
    }
    std::vector<u64> values;
    {
        std::FILE *f = std::fopen(argv[2], "rb");
        if (!f)
            return 2;
        std::fseek(f, 0, SEEK_END);
        const long end = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (end <= 0 || end % 8 || (end / 8) % block_rows)
            return 2;
        values.resize(static_cast<std::size_t>(end) / 8);
        if (std::fread(values.data(), 8, values.size(), f) != values.size())
            return 2;
        std::fclose(f);
    }
    const column c = encode(values, options);
    const std::size_t nblocks = c.blocks.size();
    block_mask all(nblocks);
    for (std::size_t b = 0; b < nblocks; ++b)
        all.set(b);

    std::vector<u64> sorted(values);
    std::sort(sorted.begin(), sorted.end());
    auto q = [&](unsigned per_mille) { return sorted[std::min(sorted.size() - 1, sorted.size() * per_mille / 1000)]; };
    u64 mode = sorted[0], run = 0, best = 0;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        run = i && sorted[i] == sorted[i - 1] ? run + 1 : 1;
        if (run > best)
            best = run, mode = sorted[i];
    }
    const u64 point = values[values.size() / 3];  // a present value, usually rare
    struct named {
        const char *name;
        value_predicate p;
    };
    const named preds[] = {
        {"ge_median", {predicate_op::ge, q(500)}},
        {"lt_median", {predicate_op::lt, q(500)}},
        {"between_p25_p75", {predicate_op::between, q(250), q(750)}},
        {"between_1pct", {predicate_op::between, q(500), std::max(q(510), q(500) + 1)}},
        {"eq_mode", {predicate_op::eq, mode}},
        {"eq_point", {predicate_op::eq, point}},
        {"ne_mode", {predicate_op::ne, mode}},
    };

    std::map<std::string, std::size_t> kinds;
    const char *kind_names[] = {"constant", "runs", "packed", "delta", "patched", "delta_patched", "arrival"};
    for (std::size_t b = 0; b < nblocks; ++b)
        ++kinds[std::string(is_coded(c, b) ? "coded_" : "") + kind_names[static_cast<unsigned>(c.blocks[b].type)]];

    std::printf("{\"name\":\"%s\",\"checkpoints\":\"%s\",\"delta_exceptions\":%s,\"dictionary\":\"%s\",\"rows\":%zu,"
                "\"bits_per_row\":%.3f,\"trials\":%d,\"kinds\":{",
                argv[1], checkpoint_name(options.delta_checkpoints), options.delta_exceptions ? "true" : "false",
                dictionary_name(options.dictionary), c.rows, 8.0 * c.total_bytes() / c.rows, trials);
    bool first = true;
    for (const auto &[k, v] : kinds) {
        std::printf("%s\"%s\":%zu", first ? "" : ",", k.c_str(), v);
        first = false;
    }
    std::printf("},\"predicates\":{");
    std::vector<u64> out(4 * nblocks), dup(4 * nblocks), cached_out(4 * nblocks), control(4 * nblocks),
        plain(4 * nblocks);
    dictionary_cache cache;
    const bool coded = c.coded_blocks() > 0;
    for (std::size_t pi = 0; pi < std::size(preds); ++pi) {
        const auto &[pname, p] = preds[pi];
        std::vector<arm> arms;
        arms.push_back({"emit", [&] { return predicate_masks(c, all, p, out); }});
        arms.push_back({"emit_dup", [&] { return predicate_masks(c, all, p, dup); }});
        if (coded)
            arms.push_back({"cached", [&] { return predicate_masks(c, all, p, cached_out, {}, &cache); }});
        arms.push_back({"decode", [&] { return reference_predicate_masks(c, all, p, control); }});
        arms.push_back({"plain", [&] { return plain_masks(values, interval_of(p), plain); }});
        time_arms(arms, trials);
        if (out != control || dup != control || out != plain || (coded && cached_out != control))
            return 1;  // every arm emits the same plane
        std::printf("%s\"%s\":{\"op\":\"%s\",\"a\":%llu,\"b\":%llu,\"selected\":%llu", pi ? "," : "", pname,
                    predicate_name(p.op), static_cast<unsigned long long>(p.a), static_cast<unsigned long long>(p.b),
                    static_cast<unsigned long long>(arms[0].result));
        for (auto &a : arms) {
            std::printf(",\"%s_ns\":{\"trials\":[", a.name);
            for (std::size_t t = 0; t < a.ns.size(); ++t)
                std::printf("%s%.0f", t ? "," : "", a.ns[t]);
            auto s = a.ns;
            std::sort(s.begin(), s.end());
            std::printf("],\"median\":%.0f}", s[s.size() / 2]);
        }
        std::printf("}");
    }
    std::printf("}}\n");
}
