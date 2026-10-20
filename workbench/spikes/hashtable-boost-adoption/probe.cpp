// Code/dependency footprint probe, not a performance benchmark.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#if ADOPTION_IMPL == 0
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#elif ADOPTION_IMPL == 1
#include <ikea/hashtable/table.h>
#include <ikea/hashtable/encoding-selection.h>
#endif

using word = std::uint64_t;
template <unsigned V> using value = std::array<std::uint8_t, V>;
template <unsigned W, unsigned V>
[[gnu::noinline]] word exercise(word seed, unsigned n) {
#if ADOPTION_IMPL == 2
    return seed + n;
#else
    using key_type = std::conditional_t<(W <= 32), std::uint32_t, word>;
#if ADOPTION_IMPL == 0
    using owner = std::conditional_t<V == 0,
        boost::unordered_flat_set<key_type>,
        boost::unordered_flat_map<key_type, value<V>>>;
    owner t;
    t.reserve(n);
#else
#if defined(__aarch64__)
    constexpr auto cpu = "neoverse-v2", isa = "neon";
#elif defined(__AVX512VBMI__) && defined(__GFNI__)
    constexpr auto cpu = "zen5", isa = "avx512";
#else
    constexpr auto cpu = "zen5", isa = "avx2";
#endif
    constexpr auto choice = *ikea::hashtable::measured_encoding(W - 10, V, cpu, isa);
    using owner = ikea::hashtable::table<W, 10, V, choice.width, choice.kernel>;
    owner t(seed);
#endif
    constexpr word mask = (word{1} << W) - 1;
    auto key = [&](word i) { return key_type(((i * 997) ^ seed) & mask); };
    for (unsigned i = 0; i < n; ++i) {
        value<V> v{};
        if constexpr (V) v[0] = std::uint8_t(i);
#if ADOPTION_IMPL == 0
        if constexpr (V) t.insert_or_assign(key(i), v);
        else t.insert(key(i));
#else
        t.insert(key(i), v);
#endif
    }
    word sum = 0;
    for (unsigned i = 0; i < n * 2; ++i) {
#if ADOPTION_IMPL == 0
        const auto it = t.find(key(i));
        if (it != t.end()) {
            ++sum;
            if constexpr (V) for (auto b : it->second) sum += b;
        }
#else
        sum += t.find(key(i), [&](const auto& v) { for (auto b : v) sum += b; });
#endif
    }
    if constexpr (V) {
        for (unsigned i = 0; i < n; ++i) {
#if ADOPTION_IMPL == 0
            auto it = t.find(key(i));
            if (it != t.end()) ++it->second[0];
#else
            t.modify(key(i), [](auto& v) { ++v[0]; });
#endif
        }
    }
    for (unsigned i = 0; i < n / 4; ++i) sum += t.erase(key(i));
#if ADOPTION_IMPL == 0
    auto copied = t;
    for (const auto& entry : copied) {
        if constexpr (V) {
            sum += entry.first;
            for (auto b : entry.second) sum += b;
        } else sum += entry;
    }
#else
    auto copied = t.clone();
    copied.visit([&](word k, const auto& v) {
        sum += k;
        for (auto b : v) sum += b;
    });
#endif
    return sum + copied.size();
#endif
}

int main(int argc, char** argv) {
    const unsigned n = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 1024;
    if (!n || n > 8192) return 2;
    const word seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 713;
    auto sum = exercise<40, 8>(seed, n);
#if ADOPTION_PALETTE
    sum += exercise<24, 0>(seed, n) + exercise<24, 8>(seed, n) +
           exercise<40, 0>(seed, n) + exercise<40, 64>(seed, n);
#endif
    std::printf("%llu\n", static_cast<unsigned long long>(sum));
}
