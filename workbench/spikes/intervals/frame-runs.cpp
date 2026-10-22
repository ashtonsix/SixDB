// Actual Frame getters, projection-only counterfactual. No codec/format change.
#include "point_read.h"
#include "compact_read.h"
#include "point_reader_native_runs.h"
#include "compact_reader_native_runs.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <functional>
#include <random>
#include <stdexcept>

namespace fp = frame_points;
namespace fe = frame_encoding;
using u64 = std::uint64_t;
constexpr unsigned requests = 8192;
volatile u64 frame_run_sink;
unsigned checked_cases = 0;
alignas(64) std::array<u64, requests> request_salts;
void need(bool ok, const char *reason) { if (!ok) throw std::runtime_error(reason); }

// Every next address depends on a decoded value, for both C1 and C8. C8 retains
// eight separate states. Independent per-request salts prevent short cycles
// and chain coalescence; each next address still depends on its decoded value.
inline std::size_t next(std::size_t state, u64 value, u64 salt, std::size_t mask) {
    return (state * 713 + (value ^ (value >> 32)) + salt) & mask;
}
template <unsigned Chains, class Reader>
[[gnu::noinline]] u64 read(const Reader &reader) {
    std::array<std::size_t, Chains> states;
    for (unsigned c = 0; c < Chains; ++c) states[c] = c * 137 & (reader.rows - 1);
    u64 sum = 0;
    for (unsigned q = 0; q < requests / Chains; ++q) {
        [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
            (([&] __attribute__((always_inline)) {
                const auto value = reader.get(states[I]);
                states[I] = next(states[I], value, request_salts[q * Chains + I], reader.rows - 1);
                sum += value;
            }()), ...);
        }(std::make_index_sequence<Chains>{});
    }
    for (auto state : states) sum ^= state;
    return sum;
}
template <unsigned Chains> u64 oracle(const std::vector<u64> &values) {
    struct source {
        const std::vector<u64> &values;
        std::size_t rows;
        u64 get(std::size_t r) const { return values[r]; }
    } plain{values, values.size()};
    return read<Chains>(plain);
}
template <unsigned Chains>
void audit_trace(const std::vector<u64> &values, unsigned n, unsigned bits, unsigned blocks) {
    std::array<std::size_t, Chains> states;
    for (unsigned c = 0; c < Chains; ++c) states[c] = c * 137 & (values.size() - 1);
    std::vector<bool> visited(values.size());
    unsigned unique = 0;
    for (unsigned q = 0; q < requests / Chains; ++q)
        for (unsigned c = 0; c < Chains; ++c) {
            const auto row = states[c];
            unique += !visited[row]; visited[row] = true;
            states[c] = next(row, values[row], request_salts[q * Chains + c], values.size() - 1);
        }
    need(unique >= std::min<std::size_t>(values.size(), requests) / 2, "dependent request trace collapsed");
    std::printf("{\"trace\":true,\"runs\":%u,\"bits\":%u,\"blocks\":%u,\"chains\":%u,\"unique_rows\":%u,\"salt_bytes\":%zu}\n",
        n,bits,blocks,Chains,unique,sizeof(request_salts));
}
struct arm { const char *name; std::function<u64()> run; };

void experiment(unsigned n, unsigned bits, unsigned blocks, unsigned trials, unsigned repeat, bool reverse) {
    std::mt19937_64 random(713);
    std::vector<u64> values(std::size_t(blocks) * fe::block_rows);
    const auto mask = bits == 64 ? ~u64{} : (u64{1} << bits) - 1;
    for (unsigned b = 0; b < blocks; ++b) {
        if (!n) {
            for (unsigned row = 0; row < 256; ++row) values[std::size_t(b) * 256 + row] = random() & mask;
            continue;
        }
        std::vector<u64> runs(n);
        for (unsigned r = 0; r < n; ++r) {
            do { runs[r] = random() & mask; } while (r && runs[r] == runs[r - 1]);
        }
        for (unsigned row = 0; row < 256; ++row)
            values[std::size_t(b) * 256 + row] = runs[std::min(n - 1, row * n / 256)];
    }
    auto column = fe::encode(values);
    // The actual encoder, not a fabricated RLE-only descriptor. Record cases
    // where the owning encoder prefers a different representation.
    unsigned rle = 0;
    for (const auto &d : column.blocks) rle += n ? d.type == fe::kind::runs && d.count == n : d.type == fe::kind::packed;
    if (rle != blocks) {
        std::printf("{\"skip\":true,\"runs\":%u,\"bits\":%u,\"blocks\":%u,\"actual_rle_blocks\":%u}\n",n,bits,blocks,rle);
        return;
    }
    ++checked_cases;
    static_assert(sizeof(fp::point_reader) == sizeof(fp::point_reader_native_runs));
    static_assert(sizeof(fp::compact_reader) == sizeof(fp::compact_reader_native_runs));
    fp::point_reader original{column};
    fp::point_reader_native_runs candidate{column};
    fp::compact_image compact{fe::encode(values)};
    fp::compact_reader compact_original{compact};
    fp::compact_reader_native_runs compact_candidate{compact};
    for (std::size_t row = 0; row < values.size(); ++row) {
        need(original.get(row) == values[row], "original bound getter differs from input");
        need(candidate.get(row) == values[row], "native bound getter differs from input");
        need(compact_original.get(row) == values[row], "original compact getter differs from input");
        need(compact_candidate.get(row) == values[row], "native compact getter differs from input");
    }
    audit_trace<1>(values,n,bits,blocks);
    audit_trace<8>(values,n,bits,blocks);
    for (unsigned chains : {1u, 8u}) {
        auto callback = [&](const auto &reader) -> std::function<u64()> {
            if (chains == 1) return [&reader] { return read<1>(reader); };
            return [&reader] { return read<8>(reader); };
        };
        std::array arms{arm{"bound_projection",callback(original)}, arm{"bound_native",callback(candidate)},
                        arm{"compact_projection",callback(compact_original)}, arm{"compact_native",callback(compact_candidate)}};
        if (reverse) std::reverse(arms.begin(),arms.end());
        const auto expected = chains == 1 ? oracle<1>(values) : oracle<8>(values);
        for (const auto &a : arms) need(a.run() == expected, "dependent chain differs from plain input");
        for (unsigned trial = 0; trial < trials; ++trial)
            for (unsigned position = 0; position < arms.size(); ++position) {
                const auto &a = arms[(position + trial) % arms.size()];
                frame_run_sink = a.run();
                const auto start = std::chrono::steady_clock::now();
                for (unsigned rep = 0; rep < repeat; ++rep) {
                    asm volatile("" ::: "memory");
                    frame_run_sink = a.run();
                }
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start).count();
                std::printf("{\"arm\":\"%s\",\"runs\":%u,\"bits\":%u,\"blocks\":%u,\"chains\":%u,\"trial\":%u,\"elapsed_ns\":%lld,\"requests\":%u,\"image_bytes\":%zu,\"checksum\":\"%llu\"}\n",
                    a.name,n,bits,blocks,chains,trial,static_cast<long long>(elapsed),repeat*requests,
                    column.payload.size()+column.blocks.size()*sizeof(fe::descriptor),static_cast<unsigned long long>(expected));
            }
    }
}
int main(int argc, char **argv) {
    try {
        const bool check_only = argc > 1 && std::string(argv[1]) == "--check";
        const unsigned trials = check_only ? 0 : argc > 1 ? std::stoul(argv[1]) : 7;
        const unsigned repeat = argc > 2 ? std::stoul(argv[2]) : 8;
        const bool reverse = argc > 3 ? std::stoul(argv[3]) : false;
        need((check_only || trials) && trials <= 31 && repeat && repeat <= 128,"invalid work count");
        std::mt19937_64 request_random(13271);
        for (auto &salt : request_salts) salt = request_random();
        for (unsigned blocks : {1u,256u})
            for (unsigned bits : {8u,32u,64u})
                for (unsigned n : {0u,4u,8u,15u,16u,17u,32u,64u,128u})
                    experiment(n,bits,blocks,trials,repeat,reverse);
        need(checked_cases > 10,"too few actual encoder cases qualified");
        std::printf("{\"check_cases\":%u}\n",checked_cases);
    } catch (const std::exception &error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
}
