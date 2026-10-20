#include <ikea/hashtable/table.h>
#include <ikea/hashtable/author/table.h>
#include <ikea/hashtable/encoding-selection.h>
#include "../../benchmarks/hashtable/common.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>
#include <string_view>
#if OVERFLOW_PEERS
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#endif
namespace ht = ikea::hashtable;
using ht::word;
using ht::byte;
constexpr unsigned K = OVERFLOW_K, b = OVERFLOW_B, W = K + b, V = OVERFLOW_V;
using Value = ht::bytes<V>;
using Key = std::conditional_t<(W <= 32), std::uint32_t, std::uint64_t>;
#if defined(__x86_64__)
constexpr auto cpu = "zen5";
#if defined(__AVX512VBMI__) && defined(__GFNI__)
constexpr auto isa = "avx512";
#else
constexpr auto isa = "avx2";
#endif
#else
constexpr auto cpu = "neoverse-v2", isa = "neon";
#endif
constexpr auto profile = *ht::measured_encoding(K, V, cpu, isa);
using Core = ht::table<W, b, V, profile.width, profile.kernel>;
using Clock = std::chrono::steady_clock;
inline word bits(word x) { return screen::random_bits(x); }
word key_at(word x, word seed) {
    x = (x ^ seed) & ht::mask<W>;
    x ^= x >> (W / 2);
    x = x * 0xbf58476d1ce4e5b9ULL & ht::mask<W>;
    x ^= x >> (W / 3);
    x = x * 0x94d049bb133111ebULL & ht::mask<W>;
    return x ^ (x >> (W / 2));
}
Value payload(word key, unsigned version) {
    Value out{};
    for (unsigned i = 0; i < V; i += 8)
        ht::store<8>(out.data() + i, bits(key ^ (word(version) << 40) ^ i));
    return out;
}
struct Row { word key; Value value; };
struct Result {
    word hits = 0, keys = 0, values = 0;
    bool operator==(const Result&) const = default;
};
std::size_t allocated = 0;
template <class T> struct Allocator {
    using value_type = T;
    Allocator() = default;
    template <class U> Allocator(const Allocator<U>&) {}
    T* allocate(std::size_t n) {
        auto* p = std::allocator<T>{}.allocate(n);
        allocated += n * sizeof(T);
        return p;
    }
    void deallocate(T* p, std::size_t n) {
        allocated -= n * sizeof(T);
        std::allocator<T>{}.deallocate(p, n);
    }
    template <class U> bool operator==(const Allocator<U>&) const { return true; }
};
#if OVERFLOW_PEERS
using Pair = std::pair<const Key, Value>;
using Abseil = std::conditional_t<V == 0,
    absl::flat_hash_set<Key, absl::Hash<Key>, std::equal_to<Key>, Allocator<Key>>,
    absl::flat_hash_map<Key, Value, absl::Hash<Key>, std::equal_to<Key>, Allocator<Pair>>>;
using Boost = std::conditional_t<V == 0,
    boost::unordered_flat_set<Key, boost::hash<Key>, std::equal_to<Key>, Allocator<Key>>,
    boost::unordered_flat_map<Key, Value, boost::hash<Key>, std::equal_to<Key>, Allocator<Pair>>>;
#endif
template <class T> constexpr bool core = std::is_same_v<T, Core>;
template <class T> std::unique_ptr<T> make(word seed, std::size_t n) {
    if constexpr (core<T>) return std::make_unique<T>(seed);
    else { auto p = std::make_unique<T>(); p->reserve(n); return p; }
}
template <bool Split = false, class T> [[gnu::always_inline]] bool put(T& t, const Row& r) {
    if constexpr (core<T>) {
        if constexpr (Split) return t.template insert<ht::write_strategy::existing>(r.key, r.value);
        else return t.insert(r.key, r.value);
    } else if constexpr (V) return t.insert_or_assign(Key(r.key), r.value).second;
    else return t.insert(Key(r.key)).second;
}
template <class T, class F> [[gnu::always_inline]] bool find(const T& t, word key, F&& f) {
    if constexpr (core<T>) return t.find(key, std::forward<F>(f));
    else {
        auto it = t.find(Key(key));
        if (it == t.end()) return false;
        if constexpr (V) f(it->second);
        return true;
    }
}
template <class T> void verify(const T& t, const std::vector<Row>& rows) {
    ht::require(t.size() == rows.size(), "population oracle");
    for (const auto& r : rows)
        ht::require(find(t, r.key, [&](const Value& v) { ht::require(v == r.value, "payload oracle"); }), "membership oracle");
}
template <class T> [[gnu::always_inline]] void prefetch(const T& t, word key) {
    if constexpr (core<T>) t.prefetch(t.prepare(key));
#if OVERFLOW_PEERS
    else if constexpr (std::is_same_v<T, Abseil>) t.prefetch(Key(key));
#endif
    // Boost's native lookup already prefetches fingerprint candidates. It has
    // no public equivalent of Abseil's home-address prefetch; don't invent one.
}
template <bool Prefetch, class T>
[[gnu::noinline]] Result read_loop(const T& t, const std::vector<word>& queries) {
    Result result;
    if constexpr (Prefetch)
        for (std::size_t i = 0; i < std::min(std::size_t(32), queries.size()); ++i) prefetch(t, queries[i]);
    for (std::size_t i = 0; i < queries.size(); ++i) {
        if constexpr (Prefetch)
            if (i + 32 < queries.size()) prefetch(t, queries[i + 32]);
        const auto key = queries[i];
        const bool hit = find(t, key, [&](const Value& value) {
            for (unsigned j = 0; j < V; j += 8) result.values += ht::load<8>(value.data() + j);
        });
        result.hits += hit;
        result.keys += hit ? key : 0;
    }
    return result;
}
[[gnu::noinline]] word condition(const std::vector<byte>& bytes) {
    word sum = 0;
    for (std::size_t i = 0; i < bytes.size(); i += 64) sum += bytes[i];
    asm volatile("" : "+r"(sum) : : "memory");
    return sum;
}
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::nano>(Clock::now() - start).count();
}
struct Options {
    std::string impl = OVERFLOW_NAME;
    unsigned load = 85, seed = 713, repetitions = 3, churn = 8;
    std::size_t requests = 1 << 20, conditioning = 0;
    std::size_t target_spill = 0;
    bool reads = true, writes = true;
};
template <class T> void report(const T& t, const Options& o, std::string_view phase,
                              std::string_view scope, unsigned miss, unsigned distance,
                              unsigned rep, std::size_t n, std::size_t count, double ns,
                              Result result = {}) {
    std::size_t bytes, spill = 0, groups = 0, slots;
    if constexpr (core<T>) {
        bytes = t.bytes_used(); spill = t.spill_size(); groups = t.swiss_groups(); slots = t.capacity();
    } else { bytes = allocated; slots = t.bucket_count(); }
    std::size_t debt = 0, swiss_slots = 0;
    if constexpr (core<T>) {
        debt = ht::author::table_access::get(const_cast<T&>(t)).deleted;
        swiss_slots = groups * SWISS_GROUP_BYTES;
    }
    std::cout << o.impl << ',' << cpu << ',' << isa << ',' << K << ',' << b << ',' << V << ','
              << profile.width << ',' << o.seed << ',' << o.load << ',' << rep << ',' << phase << ','
              << scope << ',' << miss << ',' << distance << ',' << o.conditioning << ',' << n << ','
              << count << ',' << ns / count << ',' << result.hits << ',' << result.keys << ','
              << result.values << ',' << bytes << ',' << spill << ',' << groups << ',' << slots << ','
              << OVERFLOW_SWISS_CEILING32 << ',' << o.target_spill << ',' << swiss_slots << ',' << debt << ','
              << (swiss_slots ? 100.0 * spill / swiss_slots : 0) << '\n';
    std::cout.flush();
}
template <class T> void run(const Options& o) {
    std::size_t n = Core::bucket_count * Core::N * o.load / 100;
    if (o.target_spill) {
        std::vector<unsigned> counts(Core::bucket_count);
        const ht::permutation<W, K> placement{o.seed};
        std::size_t spills = 0; n = 0;
        while (spills < o.target_spill)
            spills += counts[placement(key_at(n++, o.seed)).bucket]++ >= Core::N;
    }
    ht::require(n * 16 < (word{1} << W), "fixture fits domain");
    std::vector<Row> rows; rows.reserve(n);
    std::vector<word> primary, spill;
    std::vector<unsigned> occupancy(Core::bucket_count);
    const ht::permutation<W, K> mix{o.seed};
    for (std::size_t i = 0; i < n; ++i) {
        const auto k = key_at(i, o.seed);
        rows.push_back({k, payload(k, 0)});
        auto& count = occupancy[mix(k).bucket];
        (count++ < Core::N ? primary : spill).push_back(i);
    }
    const auto start = Clock::now();
    auto t = make<T>(o.seed, n);
    word inserted = 0;
    for (const auto& r : rows) inserted += put(*t, r);
    const auto build_ns = elapsed(start);
    ht::require(inserted == n, "initial unique insertion");
    report(*t, o, "build", "all", 0, 0, 0, n, n, build_ns);
    verify(*t, rows);
    if constexpr (core<T>) { t->validate(); ht::require(t->spill_size() == spill.size(), "route oracle"); }
    std::vector<byte> conditioning(o.conditioning, byte(o.seed));
    std::mt19937_64 rng(o.seed ^ 0x374713);
    if (o.reads) {
        for (auto scope : {"all", "routed", "primary"}) {
            for (unsigned miss : {0u, 50u, 100u}) {
                if (std::string_view(scope) == "primary" && miss) continue;
                if (std::string_view(scope) == "routed" && spill.empty()) continue;
                std::vector<word> queries; queries.reserve(o.requests);
                Result expected;
                for (std::size_t i = 0; i < o.requests; ++i) {
                    const bool absent = rng() % 100 < miss;
                    word k;
                    if (!absent) {
                        std::size_t rank = rng() % n;
                        if (std::string_view(scope) == "routed") rank = spill[rng() % spill.size()];
                        if (std::string_view(scope) == "primary") rank = primary[rng() % primary.size()];
                        k = rows[rank].key;
                        ++expected.hits; expected.keys += k;
                        for (unsigned j = 0; j < V; j += 8) expected.values += ht::load<8>(rows[rank].value.data() + j);
                    } else {
                        do { k = key_at(n + rng() % n, o.seed); }
                        while (std::string_view(scope) == "routed" && occupancy[mix(k).bucket] <= Core::N);
                    }
                    queries.push_back(k);
                }
                for (unsigned distance : {0u, 32u}) {
#if OVERFLOW_PEERS
                    if constexpr (std::is_same_v<T, Boost>) if (distance) continue;
#endif
                    const auto warm = distance ? read_loop<true>(*t, queries) : read_loop<false>(*t, queries);
                    ht::require(warm == expected, "warm read oracle");
                    for (unsigned rep = 0; rep < o.repetitions; ++rep) {
                        condition(conditioning);
                        const auto begin = Clock::now();
                        const auto result = distance ? read_loop<true>(*t, queries) : read_loop<false>(*t, queries);
                        const auto ns = elapsed(begin);
                        ht::require(result == expected, "timed read oracle");
                        report(*t, o, "read", scope, miss, distance, rep, n, queries.size(), ns, result);
                    }
                }
            }
        }
    }
    if (o.writes) {
        std::vector<std::size_t> ranks(n);
        std::iota(ranks.begin(), ranks.end(), 0);
        std::shuffle(ranks.begin(), ranks.end(), rng);
        ranks.resize(std::min(n / 4, o.requests));
        std::vector<Row> updates; updates.reserve(ranks.size());
        word next = n;
        // Replace both with the shared-store and existing-key optimized core.
        // Follow with erase/refill rounds; every round starts at n live entries.
        for (unsigned round = 0; round < o.churn + 2; ++round) {
            updates.clear();
            for (auto rank : ranks) {
                const bool fresh = round >= 3 || (round == 2 && updates.size() % 2 == 0);
                const auto k = fresh ? key_at(next++, o.seed) : rows[rank].key;
                updates.push_back({k, payload(k, round + 1)});
            }
            condition(conditioning);
            const auto begin = Clock::now();
            word changed = 0;
            if (round >= 2) for (std::size_t i = 0; i < ranks.size(); ++i)
                if (round >= 3 || i % 2 == 0) changed += t->erase(rows[ranks[i]].key);
            const auto erase_ns = elapsed(begin);
            const auto fresh_count = round >= 3 ? ranks.size() : (round == 2 ? (ranks.size() + 1) / 2 : 0);
            if (round >= 2) ht::require(changed == fresh_count, "erase count");
            const auto insert_start = Clock::now();
            changed = 0;
            for (const auto& r : updates) changed += round == 1 ? put<true>(*t, r) : put(*t, r);
            const auto ns = elapsed(insert_start);
            ht::require(changed == fresh_count, "write count");
            for (std::size_t i = 0; i < ranks.size(); ++i) {
                if (round >= 3 || (round == 2 && i % 2 == 0))
                    ht::require(!find(*t, rows[ranks[i]].key, [](const auto&) {}), "old key gone");
                rows[ranks[i]] = updates[i];
            }
            ht::require(t->size() == n, "churn population");
            for (auto rank : ranks)
                ht::require(find(*t, rows[rank].key, [&](const Value& v) { ht::require(v == rows[rank].value, "churn payload"); }), "churn updated key");
            report(*t, o, round < 2 ? (round == 0 ? "replace_mixed" : "replace_existing") : (round == 2 ? "upsert50" : "refill"),
                   "all", 0, 0, round, n, updates.size(), ns);
            if (round >= 2) report(*t, o, "erase", "all", 0, 0, round, n, fresh_count, erase_ns);
        }
        verify(*t, rows);
        if constexpr (core<T>) t->validate();
        for (unsigned miss : {0u, 50u, 100u}) {
            std::vector<word> queries; queries.reserve(o.requests);
            Result expected;
            for (std::size_t i = 0; i < o.requests; ++i) {
                const auto rank = rng() % n;
                if (rng() % 100 < miss) queries.push_back(key_at(next + rank, o.seed));
                else {
                    const auto& r = rows[rank]; queries.push_back(r.key);
                    ++expected.hits; expected.keys += r.key;
                    for (unsigned j = 0; j < V; j += 8) expected.values += ht::load<8>(r.value.data() + j);
                }
            }
            for (unsigned distance : {0u, 32u}) {
#if OVERFLOW_PEERS
                if constexpr (std::is_same_v<T, Boost>) if (distance) continue;
#endif
                ht::require((distance ? read_loop<true>(*t, queries) : read_loop<false>(*t, queries)) == expected, "settled warm oracle");
                for (unsigned rep = 0; rep < o.repetitions; ++rep) {
                    condition(conditioning);
                    const auto begin = Clock::now();
                    const auto result = distance ? read_loop<true>(*t, queries) : read_loop<false>(*t, queries);
                    const auto ns = elapsed(begin);
                    ht::require(result == expected, "settled read oracle");
                    report(*t, o, "read_after_churn", "all", miss, distance, rep, n, queries.size(), ns, result);
                }
            }
        }
    }
}
int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; i += 2) {
        ht::require(i + 1 < argc, "option value");
        const std::string_view flag = argv[i];
        const auto value = std::strtoull(argv[i + 1], nullptr, 10);
        if (flag == "--impl") o.impl = argv[i + 1];
        else if (flag == "--load") o.load = value;
        else if (flag == "--seed") o.seed = value;
        else if (flag == "--repetitions") o.repetitions = value;
        else if (flag == "--requests") o.requests = value;
        else if (flag == "--conditioning") o.conditioning = value;
        else if (flag == "--churn") o.churn = value;
        else if (flag == "--target-spill") o.target_spill = value;
        else if (flag == "--reads") o.reads = value;
        else if (flag == "--writes") o.writes = value;
        else ht::require(false, "unknown option");
    }
    std::cout << "impl,cpu,isa,k,b,v,e,seed,load,rep,phase,scope,miss,prefetch,conditioning,n,operations,ns,hits,keys,values,bytes,spill,groups,slots,ceiling32,target_spill,swiss_slots,debt,swiss_load\n";
#if OVERFLOW_PEERS
    if (o.impl == "boost") run<Boost>(o);
    else if (o.impl == "abseil") run<Abseil>(o);
    else ht::require(false, "peer choice");
#else
    ht::require(o.impl == OVERFLOW_NAME, "candidate choice");
    run<Core>(o);
#endif
}
