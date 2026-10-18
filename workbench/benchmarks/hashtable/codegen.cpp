// Compile the same complete consumers against a captured source and the module.
// The baseline is an external verification input, never a maintained owner copy.
#include <ikea/hashtable.h>
#if IKEA_BEFORE_ACCUMULATION
#include <workbench/spikes/hashtable-merge/candidate.h>
#endif
namespace ht = ikea::hashtable;
#if SIXDB_TUNE_ZEN5
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
template <unsigned K, unsigned b, unsigned V> struct binding {
    static constexpr auto choice = *ht::measured_encoding(K, V, cpu, isa);
#if IKEA_BEFORE_ACCUMULATION
    static constexpr auto kernel = choice.filter;
#else
    static constexpr auto kernel = choice.kernel;
#endif
    using type = ht::table<K + b, b, V, choice.width, kernel>;
};
template <unsigned K, unsigned b, unsigned V> using owner = typename binding<K, b, V>::type;
template <class Owner>
[[gnu::always_inline]] ht::word read(const Owner* table, const ht::word* keys, std::size_t n) {
    ht::word sum = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const bool hit = table->find(keys[i], [&](const auto& value) {
            for (unsigned j = 0; j < value.size(); j += 8)
                sum += ht::load<8>(value.data() + j);
        });
        sum += hit;
    }
    return sum;
}
#define READ(K, V)                                                                                 \
    extern "C" ht::word read_k##K##_v##V(const owner<K, 10, V>* table, const ht::word* keys,       \
                                         std::size_t n) {                                          \
        return read(table, keys, n);                                                               \
    }
READ(14, 0)
READ(14, 8)
READ(14, 64) READ(30, 0) READ(30, 8) READ(30, 64)
#undef READ
    template <ht::write_strategy Strategy, class Owner>
    [[gnu::always_inline]] ht::word
    write(Owner* table, const ht::word* keys, const ht::bytes<8>* values, std::size_t n) {
    ht::word inserted = 0;
    for (std::size_t i = 0; i < n; ++i)
        inserted += table->template insert<Strategy>(keys[i], values[i]);
    return inserted;
}
#define WRITE(K, NAME, FLOW)                                                                       \
    extern "C" ht::word write_k##K##_##NAME(owner<K, 10, 8>* table, const ht::word* keys,          \
                                            const ht::bytes<8>* values, std::size_t n) {           \
        return write<ht::write_strategy::FLOW>(table, keys, values, n);                            \
    }
WRITE(14, mixed, mixed)
WRITE(14, existing, existing)
WRITE(30, mixed, mixed) WRITE(30, existing, existing)
#undef WRITE
    extern "C" auto* grow_k30_map8(const owner<30, 10, 8>* source) {
    using target =
        ht::table<40, 11, 8, binding<30, 10, 8>::choice.width, binding<30, 10, 8>::kernel>;
    return ht::grow<target>(*source).release();
}
template <bool Insert, class Owner>
ht::word modify(Owner* table, const ht::word* keys, std::size_t n) {
    ht::word count = 0;
    auto increment = [](auto& value) { ht::store<8>(value.data(), ht::load<8>(value.data()) + 1); };
    for (std::size_t i = 0; i < n; ++i) {
        if constexpr (Insert)
            count +=
                table->modify_or_insert(keys[i], {}, [&](auto& value, bool) { increment(value); });
        else
            count += table->modify(keys[i], increment);
    }
    return count;
}
#define MODIFY(K, NAME, INSERT)                                                                    \
    extern "C" ht::word modify_k##K##_##NAME(owner<K, 10, 8>* table, const ht::word* keys,         \
                                             std::size_t n) {                                      \
        return modify<INSERT>(table, keys, n);                                                     \
    }
MODIFY(14, existing, false)
MODIFY(14, inserting, true)
MODIFY(30, existing, false)
MODIFY(30, inserting, true)
#undef MODIFY
template <class D, class S> int accumulate(D* dst, const S* src) {
#if IKEA_BEFORE_ACCUMULATION
    return int(merge_study::accumulate_candidate(*dst, *src, [](const auto& a, const auto& b) {
        auto out = a;
        for (unsigned i = 0; i < out.size(); ++i)
            out[i] = ht::byte(a[i] * 3 + b[i] * 5 + 7);
        return out;
    }));
#else
    return int(ht::accumulate(*dst, *src, [](const auto& a, const auto& b) {
        auto out = a;
        for (unsigned i = 0; i < out.size(); ++i)
            out[i] = ht::byte(a[i] * 3 + b[i] * 5 + 7);
        return out;
    }));
#endif
}
#define ACCUMULATE(K, SB, DB, V)                                                                   \
    extern "C" int accumulate_k##K##_b##SB##_d##DB##_v##V(owner<K - (DB - SB), DB, V>* dst,        \
                                                          const owner<K, SB, V>* src) {            \
        return accumulate(dst, src);                                                               \
    }
ACCUMULATE(14, 10, 13, 8)
ACCUMULATE(30, 10, 13, 0)
ACCUMULATE(30, 10, 13, 8)
ACCUMULATE(30, 10, 13, 64) ACCUMULATE(47, 10, 12, 8) ACCUMULATE(47, 17, 19, 8)
#undef ACCUMULATE
