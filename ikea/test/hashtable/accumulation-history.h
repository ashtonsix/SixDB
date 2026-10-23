#pragma once
#include <ikea/hashtable.h>
#include <array>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
namespace hashtable_test {
namespace ht = ikea::hashtable;
using ht::word;
using access = ht::author::table_access;
template <class T> word identity(const T& owner) {
    word h = 17;
    owner.allocations([&](auto, const auto* p, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i)
            h = (h ^ std::to_integer<unsigned>(p[i])) * 0x100000001b3ULL;
    });
    return h;
}
template <unsigned V> ht::bytes<V> payload(word key, unsigned source) {
    ht::bytes<V> out{};
    for (unsigned i = 0; i < V; ++i)
        out[i] = std::byte(key * 19 + source * 47 + i * 11);
    return out;
}
template <unsigned W, unsigned sb, unsigned db, unsigned V, unsigned SE = W - sb,
          unsigned DE = W - db, class Run>
void ordered_history(Run&& run) {
    using S = ht::table<W, sb, V, SE>;
    using D = ht::table<W, db, V, DE>;
    constexpr word seed = 713 & ht::mask<W>;
    ht::permutation<W, S::K> mix{seed};
    std::array<std::unique_ptr<S>, 4> owners;
    std::array<const S*, 4> sources{};
    std::array<std::map<word, ht::bytes<V>>, 4> source_oracles;
    std::map<word, ht::bytes<V>> initial, expected;
    D original(seed);
    constexpr unsigned count = [] {
        if constexpr (S::K >= 8)
            return 180u;
        else
            return 1u << S::K;
    }();
    const auto quotient = [](unsigned q) {
        if constexpr (S::K > 9)
            return word(q) | ((q & 1) ? word{1} << (S::K - 1) : 0);
        else
            return word(q);
    };
    auto reduce = [](const auto& a, const auto& b) {
        auto c = a;
        for (unsigned i = 0; i < V; ++i)
            c[i] = std::byte(std::to_integer<unsigned>(a[i]) * 3 + std::to_integer<unsigned>(b[i]) * 5 + 7);
        return c;
    };
    for (unsigned q = 0; q < count; ++q)
        if (q % 3 == 0) {
            auto key = mix.recover(0, quotient(q));
            initial[key] = payload<V>(key, 0);
            original.insert(key, initial[key]);
        }
    expected = initial;
    std::array<word, 4> hashes{};
    std::size_t total_spills = 0, reductions = 0;
    for (unsigned n = 0; n < owners.size(); ++n) {
        owners[n] = std::make_unique<S>(seed);
        sources[n] = owners[n].get();
        std::vector<unsigned> order(count);
        std::iota(order.begin(), order.end(), 0);
        std::mt19937_64 rng(317 + n);
        std::shuffle(order.begin(), order.end(), rng);
        for (unsigned bucket = 0; bucket < std::min<std::size_t>(S::bucket_count, 3); ++bucket)
            for (auto q : order)
                if ((q + n) % 5 != 0) {
                    const auto key = mix.recover(bucket, quotient(q));
                    owners[n]->insert(key, payload<V>(key, n + 1));
                    source_oracles[n][key] = payload<V>(key, n + 1);
                }
        // Swiss duplicates behind primary holes, and source tombstones.
        for (unsigned q = 1; q < count; q += 13) {
            const auto key = mix.recover(0, quotient(q));
            owners[n]->erase(key);
            source_oracles[n].erase(key);
        }
        owners[n]->validate();
        hashes[n] = identity(*owners[n]);
        total_spills += access::get(*owners[n]).spill;
        for (const auto& [key, value] : source_oracles[n]) {
            auto [it, inserted] = expected.try_emplace(key, value);
            if (!inserted) {
                it->second = reduce(it->second, value);
                ++reductions;
            }
        }
    }
    const auto original_hash = identity(original);
    auto dst = original.clone();
    ht::reserve_overflow(dst, expected.size());
    const auto primary = access::get(dst).primary.data, controls = access::get(dst).controls.data,
               slots = access::get(dst).slots.data;
    std::size_t calls = 0;
    auto counted = [&](const auto& a, const auto& b) {
        ++calls;
        return reduce(a, b);
    };
    ht::require(run(dst, std::span<const S* const>(sources), counted, total_spills),
                "ordered accumulation completes");
    ht::require(calls == reductions && dst.size() == expected.size(),
                "ordered reducer count and cardinality");
    for (const auto& [key, value] : expected) {
        ht::require(dst.find(key,
                             [&](const auto& actual) {
                                 ht::require(actual == value,
                                             "independent complete ordered payload");
                             }),
                    "merged membership");
    }
    dst.visit([&](word key, const auto& value) {
        ht::require(expected.contains(key) && expected.at(key) == value,
                    "independent output census");
    });
    dst.validate();
    auto state = access::get(dst);
    ht::require(state.primary.data == primary && state.controls.data == controls &&
                    state.slots.data == slots,
                "no destination resizing");
    for (unsigned n = 0; n < owners.size(); ++n)
        ht::require(hashes[n] == identity(*owners[n]), "all source bytes unchanged");
    ht::require(identity(original) == original_hash, "caller clone responsibility");
    std::cout << "HISTORY_PASS," << W << ',' << sb << ',' << db << ',' << V << ',' << SE << ','
              << DE << '\n';
}
} // namespace hashtable_test
