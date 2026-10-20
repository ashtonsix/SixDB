#include <ikea/hashtable/table.h>
#include <map>
#include <random>
#include <iostream>
namespace ht = ikea::hashtable;
template <unsigned W, unsigned b, unsigned V> void check() {
    using T = ht::table<W, b, V>;
    T owner(713);
    std::map<ht::word, typename T::value_type> oracle;
    std::mt19937_64 rng(117);
    auto verify = [&] {
        owner.validate();
        ht::require(owner.size() == oracle.size(), "oracle size");
        std::size_t visited = 0;
        owner.visit([&](ht::word key, const auto& value) {
            ht::require(oracle.contains(key) && oracle.at(key) == value, "oracle iteration");
            ++visited;
        });
        ht::require(visited == oracle.size(), "oracle iteration count");
        for (const auto& [key, value] : oracle) {
            ht::require(owner.find(key, [&](const auto& v) { ht::require(v == value, "value"); }), "present");
            typename T::staged_probe state;
            owner.prepare_staged(state, key, [](auto*, auto) {});
            bool found = false;
            unsigned steps = 0;
            while (!owner.advance(state, found, [&](const auto& v) { ht::require(v == value, "staged value"); }, [](auto*, auto) {}))
                ht::require(++steps < 4096, "staged termination");
            ht::require(found, "staged presence");
        }
        for (unsigned i = 0; i < 500; ++i) {
            const auto key = rng() & ht::mask<W>;
            ht::require(owner.find(key, [](const auto&) {}) == oracle.contains(key), "miss oracle");
        }
    };
    // Repeatedly leave holes in full groups and reinsert overlapping keys.
    // b=0 also drives the primary's saturating overflow count above 255.
    for (unsigned epoch = 0; epoch < 8; ++epoch) {
        for (unsigned i = 0; i < 1300; ++i) {
            const auto key = (rng() % 2400) & ht::mask<W>;
            typename T::value_type value{};
            for (auto& x : value) x = ht::byte(rng());
            if (i % 4 == 0) {
                ht::require(owner.erase(key) == bool(oracle.erase(key)), "erase result");
            } else {
                const bool fresh = !oracle.contains(key);
                oracle[key] = value;
                ht::require(owner.insert(key, value) == fresh, "insert result");
            }
        }
        verify();
        if constexpr (V) {
            for (unsigned i = 0; i < 250; ++i) {
                const auto key = (rng() % 2600) & ht::mask<W>;
                const bool fresh = !oracle.contains(key);
                auto& expected = oracle[key];
                for (auto& x : expected) x += 7;
                const bool inserted = owner.modify_or_insert(key, {}, [](auto& value, bool) {
                    for (auto& x : value) x += 7;
                });
                ht::require(inserted == fresh, "RMW result");
            }
            verify();
        }
        auto copy = owner.clone();
        copy.validate();
        ht::require(copy.size() == oracle.size(), "clone population");
    }
    std::cout << W << ',' << b << ',' << V << ": passed " << oracle.size() << " entries\n";
}
void tombstone_history() {
    using T = ht::table<16, 0, 0>;
    T owner(713);
    ht::permutation<16, 16> mix{713};
    std::array<std::array<std::vector<ht::word>, 8>, 2> candidates;
    for (ht::word key = T::N; key < 65536; ++key) {
        const auto h = mix(key).swiss;
        candidates[h & 1][(h >> 56) & 7].push_back(key);
    }
    std::vector<ht::word> live;
    auto add = [&](ht::word key) {
        ht::require(owner.insert(key), "directed insertion"); live.push_back(key);
    };
    auto take = [&](unsigned group, unsigned klass) {
        auto& v = candidates[group][klass]; const auto key = v.back(); v.pop_back(); return key;
    };
    for (ht::word key = 0; key < T::N; ++key) add(key);
    std::vector<ht::word> a;
    for (unsigned i = 0; i < 15; ++i) { const auto key = take(0, 1); a.push_back(key); add(key); }
    add(take(0, 0)); // spills past the first full group
    for (unsigned i = 0; i < 10; ++i) add(take(1, 1));
    for (unsigned i = 0; i < 5; ++i) {
        ht::require(owner.erase(a[i]), "directed erasure");
        std::erase(live, a[i]);
        add(take(1, i == 4 ? 0 : 1));
    }
    // Reusable tombstones do not terminate absence resolution. Exercise
    // lookup, erase, staged reads and reinsertion after concentrated churn.
    const auto absent = take(0, 0);
    ht::require(!owner.find(absent, [](auto) {}), "full-cycle miss");
    ht::require(!owner.erase(absent), "full-cycle erase miss");
    T::staged_probe state;
    owner.prepare_staged(state, absent, [](auto*, auto) {});
    bool found = true;
    unsigned steps = 0;
    while (!owner.advance(state, found, [](auto) {}, [](auto*, auto) {}))
        ht::require(++steps < 32, "full-cycle staged termination");
    ht::require(!found, "full-cycle staged miss");
    add(absent); // absence proof also has to finish before reusing a hole
    owner.validate();
    ht::require(owner.size() == live.size(), "directed population");
    for (auto key : live) ht::require(owner.find(key, [](auto) {}), "directed reachability");
    std::cout << "directed tombstone history: passed\n";
}
void concentrated_groups() {
    using T = ht::table<16, 0, 8>;
    T owner(713);
    ht::permutation<16, 16> mix{713};
    for (ht::word k = 0; k < T::N; ++k) owner.insert(k);
    std::vector<ht::word> keys;
    for (ht::word k = T::N; k < 65536 && keys.size() < 128; ++k)
        if (!(mix(k).swiss & 255)) {
            keys.push_back(k);
            ht::bytes<8> value{};
            ht::store<8>(value.data(), k + 17);
            ht::require(owner.insert(k, value), "concentrated group insert");
        }
    ht::require(keys.size() == 128, "concentrated key fixture");
    // A shared home forces complete logical groups along the triangular
    // sequence, while the owner as a whole still has vacant groups.
    owner.validate();
    for (auto k : keys) {
        ht::require(owner.find(k, [&](const auto& value) {
            ht::require(ht::load<8>(value.data()) == k + 17, "concentrated value");
        }), "concentrated membership");
    }
    for (auto k : keys) ht::require(owner.erase(k), "concentrated erase");
    for (auto k : keys) ht::require(!owner.find(k, [](auto) {}), "concentrated absence");
    owner.validate();
    std::cout << "directed full logical groups: passed\n";
}
int main() {
    check<16, 0, 0>();
    check<20, 4, 8>();
    check<64, 4, 64>();
    tombstone_history();
    concentrated_groups();
}
