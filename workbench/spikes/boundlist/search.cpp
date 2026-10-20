#include "boundlist.h"
#include "fused.h"
#include "scan-count.h"
#include <pmu/pmu.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace bl = boundlist_spike;
using word = std::uint64_t;
inline word mix(word x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
struct alignas(64) child { word salt; std::array<word, 7> padding{}; };
template <class T> struct input {
    T query;
    unsigned rank, link, child_link;
};
template <class T> struct fixture {
    static constexpr unsigned inputs = 1024;
    unsigned count, domain;
    T origin;
    std::vector<T> ends, checkpoints;
    std::vector<std::uint8_t> widths;
    std::vector<child> children;
    std::vector<input<T>> prepared;
    std::vector<unsigned> answers;
    explicit fixture(unsigned n, unsigned distribution)
        : count(n), origin(sizeof(T) == 2 ? T(4096) : T(1) << (sizeof(T) * 8 - 2)),
          ends(n), checkpoints((n + 15) / 16), widths(((n + 15) / 16) * 16),
          children(n + 1), prepared(inputs) {
        T total = origin;
        for (unsigned r = 0; r < n; ++r) {
            if (!(r % 16)) checkpoints[r / 16] = total;
            // Uniform-ish positive geometry, duplicate endpoints, or a strongly
            // skewed positive geometry. The latter two test lerp's weaknesses.
            const unsigned w = distribution == 1 ? (r % 17 == 0 ? 3 : 0)
                : distribution == 2 ? (r < n * 7 / 8 ? 0 : 3) : 1 + mix(r + 713) % 3;
            widths[r] = w;
            total += T(w); ends[r] = total;
            children[r].salt = mix(r + 0x3917);
        }
        children[n].salt = mix(n + 0x3917);
        domain = unsigned(total - origin) + 2;
        answers.resize(domain);
        unsigned r = 0;
        for (unsigned q = 0; q < domain; ++q) {
            while (r < n && ends[r] < T(origin + q)) ++r;
            answers[q] = r;
        }
        // One shuffled ring traverses every prepared query. Feedback depends on
        // the measured rank; unlike a random functional graph it cannot collapse
        // onto a short predictable cycle. Replay follows the identical ring.
        std::vector<unsigned> order(inputs); std::iota(order.begin(), order.end(), 0);
        for (unsigned i = inputs - 1; i; --i) std::swap(order[i], order[mix(i + 971) % (i + 1)]);
        for (unsigned i = 0; i < inputs; ++i) {
            const auto index = order[i], next = order[(i + 1) % inputs];
            const auto q = unsigned(mix(index + 1771) % domain), rank = answers[q];
            prepared[index] = {T(origin + q), rank, next - rank,
                next - rank - unsigned(children[rank].salt)};
        }
    }
    bl::direct_view<T> direct() const { return {ends.data(), count, {}}; }
    bl::delta_view<std::uint8_t, T> delta() const {
        return {widths.data(), checkpoints.data(), count, 16, ends.back(), {}};
    }
};

template <unsigned Method, class T>
[[gnu::always_inline]] unsigned find(const fixture<T>& f, T q) {
    if constexpr (Method == 0) return f.answers[unsigned(q - f.origin)];
    else if constexpr (Method == 1) return boundlist_fused::flat<true>(f.ends.data(), f.count, q);
    else if constexpr (Method == 2) return boundlist_count_control::scan<true>(f.ends.data(), f.count, q);
    else if constexpr (Method == 3) return bl::seek<bl::edge::at_or_after, bl::method::select_hybrid>(f.direct(), q);
    else if constexpr (Method == 4) return unsigned(std::lower_bound(f.ends.begin(), f.ends.end(), q) - f.ends.begin());
    else if constexpr (Method == 5) return bl::seek<bl::edge::at_or_after, bl::method::scan>(f.delta(), q);
    else if constexpr (Method == 6) return boundlist_fused::width<true>(f.widths.data(), f.checkpoints.data(), 16, f.count, f.ends.back(), q);
    else if constexpr (Method == 7) {
        if (f.count == 256) return boundlist_count_control::scan<true>(f.ends.data(), 256, q);
        return boundlist_count_control::scan<true>(f.ends.data(), f.count, q);
    } else if constexpr (Method == 8) return bl::seek<bl::edge::at_or_after, bl::method::lerp>(f.direct(), q);
    else if constexpr (Method == 11 || Method == 12) {
        const auto next = [&]() __attribute__((always_inline)) {
            if constexpr (Method == 11) return boundlist_fused::flat<true>(f.checkpoints.data(), unsigned(f.checkpoints.size()), q);
            else return boundlist_count_control::scan<true>(f.checkpoints.data(), unsigned(f.checkpoints.size()), q);
        }();
        if (next == f.checkpoints.size() && f.ends.back() < q) return f.count;
        const auto group = next ? next - 1 : 0, first = group * 16, take = std::min(16u, f.count - first);
        const auto within = [&]() __attribute__((always_inline)) {
            if constexpr (Method == 11) return boundlist_fused::flat<true>(f.ends.data() + first, take, q);
            else return boundlist_count_control::scan<true>(f.ends.data() + first, take, q);
        }();
        return first + within;
    } else if constexpr (Method == 13) return boundlist_count_control::batch<1>(f.ends.data(), f.count, std::array{q})[0];
    else {
        std::array<unsigned, 1> result;
        bl::shotgun_array<bl::edge::at_or_after, 1, Method == 9>(
            std::array{f.ends.data()}, std::array{f.count}, std::array{q}, result);
        return result[0];
    }
}

// Mode0: prepared query + search + rank-dependent next input; Mode1 adds a
// selected 64-byte child. Mode2 is the complete generated-query/hash envelope
// of the original benchmark. None is an instruction-latency subtraction.
template <unsigned Chains, unsigned Mode, unsigned Method, class T>
[[gnu::noinline]] word consume(const fixture<T>& f, unsigned rounds) {
    std::array<unsigned, Chains> state;
    std::array<word, Chains> salt;
    for (unsigned c = 0; c < Chains; ++c) { state[c] = c * 127; salt[c] = mix(713 + c * 0x3917); }
    for (unsigned step = 0; step < rounds; ++step) {
        std::array<unsigned, Chains> ranks;
        if constexpr (Method == 9 || Method == 10 || Method == 13) {
            std::array<const T*, Chains> arrays;
            std::array<unsigned, Chains> counts;
            std::array<T, Chains> queries;
            for (unsigned c = 0; c < Chains; ++c) {
                arrays[c] = f.ends.data(); counts[c] = f.count;
                if constexpr (Mode == 2) queries[c] = T(f.origin + unsigned((__uint128_t(salt[c]) * f.domain) >> 64));
                else queries[c] = f.prepared[state[c]].query;
            }
            if constexpr (Method == 13) ranks = boundlist_count_control::batch<Chains>(f.ends.data(), f.count, queries);
            else bl::shotgun_array<bl::edge::at_or_after, Chains, Method == 9>(arrays, counts, queries, ranks);
        }
        [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
            auto one = [&](auto index) __attribute__((always_inline)) {
                constexpr auto c = index.value;
                if constexpr (Mode == 2) {
                    const auto ordinal = unsigned((__uint128_t(salt[c]) * f.domain) >> 64);
                    const auto rank = [&]() __attribute__((always_inline)) {
                        if constexpr (Method == 9 || Method == 10 || Method == 13) return ranks[c];
                        else return find<Method>(f, T(f.origin + ordinal));
                    }();
                    salt[c] = mix(salt[c] ^ f.children[rank].salt);
                } else {
                    const auto& in = f.prepared[state[c]];
                    const auto rank = [&]() __attribute__((always_inline)) {
                        if constexpr (Method == 9 || Method == 10 || Method == 13) return ranks[c];
                        else if constexpr (Method == 0) return in.rank;
                        else return find<Method>(f, in.query);
                    }();
                    if constexpr (Mode == 0) state[c] = (rank + in.link) & (fixture<T>::inputs - 1);
                    else state[c] = (rank + in.child_link + unsigned(f.children[rank].salt)) & (fixture<T>::inputs - 1);
                }
            };
            (one(std::integral_constant<unsigned, I>{}), ...);
        }(std::make_index_sequence<Chains>{});
    }
    word result = 0;
    for (unsigned c = 0; c < Chains; ++c) result ^= Mode == 2 ? salt[c] : word(state[c] + c * 713);
    return result;
}
inline volatile word observed;
template <unsigned Chains, unsigned Mode, unsigned Method, class T>
void trial(const fixture<T>& f, unsigned distribution, const char* name, unsigned ops, unsigned rep, word expected) {
    const auto rounds = std::max(1u, ops / Chains);
    observed = consume<Chains, Mode, Method>(f, 1024);
    workbench::pmu::Session counters(std::getenv("BOUNDLIST_COUNTERS")
        ? workbench::pmu::Mode::stalls : workbench::pmu::Mode::off);
    counters.start();
    const auto begin = std::chrono::steady_clock::now();
    const auto result = consume<Chains, Mode, Method>(f, rounds);
    const auto end = std::chrono::steady_clock::now();
    counters.stop();
    assert(result == expected); observed = result;
    constexpr const char* modes[] = {"prepared_search", "prepared_search_child", "complete_query_child_hash"};
    std::printf("%zu,%u,%u,%s,%s,%u,%u,%.6f,%llu,%zu,%zu,%zu,%zu\n", sizeof(T) * 8, f.count, distribution,
        name, modes[Mode], Chains, rep,
        double(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()) / (rounds * Chains),
        (unsigned long long)result, f.ends.size() * sizeof(T),
        f.checkpoints.size() * sizeof(T) + f.widths.size(),
        (f.ends.size() + f.checkpoints.size()) * sizeof(T), f.children.size() * sizeof(child));
    if (std::getenv("BOUNDLIST_COUNTERS")) {
        std::cerr << "{\"implementation\":\"" << name << "\",\"mode\":" << Mode << ",\"chains\":" << Chains
            << ",\"bits\":" << sizeof(T) * 8 << ",\"count\":" << f.count << ",\"operations\":" << rounds * Chains << ",\"pmu\":";
        std::ostringstream report; counters.write_json(report);
        auto encoded = report.str();
        if (!encoded.empty() && encoded.back() == '\n') encoded.pop_back();
        std::cerr << encoded << "}\n";
    }
}
template <class T, unsigned Chains, unsigned Mode>
void panel(const fixture<T>& f, unsigned distribution, unsigned ops, unsigned reps) {
    const auto expected = consume<Chains, Mode, 0>(f, std::max(1u, ops / Chains));
    for (unsigned rep = 0; rep < reps; ++rep) for (unsigned i = 0; i < 14; ++i) {
        auto run = [&]<unsigned Method>(const char* name) { trial<Chains, Mode, Method>(f, distribution, name, ops, rep, expected); };
        switch (rep % 2 ? 13 - i : i) {
        case 0: run.template operator()<0>("rank_replay_envelope"); break;
        case 1: run.template operator()<1>("fused_scan_shortcircuit"); break;
        case 2: run.template operator()<2>("fused_scan_count"); break;
        case 3: run.template operator()<3>("select_convergence"); break;
        case 4: run.template operator()<4>("std_lower_bound"); break;
        case 5: run.template operator()<5>("width_scan"); break;
        case 6: run.template operator()<6>("fused_width_scan"); break;
        case 7: run.template operator()<7>("fused_scan_count_fixed256"); break;
        case 8: run.template operator()<8>("lerp_binary"); break;
        case 9: run.template operator()<9>("shotgun_lerp_select_window"); break;
        case 10: run.template operator()<10>("shotgun_select_window"); break;
        case 11: run.template operator()<11>("direct_guide_shortcircuit"); break;
        case 12: run.template operator()<12>("direct_guide_count"); break;
        case 13: run.template operator()<13>("same_list_batch_scan"); break;
        }
    }
}
template <class T> void run(unsigned count, unsigned distribution, unsigned ops, unsigned reps) {
    fixture<T> f(count, distribution);
    // Every query, including duplicates, extremes and absence. Expected ranks
    // come from independently generated geometry, not another search kernel.
    for (unsigned q = 0; q < f.domain; ++q) {
        const auto point = T(f.origin + q);
        const auto expected = f.answers[q];
        assert(find<1>(f, point) == expected); assert(find<2>(f, point) == expected);
        assert(find<3>(f, point) == expected); assert(find<4>(f, point) == expected);
        assert(find<5>(f, point) == expected); assert(find<6>(f, point) == expected);
        assert(find<7>(f, point) == expected); assert(find<8>(f, point) == expected);
        assert(find<9>(f, point) == expected); assert(find<10>(f, point) == expected);
        assert(find<11>(f, point) == expected); assert(find<12>(f, point) == expected); assert(find<13>(f, point) == expected);
    }
    panel<T, 1, 0>(f, distribution, ops, reps); panel<T, 8, 0>(f, distribution, ops, reps);
    panel<T, 1, 1>(f, distribution, ops, reps); panel<T, 8, 1>(f, distribution, ops, reps);
    panel<T, 1, 2>(f, distribution, ops, reps); panel<T, 8, 2>(f, distribution, ops, reps);
}
int main(int argc, char** argv) {
    if (argc != 6) return 2;
    const unsigned bits = std::stoul(argv[1]), count = std::stoul(argv[2]), distribution = std::stoul(argv[3]);
    const unsigned ops = std::stoul(argv[4]), reps = std::stoul(argv[5]);
    if (!count || count > 16384 || distribution > 2 || !ops || !reps) return 2;
    std::puts("bits,count,distribution,implementation,metric,chains,rep,ns,checksum,direct_bytes,width_bytes,direct_guide_bytes,child_bytes");
    switch (bits) {
    case 16: run<std::uint16_t>(count, distribution, ops, reps); break;
    case 32: run<std::uint32_t>(count, distribution, ops, reps); break;
    case 64: run<std::uint64_t>(count, distribution, ops, reps); break;
    default: return 2;
    }
}
