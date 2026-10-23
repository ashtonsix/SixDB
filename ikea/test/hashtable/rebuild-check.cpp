#include <ikea/hashtable/rebuild.h>
#include <ikea/hashtable/accumulate.h>
#include <iostream>
#include <map>
#include <random>

namespace study {
namespace r = ikea::hashtable::detail;
namespace ht = ikea::hashtable;
using ht::word;
template <unsigned V> ht::bytes<V> value(word key, unsigned generation) {
    ht::bytes<V> result{};
    for (unsigned i = 0; i < V; ++i)
        result[i] = std::byte((key >> (i % 8) * 8) ^ i * 137 ^ generation * 53);
    return result;
}
template <class Owner, class Oracle> void verify(const Owner& owner, const Oracle& expected) {
    owner.validate();
    ht::require(owner.size() == expected.size(), "rebuild cardinality");
    for (const auto& [key, value] : expected) {
        unsigned callbacks = 0;
        ht::require(owner.find(key,
                               [&](const auto& got) {
                                   ++callbacks;
                                   ht::require(got == value, "rebuild complete payload");
                               }),
                    "rebuild membership");
        ht::require(callbacks == (value.size() ? 1u : 0u), "rebuild callback count");
    }
    owner.visit([&](word key, const auto& value) {
        const auto it = expected.find(key);
        ht::require(it != expected.end() && it->second == value, "rebuild independent census");
    });
    const auto state = r::access::get(owner);
    if constexpr (!Owner::bitmap)
        for (std::size_t bucket = 0; bucket < Owner::bucket_count; ++bucket) {
            const auto* p = ikea::to_u8(state.primary.data) + bucket * Owner::stride;
            for (unsigned row = 0; row < Owner::N; ++row)
                if (!r::live_row<Owner>(p, row))
                    ht::require(ht::point<Owner::encoding>(p, row, Owner::N) ==
                                    (Owner::encoding > Owner::K ? ht::mask<Owner::encoding> : 0),
                                "canonical inactive row");
        }
}
template <unsigned K, unsigned E, unsigned NextE, unsigned V> void check() {
    constexpr unsigned W = std::min(64u, K + 2), b = W - K;
    using Source = ht::table<W, b, V, E>;
    using Destination = ht::table<W, b + 1, V, NextE>;
    constexpr word seed = 713 & ht::mask<W>;
    Source source(seed);
    ht::permutation<W, K> permutation{seed};
    std::map<word, ht::bytes<V>> expected;
    auto put = [&](word bucket, word q, unsigned generation = 0) {
        const auto key = permutation.recover(bucket, q);
        auto v = value<V>(key, generation);
        source.insert(key, v);
        expected[key] = v;
    };
    // A mostly empty bucket with zero and split-bit-only quotients exposes
    // readers falsely matching excluded rows after flags-only partitioning.
    put(0, 0);
    put(0, word{1} << (K - 1));
    std::mt19937_64 rng(K * 131 + E);
    for (unsigned i = 0; i < Source::N / 2; ++i)
        put(0, rng() & ht::mask<K>);
    // One child must reconstruct a saturated sticky count; churn creates Swiss
    // tombstones and primary holes. Bitmap cases exhaust their tiny universe.
    const auto bucket = Source::bucket_count - 1;
    const unsigned count = K > 9 ? 600 : unsigned(std::min<word>(ht::mask<K> + 1, 400));
    for (unsigned q = 0; q < count; ++q)
        put(bucket, q);
    for (unsigned q = 1; q < count; q += 3) {
        const auto key = permutation.recover(bucket, q);
        source.erase(key);
        expected.erase(key);
    }
    std::vector<std::vector<std::byte>> original;
    source.allocations(
        [&](auto, const std::byte* p, std::size_t n) { original.emplace_back(p, p + n); });
    auto run = [&]<bool Tiles, bool Swiss, bool Image = false>() {
        auto next = ht::grow<Destination>(source);
        verify(*next, expected);
        verify(source, expected);
        unsigned allocation = 0;
        source.allocations([&](auto, const std::byte* p, std::size_t n) {
            ht::require(original[allocation].size() == n &&
                            std::equal(p, p + n, original[allocation].begin()),
                        "growth mutated source");
            ++allocation;
        });
        for (unsigned i = 0; i < 1000; ++i) {
            const auto q =
                (i < 4 ? word(i) * (word{1} << (K > 1 ? K - 2 : 0)) : rng()) & ht::mask<K>;
            const auto key = permutation.recover(rng() % Source::bucket_count, q);
            ht::require(next->find(key, [](const auto&) {}) == expected.contains(key),
                        "postgrowth miss oracle");
        }
        auto changed = expected;
        for (unsigned i = 0; i < 250; ++i) {
            const auto key = permutation.recover(rng() % Source::bucket_count, rng() & ht::mask<K>);
            if (i % 3 == 0) {
                ht::require(next->erase(key) == bool(changed.erase(key)), "postgrowth erase");
            } else {
                const auto v = value<V>(key, i);
                ht::require(next->insert(key, v) == !changed.contains(key), "postgrowth insert");
                changed[key] = v;
            }
        }
        verify(*next, changed);
        verify(source, expected);
    };
    run.template operator()<true, false>();
    std::cout << "PASS," << W << ',' << K << ',' << E << ',' << NextE << ',' << V << '\n';
}
} // namespace study
#ifndef REBUILD_CHECK_NO_MAIN
void growth_cases() {
    study::check<14, 14, 14, 0>();
    study::check<14, 14, 14, 8>();
    study::check<14, 14, 14, 64>();
    study::check<30, 30, 30, 8>();
    study::check<30, 30, 29, 64>();
    study::check<15, 15, 14, 3>();
    study::check<31, 31, 30, 9>();
    study::check<10, 10, 9, 3>();
    study::check<9, 9, 8, 9>();
    study::check<2, 2, 1, 8>();
    study::check<1, 1, 0, 0>();
    study::check<32, 40, 40, 8>();
    study::check<64, 64, 64, 64>();
}
#endif

template <unsigned K, unsigned V> void check_merge() {
    namespace ht = ikea::hashtable;
    namespace r = ikea::hashtable;
    using ht::word;
    constexpr unsigned W = K + 2;
    using Owner = ht::table<W, 2, V>;
    Owner left(731), right(731);
    ht::permutation<W, K> mix{731 & ht::mask<W>};
    std::map<word, ht::bytes<V>> a, b;
    std::mt19937_64 rng(713);
    std::vector<unsigned> order(750);
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), rng);
    auto put = [&](auto& table, auto& oracle, unsigned bucket, word q, unsigned generation) {
        const auto key = mix.recover(bucket, q & ht::mask<K>);
        const auto v = study::value<V>(key, generation);
        table.insert(key, v);
        oracle[key] = v;
    };
    for (unsigned i = 0; i < 500; ++i)
        put(left, a, 0, i, 1);
    for (auto i : order)
        if (i >= 250)
            put(right, b, 0, i, 2);
    for (unsigned i = 0; i < 100; ++i) {
        put(left, a, 1, i, 3);
        put(right, b, 2, i, 4);
    }
    for (unsigned i = 0; i < 100; i += 3) {
        const auto key = mix.recover(0, i & ht::mask<K>);
        left.erase(key);
        a.erase(key);
    }
    put(left, a, 0, 0, 5); // Keep a duplicate even when churn exhausts a bitmap domain.
    auto combine = [](const auto& left, const auto& right) {
        auto result = left;
        for (unsigned i = 0; i < V; ++i)
            result[i] = std::byte(std::to_integer<unsigned>(left[i]) * 3 + std::to_integer<unsigned>(right[i]) * 5 + 7);
        return result;
    };
    auto expected = a;
    std::size_t duplicates = 0;
    for (const auto& [key, value] : b) {
        if (expected.contains(key)) {
            expected[key] = combine(expected[key], value);
            ++duplicates;
        } else
            expected[key] = value;
    }
    auto merge_copy = [](const Owner& a, const Owner& b, auto&& reduce) {
        auto next = std::make_unique<Owner>(a.clone());
        ht::reserve_overflow(*next, a.size() + b.size());
        ht::require(ht::accumulate(*next, b, std::forward<decltype(reduce)>(reduce)) ==
                        ht::accumulation_status::complete,
                    "caller-prepared accumulation");
        return next;
    };
    std::size_t calls = 0;
    auto result = merge_copy(left, right, [&](const auto& x, const auto& y) {
        ++calls;
        return combine(x, y);
    });
    ht::require(calls == duplicates, "merge invokes reducer exactly once per duplicate");
    study::verify(*result, expected);
    study::verify(left, a);
    study::verify(right, b);
    auto same = merge_copy(left, left, combine);
    auto self = a;
    for (auto& [key, value] : self)
        value = combine(value, value);
    study::verify(*same, self);
    Owner foreign(732);
    bool refused = false;
    try {
        auto bad = merge_copy(left, foreign, combine);
        (void)bad;
    } catch (const std::runtime_error&) {
        refused = true;
    }
    ht::require(refused, "different seeds refused");
    refused = false;
    try {
        auto bad = merge_copy(left, left, [](const auto&, const auto&) -> ht::bytes<V> {
            throw std::runtime_error("caller reducer");
        });
        (void)bad;
    } catch (const std::runtime_error&) {
        refused = true;
    }
    ht::require(refused, "throwing reducer discards destination");
    study::verify(left, a);
    study::verify(right, b);
    std::cout << "MERGE_PASS," << K << ',' << V << ',' << duplicates << '\n';
}
#ifndef REBUILD_CHECK_NO_MAIN
int main() {
    growth_cases();
    check_merge<14, 0>();
    check_merge<14, 8>();
    check_merge<30, 64>();
    check_merge<10, 3>();
    check_merge<3, 9>();
}

#endif
