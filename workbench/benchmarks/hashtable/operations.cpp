#include "support.h"
#include <map>
namespace qualification {
namespace ht = ikea::hashtable;

using ht::word;
constexpr unsigned W = IKEA_HASHTABLE_W, b = IKEA_HASHTABLE_B, K = W - b, V = IKEA_HASHTABLE_V;
#if SIXDB_TUNE_ZEN5 || SIXDB_TUNE_ZEN4
constexpr auto cpu = "zen5";
#elif SIXDB_TUNE_GRANITE_RAPIDS
constexpr auto cpu = "granite-rapids";
#else
constexpr auto cpu = "neoverse-v2";
#endif
#if defined(__AVX512VBMI__) && defined(__GFNI__)
constexpr auto isa = "avx512";
#elif defined(__AVX2__) && defined(__GFNI__)
constexpr auto isa = "avx2-gfni";
#elif defined(__AVX2__)
constexpr auto isa = "avx2";
#else
constexpr auto isa = "neon";
#endif
constexpr auto selected = *ht::measured_encoding(K, V, cpu, isa);
using Value = ht::bytes<V>;
using Key = screen::Key;
using Promoted = ht::table<W, b, V, selected.width, selected.kernel>;
struct PromotedSplit : Promoted {
    using Promoted::Promoted;
    [[gnu::always_inline]] bool insert(word k, const Value& v = {}) {
        return Promoted::template insert<ht::write_strategy::existing>(k, v);
    }
};
using Abseil = screen::Abseil<V>;
using clock = std::chrono::steady_clock;
word bits(word x) {
    return screen::random_bits(x);
}
Value payload(word k, unsigned round) {
    Value v{};
    for (unsigned i = 0; i < V; ++i)
        v[i] = std::byte(bits(k + word(round) * 65537 + i * 131));
    return v;
}
struct row {
    word key;
    Value value;
};
template <class Owner> [[gnu::always_inline]] bool put(Owner& owner, word k, const Value& v) {
    if constexpr (std::is_same_v<Owner, Abseil>) {
        if constexpr (V)
            return owner.insert_or_assign(Key(k), v).second;
        else
            return owner.insert(Key(k)).second;
    } else
        return owner.insert(k, v);
}
template <class Owner> std::unique_ptr<Owner> create(word seed, std::size_t n) {
    if constexpr (std::is_same_v<Owner, Abseil>) {
        auto p = std::make_unique<Owner>();
        p->reserve(n);
        return p;
    } else
        return std::make_unique<Owner>(seed & ht::mask<W>);
}
template <class Owner> std::size_t bytes(const Owner& owner) {
    if constexpr (std::is_same_v<Owner, Abseil>)
        return absl::container_internal::AllocatedByteSize(owner);
    else
        return owner.bytes_used();
}
double elapsed(clock::time_point start) {
    return std::chrono::duration<double, std::nano>(clock::now() - start).count();
}
void report(std::string_view owner, std::string_view phase, word seed, unsigned rep, unsigned load,
            unsigned fresh, unsigned miss, std::size_t condition, std::size_t operations, double ns,
            std::size_t memory) {
    std::cout << owner << ',' << phase << ',' << W << ',' << b << ',' << K << ',' << V << ','
              << seed << ',' << rep << ',' << load << ',' << fresh << ',' << miss << ','
              << condition << ',' << operations << ',' << ns / operations << ',' << memory << '\n';
}
template <class Owner> void verify(const Owner& owner, const std::vector<row>& rows) {
    ht::require(owner.size() == rows.size(), "complete population oracle");
    for (const auto& r : rows) {
        unsigned calls = 0;
        ht::require(screen::read<V>(owner, r.key,
                                    [&](const Value& v) {
                                        ++calls;
                                        ht::require(v == r.value, "complete value oracle");
                                    }),
                    "complete membership oracle");
        ht::require(calls == (V ? 1u : 0u), "set/map callback count");
    }
}
struct history {
    word seed;
    unsigned occupancy, new_percent;
    std::size_t n, changed;
    std::vector<std::vector<row>> states, updates;
    std::vector<std::vector<word>> erased;
    history(word s, unsigned load, unsigned fresh, unsigned count)
        : seed(s), occupancy(load), new_percent(fresh),
          n(Promoted::bucket_count * Promoted::N * load / 100), changed(n * fresh / 100) {
        ht::require(__uint128_t(n) * (count + 1) < (__uint128_t{1} << W), "trace fits universe");
        auto& initial = states.emplace_back();
        initial.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            const auto k = screen::key(i, seed);
            initial.push_back({k, payload(k, 0)});
        }
        std::vector<unsigned> order(n);
        std::iota(order.begin(), order.end(), 0);
        std::mt19937_64 rng(seed);
        for (unsigned round = 1; round <= count; ++round) {
            auto next = states.back();
            auto& removed = erased.emplace_back();
            std::shuffle(order.begin(), order.end(), rng);
            for (std::size_t j = 0; j < n; ++j) {
                const auto i = order[j];
                if (j < changed) {
                    removed.push_back(next[i].key);
                    next[i].key = screen::key(n * round + i, seed);
                }
                next[i].value = payload(next[i].key, round);
            }
            states.push_back(next);
            std::shuffle(next.begin(), next.end(), rng);
            updates.push_back(std::move(next));
        }
    }
};
template <class Owner>
[[gnu::noinline]] void writes(std::string_view name, const history& in, unsigned rep) {
    auto start = clock::now();
    auto owner = create<Owner>(bits(in.seed), in.n);
    std::size_t inserted = 0;
    for (const auto& r : in.states[0])
        inserted += put(*owner, r.key, r.value);
    auto ns = elapsed(start);
    report(name, "build", in.seed, rep, in.occupancy, in.new_percent, 0, 0, in.n, ns,
           bytes(*owner));
    ht::require(inserted == in.n, "build cardinality");
    verify(*owner, in.states[0]);
    for (unsigned round = 0; round < in.updates.size(); ++round) {
        if (in.changed) {
            std::size_t erased = 0;
            start = clock::now();
            for (auto k : in.erased[round]) {
                if constexpr (std::is_same_v<Owner, Abseil>)
                    erased += owner->erase(Key(k));
                else
                    erased += owner->erase(k);
            }
            ns = elapsed(start);
            ht::require(erased == in.changed, "erase cardinality");
            report(name, "erase", in.seed, rep, in.occupancy, in.new_percent, 0, 0, in.changed, ns,
                   bytes(*owner));
        }
        inserted = 0;
        start = clock::now();
        for (const auto& r : in.updates[round])
            inserted += put(*owner, r.key, r.value);
        ns = elapsed(start);
        report(name, "upsert", in.seed, rep, in.occupancy, in.new_percent, 0, 0, in.n, ns,
               bytes(*owner));
        ht::require(inserted == in.changed, "upsert cardinality");
        verify(*owner, in.states[round + 1]);
        for (auto k : in.erased[round])
            ht::require(!screen::read<V>(*owner, k, [](const auto&) {}), "erased key absent");
    }
    start = clock::now();
    owner.reset();
    report(name, "destroy", in.seed, rep, in.occupancy, in.new_percent, 0, 0, in.n, elapsed(start),
           0);
}
struct DescriptorRmw {
    using D = ht::erased_table<W, V>;
    D owner;
    explicit DescriptorRmw(word seed)
        : owner(D::template explicit_encoding<b, selected.width, selected.kernel>(seed)) {}
    bool insert(word key, const Value& value) {
        return owner.insert(key, value);
    }
    bool erase(word key) {
        return owner.erase(key);
    }
    template <class F> bool find(word key, F&& f) const {
        return owner.read().find(key, std::forward<F>(f));
    }
    std::size_t size() const {
        return owner.size();
    }
    std::size_t bytes_used() const {
        return owner.bytes_used();
    }
};
void increment(Value& value) {
    if constexpr (V == 8) {
        word x;
        std::memcpy(&x, value.data(), 8);
        ++x;
        std::memcpy(value.data(), &x, 8);
    } else
        for (unsigned i = 0; i < V; ++i)
            value[i] = std::byte(std::to_integer<unsigned>(value[i]) + i * 2 + 1);
}
template <class Owner>
[[gnu::noinline]] void modifies(std::string_view name, const history& in, unsigned rep) {
    if constexpr (V) {
        auto owner = create<Owner>(bits(in.seed), in.n);
        std::map<word, Value> oracle;
        for (const auto& r : in.states[0]) {
            put(*owner, r.key, r.value);
            oracle[r.key] = r.value;
        }
        verify(*owner, in.states[0]);
        for (unsigned round = 0; round < in.updates.size(); ++round) {
            for (auto key : in.erased[round]) {
                owner->erase(key);
                oracle.erase(key);
            }
            std::vector<row> expected;
            expected.reserve(in.n);
            for (const auto& r : in.updates[round]) {
                auto [it, fresh] = oracle.try_emplace(r.key, payload(r.key, 0));
                // Independent byte arithmetic for the uint64 increment oracle.
                if constexpr (V == 8) {
                    unsigned carry = 1;
                    for (auto& x : it->second) {
                        const unsigned sum = std::to_integer<unsigned>(x) + carry;
                        x = std::byte(sum);
                        carry = sum >> 8;
                    }
                } else
                    for (unsigned i = 0; i < V; ++i)
                        it->second[i] = std::byte(std::to_integer<unsigned>(it->second[i]) + i * 2 + 1);
            }
            for (const auto& [key, value] : oracle)
                expected.push_back({key, value});
            std::size_t inserted = 0;
            auto start = clock::now();
            auto loop = [&](auto& mutable_owner) {
                using M = std::remove_cvref_t<decltype(mutable_owner)>;
                for (const auto& r : in.updates[round]) {
                    if constexpr (std::is_same_v<M, Abseil>) {
                        if (in.new_percent) {
                            auto [it, fresh] =
                                mutable_owner.try_emplace(Key(r.key), payload(r.key, 0));
                            inserted += fresh;
                            increment(it->second);
                        } else {
                            auto it = mutable_owner.find(Key(r.key));
                            ht::require(it != mutable_owner.end(), "RMW present key");
                            increment(it->second);
                        }
                    } else {
                        if (in.new_percent)
                            inserted += mutable_owner.modify_or_insert(
                                r.key, payload(r.key, 0),
                                [](Value& value, bool) { increment(value); });
                        else
                            ht::require(
                                mutable_owner.modify(r.key, [](Value& value) { increment(value); }),
                                "RMW present key");
                    }
                }
            };
            if constexpr (std::is_same_v<Owner, DescriptorRmw>)
                owner->owner.template with_native_mutation<b, selected.width, selected.kernel>(
                    [&](auto& concrete, auto) { loop(concrete); });
            else
                loop(*owner);
            const auto ns = elapsed(start);
            ht::require(inserted == in.changed, "RMW cardinality");
            report(name, in.new_percent ? "modify_or_insert" : "modify", in.seed, rep, in.occupancy,
                   in.new_percent, 0, 0, in.n, ns, bytes(*owner));
            verify(*owner, expected);
        }
    }
}
template <class Owner>
[[gnu::noinline]] void reads(std::string_view name, word seed, unsigned rep, std::size_t count,
                             unsigned miss, std::size_t condition) {
    screen::data in(seed, count, miss);
    auto owner = create<Owner>(seed, in.n);
    for (auto k : in.keys)
        put(*owner, k, screen::payload<V>(k, 0));
    screen::verify<V>(*owner, in);
    std::vector<std::byte> conditioning(condition, std::byte{17});
    const auto expected = in.expected<V>();
    for (unsigned pass = 0; pass < 2; ++pass) {
        const bool prefetch = (pass + seed + rep) % 2;
        const auto call = prefetch ? &screen::run<V, Owner, true> : &screen::run<V, Owner, false>;
        ht::require(call(owner.get(), in) == expected, "warm full consumer");
        screen::condition(conditioning);
        auto start = clock::now();
        auto got = call(owner.get(), in);
        asm volatile(""
                     : "+r"(got.sum), "+r"(got.hits), "+r"(got.keys), "+r"(got.values)::"memory");
        auto ns = elapsed(start);
        ht::require(got == expected, "timed full consumer");
        report(name, prefetch ? "prefetch32" : "independent", seed, rep, 85, 0, miss, condition,
               count, ns, bytes(*owner));
    }
}
template <class Owner>
[[gnu::noinline]] void grow(std::string_view name, word seed, unsigned rep, unsigned load) {
    history in(seed, load, 0, 0);
    auto source = create<Owner>(bits(seed), in.n);
    for (const auto& r : in.states[0])
        put(*source, r.key, r.value);
    verify(*source, in.states[0]);
    auto start = clock::now();
    auto result = [&] {
        if constexpr (std::is_same_v<Owner, Abseil>) {
            if (name == "abseil_insert") {
                auto next = create<Abseil>(seed, in.n * 2);
                for (const auto& entry : *source) {
                    if constexpr (V)
                        put(*next, entry.first, entry.second);
                    else
                        put(*next, entry, {});
                }
                return next;
            }
            auto next = std::make_unique<Abseil>(*source);
            next->reserve(in.n * 2);
            return next;
        } else {
            using Target = ht::table<W, b + 1, V, selected.width, selected.kernel>;
            return ht::grow<Target>(*source);
        }
    }();
    auto ns = elapsed(start);
    report(name, "grow", seed, rep, load, 0, 0, 0, in.n, ns, bytes(*result));
    verify(*result, in.states[0]);
    verify(*source, in.states[0]);
}
[[gnu::noinline]] void descriptor_growth(word seed, unsigned rep, unsigned load) {
    using Descriptor = ht::erased_table<W, V>;
    auto owner =
        Descriptor::template explicit_encoding<b, selected.width, selected.kernel>(bits(seed));
    history in(seed, load, 0, 0);
    for (const auto& r : in.states[0])
        owner.insert(r.key, r.value);
    auto old = owner.template read_native<b, selected.width, selected.kernel>();
    auto start = clock::now();
    owner.template grow<b, selected.width, selected.kernel>();
    auto ns = elapsed(start);
    report("ikea_descriptor", "grow", seed, rep, load, 0, 0, 0, in.n, ns, owner.bytes_used());
    auto current = owner.template read_native<b + 1, selected.width, selected.kernel>();
    verify(*current.borrow().owner, in.states[0]);
    verify(*old.borrow().owner, in.states[0]);
}
} // namespace qualification
int main(int argc, char** argv) {
    try {
        using namespace qualification;
        const auto args = screen::parse(argc, argv);
        const auto& mode = args.operation;
        const auto& owner = args.owner;
        std::cout
            << "implementation,phase,W,b,K,V,seed,rep,occupancy,new_percent,miss,conditioning,operations,ns,bytes\n";
        auto dispatch = [&]<class Owner>() {
            if (mode == "modify")
                modifies<Owner>(owner,
                                history(args.seed, args.occupancy_percent, args.new_percent, 4),
                                args.rep);
            else if (mode == "read")
                reads<Owner>(owner, args.seed, args.rep, args.requests, args.miss_percent,
                             args.conditioning_bytes);
            else if (mode == "write")
                writes<Owner>(owner,
                              history(args.seed, args.occupancy_percent, args.new_percent, 4),
                              args.rep);
            else if (mode == "grow")
                grow<Owner>(owner, args.seed, args.rep, args.occupancy_percent);
            else
                ht::require(false, "known complete-operation mode");
        };
        if (owner == "ikea_batch") {
            ht::require(mode == "modify", "mutable batch mode");
            modifies<DescriptorRmw>(
                owner, history(args.seed, args.occupancy_percent, args.new_percent, 4), args.rep);
        } else if (owner == "ikea_descriptor") {
            ht::require(mode == "grow", "descriptor mode");
            descriptor_growth(args.seed, args.rep, args.occupancy_percent);
        } else if (owner == "ikea")
            dispatch.template operator()<Promoted>();
        else if (owner == "ikea_split") {
            ht::require(mode == "write" || mode == "modify", "writer strategy surface");
            dispatch.template operator()<PromotedSplit>();
        } else if (owner == "abseil" || owner == "abseil_insert")
            dispatch.template operator()<Abseil>();
        else
            ht::require(false, "known owner");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
