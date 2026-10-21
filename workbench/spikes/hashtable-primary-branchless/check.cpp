#include <array>
#include <ikea/hashtable/author/table.h>
#include <ikea/hashtable/table.h>
#include <iostream>
namespace ht = ikea::hashtable;
template <unsigned W, unsigned b, unsigned E, unsigned V,
          ht::search_kernel Kernel = ht::search_kernel::generic>
void check() {
    using T = ht::table<W, b, V, E, Kernel>;
    using Value = typename T::value_type;
    for (ht::word seed : {0ULL, 713ULL, 1729ULL}) {
        for (ht::word bucket : {ht::word{0}, ht::word(T::bucket_count - 1)}) {
            T table(seed);
            const ht::permutation<W, W - b> mix{seed & ht::mask<W>};
            constexpr unsigned count = T::bitmap ? std::min(300u, T::N) : 300;
            std::array<ht::word, count> keys{};
            auto payload = [](ht::word key) {
                Value v{};
                if constexpr (V)
                    ht::store<8>(v.data(), key ^ 0xd31a9e8367e2ab19ULL);
                return v;
            };
            auto find = [&](ht::word key, bool expected) {
                unsigned calls = 0;
                bool found = table.find(key, [&](const Value& v, const typename T::location& loc) {
                    ++calls;
                    ht::require(expected, "consumer called for absent key");
                    ht::require(v == payload(key), "payload");
                    ht::require(loc.bucket == bucket, "primary location bucket");
                    ht::require(!loc.primary || loc.slot < T::N, "primary location row");
                });
                ht::require(found == expected, "membership");
                if constexpr (requires { table.lookup(key); }) {
                    const auto r = table.lookup(key);
                    ht::require(r.found == expected, "result membership");
                    ht::require(r.value == (expected ? payload(key) : Value{}),
                                "result hit payload / zero-on-absence");
                }
                ht::require(calls == (V && expected ? 1u : 0u),
                            "consumer called exactly once on map hit");
            };
            // All quotients collide in the selected bucket; includes empty-table
            // misses, first/last primary row, spills and the saturated overflow-byte
            // case.
            for (unsigned i = 0; i < count; ++i) {
                keys[i] = mix.recover(bucket, i == count - 1 ? ht::mask<W - b> : i);
                find(keys[i], false);
            }
            for (unsigned i = 0; i < count; ++i)
                ht::require(table.insert(keys[i], payload(keys[i])), "fresh insertion");
            for (unsigned i = 0; i < count; ++i)
                find(keys[i], true);
            for (unsigned i = 0; i < count; i += 3)
                ht::require(table.erase(keys[i]), "erase");
            for (unsigned i = 0; i < count; ++i)
                find(keys[i], i % 3 != 0);
            for (unsigned i = 0; i < count; i += 3)
                ht::require(table.insert(keys[i], payload(keys[i])), "refill");
            for (unsigned i = 0; i < count; ++i)
                find(keys[i], true);
            table.validate();
        }
    }
}
template <unsigned W, unsigned b> void widths() {
    constexpr auto g = ht::geometry::at(W, b);
    check<W, b, W - b, 0>();
    check<W, b, W - b, 8>();
    if constexpr (g.maximum_encoding != W - b) {
        check<W, b, g.maximum_encoding, 0>();
        check<W, b, g.maximum_encoding, 8>();
    }
}
int main() {
    widths<9, 0>();
    widths<10, 0>();
    widths<11, 0>();
    widths<16, 2>();
    widths<22, 2>();
    widths<23, 2>();
    widths<26, 2>();
    widths<31, 2>();
    widths<32, 2>();
    widths<33, 2>();
    widths<34, 2>();
    widths<42, 2>();
    widths<50, 2>();
    widths<64, 2>();
    widths<64, 1>();
    widths<64, 0>();
    check<22, 2, 20, 0, ht::search_kernel::exact_small512>();
    check<22, 2, 20, 8, ht::search_kernel::exact_small512>();
    check<21, 2, 19, 0, ht::search_kernel::exact_small512>();
    check<21, 2, 19, 8, ht::search_kernel::exact_small512>();
    std::cout << "generic bitmap/packed, zero/max encoding, 0/8-byte value, "
                 "primary/spill/tombstone checks passed\n";
}
