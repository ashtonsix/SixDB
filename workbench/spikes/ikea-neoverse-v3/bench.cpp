#include "kernels.h"
#include "../../benchmarks/bec256/fixture.h"
#include <benchmark/benchmark.h>
#include <sys/auxv.h>
#include <asm/hwcap.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>

namespace {
using namespace v3_spike;
bool has_sve() {
    return (getauxval(AT_HWCAP) & HWCAP_SVE) != 0 &&
           (getauxval(AT_HWCAP2) & HWCAP2_SVE2) != 0 &&
           (prctl(PR_SVE_GET_VL) & PR_SVE_VL_LEN_MASK) == 16;
}
unsigned expected(const bc::plain_block& a, const bc::plain_block& b,
                  const bc::plain_block& qa, const bc::plain_block& qb) {
    unsigned n = 0;
    for (unsigned i = 0; i < 32; ++i)
        n += std::popcount(std::to_integer<unsigned>(a[i] & qa[i])) +
             std::popcount(std::to_integer<unsigned>(b[i] & qb[i]));
    return n;
}
void verify_guards() {
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    auto* memory = static_cast<bc::byte*>(mmap(nullptr, 3 * page, PROT_READ | PROT_WRITE,
                                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (memory == MAP_FAILED) throw std::runtime_error("mmap failed");
    if (mprotect(memory, page, PROT_NONE) || mprotect(memory + 2 * page, page, PROT_NONE))
        throw std::runtime_error("mprotect failed");
    std::mt19937_64 rng(0x7654321);
    std::array<unsigned, 256> order;
    std::iota(order.begin(), order.end(), 0);
    std::array<bool, 48> lengths{};
    for (unsigned population = 0; population <= 256; ++population) {
        std::shuffle(order.begin(), order.end(), rng);
        bc::plain_block plain{}, query{}, zero{};
        for (unsigned j = 0; j < population; ++j)
            plain[order[j] / 8] |= bc::byte(1u << (order[j] % 8));
        auto encoded = bc::prepare(plain, population);
        if (!encoded) throw std::runtime_error("prepare failed");
        const unsigned bytes = encoded->bytes();
        lengths[bytes] = true;
        for (bool at_end : {false, true}) {
            auto* location = memory + (at_end ? 2 * page - bytes : page);
            std::copy(encoded->body().begin(), encoded->body().end(), location);
            auto source = bc::source::admit({location, bytes}, bytes, population);
            if (!source) throw std::runtime_error("admit failed");
            for (unsigned bit = 0; bit < 256; ++bit) {
                query.fill(bc::byte{0});
                query[bit / 8] = bc::byte(1u << (bit % 8));
                for (const auto& c : candidates) {
                    if (c.sve && !has_sve()) continue;
                    const auto result = c.consume(*source, *source, query.data(), zero.data());
                    const auto want = std::to_integer<unsigned>(plain[bit / 8] & query[bit / 8]) != 0;
                    if (result != want) throw std::runtime_error(std::string("guard/reference mismatch: ") + c.name);
                }
            }
        }
    }
    munmap(memory, 3 * page);
    std::cout << "All 257 populations checked bit-by-bit at both guarded boundaries; body lengths:";
    for (unsigned i = 0; i < lengths.size(); ++i) if (lengths[i]) std::cout << ' ' << i;
    std::cout << "; SVE=" << has_sve() << '\n';
}
void pair_bench(benchmark::State& state, unsigned shape, bool padded, consumer function) {
    bec_bench::fixture data(shape);
    const auto& sources = padded ? data.padded : data.exact;
    for (unsigned i = 0; i < bec_bench::block_count; ++i) {
        auto j = (i + 73) % bec_bench::block_count;
        if (function(sources[i], sources[j], data.query[i].data(), data.query[j].data()) !=
            expected(data.plain[i], data.plain[j], data.query[i], data.query[j]))
            throw std::runtime_error("fixture mismatch");
    }
    unsigned i = 0;
    for (auto _ : state) {
        auto j = (i + 73) % bec_bench::block_count;
        benchmark::DoNotOptimize(function(sources[i], sources[j], data.query[i].data(), data.query[j].data()));
        i = (i + 1) % bec_bench::block_count;
    }
    state.SetItemsProcessed(state.iterations() * 2);
    state.counters["blocks_per_iteration"] = 2;
}
// Standalone lookup controls cover runtime indices over all 256 byte values.
// They consume complete vectors and preserve exactly the same table contents.
template <unsigned Method> void lookup_bench(benchmark::State& state) {
    alignas(64) std::array<std::uint8_t, 256> table{};
    for (unsigned i = 0; i < 256; ++i) table[i] = std::uint8_t(i * 71 + 3);
    for (unsigned i = 0; i < 256; i += 16) {
        auto x = vaddq_u8(uint8x16_t{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}, vdupq_n_u8(i));
        auto y = lookup256<Method>(table.data(), x);
        std::array<std::uint8_t, 16> bytes;
        vst1q_u8(bytes.data(), y);
        for (unsigned j = 0; j < 16; ++j)
            if (bytes[j] != table[i + j]) throw std::runtime_error("lookup mismatch");
    }
    auto a = uint8x16_t{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    auto b = vaddq_u8(a, vdupq_n_u8(63));
    for (auto _ : state) {
        a = vaddq_u8(a, vdupq_n_u8(1));
        b = vaddq_u8(b, vdupq_n_u8(7));
        benchmark::DoNotOptimize(a); benchmark::DoNotOptimize(b);
        benchmark::DoNotOptimize(lookup256<Method>(table.data(), a));
        benchmark::DoNotOptimize(lookup256<Method>(table.data(), b));
    }
    state.SetItemsProcessed(state.iterations() * 32);
}
}
int main(int argc, char** argv) {
    verify_guards();
    if (argc == 2 && std::string_view(argv[1]) == "--check") return 0;
    for (unsigned shape : {0u, 1u, 2u, 3u, 7u, 9u, 10u, 14u, 17u})
        for (bool padded : {false, true})
            for (const auto& c : v3_spike::candidates) {
                if (c.sve && !has_sve()) continue;
                const auto name = "pair/" + bec_bench::shape_name(shape) + "/" +
                                  (padded ? "padded/" : "exact/") + c.name;
                benchmark::RegisterBenchmark(name.c_str(), pair_bench, shape, padded, c.consume);
            }
    benchmark::RegisterBenchmark("lookup/tbl4", lookup_bench<0>);
    benchmark::RegisterBenchmark("lookup/tbl2", lookup_bench<1>);
    benchmark::RegisterBenchmark("lookup/tbx4", lookup_bench<2>);
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
