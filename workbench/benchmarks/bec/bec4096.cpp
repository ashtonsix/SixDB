// Bec4096 against sixteen Bec256 bodies over the same 4096-position groups,
// carried from the graduation study (workbench/spikes/bec4096).
// Bec256 bodies are densely packed per group with one 64-byte readable suffix
// at the end; Bec4096 bodies likewise. "exact" readers grant only body bytes.
// The Bec256 directory is a resident array of admitted readers: a generous
// direct control whose lookup is one indexed load.
#include "../../../ikea/test/bec/bec4096_reference.h"
#include <ikea/bec.h>
#include <ikea/bec/author/native.h>
#include <benchmark/benchmark.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <vector>

namespace {
using namespace ikea::bec4096;
using ikea::bitset256;
using ikea::bitset4096;
namespace bc = ikea::bec256;
namespace inputs = bec4096_inputs;

struct group {
    bitset4096 plain;
    std::vector<byte> body4096; // body + 64-byte suffix
    unsigned bytes4096 = 0;
    std::vector<byte> body256; // 16 packed bodies + 64-byte suffix
    std::array<unsigned, 17> offset{};
};
struct corpus {
    std::string label;
    std::vector<group> groups;
    std::vector<reader> exact4096, padded4096;
    std::vector<bc::reader> exact256, padded256; // group * 16 + block
    std::vector<std::uint32_t> blocks;    // random group*16 + block
    std::vector<std::uint32_t> positions; // random group*4096 + position
    double mean4096 = 0, mean256 = 0;
};
std::vector<std::unique_ptr<corpus>> corpora;

void admit(corpus &c) {
    for (auto &g : c.groups) {
        const unsigned population = g.plain.count();
        std::vector<byte> scratch(max_bytes);
        view target{scratch};
        std::array<ikea::journal::entry, 1> records;
        ikea::journal effects{records};
        g.bytes4096 = *encode(g.plain, population, target, 0, effects);
        g.body4096.assign(scratch.begin(), scratch.begin() + g.bytes4096);
        g.body4096.resize(g.bytes4096 + 64);
        for (unsigned k = 0; k < 16; ++k) {
            const auto block = g.plain.block(k);
            auto body = bc::encoding::make(block, block.count());
            g.offset[k + 1] = g.offset[k] + body->bytes();
            g.body256.insert(g.body256.end(), body->body().begin(), body->body().end());
        }
        g.body256.resize(g.offset[16] + 64);
        c.mean4096 += g.bytes4096;
        c.mean256 += g.offset[16];
    }
    c.mean4096 /= c.groups.size();
    c.mean256 /= c.groups.size();
    for (auto &g : c.groups) {
        const unsigned population = g.plain.count();
        c.exact4096.push_back(*bind_reader(std::span(g.body4096).first(g.bytes4096), g.bytes4096,
                                           population));
        c.padded4096.push_back(*bind_reader(g.body4096, g.bytes4096, population));
        for (unsigned k = 0; k < 16; ++k) {
            const unsigned n = g.offset[k + 1] - g.offset[k], pop = g.plain.block(k).count();
            c.exact256.push_back(
                *bc::bind_reader(std::span(g.body256).subspan(g.offset[k], n), n, pop));
            c.padded256.push_back(*bc::bind_reader(std::span(g.body256).subspan(g.offset[k]), n, pop));
        }
    }
    std::mt19937_64 rng(0x4096);
    for (unsigned i = 0; i < 4096; ++i) {
        c.blocks.push_back(std::uint32_t(rng() % (16 * c.groups.size())));
        c.positions.push_back(std::uint32_t(rng() % (4096 * c.groups.size())));
    }
}

[[gnu::always_inline]] inline bool terminal(unsigned p) { return p == 0 || p == 256; }

// Bec256 control with the same terminal policy as Bec4096's decode_all.
[[gnu::always_inline]] inline void decode16(const bc::reader *r,
                                            std::uint64_t *words) {
    for (unsigned k = 0; k < 16; k += 2) {
        const unsigned a = r[k].population(), b = r[k + 1].population();
        if (!terminal(a) && !terminal(b))
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
            bc::native::store_pair(reinterpret_cast<byte *>(words + 4 * k),
                                   bc::native::read_pair(r[k], r[k + 1]));
#else
            bc::decode_pair(r[k], r[k + 1],
                            std::span<byte, 64>(reinterpret_cast<byte *>(words + 4 * k), 64));
#endif
        else
            for (unsigned j = k; j < k + 2; ++j) {
                const unsigned p = r[j].population();
                if (terminal(p))
                    std::memset(words + 4 * j, p ? 255 : 0, 32);
                else
                    bc::decode(r[j], std::span<byte, 32>(reinterpret_cast<byte *>(words + 4 * j), 32));
            }
    }
}

void decode_all_4096(benchmark::State &state, const corpus *c, bool padded) {
    const auto &readers = padded ? c->padded4096 : c->exact4096;
    bitset4096 out;
    std::size_t i = 0;
    for (auto _ : state) {
        decode(readers[i], out);
        benchmark::DoNotOptimize(out);
        if (++i == readers.size())
            i = 0;
    }
    state.SetItemsProcessed(state.iterations() * 16);
}
void decode_all_256(benchmark::State &state, const corpus *c, bool padded) {
    const auto &readers = padded ? c->padded256 : c->exact256;
    bitset4096 out;
    std::size_t i = 0;
    for (auto _ : state) {
        decode16(&readers[16 * i], out.words.data());
        benchmark::DoNotOptimize(out);
        if (++i == c->groups.size())
            i = 0;
    }
    state.SetItemsProcessed(state.iterations() * 16);
}
void block_4096(benchmark::State &state, const corpus *c, bool padded) {
    const auto &readers = padded ? c->padded4096 : c->exact4096;
    bitset256 out;
    std::size_t i = 0;
    for (auto _ : state) {
        const auto at = c->blocks[i];
        decode(readers[at / 16], at % 16, out);
        benchmark::DoNotOptimize(out);
        if (++i == c->blocks.size())
            i = 0;
    }
    state.SetItemsProcessed(state.iterations());
}
void block_256(benchmark::State &state, const corpus *c, bool padded) {
    const auto &readers = padded ? c->padded256 : c->exact256;
    bitset256 out;
    std::size_t i = 0;
    for (auto _ : state) {
        const auto at = c->blocks[i];
        bc::decode(readers[at], out);
        benchmark::DoNotOptimize(out);
        if (++i == c->blocks.size())
            i = 0;
    }
    state.SetItemsProcessed(state.iterations());
}
// No retained frame: the head is decoded again for every block access.
void block_fresh_4096(benchmark::State &state, const corpus *c) {
    bitset256 out;
    std::size_t i = 0;
    for (auto _ : state) {
        const auto at = c->blocks[i];
        const auto &g = c->groups[at / 16];
        const auto r = bind_reader_unchecked(g.body4096, g.bytes4096, c->padded4096[at / 16].population());
        decode(r, at % 16, out);
        benchmark::DoNotOptimize(out);
        if (++i == c->blocks.size())
            i = 0;
    }
    state.SetItemsProcessed(state.iterations());
}
void bind_4096(benchmark::State &state, const corpus *c) {
    std::size_t i = 0;
    for (auto _ : state) {
        const auto &g = c->groups[i];
        auto r = bind_reader_unchecked(g.body4096, g.bytes4096, c->padded4096[i].population());
        benchmark::DoNotOptimize(r);
        if (++i == c->groups.size())
            i = 0;
    }
    state.SetItemsProcessed(state.iterations());
}
void rank_4096(benchmark::State &state, const corpus *c) {
    std::size_t i = 0;
    for (auto _ : state) {
        const auto at = c->positions[i];
        benchmark::DoNotOptimize(rank(c->padded4096[at / 4096], at % 4096));
        if (++i == c->positions.size())
            i = 0;
    }
    state.SetItemsProcessed(state.iterations());
}
// Control: populations from the directory, then decode the block and count.
void rank_256(benchmark::State &state, const corpus *c) {
    std::size_t i = 0;
    for (auto _ : state) {
        const auto at = c->positions[i];
        const auto *r = &c->padded256[16 * (at / 4096)];
        const unsigned position = at % 4096, k = position / 256, within = position % 256;
        unsigned result = 0;
        for (unsigned j = 0; j < k; ++j)
            result += r[j].population();
        if (within) {
            const unsigned p = r[k].population();
            if (terminal(p))
                result += p ? within : 0;
            else {
                bitset256 block;
                bc::decode(r[k], block);
                result += block.rank(within);
            }
        }
        benchmark::DoNotOptimize(result);
        if (++i == c->positions.size())
            i = 0;
    }
    state.SetItemsProcessed(state.iterations());
}
void counts_4096(benchmark::State &state, const corpus *c) {
    std::array<std::uint8_t, 32> out;
    std::size_t i = 0;
    for (auto _ : state) {
        const auto at = c->blocks[i];
        counts(c->padded4096[at / 16], at % 16, out);
        benchmark::DoNotOptimize(out);
        if (++i == c->blocks.size())
            i = 0;
    }
    state.SetItemsProcessed(state.iterations());
}
void counts_all_4096(benchmark::State &state, const corpus *c) {
    alignas(64) std::array<std::uint8_t, 512> out;
    std::size_t i = 0;
    for (auto _ : state) {
        counts(c->padded4096[i], out);
        benchmark::DoNotOptimize(out);
        if (++i == c->groups.size())
            i = 0;
    }
    state.SetItemsProcessed(state.iterations() * 16);
}
void encode_4096(benchmark::State &state, const corpus *c) {
    std::vector<byte> storage(max_bytes);
    view target{storage};
    std::array<ikea::journal::entry, 1> records;
    ikea::journal effects{records};
    std::vector<unsigned> populations;
    for (auto &g : c->groups)
        populations.push_back(g.plain.count());
    benchmark::DoNotOptimize(storage.data());
    benchmark::DoNotOptimize(records.data());
    std::size_t i = 0;
    for (auto _ : state) {
        effects.used = 0;
        auto n = encode(c->groups[i].plain, populations[i], target, 0, effects);
        benchmark::DoNotOptimize(n);
        benchmark::ClobberMemory();
        if (++i == c->groups.size())
            i = 0;
    }
    state.SetItemsProcessed(state.iterations() * 16);
}
void encode_256(benchmark::State &state, const corpus *c) {
    std::vector<byte> storage(16 * bc::max_bytes);
    bc::view target{storage};
    std::array<ikea::journal::entry, 16> records;
    ikea::journal effects{records};
    std::vector<std::array<unsigned, 16>> populations;
    for (auto &g : c->groups) {
        std::array<unsigned, 16> p;
        for (unsigned k = 0; k < 16; ++k)
            p[k] = g.plain.block(k).count();
        populations.push_back(p);
    }
    benchmark::DoNotOptimize(storage.data());
    benchmark::DoNotOptimize(records.data());
    std::size_t i = 0;
    for (auto _ : state) {
        effects.used = 0;
        std::size_t offset = 0;
        const auto &plain = c->groups[i].plain;
        for (unsigned k = 0; k < 16; ++k) {
            bitset256 block;
            std::memcpy(block.words.data(), plain.words.data() + 4 * k, 32);
            offset += *bc::encode(block, populations[i][k], target, offset, effects);
        }
        benchmark::DoNotOptimize(offset);
        benchmark::ClobberMemory();
        if (++i == c->groups.size())
            i = 0;
    }
    state.SetItemsProcessed(state.iterations() * 16);
}

void build_corpora() {
    for (auto s : {inputs::shape::random, inputs::shape::runs, inputs::shape::mixed,
                   inputs::shape::sparse, inputs::shape::clustered}) {
        auto c = std::make_unique<corpus>();
        c->label = inputs::name(s);
        for (unsigned seed = 0; seed < 64; ++seed)
            {
                group g;
                g.plain = inputs::make(s, seed);
                c->groups.push_back(std::move(g));
            }
        corpora.push_back(std::move(c));
    }
    if (const char *root = std::getenv("BEC_WINDOWS"))
        for (const char *family : {"census-income", "weather_sept_85", "wikileaks-noquotes"}) {
            std::ifstream in(std::filesystem::path(root) / (std::string(family) + ".windows"),
                             std::ios::binary);
            std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
            if (data.size() < 512)
                throw std::runtime_error("missing retained windows");
            auto c = std::make_unique<corpus>();
            c->label = family;
            for (std::size_t at = 0; at + 512 <= data.size(); at += 512) {
                group g;
                std::memcpy(g.plain.words.data(), data.data() + at, 512);
                c->groups.push_back(std::move(g));
            }
            corpora.push_back(std::move(c));
        }
    for (auto &c : corpora)
        admit(*c);
}
} // namespace

int main(int argc, char **argv) {
    build_corpora();
    for (auto &up : corpora) {
        const corpus *c = up.get();
        const auto &l = c->label;
        for (bool padded : {false, true}) {
            const std::string grant = padded ? "padded" : "exact";
            benchmark::RegisterBenchmark("decode_all/4096/" + grant + "/" + l, decode_all_4096, c, padded);
                        benchmark::RegisterBenchmark("decode_all/256x16/" + grant + "/" + l, decode_all_256, c, padded);
            benchmark::RegisterBenchmark("block/4096/" + grant + "/" + l, block_4096, c, padded);
            benchmark::RegisterBenchmark("block/256/" + grant + "/" + l, block_256, c, padded);
        }
        benchmark::RegisterBenchmark("block_fresh/4096/padded/" + l, block_fresh_4096, c);
        benchmark::RegisterBenchmark("bind/4096/padded/" + l, bind_4096, c);
        benchmark::RegisterBenchmark("rank/4096/padded/" + l, rank_4096, c);
        benchmark::RegisterBenchmark("rank/256/padded/" + l, rank_256, c);
        benchmark::RegisterBenchmark("counts/4096/padded/" + l, counts_4096, c);
        benchmark::RegisterBenchmark("counts_all/4096/padded/" + l, counts_all_4096, c);
        benchmark::RegisterBenchmark("encode/4096/" + l, encode_4096, c);
        benchmark::RegisterBenchmark("encode/256x16/" + l, encode_256, c);
    }
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
