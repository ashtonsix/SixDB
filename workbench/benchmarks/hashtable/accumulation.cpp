#include "common.h"
#include <ikea/hashtable.h>
#include <ikea/hashtable/encoding-selection.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/internal/hashtable_debug.h>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <numeric>
#include <random>
namespace study {
namespace access = ikea::hashtable::author;
namespace ht = ikea::hashtable;
using ht::word;
constexpr unsigned V = MERGE_VALUE_BYTES;
using Value = ht::bytes<V>;
#if SIXDB_TUNE_ZEN5
constexpr std::string_view cpu = "zen5", isa = "avx512";
#elif SIXDB_TUNE_GRANITE_RAPIDS
constexpr std::string_view cpu = "granite-rapids", isa = "avx512";
#else
constexpr std::string_view cpu = "neoverse-v2", isa = "neon";
#endif
template <unsigned K> constexpr auto recipe = *ht::measured_encoding(K, V, cpu, isa);
using Clock = std::chrono::steady_clock;
word elapsed(Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count();
}
struct reducer {
    [[gnu::always_inline]] Value operator()(const Value& a, const Value& b) const {
#if MERGE_REDUCER_KIND == 1
        return b;
#elif MERGE_REDUCER_KIND == 2 || MERGE_REDUCER_KIND == 3
        static_assert(V % 8 == 0);
        Value v{};
        for (unsigned i = 0; i < V; i += 8) {
            const auto x = ht::load<8>(a.data() + i), y = ht::load<8>(b.data() + i);
#if MERGE_REDUCER_KIND == 2
            ht::store<8>(v.data() + i, x + y);
#else
            ht::store<8>(v.data() + i, x * 3 + y * 5 + 7);
#endif
        }
        return v;
#else
        auto v = a;
        for (unsigned i = 0; i < V; ++i)
            v[i] = ht::byte(a[i] * 3 + b[i] * 5 + 7);
        return v;
#endif
    }
};
word scramble(word x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
template <unsigned W> word key(word rank, word seed) {
    word x = (rank ^ seed) & ht::mask<W>;
    x ^= x >> (W / 2);
    x = x * 0xbf58476d1ce4e5b9ULL & ht::mask<W>;
    x ^= x >> (W / 3);
    return x * 0x94d049bb133111ebULL & ht::mask<W>;
}
Value payload(word k, unsigned source) {
    Value v{};
    for (unsigned i = 0; i < V; ++i)
        v[i] = ht::byte(scramble(k + i * 131 + source * 65537));
    return v;
}
template <class S> word identity(const S& src) {
    word h = 17;
    src.allocations([&](auto, const auto* p, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i)
            h = (h ^ p[i]) * 0x100000001b3ULL;
    });
    return h;
}
template <unsigned K, unsigned B> struct fixture {
    static constexpr unsigned W = K + B;
    using S = ht::table<W, B, V, recipe<K>.width, recipe<K>.kernel>;
    using Key = std::conditional_t<(W <= 32), std::uint32_t, word>;
    using Abseil =
        std::conditional_t<V == 0, absl::flat_hash_set<Key>, absl::flat_hash_map<Key, Value>>;
    unsigned m, overlap;
    word seed;
    std::size_t n, common, unique, spills = 0;
    std::vector<std::unique_ptr<S>> owners;
    std::vector<const S*> sources;
    std::vector<Abseil> controls;
    std::vector<word> hashes;
    fixture(unsigned count, unsigned shared, word s)
        : m(count), overlap(shared), seed(s & ht::mask<W>), n(S::bucket_count * S::N * 85 / 100),
          common(n * shared / 100), unique(common + count * (n - common)), controls(count) {
        ht::require(unique <= ht::mask<W>, "fixture fits independent key domain");
        std::vector<std::size_t> order(n);
        std::iota(order.begin(), order.end(), 0);
        for (unsigned source = 0; source < m; ++source) {
            owners.push_back(std::make_unique<S>(seed));
            sources.push_back(owners.back().get());
            controls[source].reserve(n);
            std::mt19937_64 random(seed + source * 271);
            std::shuffle(order.begin(), order.end(), random);
            for (auto j : order) {
                const word rank = j < common ? j : common + source * (n - common) + j - common;
                const auto k = key<W>(rank, seed);
                const auto value = payload(k, source);
                owners[source]->insert(k, value);
                if constexpr (V)
                    controls[source].try_emplace(Key(k), value);
                else
                    controls[source].insert(Key(k));
            }
            owners[source]->validate();
            hashes.push_back(identity(*owners[source]));
            spills += access::table_access::get(*owners[source]).spill;
        }
    }
    Value expected(std::size_t rank) const {
        const auto k = key<W>(rank, seed);
        if (rank < common) {
            auto v = payload(k, 0);
            for (unsigned i = 1; i < m; ++i)
                v = reducer{}(v, payload(k, i));
            return v;
        }
        return payload(k, unsigned((rank - common) / (n - common)));
    }
    template <class D> std::size_t final_spills() const {
        std::vector<unsigned> counts(D::bucket_count);
        ht::permutation<W, D::K> mix{seed};
        std::size_t out = 0;
        // Fixture oracle, outside all timings. Both controls know exact union
        // cardinality; this is a perfectly reserved merge-kernel comparison.
        for (std::size_t rank = 0; rank < unique; ++rank)
            out += counts[mix(key<W>(rank, seed)).bucket]++ >= D::N;
        return out;
    }
    template <class D> word verify(const D& dst) const {
        ht::require(dst.size() == unique, "full union cardinality");
        word checksum = 0;
        for (std::size_t rank = 0; rank < unique; ++rank) {
            const auto k = key<W>(rank, seed);
            const auto want = expected(rank);
            if constexpr (std::is_same_v<D, Abseil>) {
                auto it = dst.find(Key(k));
                ht::require(it != dst.end(), "Abseil union membership");
                if constexpr (V)
                    ht::require(it->second == want, "Abseil ordered complete payload");
            } else
                ht::require(dst.find(k,
                                     [&](const auto& got) {
                                         ht::require(got == want, "ordered complete payload");
                                     }),
                            "union membership");
            checksum += scramble(k);
            for (auto x : want)
                checksum = checksum * 3 + x;
        }
        if constexpr (!std::is_same_v<D, Abseil>)
            dst.validate();
        return checksum;
    }
    // Condition each control's own incoming data before destination preparation.
    // Otherwise the previous packed control warms inputs for the next one,
    // while Abseil walks different storage. This is outside all reported times.
    void condition_inputs(bool abseil) const {
        word sum = 0;
        auto consume = [&](word k, const auto& value) {
            sum += k;
            for (auto b : value)
                sum += b;
        };
        for (unsigned i = 1; i < m; ++i) {
            if (abseil) {
                for (const auto& entry : controls[i]) {
                    if constexpr (V)
                        consume(entry.first, entry.second);
                    else
                        consume(entry, Value{});
                }
            } else
                owners[i]->visit(consume);
        }
        asm volatile("" : : "r"(sum) : "memory");
    }
    void verify_sources() const {
        for (unsigned i = 0; i < m; ++i)
            ht::require(hashes[i] == identity(*owners[i]), "source bytes unchanged");
    }
};
template <class D, class S>
[[gnu::noinline]] ht::accumulation_status run(D& dst, std::span<const S* const> sources) {
    for (const auto* src : sources)
        if (const auto status = ht::accumulate(dst, *src, reducer{});
            status != ht::accumulation_status::complete)
            return status;
    return ht::accumulation_status::complete;
}
template <class A, class S>
[[gnu::noinline]] void run_abseil(A& dst, const std::vector<A>& sources) {
    for (std::size_t i = 1; i < sources.size(); ++i)
        for (const auto& entry : sources[i]) {
            if constexpr (V) {
#if MERGE_REDUCER_KIND == 1
                dst.insert_or_assign(entry.first, entry.second);
#else
                auto [it, inserted] = dst.try_emplace(entry.first, entry.second);
                if (!inserted)
                    it->second = reducer{}(it->second, entry.second);
#endif
            } else
                dst.insert(entry);
        }
}
void row(std::string_view owner, unsigned k, unsigned b, unsigned db, unsigned m, unsigned overlap,
         word seed, unsigned rep, std::size_t incoming, std::size_t unique, word preparation,
         word merge, std::size_t primary, std::size_t overflow, std::size_t work,
         std::size_t spills, word sum) {
    std::cout << owner << ',' << k << ',' << V << ',' << b << ',' << db << ',' << m << ','
              << overlap << ',' << seed << ',' << rep << ',' << incoming << ',' << unique << ','
              << double(preparation) / incoming << ',' << double(merge) / incoming << ','
              << double(preparation + merge) / incoming << ',' << primary << ',' << overflow << ','
              << work << ',' << spills << ',' << sum << '\n';
}
template <unsigned K, unsigned B, unsigned Delta>
void bench(fixture<K, B>& in, unsigned repetitions) {
    using F = fixture<K, B>;
    using S = typename F::S;
    constexpr unsigned DK = K - Delta;
    constexpr auto choice = recipe<DK>;
    using D = ht::table<F::W, B + Delta, V, choice.width, choice.kernel>;
    const auto spill_bound = in.template final_spills<D>();
    const auto incoming = (in.m - 1) * in.n;
    const auto ordered = std::span<const S* const>(in.sources).subspan(1);
    // Each control prewalks its own source; alternate AB/BA across repetitions.
    for (unsigned rep = 0; rep < repetitions; ++rep)
        for (unsigned rotation = 0; rotation < 2; ++rotation) {
            const unsigned owner = (rotation + rep + unsigned(in.seed % 2)) % 2;
            in.condition_inputs(owner == 1);
            const auto start = Clock::now();
            if (owner == 1) {
                auto dst = in.controls[0];
                dst.reserve(in.unique);
                const auto preparation = elapsed(start);
                const auto merge_start = Clock::now();
                run_abseil<typename F::Abseil, S>(dst, in.controls);
                const auto merge = elapsed(merge_start);
                const auto sum = in.verify(dst);
                row("abseil", K, B, B + Delta, in.m, in.overlap, in.seed, rep, incoming, in.unique,
                    preparation, merge, absl::container_internal::AllocatedByteSize(dst), 0, 0, 0,
                    sum);
            } else {
                auto dst = [&] {
                    if constexpr (std::is_same_v<D, S>)
                        return std::make_unique<D>(in.owners[0]->clone());
                    else
                        return std::make_unique<D>(in.seed);
                }();
                ht::reserve_overflow(*dst, spill_bound);
                if constexpr (!std::is_same_v<D, S>)
                    ht::require(ht::accumulate(*dst, *in.owners[0], reducer{}) ==
                                    ht::accumulation_status::complete,
                                "initial destination transfer");
                const auto preparation = elapsed(start);
                const auto state = access::table_access::get(*dst);
                const auto primary = state.primary.data, controls = state.controls.data,
                           slots = state.slots.data;
                const auto merge_start = Clock::now();
                const auto status = run(*dst, ordered);
                const auto merge = elapsed(merge_start);
                ht::require(status == ht::accumulation_status::complete,
                            "fixed-capacity merge completes");
                ht::require(state.primary.data == primary && state.controls.data == controls &&
                                state.slots.data == slots,
                            "leaf capacity remains fixed");
                const auto sum = in.verify(*dst);
                row("ikea", K, B, B + Delta, in.m, in.overlap, in.seed, rep, incoming, in.unique,
                    preparation, merge, state.primary.size, state.controls.size + state.slots.size,
                    0, state.spill, sum);
            }
        }
    in.verify_sources();
}
template <unsigned K, unsigned B>
void dispatch(unsigned count, unsigned overlap, word seed, unsigned reps) {
    fixture<K, B> in(count, overlap, seed);
    if (overlap == 100)
        bench<K, B, 0>(in, reps);
    else if (count == 2)
        bench<K, B, 1>(in, reps);
    else if constexpr (K == 47)
        bench<K, B, 2>(in, reps);
    else
        bench<K, B, 3>(in, reps);
}
} // namespace study
int main(int argc, char** argv) {
    try {
        const auto args = screen::parse(argc, argv);
        const unsigned k = args.source_k, b = args.source_b, m = args.inputs,
                       overlap = args.overlap_percent, reps = args.repetitions;
        const auto seed = args.seed;
        ikea::hashtable::require((m == 2 || m == 8) &&
                                     (overlap == 0 || overlap == 50 || overlap == 100) &&
                                     reps % 2 == 0,
                                 "accumulation panel domain/even balanced repetitions");
        std::cout
            << std::fixed << std::setprecision(6)
            << "owner,K,V,b,destination_b,inputs,overlap,seed,rep,incoming,union,prepare_ns,merge_ns,total_ns,primary_bytes,overflow_bytes,workspace_bytes,spills,checksum\n";
#if MERGE_WIDE_FAMILIES
        if (k == 31 && b == 10)
            study::dispatch<31, 10>(m, overlap, seed, reps);
        else if (k == 47 && b == 10)
            study::dispatch<47, 10>(m, overlap, seed, reps);
        else if (k == 47 && b == 17)
            study::dispatch<47, 17>(m, overlap, seed, reps);
        else if (k == 55 && b == 9)
            study::dispatch<55, 9>(m, overlap, seed, reps);
#else
        if (k == 30 && b == 10)
            study::dispatch<30, 10>(m, overlap, seed, reps);
        else if (k == 30 && b == 15)
            study::dispatch<30, 15>(m, overlap, seed, reps);
        else if (k == 14 && b == 10)
            study::dispatch<14, 10>(m, overlap, seed, reps);
#endif
        else
            throw std::runtime_error("unsupported accumulation fixture geometry");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
