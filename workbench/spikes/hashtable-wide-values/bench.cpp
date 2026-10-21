#include "model.h"
#include "staged-read.h"
#if WIDE_LOOM
#include "loom-read.h"
#endif
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#if defined(__x86_64__)
#include <immintrin.h>
#endif

namespace wide {
using Clock = std::chrono::steady_clock;
struct Options {
    std::string impl = "borrow", phase = "all", hint = "auto", temperature = "warm";
    std::size_t population = std::size_t(Table<b, V>::logical_bucket_count) * Table<b, V>::N * 85 / 100;
    std::size_t requests = 262144;
    unsigned repetitions = 3, seed = 713, prefetch = 0, pipeline = 0, loom = 0, staged = 0;
};
Options options;
word random_bits(word x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
Key key_at(word x) {
    x = (x ^ options.seed) & ht::mask<W>;
    x ^= x >> (W / 2); x = (x * 0xbf58476d1ce4e5b9ULL) & ht::mask<W>;
    x ^= x >> (W / 3); x = (x * 0x94d049bb133111ebULL) & ht::mask<W>;
    return Key(x ^ (x >> (W / 2)));
}
Value payload(Key key, unsigned version = 0) {
    Value value{};
    for (unsigned at = 0; at < V; at += 8)
        ht::store<8>(value.data() + at, random_bits(word(key) ^ (word(version) << 48) ^ at));
    return value;
}
struct Result {
    word hits{}, keys{}, digest{};
    bool operator==(const Result&) const = default;
};
template <unsigned Bytes> [[gnu::always_inline]] word digest(const byte* value) {
    word result{};
    for (unsigned at = 0; at < Bytes; at += 8) result += ht::load<8>(value + at);
    return result;
}
void flush_backing() {
#if defined(__x86_64__)
    for (auto [p, n] : backing)
        for (std::size_t at = 0; at < n; at += 64)
            _mm_clflushopt(static_cast<const byte*>(p) + at);
    _mm_sfence();
#else
    ht::require(options.temperature != "cold", "cold runs require native Zen 5 cache flush");
#endif
}
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::nano>(Clock::now() - start).count();
}
void row(std::string_view operation, std::string_view pattern, unsigned consumed, unsigned rep,
         std::size_t count, double ns, Result result = {}, std::size_t source_bytes = 0) {
    std::cout << options.impl << ',' << W << ',' << b << ',' << V << ',' << options.seed
              << ',' << options.population << ',' << options.temperature << ',' << options.hint
              << ',' << options.prefetch << ',' << options.pipeline << ',' << options.loom << ',' << options.staged << ',' << operation << ',' << pattern << ',' << consumed
              << ',' << rep << ',' << count << ',' << ns / count << ',' << allocated
              << ',' << peak << ',' << source_bytes << ',' << allocations << ',' << result.hits
              << ',' << result.keys << ',' << result.digest << '\n';
}
template <class M> void fill(M& model, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) ht::require(model.put(key_at(i), payload(key_at(i))), "fresh input key");
}
template <class M, class Expected> void verify(const M& model, std::size_t count, Expected&& expected) {
    ht::require(model.size() == count, "post-state cardinality");
    for (std::size_t i = 0; i < options.population; ++i) {
        const auto key = key_at(i);
        const auto value = expected(i);
        bool observed = model.template read<ht::lookup_hint::predictable>(key, [&](const byte* p) {
            ht::require(value.has_value() && std::memcmp(p, value->data(), V) == 0, "post-state full payload");
        });
        ht::require(observed == value.has_value(), "post-state exact membership");
    }
}
template <unsigned Bytes, ht::lookup_hint Hint, unsigned Distance, unsigned Pipeline, class M>
[[gnu::noinline]] Result read_range(const M& model, const Key* q, std::size_t first, std::size_t count) {
    Result result;
    if constexpr (Pipeline) {
        struct Pending {
            const byte* address = zero<V>.data();
            Key key{};
            bool found{};
            ht::bytes<Bytes> copy;
        };
        std::array<Pending, Pipeline> pending;
        const auto finish = [&](const Pending& item) {
            result.digest += digest<Bytes>(item.address);
            result.hits += item.found;
            result.keys += item.found ? item.key : 0;
        };
        for (std::size_t i = first; i < first + count; ++i) {
            if constexpr (Distance)
                if (i + Distance < options.requests) model.prefetch(q[i + Distance]);
            const auto relative = i - first;
            auto& item = pending[relative % Pipeline];
            if (relative >= Pipeline) finish(item);
            item.address = zero<V>.data(); item.key = q[i];
            item.found = model.template read<Hint>(item.key, [&](const byte* value) {
                if constexpr (M::kind == Kind::materialized || (M::kind == Kind::borrowed && V <= 8)) {
                    // Its result is temporary. Retaining that pointer would make
                    // a fake fast baseline; copy the actually consumed bytes.
                    std::memcpy(item.copy.data(), value, Bytes);
                    item.address = item.copy.data();
                } else {
                    item.address = value;
                    __builtin_prefetch(value, 0, 3);
                    if constexpr (Bytes > 64) __builtin_prefetch(value + 64, 0, 3);
                    if constexpr (Bytes > 1) __builtin_prefetch(value + Bytes - 1, 0, 3);
                }
            });
        }
        for (std::size_t i = count > Pipeline ? count - Pipeline : 0; i < count; ++i)
            finish(pending[i % Pipeline]);
        return result;
    }
    for (std::size_t i = first; i < first + count; ++i) {
        if constexpr (Distance)
            if (i + Distance < options.requests) model.prefetch(q[i + Distance]);
        const auto key = q[i];
        const bool hit = model.template read<Hint>(key, [&](const byte* p) { result.digest += digest<Bytes>(p); });
        result.hits += hit;
        result.keys += hit ? key : 0;
    }
    return result;
}
template <unsigned Bytes, unsigned Distance, unsigned Pipeline, class M>
Result read_requests_at(const M& model, const std::vector<Key>& queries, ht::lookup_selector& selector) {
    if (options.hint == "p") return read_range<Bytes, ht::lookup_hint::predictable, Distance, Pipeline>(model, queries.data(), 0, queries.size());
    if (options.hint == "u") return read_range<Bytes, ht::lookup_hint::unpredictable, Distance, Pipeline>(model, queries.data(), 0, queries.size());
    Result result;
    selector.run(queries.size(), [&](auto hint, std::size_t first, std::size_t count) {
        auto part = read_range<Bytes, decltype(hint)::value, Distance, Pipeline>(model, queries.data(), first, count);
        result.hits += part.hits; result.keys += part.keys; result.digest += part.digest;
    });
    return result;
}
template <unsigned Bytes, class M>
Result read_requests(const M& model, const std::vector<Key>& queries, ht::lookup_selector& selector) {
    if constexpr (indirect<M::kind>) {
        if (options.staged) {
            Result result;
            const auto add = [&](auto hint, std::size_t first, std::size_t count) {
                const auto part = options.prefetch
                    ? staged_read_range<Bytes, decltype(hint)::value, 32, M, Result>(model, queries.data(), first, count, queries.size())
                    : staged_read_range<Bytes, decltype(hint)::value, 0, M, Result>(model, queries.data(), first, count, queries.size());
                result.hits += part.hits; result.keys += part.keys; result.digest += part.digest;
            };
            if (options.hint == "p") add(std::integral_constant<ht::lookup_hint, ht::lookup_hint::predictable>{}, 0, queries.size());
            else if (options.hint == "u") add(std::integral_constant<ht::lookup_hint, ht::lookup_hint::unpredictable>{}, 0, queries.size());
            else selector.run(queries.size(), add);
            return result;
        }
    } else ht::require(!options.staged, "ID staging requires an indirect model");
    if (options.pipeline) {
        if (options.prefetch) return read_requests_at<Bytes, 32, 16>(model, queries, selector);
        return read_requests_at<Bytes, 0, 16>(model, queries, selector);
    }
    if (options.prefetch) return read_requests_at<Bytes, 32, 0>(model, queries, selector);
    return read_requests_at<Bytes, 0, 0>(model, queries, selector);
}
#if WIDE_LOOM
template <unsigned Bytes, bool StagedIDs, class M>
void loom_case(const M& model, std::string_view pattern, const std::vector<Key>& queries, Result expected) {
    LoomReader<Bytes, M, Result, StagedIDs> reader(model, queries.data(), queries.size());
    ht::lookup_selector selector;
    const auto execute = [&] {
        Result result;
        const auto add = [&](auto hint, std::size_t first, std::size_t count) {
            const auto part = options.prefetch ? reader.template run<decltype(hint)::value, 32>(first, count)
                                               : reader.template run<decltype(hint)::value, 0>(first, count);
            result.hits += part.hits; result.keys += part.keys; result.digest += part.digest;
        };
        if (options.hint == "p") add(std::integral_constant<ht::lookup_hint, ht::lookup_hint::predictable>{}, 0, queries.size());
        else if (options.hint == "u") add(std::integral_constant<ht::lookup_hint, ht::lookup_hint::unpredictable>{}, 0, queries.size());
        else selector.run(queries.size(), add);
        return result;
    };
    (void)execute();
    for (unsigned rep = 0; rep < options.repetitions; ++rep) {
        if (options.temperature == "cold") flush_backing();
        const auto start = Clock::now();
        const auto actual = execute();
        const auto ns = elapsed(start);
        ht::require(actual == expected, "actual Loom complete consumption oracle");
        row("read", pattern, Bytes, rep, queries.size(), ns, actual);
    }
}
#endif
template <unsigned Bytes, class M>
void reads_case(const M& model, std::string_view pattern, const std::vector<Key>& queries, Result expected) {
#if WIDE_LOOM
    if (options.loom) {
        if constexpr (indirect<M::kind>) {
            if (options.staged) loom_case<Bytes, true>(model, pattern, queries, expected);
            else loom_case<Bytes, false>(model, pattern, queries, expected);
        } else {
            ht::require(!options.staged, "ID staging requires an indirect model");
            loom_case<Bytes, false>(model, pattern, queries, expected);
        }
        return;
    }
#else
    ht::require(!options.loom, "Loom comparison enabled at construction");
#endif
    ht::lookup_selector selector;
    (void)read_requests<Bytes>(model, queries, selector);
    for (unsigned rep = 0; rep < options.repetitions; ++rep) {
        if (options.temperature == "cold") flush_backing();
        const auto start = Clock::now();
        const auto actual = read_requests<Bytes>(model, queries, selector);
        const double ns = elapsed(start);
        ht::require(actual == expected, "complete consumed read oracle");
        row("read", pattern, Bytes, rep, queries.size(), ns, actual);
    }
}
template <class M> void reads(const M& model) {
    for (std::string_view pattern : {"hit", "miss", "random50", "alternating50", "runs64"}) {
        std::mt19937_64 random(options.seed ^ 0x8783);
        std::vector<Key> queries(options.requests);
        Result full, projected;
        for (std::size_t i = 0; i < queries.size(); ++i) {
            const auto ordinal = random() % options.population;
            const bool hit = pattern == "hit" || (pattern == "random50" && (random() & 1)) ||
                             (pattern == "alternating50" && (i & 1)) ||
                             (pattern == "runs64" && ((i / 64) & 1));
            const auto key = key_at(ordinal + (hit ? 0 : options.population));
            queries[i] = key;
            if (hit) {
                const auto value = payload(key);
                ++full.hits; full.keys += key; full.digest += digest<V>(value.data());
                ++projected.hits; projected.keys += key; projected.digest += digest<std::min(16u, V)>(value.data());
            }
        }
        reads_case<V>(model, pattern, queries, full);
        if constexpr (V > 16) reads_case<16>(model, pattern, queries, projected);
    }
}

struct Write { Key key; Value value; };
template <bool Existing, class M> [[gnu::noinline]] word write_requests(M& model, const std::vector<Write>& input) {
    word changed{};
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (options.prefetch && i + options.prefetch < input.size()) model.prefetch(input[i + options.prefetch].key);
        changed += model.put(input[i].key, input[i].value, Existing);
    }
    return changed;
}
template <Kind K> void writes() {
    using M = Model<K, b>;
    // All writes have caller-prepared headroom. Growth is measured separately:
    // fresh insertion ends at the same population as the read fixture.
    const auto count = std::min(options.requests, std::max(std::size_t{1}, options.population / 4));
    for (std::string_view operation : {"replace", "increment", "upsert50", "insert", "erase_refill"}) {
        const auto fresh_count = operation == "insert" ? count : operation == "upsert50" ? (count + 1) / 2 : 0;
        const auto initial = options.population - fresh_count;
        std::vector<std::size_t> ordinals(initial);
        std::iota(ordinals.begin(), ordinals.end(), 0);
        std::mt19937_64 random(options.seed ^ 0xa915);
        std::shuffle(ordinals.begin(), ordinals.end(), random);
        std::vector<Write> input(count);
        std::vector<bool> changed(options.population);
        std::size_t added{};
        for (std::size_t i = 0; i < count; ++i) {
            const bool fresh = operation == "insert" || (operation == "upsert50" && !(i & 1));
            const auto ordinal = fresh ? initial + added++ : ordinals[i];
            const auto key = key_at(ordinal);
            input[i] = {key, payload(key, 1)};
            changed[ordinal] = true;
        }
        for (unsigned rep = 0; rep < options.repetitions; ++rep) {
            M model(options.seed, options.population);
            fill(model, initial);
            peak = allocated;
            if (options.temperature == "cold") flush_backing();
            if (operation == "increment") {
                const auto start = Clock::now(); word modified{};
                for (const auto& update : input) modified += model.increment(update.key);
                const auto ns = elapsed(start);
                ht::require(modified == count, "increment membership");
                row(operation, "one_limb", 8, rep, count, ns);
                verify(model, options.population, [&](std::size_t i) {
                    auto value = payload(key_at(i));
                    if (changed[i]) ht::store<8>(value.data(), ht::load<8>(value.data()) + 1);
                    return std::optional(value);
                });
                continue;
            } else if (operation == "erase_refill") {
                auto start = Clock::now(); word erased{};
                for (const auto& update : input) erased += model.erase(update.key);
                const auto erase_ns = elapsed(start);
                ht::require(erased == count, "erase result");
                row("erase", "existing", V, rep, count, erase_ns);
                verify(model, initial - count, [&](std::size_t i) -> std::optional<Value> {
                    if (changed[i]) return {}; return payload(key_at(i));
                });
                if (options.temperature == "cold") flush_backing();
                start = Clock::now(); const auto inserted = write_requests<false>(model, input);
                const auto refill_ns = elapsed(start);
                ht::require(inserted == count, "refill cardinality result");
                row("refill", "holes", V, rep, count, refill_ns);
            } else {
                const auto start = Clock::now(); const auto inserted = operation == "replace" ? write_requests<true>(model, input) : write_requests<false>(model, input);
                const auto ns = elapsed(start);
                ht::require(inserted == fresh_count, "prepared write insertion count");
                row(operation, operation == "replace" ? "existing" : operation == "insert" ? "fresh" : "alternating50", V, rep, count, ns);
            }
            verify(model, options.population, [&](std::size_t i) { return std::optional(payload(key_at(i), changed[i] ? 1 : 0)); });
        }
    }
}
template <Kind K> void growth() {
    for (unsigned rep = 0; rep < options.repetitions; ++rep) {
        Model<K, b> model(options.seed, options.population);
        fill(model, options.population);
        const auto before = allocated;
        peak = allocated;
        if (options.temperature == "cold") flush_backing();
        const auto start = Clock::now();
#if WIDE_PEERS
        model.table->rehash(model.table->bucket_count() * 2);
        const auto ns = elapsed(start);
        row("growth", "native_rehash", V, rep, options.population, ns, {}, before);
        verify(model, options.population, [&](std::size_t i) { return std::optional(payload(key_at(i))); });
#else
        using Target = Table<b + 1, indirect<K> ? cell_bytes<K> : V>;
        auto grown = ht::grow<Target>(*model.table);
        const auto ns = elapsed(start);
        row("growth", "same_seed_source_retained", V, rep, options.population, ns, {}, before);
        ht::require(grown->size() == model.size(), "grown cardinality");
        for (std::size_t i = 0; i < options.population; ++i) {
            const auto key = key_at(i); const auto expected = payload(key);
            ht::require(grown->find(key, [&](const auto& value) {
                if constexpr (indirect<K>) ht::require(std::memcmp(model.payloads->at(ht::load<cell_bytes<K>>(value.data())), expected.data(), V) == 0, "grown slot payload");
                else ht::require(value == expected, "grown full payload");
            }), "grown membership");
        }
#endif
    }
}
template <Kind K> void check() {
    const auto count = options.population;
    Model<K, b> model(options.seed, count);
    fill(model, count);
    verify(model, count, [&](std::size_t i) { return std::optional(payload(key_at(i))); });
    // Independent std map oracle; include absent keys, mixed updates, tombstone
    // reuse and slots recycled to other keys. Compare every live payload byte.
    std::unordered_map<Key, Value> expected;
    for (std::size_t i = 0; i < count; ++i) expected.emplace(key_at(i), payload(key_at(i)));
    std::mt19937_64 random(options.seed);
    for (unsigned step = 0; step < 12000; ++step) {
        const auto key = key_at(random() % (count * 2));
        if (random() % 3 == 0) ht::require(model.erase(key) == (expected.erase(key) != 0), "history erase oracle");
        else { const auto value = payload(key, step); const auto fresh = !expected.contains(key); expected[key] = value;
            ht::require(model.put(key, value) == fresh, "history put oracle"); }
        for (unsigned hint = 0; hint < 2; ++hint) {
            const auto it = expected.find(key);
            const auto consume = [&](const byte* p) {
                if (it != expected.end()) ht::require(std::memcmp(p, it->second.data(), V) == 0, "history payload oracle");
                else ht::require(digest<V>(p) == 0, "readable zero miss");
            };
            const bool found = hint ? model.template read<ht::lookup_hint::unpredictable>(key, consume)
                                    : model.template read<ht::lookup_hint::predictable>(key, consume);
            ht::require(found == (it != expected.end()), "history lookup oracle");
        }
    }
    ht::require(model.size() == expected.size(), "history final cardinality");
    for (const auto& [key, value] : expected) ht::require(model.template read<ht::lookup_hint::predictable>(key,
        [&](const byte* p) { ht::require(std::memcmp(p, value.data(), V) == 0, "history final payload"); }), "history final membership");
    row("check", "independent_history", V, 0, 12000, 0);
}
#if !WIDE_PEERS
template <unsigned Width, unsigned Buckets> void borrowed_boundaries() {
    using T = ht::table<Width, Buckets, V, Width - Buckets, ht::search_kernel::generic, Storage>;
    T table(options.seed);
    const typename T::mix_type mix{word(options.seed) & ht::mask<Width>};
    std::unordered_map<word, Value> expected;
    const auto count = unsigned(std::min(word{384}, ht::mask<T::K> + (T::K < 64)));
    const auto inspect = [&](word key) {
        const auto it = expected.find(key);
        const auto p = borrow<ht::lookup_hint::predictable>(table, key);
        const auto u = borrow<ht::lookup_hint::unpredictable>(table, key);
        ht::require(p.found == (it != expected.end()) && p.found == u.found, "borrowed boundary membership");
        if (p.found) {
            ht::require(std::memcmp(p.address, it->second.data(), V) == 0 &&
                        std::memcmp(u.address, it->second.data(), V) == 0, "borrowed boundary payload");
            bool owned = false;
            for (auto [begin, bytes] : backing)
                owned |= std::uintptr_t(p.address) >= std::uintptr_t(begin) &&
                         std::uintptr_t(p.address) + V <= std::uintptr_t(begin) + bytes;
            ht::require(owned, "borrow is inside retained assigned backing");
        } else ht::require(digest<V>(p.address) == 0 && digest<V>(u.address) == 0, "borrowed boundary zero miss");
    };
    for (unsigned i = 0; i < count; ++i) {
        const auto key = mix.recover(0, i);
        const auto value = payload(Key(key));
        table.insert(key, value); expected[key] = value;
        for (unsigned j = 0; j <= i; ++j) inspect(mix.recover(0, j));
        if (i + 1 < ht::mask<T::K>) inspect(mix.recover(0, i + 1));
    }
    if constexpr (!T::bitmap) ht::require(table.spill_size() > 255, "sticky overflow count exercised");
    for (unsigned i = 0; i < count; i += 3) { const auto key = mix.recover(0, i); table.erase(key); expected.erase(key); }
    for (unsigned i = 0; i < count; ++i) inspect(mix.recover(0, i));
    for (unsigned i = 0; i < count; i += 3) { const auto key = mix.recover(0, i); const auto value = payload(Key(key), 1); table.insert(key, value); expected[key] = value; }
    for (unsigned i = 0; i < count; ++i) inspect(mix.recover(0, i));
    const auto last = mix.recover(0, ht::mask<T::K>);
    const auto last_value = payload(Key(last), 2);
    table.insert(last, last_value); expected[last] = last_value;
    inspect(last);
    table.validate();
}
#endif
template <Kind K> void run() {
    if (options.phase == "check") {
        {
            Storage::buffer calibration(64);
            ht::require(allocated == 64, "buffer accounting starts at capacity");
            peak = allocated;
            calibration.grow(128);
            ht::require(allocated == 128 && peak == 192, "replacement temporarily retains both buffers");
            peak = allocated;
            calibration.grow(128);
            ht::require(allocated == 128 && peak == 128, "in-capacity growth allocates nothing");
        }
        ht::require(allocated == 0, "calibration releases backing");
        check<K>();
#if !WIDE_PEERS
        if constexpr (K == Kind::borrowed) {
            borrowed_boundaries<4, 0>();
            borrowed_boundaries<9, 0>();
            borrowed_boundaries<17, 3>();
            borrowed_boundaries<31, 3>();
            borrowed_boundaries<40, 3>();
            borrowed_boundaries<64, 0>();
        }
#endif
    }
    if (options.phase == "reads" || options.phase == "all") {
        Model<K, b> model(options.seed, options.population);
        fill(model, options.population);
        row("memory", "built", V, 0, options.population, 0);
        reads(model);
    }
    if (options.phase == "writes" || options.phase == "all") writes<K>();
    if (options.phase == "growth" || options.phase == "all") growth<K>();
    ht::require(allocated == 0 && backing.empty(), "owned backing accounting returns to zero");
}
} // namespace wide

int main(int argc, char** argv) {
    using namespace wide;
    for (int i = 1; i < argc; i += 2) {
        ht::require(i + 1 < argc, "argument value");
        const std::string name = argv[i], value = argv[i + 1];
        if (name == "--impl") options.impl = value;
        else if (name == "--phase") options.phase = value;
        else if (name == "--hint") options.hint = value;
        else if (name == "--temperature") options.temperature = value;
        else if (name == "--population") options.population = std::stoull(value);
        else if (name == "--requests") options.requests = std::stoull(value);
        else if (name == "--seed") options.seed = unsigned(std::stoul(value));
        else if (name == "--repetitions") options.repetitions = unsigned(std::stoul(value));
        else if (name == "--prefetch") options.prefetch = unsigned(std::stoul(value));
        else if (name == "--pipeline") options.pipeline = unsigned(std::stoul(value));
        else if (name == "--loom") options.loom = unsigned(std::stoul(value));
        else if (name == "--staged") options.staged = unsigned(std::stoul(value));
        else ht::require(false, "known option");
    }
    ht::require(options.population && options.population * 3 < (word{1} << W), "distinct key domains");
    ht::require(options.hint == "p" || options.hint == "u" || options.hint == "auto", "known lookup hint");
    std::cout << "impl,w,b,v,seed,population,temperature,hint,prefetch,pipeline,loom,staged,operation,pattern,consumed,rep,count,ns,bytes,peak_bytes,source_bytes,allocations,hits,keys,digest\n";
#if WIDE_PEERS
    if (options.impl == "abseil") run<Kind::abseil>();
    else if (options.impl == "boost") run<Kind::boost>();
    else if (options.impl == "f14") run<Kind::f14>();
    else if (options.impl == "f14value") run<Kind::f14value>();
    else if (options.impl == "abseil_slots") run<Kind::abseil_slots>();
    else if (options.impl == "boost_slots") run<Kind::boost_slots>();
    else if (options.impl == "f14_slots") run<Kind::f14_slots>();
#else
    if (options.impl == "materialized") run<Kind::materialized>();
    else if (options.impl == "borrow") run<Kind::borrowed>();
    else if (options.impl == "slots") run<Kind::slots>();
    else if (options.impl == "slots32") run<Kind::slots32>();
#endif
    else ht::require(false, "known implementation");
}
