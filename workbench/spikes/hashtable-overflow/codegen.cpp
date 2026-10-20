#include <ikea/hashtable/table.h>
#include <ikea/hashtable/encoding-selection.h>
namespace ht = ikea::hashtable;
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
template <unsigned K, unsigned V> struct bound {
    static constexpr auto p = *ht::measured_encoding(K, V, cpu, isa);
    using type = ht::table<K + 10, 10, V, p.width, p.kernel>;
};
template <class T> [[gnu::always_inline]] ht::word read(const T* owner, const ht::word* keys, std::size_t n) {
    ht::word sum = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const bool hit = owner->find(keys[i], [&](const auto& v) {
            for (unsigned j = 0; j < v.size(); j += 8) sum += ht::load<8>(v.data() + j);
        });
        sum += hit;
    }
    return sum;
}
#define PROBE(K,V) extern "C" ht::word overflow_read_k##K##_v##V(const bound<K,V>::type* t, const ht::word* k, std::size_t n) { return read(t,k,n); }
PROBE(14,0) PROBE(14,8) PROBE(14,64)
PROBE(30,0) PROBE(30,8) PROBE(30,64)
template <ht::write_strategy Flow, class T>
[[gnu::always_inline]] ht::word write(T* owner, const ht::word* keys,
                                     const typename T::value_type* values, std::size_t n) {
    ht::word count = 0;
    for (std::size_t i = 0; i < n; ++i) count += owner->template insert<Flow>(keys[i], values[i]);
    return count;
}
#define WRITE(K,V,NAME,FLOW) extern "C" ht::word overflow_write_##NAME##_k##K##_v##V(bound<K,V>::type* t, const ht::word* k, const bound<K,V>::type::value_type* v, std::size_t n) { return write<ht::write_strategy::FLOW>(t,k,v,n); }
#define WRITES(K,V) WRITE(K,V,mixed,mixed) WRITE(K,V,existing,existing)
WRITES(14,0) WRITES(14,8) WRITES(14,64)
WRITES(30,0) WRITES(30,8) WRITES(30,64)
int main() {}
