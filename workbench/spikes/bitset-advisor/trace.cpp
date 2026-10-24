// Prices fixed representation/preparation plans for collections of
// 4096-position groups. For each collection it measures construction,
// preparation and three demand phases (scattered membership probes, ranks
// within a few hot groups, whole-collection masked counts); for synthetic
// calibration classes it measures the same operations as an Ikea-side prior.
// It also times the cheap analysis an advisor would run. Output is JSON.
#include "../../../ikea/test/bec/bec4096_reference.h"
#include <ikea/bec.h>
#include <ikea/bec/analysis.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace {
using ikea::bitset256;
using ikea::bitset4096;
using byte = std::byte;
namespace b4 = ikea::bec4096;
namespace b2 = ikea::bec256;

std::uint64_t sink = 0;
template <class T> [[gnu::always_inline]] inline void keep(T value) {
    sink += std::uint64_t(value);
    asm volatile("" : "+r"(sink));
}

// Median per-item nanoseconds of `body`, which performs `items` operations.
double time_ns(std::size_t items, const std::function<void()> &body) {
    using clock = std::chrono::steady_clock;
    unsigned rounds = 1;
    for (;;) {
        const auto t0 = clock::now();
        for (unsigned r = 0; r < rounds; ++r)
            body();
        if (clock::now() - t0 > std::chrono::milliseconds(4) || rounds >= (1u << 20))
            break;
        rounds *= 2;
    }
    std::vector<double> samples;
    for (unsigned s = 0; s < 5; ++s) {
        const auto t0 = clock::now();
        for (unsigned r = 0; r < rounds; ++r)
            body();
        samples.push_back(std::chrono::duration<double, std::nano>(clock::now() - t0).count() /
                          (double(rounds) * double(items)));
    }
    std::sort(samples.begin(), samples.end());
    return samples[2];
}

bool terminal(unsigned p) { return p == 0 || p == 256; }

// ---- Plans. Each holds its stored representation and answers three operations.

struct plain_plan {
    std::vector<bitset4096> groups;
    void build(const std::vector<bitset4096> &in) { groups = in; }
    std::size_t stored() const { return groups.size() * 512; }
    bool contains(unsigned g, unsigned p) const { return groups[g].contains(p); }
    unsigned rank(unsigned g, unsigned p) const {
        // Branch-free over all 64 words, so the loop vectorizes.
        unsigned r = 0;
        for (unsigned w = 0; w < 64; ++w) {
            const unsigned bits = std::min(64u, p - std::min(p, 64 * w));
            const auto mask = bits == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1;
            r += std::popcount(groups[g].words[w] & mask);
        }
        return r;
    }
    unsigned and_count(unsigned g, const bitset4096 &m) const {
        unsigned r = 0;
        for (unsigned w = 0; w < 64; ++w)
            r += std::popcount(groups[g].words[w] & m.words[w]);
        return r;
    }
};

// Plain words with a retained 16-bit population per block, for rank.
struct counted_plan : plain_plan {
    std::vector<std::uint16_t> counts; // group * 16 + block
    void prepare() {
        counts.clear();
        for (auto &g : groups)
            for (unsigned k = 0; k < 16; ++k)
                counts.push_back(std::uint16_t(g.block(k).count()));
    }
    std::size_t retained_bytes() const { return counts.size() * 2; }
    unsigned rank(unsigned g, unsigned p) const {
        unsigned r = 0;
        for (unsigned k = 0; k < p / 256; ++k)
            r += counts[g * 16 + k];
        if (p % 256)
            r += groups[g].block(p / 256).rank(p % 256);
        return r;
    }
};

// Sorted 16-bit positions per group with a 32-bit offset per group.
struct positions_plan {
    std::vector<std::uint16_t> pos;
    std::vector<std::uint32_t> offset;
    void build(const std::vector<bitset4096> &in) {
        pos.clear();
        offset.assign(1, 0);
        for (auto &g : in) {
            for (unsigned w = 0; w < 64; ++w)
                for (auto x = g.words[w]; x; x &= x - 1)
                    pos.push_back(std::uint16_t(64 * w + std::countr_zero(x)));
            offset.push_back(std::uint32_t(pos.size()));
        }
    }
    std::size_t stored() const { return pos.size() * 2 + offset.size() * 4; }
    unsigned rank(unsigned g, unsigned p) const {
        auto first = pos.begin() + offset[g], last = pos.begin() + offset[g + 1];
        return unsigned(std::lower_bound(first, last, std::uint16_t(p)) - first);
    }
    bool contains(unsigned g, unsigned p) const {
        auto first = pos.begin() + offset[g], last = pos.begin() + offset[g + 1];
        auto at = std::lower_bound(first, last, std::uint16_t(p));
        return at != last && *at == p;
    }
    unsigned and_count(unsigned g, const bitset4096 &m) const {
        unsigned r = 0;
        for (unsigned i = offset[g]; i < offset[g + 1]; ++i)
            r += m.contains(pos[i]);
        return r;
    }
};

// Sixteen Bec256 bodies per group, densely packed, one 64-byte readable
// suffix. Direct parent metadata: a 32-bit base per group, then per block a
// 16-bit offset from the base and a 16-bit population (68 bytes per group).
struct bec256_plan {
    std::vector<byte> bodies;
    std::vector<std::uint32_t> base;
    std::vector<std::uint16_t> rel, pop; // group * 17 + block (17th: end)
    void build(const std::vector<bitset4096> &in) {
        bodies.clear();
        base.clear();
        rel.clear();
        pop.clear();
        for (auto &g : in) {
            base.push_back(std::uint32_t(bodies.size()));
            for (unsigned k = 0; k < 16; ++k) {
                const auto block = g.block(k);
                rel.push_back(std::uint16_t(bodies.size() - base.back()));
                pop.push_back(std::uint16_t(block.count()));
                auto body = b2::encoding::make(block, block.count());
                bodies.insert(bodies.end(), body->body().begin(), body->body().end());
            }
            rel.push_back(std::uint16_t(bodies.size() - base.back()));
            pop.push_back(0);
        }
        bodies.resize(bodies.size() + 64);
    }
    std::size_t stored() const { return bodies.size() - 64 + base.size() * (4 + 16 * 4); }
    bitset256 block(unsigned g, unsigned k) const {
        const unsigned i = g * 17 + k, p = pop[i];
        bitset256 out;
        if (terminal(p)) {
            if (p)
                out.words.fill(~std::uint64_t{0});
            return out;
        }
        const unsigned at = base[g] + rel[i], n = rel[i + 1] - rel[i];
        b2::decode(b2::bind_reader_unchecked(std::span(bodies).subspan(at), n, p), out);
        return out;
    }
    bool contains(unsigned g, unsigned p) const { return block(g, p / 256).contains(p % 256); }
    unsigned rank(unsigned g, unsigned p) const {
        unsigned r = 0;
        for (unsigned k = 0; k < p / 256; ++k)
            r += pop[g * 17 + k];
        if (p % 256)
            r += block(g, p / 256).rank(p % 256);
        return r;
    }
    unsigned and_count(unsigned g, const bitset4096 &m) const {
        unsigned r = 0;
        for (unsigned k = 0; k < 16; ++k) {
            const unsigned p = pop[g * 17 + k];
            if (!p)
                continue;
            const auto b = block(g, k);
            for (unsigned w = 0; w < 4; ++w)
                r += std::popcount(b.words[w] & m.words[4 * k + w]);
        }
        return r;
    }
};

// Bec4096 bodies densely packed with one 64-byte suffix; a 32-bit offset,
// 16-bit length and 16-bit population per group (8 bytes). `retained` keeps
// one bound reader per group; otherwise every operation binds afresh.
struct bec4096_plan {
    bool retained = false;
    std::vector<byte> bodies;
    std::vector<std::uint32_t> offset;
    std::vector<std::uint16_t> length, pop;
    std::vector<b4::reader> readers;
    void build(const std::vector<bitset4096> &in) {
        bodies.clear();
        offset.clear();
        length.clear();
        pop.clear();
        std::vector<byte> scratch(b4::max_bytes);
        std::array<ikea::journal::entry, 1> records;
        for (auto &g : in) {
            b4::view target{scratch};
            ikea::journal effects{records};
            const unsigned n = g.count(), bytes = *b4::encode(g, n, target, 0, effects);
            offset.push_back(std::uint32_t(bodies.size()));
            length.push_back(std::uint16_t(bytes));
            pop.push_back(std::uint16_t(n));
            bodies.insert(bodies.end(), scratch.begin(), scratch.begin() + bytes);
        }
        bodies.resize(bodies.size() + 64);
    }
    b4::reader bind(unsigned g) const {
        return b4::bind_reader_unchecked(std::span(bodies).subspan(offset[g]), length[g], pop[g]);
    }
    void prepare() {
        readers.clear();
        if (retained)
            for (unsigned g = 0; g < offset.size(); ++g)
                readers.push_back(bind(g));
    }
    std::size_t stored() const { return bodies.size() - 64 + offset.size() * 8; }
    std::size_t retained_bytes() const { return readers.size() * sizeof(b4::reader); }
    template <class F> auto with(unsigned g, F f) const {
        if (retained)
            return f(readers[g]);
        return f(bind(g));
    }
    bool contains(unsigned g, unsigned p) const {
        return with(g, [&](const b4::reader &r) {
            const unsigned k = p / 256, n = r.population(k);
            if (terminal(n))
                return n != 0;
            bitset256 b;
            b4::decode(r, k, b);
            return b.contains(p % 256);
        });
    }
    unsigned rank(unsigned g, unsigned p) const {
        return with(g, [&](const b4::reader &r) { return b4::rank(r, p); });
    }
    unsigned and_count(unsigned g, const bitset4096 &m) const {
        return with(g, [&](const b4::reader &r) {
            if (r.population() == 0)
                return 0u;
            bitset4096 x;
            b4::decode(r, x);
            unsigned c = 0;
            for (unsigned w = 0; w < 64; ++w)
                c += std::popcount(x.words[w] & m.words[w]);
            return c;
        });
    }
};

// ---- Demand.

struct demand {
    std::vector<std::uint32_t> probes;            // g * 4096 + p, uniform
    std::array<std::vector<std::uint32_t>, 2> hot; // ranks within 1 or 8 hot groups
    std::vector<bitset4096> masks;
    std::vector<unsigned> order; // hot groups are order's prefix
};
constexpr unsigned hot_groups[2]{1, 8};

demand make_demand(unsigned groups, unsigned seed) {
    demand d;
    std::mt19937_64 rng(0xad715e + seed);
    for (unsigned i = 0; i < 8192; ++i)
        d.probes.push_back(std::uint32_t(rng() % (4096ull * groups)));
    std::vector<unsigned> order(groups);
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), rng);
    d.order = std::vector<unsigned>(order.begin(), order.begin() + std::min(8u, groups));
    for (unsigned h = 0; h < 2; ++h)
        for (unsigned i = 0; i < 8192; ++i)
            d.hot[h].push_back(order[rng() % std::min(hot_groups[h], groups)] * 4096 +
                               unsigned(rng() % 4097) % 4096);
    for (unsigned g = 0; g < groups; ++g) {
        bitset4096 m;
        for (auto &w : m.words)
            w = rng();
        d.masks.push_back(m);
    }
    return d;
}

struct prices {
    std::string plan;
    std::size_t stored = 0, retained = 0;
    double construct = 0, prepare = 0, probe = 0, rank1 = 0, rank8 = 0, whole = 0;
    std::uint64_t check = 0;
};

template <class P>
prices price(const char *name, P &plan, const std::vector<bitset4096> &in, const demand &d) {
    prices out;
    out.plan = name;
    out.construct = time_ns(in.size(), [&] { plan.build(in); keep(plan.stored()); });
    plan.build(in);
    if constexpr (requires { plan.prepare(); }) {
        out.prepare = time_ns(in.size(), [&] { plan.prepare(); keep(plan.retained_bytes()); });
        plan.prepare();
        out.retained = plan.retained_bytes();
    }
    out.stored = plan.stored();
    out.probe = time_ns(d.probes.size(), [&] {
        for (auto x : d.probes)
            keep(plan.contains(x / 4096, x % 4096));
    });
    double *rank[2]{&out.rank1, &out.rank8};
    for (unsigned h = 0; h < 2; ++h)
        *rank[h] = time_ns(d.hot[h].size(), [&] {
            for (auto x : d.hot[h])
                keep(plan.rank(x / 4096, x % 4096));
        });
    out.whole = time_ns(in.size(), [&] {
        for (unsigned g = 0; g < in.size(); ++g)
            keep(plan.and_count(g, d.masks[g]));
    });
    // Independent result: the same answers from the plain input.
    for (auto x : d.probes)
        out.check += plan.contains(x / 4096, x % 4096) * (x + 1);
    for (auto &h : d.hot)
        for (auto x : h)
            out.check += plan.rank(x / 4096, x % 4096) * 7;
    for (unsigned g = 0; g < in.size(); ++g)
        out.check += plan.and_count(g, d.masks[g]) * 13;
    return out;
}

std::uint64_t expected(const std::vector<bitset4096> &in, const demand &d) {
    std::uint64_t r = 0;
    for (auto x : d.probes)
        r += in[x / 4096].contains(x % 4096) * (x + 1);
    for (auto &h : d.hot)
        for (auto x : h) {
            const auto &g = in[x / 4096];
            const unsigned p = x % 4096;
            unsigned c = 0;
            for (unsigned k = 0; k < p / 256; ++k)
                c += g.block(k).count();
            if (p % 256)
                c += g.block(p / 256).rank(p % 256);
            r += c * 7;
        }
    for (unsigned g = 0; g < in.size(); ++g) {
        unsigned c = 0;
        for (unsigned p = 0; p < 4096; ++p)
            c += in[g].contains(p) && d.masks[g].contains(p);
        r += c * 13;
    }
    return r;
}

// ---- Cheap analysis: what an advisor may read without encoding.

struct facts {
    std::vector<unsigned> population, live, estimate; // per group
    double ns = 0;                                    // per group
};

facts analyse(const std::vector<bitset4096> &in) {
    facts f;
    auto run = [&] {
        f.population.clear();
        f.live.clear();
        f.estimate.clear();
        for (auto &g : in) {
            unsigned n = 0, live = 0, est = 0;
            for (unsigned k = 0; k < 16; ++k) {
                const auto block = g.block(k);
                const unsigned c = block.count();
                n += c;
                if (!terminal(c)) {
                    ++live;
                    est += b2::estimate_bytes(block);
                }
            }
            f.population.push_back(n);
            f.live.push_back(live);
            f.estimate.push_back(est);
        }
        keep(f.estimate.size());
    };
    f.ns = time_ns(in.size(), run);
    run();
    return f;
}

// ---- Collections.

struct collection {
    std::string name, kind;
    std::vector<bitset4096> groups;
};

bitset4096 dense_random(std::mt19937_64 &rng) {
    bitset4096 x;
    for (auto &w : x.words)
        w = rng();
    return x;
}

std::vector<collection> evaluation(const char *windows) {
    std::vector<collection> out;
    namespace in = bec4096_inputs;
    for (auto s : {in::shape::random, in::shape::runs, in::shape::mixed, in::shape::sparse,
                   in::shape::dense, in::shape::clustered}) {
        collection c{in::name(s), "synthetic", {}};
        for (unsigned seed = 0; seed < 128; ++seed)
            c.groups.push_back(in::make(s, seed + 1000));
        out.push_back(std::move(c));
    }
    // Fixed-sampling counterexamples from the whole-bitset study: every
    // eighth group differs from the rest.
    for (bool sparse_eighth : {true, false}) {
        std::mt19937_64 rng(sparse_eighth ? 81 : 18);
        collection c{sparse_eighth ? "eighth-empty" : "eighth-dense", "probe", {}};
        for (unsigned g = 0; g < 128; ++g)
            c.groups.push_back((g % 8 == 0) == sparse_eighth ? bitset4096{} : dense_random(rng));
        out.push_back(std::move(c));
    }
    if (windows)
        for (auto &entry : std::filesystem::directory_iterator(windows)) {
            if (entry.path().extension() != ".windows")
                continue;
            std::ifstream f(entry.path(), std::ios::binary);
            std::vector<char> data((std::istreambuf_iterator<char>(f)), {});
            collection c{entry.path().stem().string(), "realroaring", {}};
            for (std::size_t at = 0; at + 512 <= data.size(); at += 512) {
                bitset4096 g;
                std::memcpy(g.words.data(), data.data() + at, 512);
                c.groups.push_back(g);
            }
            out.push_back(std::move(c));
        }
    std::sort(out.begin() + 8, out.end(), [](auto &a, auto &b) { return a.name < b.name; });
    return out;
}

// Calibration classes: a population spread uniformly over `live` blocks.
std::vector<collection> calibration() {
    std::vector<collection> out;
    for (bool full : {false, true}) {
        collection c{full ? "full" : "empty", "calibration", {}};
        bitset4096 x;
        if (full)
            x.words.fill(~std::uint64_t{0});
        c.groups.assign(64, x);
        out.push_back(std::move(c));
    }
    for (unsigned live : {2u, 16u})
        for (unsigned n : {1u, 4u, 16u, 64u, 256u, 1024u, 2048u, 3072u, 4000u}) {
            if (n > live * 256)
                continue;
            collection c{"live" + std::to_string(live) + "/n" + std::to_string(n), "calibration", {}};
            std::mt19937_64 rng(live * 7919 + n);
            for (unsigned g = 0; g < 64; ++g) {
                bitset4096 x;
                unsigned first = unsigned(rng() % (17 - live));
                if (2 * n <= live * 256) {
                    for (unsigned placed = 0; placed < n;) {
                        const unsigned p = 256 * first + unsigned(rng() % (256 * live));
                        placed += !x.contains(p);
                        x.set(p);
                    }
                } else {
                    for (unsigned k = first; k < first + live; ++k)
                        for (unsigned j = 0; j < 4; ++j)
                            x.words[4 * k + j] = ~std::uint64_t{0};
                    for (unsigned cleared = 0; cleared < live * 256 - n;) {
                        const unsigned p = 256 * first + unsigned(rng() % (256 * live));
                        cleared += x.contains(p);
                        x.set(p, false);
                    }
                }
                c.groups.push_back(x);
            }
            out.push_back(std::move(c));
        }
    return out;
}

void emit_prices(std::FILE *o, const prices &p) {
    std::fprintf(o,
                 "{\"plan\":\"%s\",\"stored\":%zu,\"retained\":%zu,\"construct\":%.2f,"
                 "\"prepare\":%.2f,\"probe\":%.2f,\"rank1\":%.2f,\"rank8\":%.2f,\"whole\":%.2f}",
                 p.plan.c_str(), p.stored, p.retained, p.construct, p.prepare, p.probe, p.rank1,
                 p.rank8, p.whole);
}

template <class V> void emit_list(std::FILE *o, const V &v) {
    std::fputc('[', o);
    for (std::size_t i = 0; i < v.size(); ++i)
        std::fprintf(o, "%s%u", i ? "," : "", unsigned(v[i]));
    std::fputc(']', o);
}

bool price_all(std::FILE *o, const collection &c, unsigned seed) {
    const auto d = make_demand(unsigned(c.groups.size()), seed);
    plain_plan plain;
    counted_plan counted;
    positions_plan positions;
    bec256_plan bec256;
    bec4096_plan fresh, retained;
    retained.retained = true;
    const prices all[]{price("plain", plain, c.groups, d), price("plain+counts", counted, c.groups, d),
                       price("positions", positions, c.groups, d),
                       price("bec256", bec256, c.groups, d), price("bec4096", fresh, c.groups, d),
                       price("bec4096+retained", retained, c.groups, d)};
    const auto want = expected(c.groups, d);
    bool ok = true;
    for (auto &p : all)
        if (p.check != want) {
            std::fprintf(stderr, "%s/%s: check mismatch\n", c.name.c_str(), p.plan.c_str());
            ok = false;
        }
    std::fputs("\"hot\":", o);
    emit_list(o, d.order);
    std::fputs(",\"plans\":[", o);
    for (unsigned i = 0; i < std::size(all); ++i) {
        if (i)
            std::fputc(',', o);
        emit_prices(o, all[i]);
    }
    std::fputc(']', o);
    return ok;
}
} // namespace

int main(int argc, char **argv) {
    const char *windows = argc > 1 ? argv[1] : nullptr;
    std::FILE *o = stdout;
    bool ok = true;
    std::fprintf(o, "{\"reader_bytes\":%zu,\"calibration\":[", sizeof(b4::reader));
    auto cal = calibration();
    for (std::size_t i = 0; i < cal.size(); ++i) {
        const auto f = analyse(cal[i].groups);
        std::fprintf(o, "%s{\"name\":\"%s\",\"groups\":%zu,\"population\":%u,\"live\":%.2f,",
                     i ? "," : "", cal[i].name.c_str(), cal[i].groups.size(), f.population[0],
                     double(std::accumulate(f.live.begin(), f.live.end(), 0u)) / f.live.size());
        ok &= price_all(o, cal[i], unsigned(i));
        std::fputs("}\n", o);
    }
    std::fputs("],\"collections\":[", o);
    auto eval = evaluation(windows);
    for (std::size_t i = 0; i < eval.size(); ++i) {
        const auto &c = eval[i];
        const auto f = analyse(c.groups);
        std::fprintf(o, "%s{\"name\":\"%s\",\"kind\":\"%s\",\"groups\":%zu,\"analysis_ns\":%.2f,",
                     i ? "," : "", c.name.c_str(), c.kind.c_str(), c.groups.size(), f.ns);
        std::fputs("\"population\":", o);
        emit_list(o, f.population);
        std::fputs(",\"live\":", o);
        emit_list(o, f.live);
        std::fputs(",\"estimate\":", o);
        emit_list(o, f.estimate);
        std::fputc(',', o);
        ok &= price_all(o, c, 100 + unsigned(i));
        std::fputs("}\n", o);
    }
    std::fputs("]}\n", o);
    std::fprintf(stderr, "%s (sink %llu)\n", ok ? "checks passed" : "CHECKS FAILED",
                 (unsigned long long)(sink & 1));
    return ok ? 0 : 1;
}
