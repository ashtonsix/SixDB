#pragma once
#include "common.h"
#include <ikea/hashtable.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/internal/hashtable_debug.h>
#include <iostream>
#include <numeric>
#include <random>

namespace screen {
using namespace ikea::hashtable;
constexpr unsigned W = IKEA_HASHTABLE_W, b = IKEA_HASHTABLE_B, K = W - b;
constexpr auto layout = geometry::at(W, b);
constexpr unsigned B = layout.bytes, T = layout.tiles, N = layout.slots;
static_assert(b < 63);
using Key = std::conditional_t<(W <= 16), std::uint16_t,
                               std::conditional_t<(W <= 32), std::uint32_t, word>>;
inline word key(word x, word seed) {
    x = (x ^ seed) & mask<W>;
    x ^= x >> std::max(1u, W / 2);
    x = x * 0xbf58476d1ce4e5b9ULL & mask<W>;
    x ^= x >> std::max(1u, W / 3);
    x = x * 0x94d049bb133111ebULL & mask<W>;
    return x ^ (x >> std::max(1u, W / 2));
}
template <unsigned V> bytes<V> payload(word key, unsigned ordinal) {
    static_assert(V % 8 == 0);
    bytes<V> out{};
    for (unsigned i = 0; i < V; i += 8)
        store<8>(out.data() + i, random_bits(key ^ (word(ordinal) << 40) ^ i));
    return out;
}
struct result {
    word hits = 0, sum = 0, keys = 0, values = 0;
    bool operator==(const result&) const = default;
};
struct data {
    std::size_t n = std::size_t(std::min(layout.capacity * 85 / 100, (__uint128_t{1} << W) / 2));
    word seed;
    unsigned miss;
    std::vector<word> keys, requests;
    std::vector<unsigned> ranks;
    data(word s, std::size_t count, unsigned miss_percent)
        : seed(s), miss(miss_percent), keys(n), requests(count), ranks(count) {
        require(n && n < (word{1} << 31), "benchmark population range");
        for (unsigned i = 0; i < n; ++i)
            keys[i] = key(i, seed);
        std::mt19937_64 rng(seed + 0x4d4d);
        for (std::size_t i = 0; i < count; ++i) {
            const auto rank = unsigned(rng() % n);
            ranks[i] = rank + (rng() % 100 < miss ? n : 0);
            requests[i] = key(ranks[i], seed);
        }
    }
    template <unsigned V> result expected() const {
        result out;
        for (std::size_t i = 0; i < requests.size(); ++i)
            if (ranks[i] < n) {
                ++out.hits;
                out.keys += requests[i];
                if constexpr (V) {
                    const auto value = payload<V>(requests[i], 0);
                    for (unsigned at = 0; at < V; at += 8)
                        out.sum += load<8>(value.data() + at);
                    ++out.values;
                }
            }
        return out;
    }
};
template <unsigned V>
using Abseil =
    std::conditional_t<V == 0, absl::flat_hash_set<Key>, absl::flat_hash_map<Key, bytes<V>>>;
template <unsigned V, class Owner, class Consume>
[[gnu::always_inline]] bool read(const Owner& owner, word query, Consume&& consume) {
    if constexpr (std::is_same_v<Owner, Abseil<V>>) {
        const auto it = owner.find(Key(query));
        if (it == owner.end())
            return false;
        if constexpr (V)
            consume(it->second);
        return true;
    } else
        return owner.find(query, std::forward<Consume>(consume));
}
template <unsigned V, class Owner, bool Prefetch = false>
[[gnu::noinline]] result run(const void* ptr, const data& in) {
    const auto& owner = *static_cast<const Owner*>(ptr);
    word hits = 0, sum = 0, keys = 0, values = 0;
    auto hint = [&](word query) {
        if constexpr (std::is_same_v<Owner, Abseil<V>>)
            owner.prefetch(Key(query));
        else
            owner.prefetch(owner.prepare(query));
    };
    constexpr std::size_t distance = 32;
    if constexpr (Prefetch)
        for (std::size_t i = 0; i < std::min(distance, in.requests.size()); ++i)
            hint(in.requests[i]);
    for (std::size_t i = 0; i < in.requests.size(); ++i) {
        if constexpr (Prefetch)
            if (distance < in.requests.size() - i)
                hint(in.requests[i + distance]);
        const auto query = in.requests[i];
        const bool found = read<V>(owner, query, [&](const bytes<V>& value) {
            for (unsigned at = 0; at < V; at += 8)
                sum += load<8>(value.data() + at);
            ++values;
        });
        hits += found;
        keys += found ? query : 0;
    }
    return {hits, sum, keys, values};
}
template <unsigned V, class Owner> void verify(const Owner& owner, const data& in) {
    require(owner.size() == in.n, "complete read fixture cardinality");
    for (unsigned i = 0; i < in.n * 2; ++i) {
        const auto query = key(i, in.seed);
        unsigned calls = 0;
        const bool found = read<V>(owner, query, [&](const bytes<V>& value) {
            ++calls;
            require(value == payload<V>(query, 0), "complete read fixture payload");
        });
        require(found == (i < in.n) && calls == (V && found ? 1u : 0u),
                "read fixture membership/callbacks");
    }
}
} // namespace screen
