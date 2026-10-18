#include <ikea/hashtable/table.h>
#include <iostream>
#include <unordered_map>

namespace {
using namespace ikea::hashtable;
template <unsigned W, unsigned b, unsigned V, unsigned E> void exercise() {
    using owner_type = table<W, b, V, E>;
    owner_type owner(713);
    permutation<W, W - b> route{713 & mask<W>};
    std::unordered_map<word, bytes<V>> expected;
    const unsigned count = owner_type::bitmap ? owner_type::N : owner_type::N + 19;
    auto value_for = [](unsigned bucket, unsigned q, unsigned epoch) {
        bytes<V> value{};
        for (unsigned i = 0; i < V; ++i)
            value[i] = byte((bucket * 137 + q * 71 + epoch * 29 + i * 43) ^ (i >> 2));
        return value;
    };
    auto verify = [&] {
        owner.validate();
        // Recover keys independently from their known bucket and quotient.
        // This fills every primary row, forces overflow, and includes q=0.
        for (unsigned bucket = 0; bucket < (1u << b); ++bucket)
            for (unsigned q = 0; q < count; ++q) {
                const auto key = route.recover(bucket, q);
                const auto it = expected.find(key);
                unsigned visits = 0;
                const auto consume = [&](const auto& value) {
                    require(it != expected.end() && it->second == value, "complete wide value");
                    ++visits;
                };
                const bool hit = owner.find(key, consume);
                require(hit == (it != expected.end()) && visits == unsigned(hit),
                        "wide direct membership");
                typename owner_type::staged_probe probe;
                owner.prepare_staged(probe, key, [](auto, auto) {});
                bool staged = false;
                unsigned steps = 0;
                visits = 0;
                while (!owner.advance(probe, staged, consume, [](auto, auto) {}))
                    require(++steps < 4096, "wide staged completion");
                require(staged == hit && visits == unsigned(hit), "wide staged membership");
            }
        std::size_t seen = 0;
        owner.visit([&](word key, const auto& value) {
            const auto it = expected.find(key);
            require(it != expected.end() && it->second == value, "wide iteration value");
            ++seen;
        });
        require(seen == expected.size(), "wide iteration population");
    };
    for (unsigned epoch = 0; epoch < 3; ++epoch) {
        for (unsigned bucket = 0; bucket < (1u << b); ++bucket)
            for (unsigned q = 0; q < count; ++q) {
                const auto key = route.recover(bucket, q);
                const auto value = value_for(bucket, q, epoch);
                const bool fresh = !expected.contains(key);
                expected[key] = value;
                require(owner.insert(key, value) == fresh, "wide insert/update result");
            }
        verify();
        for (unsigned bucket = 0; bucket < (1u << b); ++bucket)
            for (unsigned q = epoch % 2; q < count; q += 2) {
                const auto key = route.recover(bucket, q);
                require(owner.erase(key) == bool(expected.erase(key)), "wide erase result");
            }
        verify();
    }
}
template <unsigned V> void width() {
    exercise<33, 3, V, 30>();
    exercise<64, 3, V, 64>();
    exercise<10, 2, V, 8>();
}
} // namespace
int main() {
    try {
        width<8>();
        width<16>();
        width<24>();
        width<32>();
        width<40>();
        width<48>();
        width<56>();
        width<64>();
        width<9>();
        width<63>();
        std::cout
            << "complete values, all primary rows, Swiss overflow, bitmap, mutation and staged reads passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
