#include <ikea/hashtable.h>
#include <array>
#include <iostream>
#include <map>

namespace ht = ikea::hashtable;

int main() {
    using Table = ht::table<16, 0, 8>;
    Table owner(713);
    ht::permutation<16, 16> mix{713};
    std::map<ht::word, ht::word> expected;
    auto value = [](ht::word x) {
        ht::bytes<8> v{};
        ht::store<8>(v.data(), x);
        return v;
    };
    auto verify = [&](const auto& table, ht::word factor = 1) {
        table.validate();
        ht::require(table.size() == expected.size(), "tag population");
        for (const auto& [key, payload] : expected)
            ht::require(table.find(key, [&](const auto& v) {
                ht::require(ht::load<8>(v.data()) == payload * factor, "tag payload");
            }), "tag membership");
    };
    // Fill primary, then force every possible hash-byte class into Swiss.
    for (ht::word k = 0; k < Table::N; ++k) {
        owner.insert(k, value(k + 1));
        expected[k] = k + 1;
    }
    std::array<bool, 256> seen{};
    std::array<ht::word, 256> keys{};
    for (ht::word k = Table::N; k < 65536; ++k) {
        const auto raw = mix(k).swiss >> 56;
        if (!seen[raw]) {
            seen[raw] = true;
            keys[raw] = k;
            owner.insert(k, value(k + 1));
            expected[k] = k + 1;
        }
    }
    for (bool found : seen)
        ht::require(found, "all raw tag classes exercised");
    verify(owner);
    // These boundary cases distinguish eight-bit tags from a high-bit liveness test.
    for (unsigned raw : {0u, 1u, 2u, 127u, 128u, 254u, 255u}) {
        ht::require(owner.erase(keys[raw]), "tag boundary erase");
        ht::require(owner.insert(keys[raw], value(keys[raw] + 1)), "tag tombstone reuse");
    }
    ht::reserve_overflow(owner, 1024); // Relocation must retain high-bit tags.
    verify(owner);
    auto copy = owner.clone();
    verify(copy);
    auto grown = ht::grow<ht::table<16, 1, 8>>(owner);
    verify(*grown);
    auto add = [&](const auto& a, const auto& b) {
        return value(ht::load<8>(a.data()) + ht::load<8>(b.data()));
    };
    ht::require(ht::accumulate(copy, owner, add) == ht::accumulation_status::complete,
                "tag accumulation");
    verify(copy, 2);
    verify(owner);
    std::cout << "PASS: every raw tag byte, tombstone reuse, reserve, clone, growth, accumulation\n";
}
