#include "packed.h"
#include "fused.h"
#include "../filter-composition/tile-baseline.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace bl = boundlist_spike;
namespace series = ikea::seriespack;
using word = std::uint64_t;
inline word mix(word x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
struct alignas(64) child { word salt; std::array<word, 7> other{}; };
template <unsigned K, bool Packed> struct fixture {
    using T = series::uint_for<K>;
    static constexpr word limit = ~word{} >> (64 - K);
    static constexpr unsigned count = 256;
    unsigned nodes;
    std::size_t stride;
    std::vector<std::uint8_t> metadata;
    std::vector<child> children;
    fixture(unsigned population) : nodes(population), stride(Packed ? K * 32 : sizeof(T) * count),
        metadata(nodes * stride), children(std::size_t(nodes) * (count + 1)) {
        for (unsigned node = 0; node < nodes; ++node) {
            auto* p = metadata.data() + node * stride;
            for (unsigned r = 0; r < count; ++r) {
                const auto value = key(node, r);
                if constexpr (Packed) {
                    auto* tile = p + r / 8 * K;
                    for (unsigned b = 0; b < K / 8; ++b) tile[r % 8 * (K / 8) + b] = value >> (K % 8 + 8 * b);
                    for (unsigned b = 0; b < K % 8; ++b) tile[8 * (K / 8) + b] |= (value >> b & 1) << (r % 8);
                } else {
                    const T narrow = T(value);
                    std::memcpy(p + r * sizeof(T), &narrow, sizeof(T));
                }
                children[std::size_t(node) * (count + 1) + r].salt = mix((word(node) << 32) ^ (r + 713));
            }
            children[std::size_t(node) * (count + 1) + count].salt = mix(node ^ UINT64_MAX);
        }
    }
    static word key(unsigned node, unsigned rank) {
        constexpr unsigned shift = K - 8;
        constexpr word low = shift ? (~word{} >> (64 - shift)) : 0;
        return (word(rank) << shift) | (mix((word(node) << 32) ^ (rank + 713)) & low);
    }
    static unsigned oracle(unsigned node, word query) {
        for (unsigned r = 0; r < count; ++r) if (key(node, r) >= query) return r;
        return count;
    }
    const auto* data(unsigned node) const { return metadata.data() + node * stride; }
};

template <unsigned K> [[gnu::always_inline]] word independent_point(const std::uint8_t* p, unsigned rank) {
    const auto* tile = p + rank / 8 * K;
    word value = 0;
    for (unsigned b = 0; b < K / 8; ++b) value |= word(tile[rank % 8 * (K / 8) + b]) << (8 * b + K % 8);
    for (unsigned b = 0; b < K % 8; ++b) value |= word(tile[8 * (K / 8) + b] >> (rank % 8) & 1) << b;
    return value;
}
template <unsigned K, bool Packed, unsigned Method>
[[gnu::always_inline]] unsigned lookup(const fixture<K, Packed>& f, unsigned node, word query) {
    const auto* p = f.data(node);
    if constexpr (Method == 99) return f.oracle(node, query);
    else if constexpr (Packed) {
        if constexpr (Method < 2) {
            const series::dense_source<series::format<K>> source{p, f.count};
            const bl::packed_view v{series::composition::describe(source), f.count, bl::population{}};
            return bl::seek<bl::edge::at_or_after, Method == 0 ? bl::method::binary : bl::method::scan>(v, query);
        } else {
            // Independent routing and decoding; complete fused16 control from
            // the earlier tile study calls no Ikea read/compare/mask helpers.
            unsigned first_group = 0;
            if constexpr (Method == 2) {
                unsigned lo = 0, hi = 16;
                while (lo < hi) {
                    const auto mid = lo + (hi - lo) / 2;
                    if (independent_point<K>(p, mid * 16 + 15) >= query) hi = mid;
                    else lo = mid + 1;
                }
                first_group = lo;
            }
            for (unsigned group = first_group; group < 16; ++group) {
                const auto mask = tile_control::window<K, 16, 4>(p + group * 2 * K, query);
                if (mask) return group * 16 + std::countr_zero(mask);
            }
            return f.count;
        }
    } else {
        using T = typename fixture<K, Packed>::T;
        const auto* ends = reinterpret_cast<const T*>(p);
        if constexpr (Method == 0) return unsigned(std::lower_bound(ends, ends + f.count, T(query)) - ends);
        else if constexpr (Method == 1) return boundlist_fused::flat<true>(ends, f.count, T(query));
        else return bl::hybrid_array<bl::edge::at_or_after>(ends, f.count, T(query));
    }
}
template <unsigned Chains, unsigned K, bool Packed, unsigned Method>
[[gnu::noinline]] word consumer(const fixture<K, Packed>& f, unsigned rounds) {
    std::array<word, Chains> cursor;
    for (unsigned i = 0; i < Chains; ++i) cursor[i] = mix(713 + i * 0x3917);
    for (unsigned step = 0; step < rounds; ++step) {
        [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
            auto one = [&](auto lane) __attribute__((always_inline)) {
                const auto node = unsigned(cursor[lane]) & (f.nodes - 1);
                const auto query = mix(cursor[lane]) & f.limit;
                const auto rank = lookup<K, Packed, Method>(f, node, query);
                const auto value = f.children[std::size_t(node) * (f.count + 1) + rank].salt;
                cursor[lane] = mix(cursor[lane] ^ value);
            };
            (one(std::integral_constant<unsigned, I>{}), ...);
        }(std::make_index_sequence<Chains>{});
    }
    word result = 0;
    for (auto c : cursor) result ^= c;
    return result;
}
inline volatile word observed;
template <unsigned Chains, unsigned K, bool Packed, unsigned Method>
void trial(const fixture<K, Packed>& f, const char* name, unsigned operations, unsigned rep, word expected) {
    observed = consumer<Chains, K, Packed, Method>(f, 256);
    const auto rounds = std::max(1u, operations / Chains);
    const auto begin = std::chrono::steady_clock::now();
    const auto result = consumer<Chains, K, Packed, Method>(f, rounds);
    const auto end = std::chrono::steady_clock::now();
    observed = result;
    assert(result == expected);
    std::printf("%u,%u,%s,%s,%s,%u,%.6f,%zu,%zu,%llu\n", K, f.nodes, Packed ? "series_local" : "native",
        name, Chains == 1 ? "dependent_point_child" : "independent8_point_child", rep,
        double(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()) / (rounds * Chains),
        f.metadata.size(), f.children.size() * sizeof(child), (unsigned long long)result);
}
template <unsigned K, bool Packed> void panel(unsigned nodes, unsigned operations, unsigned reps) {
    fixture<K, Packed> f(nodes);
    for (unsigned node : {0u, nodes / 2, nodes - 1}) for (unsigned rank = 0; rank < f.count; ++rank) {
        const auto key = f.key(node, rank);
        for (word query : {key, key ? key - 1 : 0, key == f.limit ? key : key + 1}) {
            const auto wanted = f.oracle(node, query);
            assert((lookup<K, Packed, 0>(f, node, query) == wanted));
            assert((lookup<K, Packed, 1>(f, node, query) == wanted));
            assert((lookup<K, Packed, 2>(f, node, query) == wanted));
            if constexpr (Packed) assert((lookup<K, Packed, 3>(f, node, query) == wanted));
        }
    }
    const auto expected1 = consumer<1, K, Packed, 99>(f, operations);
    const auto expected8 = consumer<8, K, Packed, 99>(f, std::max(1u, operations / 8));
    auto one = [&]<unsigned Method>(const char* name, unsigned rep) {
        trial<1, K, Packed, Method>(f, name, operations, rep, expected1);
        trial<8, K, Packed, Method>(f, name, operations, rep, expected8);
    };
    for (unsigned rep = 0; rep < reps; ++rep) {
        constexpr unsigned methods = Packed ? 4 : 3;
        for (unsigned i = 0; i < methods; ++i) switch (rep % 2 ? methods - i - 1 : i) {
        case 0: one.template operator()<0>(Packed ? "composition_binary" : "std_lower_bound", rep); break;
        case 1: one.template operator()<1>(Packed ? "composition_scan" : "independent_fused_scan", rep); break;
        case 2: one.template operator()<2>(Packed ? "independent_fused_binary" : "hybrid_convergence", rep); break;
        case 3: if constexpr (Packed) one.template operator()<3>("independent_fused_scan", rep); break;
        }
    }
}
template <unsigned K> void width(unsigned nodes, unsigned operations, unsigned reps) {
    panel<K, false>(nodes, operations, reps);
    panel<K, true>(nodes, operations, reps);
}
int main(int argc, char** argv) {
    const unsigned nodes = argc > 1 ? std::stoul(argv[1]) : 64;
    const unsigned operations = argc > 2 ? std::stoul(argv[2]) : 131072;
    const unsigned reps = argc > 3 ? std::stoul(argv[3]) : 5;
    if (!std::has_single_bit(nodes) || !operations) return 2;
    std::puts("bits,nodes,layout,implementation,metric,rep,ns,metadata_bytes,child_bytes,checksum");
    width<8>(nodes, operations, reps); width<12>(nodes, operations, reps); width<16>(nodes, operations, reps);
    width<24>(nodes, operations, reps); width<32>(nodes, operations, reps); width<40>(nodes, operations, reps);
    width<48>(nodes, operations, reps); width<64>(nodes, operations, reps);
}
