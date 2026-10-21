#include <ikea/hashtable.h>
#include <map>
#include <iostream>
using namespace ikea::hashtable;
template <unsigned W, unsigned V, write_strategy Strategy> void width() {
    constexpr unsigned b = std::min(3u, W), K = W - b;
    constexpr bool Multi = false;
    using Owner = table<W, b, V>;
    const unsigned n = W < 10 ? unsigned(word{1} << W) : 512;
    auto key = [](unsigned i) { return (word(i) * 129 + 37) & mask<W>; };
    auto values = [](word key, unsigned epoch) {
        std::vector<bytes<V>> out(Multi ? 1 + unsigned(key % 5) : 1);
        for (unsigned j = 0; j < out.size(); ++j)
            for (unsigned k = 0; k < V; ++k)
                out[j][k] = byte(key * 17 + epoch * 71 + j * 23 + k * 13);
        if constexpr (Multi)
            if (out.size() > 2)
                out.back() = out.front(); // Repeated values are preserved.
        return out;
    };
    Owner owner(71);
    std::map<word, std::vector<bytes<V>>> expected;
    auto verify = [&] {
        require(owner.size() == expected.size(), "all-W population");
        for (unsigned i = 0; i < n; ++i) {
            const auto query = key(i);
            const auto entry = expected.find(query);
            std::vector<bytes<V>> got;
            require(owner.find(query, [&](const auto& v) { got.push_back(v); }) ==
                        (entry != expected.end()),
                    "all-W direct membership");
            if constexpr (V)
                require(got == (entry == expected.end() ? std::vector<bytes<V>>{} : entry->second),
                        "all-W complete ordered values");
            else
                require(got.empty(), "all-W set has no payload");
            auto get = [&]<lookup_hint Hint>() {
                const auto match = owner.template get<Hint>(query);
                require(match.found == (entry != expected.end()), "all-W hinted membership");
                require(match.value == (entry == expected.end() ? bytes<V>{} : entry->second.front()),
                        "all-W hinted complete value / zero miss");
            };
            get.template operator()<lookup_hint::predictable>();
            get.template operator()<lookup_hint::unpredictable>();
            if constexpr (V) {
                auto borrowed = [&]<lookup_hint Hint>() {
                    const auto match = owner.template get_borrowed<Hint>(query);
                    const auto want = entry == expected.end() ? bytes<V>{} : entry->second.front();
                    require(match.found == (entry != expected.end()) &&
                                std::memcmp(match.address, want.data(), V) == 0,
                            "all-W borrowed complete value / readable zero miss");
                };
                borrowed.template operator()<lookup_hint::predictable>();
                borrowed.template operator()<lookup_hint::unpredictable>();
            }
            auto staged = [&]<staged_values Values>() {
                // Hints may be speculative, but their complete byte extents
                // must still belong to one retained backing allocation.
                auto emit = [&](const byte* p, std::size_t bytes) {
                    bool contained = false;
                    const auto address = reinterpret_cast<std::uintptr_t>(p);
                    owner.allocations([&](auto, const byte* base, std::size_t size) {
                        const auto begin = reinterpret_cast<std::uintptr_t>(base);
                        contained |= address >= begin && address - begin <= size &&
                                     bytes <= size - (address - begin);
                    });
                    require(contained && bytes, "all-W staged hint extent");
                };
                typename Owner::staged_probe probe;
                owner.template prepare_staged<Values>(probe, query, emit);
                bool found = false;
                unsigned steps = 0;
                got.clear();
                while (!owner.template advance<Values>(
                    probe, found, [&](const auto& v) { got.push_back(v); }, emit))
                    require(++steps < 4096, "all-W staged termination");
                require(found == (entry != expected.end()), "all-W staged membership");
                if constexpr (V)
                    require(got ==
                                (entry == expected.end() ? std::vector<bytes<V>>{} : entry->second),
                            "all-W staged ordered values");
            };
            staged.template operator()<staged_values::dependent>();
            staged.template operator()<staged_values::lookahead8>();
        }
        owner.validate();
    };
    for (unsigned epoch = 0; epoch < 2; ++epoch) {
        for (unsigned i = 0; i < n; ++i) {
            auto q = key(i);
            auto v = values(q, epoch);
            if constexpr (Multi) {
                if (expected.contains(q))
                    owner.replace_bag(q, v);
                else
                    owner.insert_bag(q, v);
            } else
                owner.template insert<Strategy>(q, v.front());
            expected[q] = std::move(v);
        }
        verify();
        for (unsigned i = epoch; i < n; i += 2) {
            require(owner.erase(key(i)), "all-W erase");
            expected.erase(key(i));
        }
        verify();
    }
    if constexpr (b < W) {
        constexpr unsigned next_k = K - 1;
        constexpr unsigned next_e =
            next_k > 9 && K <= geometry::at(W, b + 1).maximum_encoding ? K : next_k;
        using Larger = table<W, b + 1, V, next_e>;
        auto grown = grow<Larger>(owner);
        grown->validate();
        require(grown->size() == expected.size(), "all-W growth population");
        for (unsigned i = 0; i < n; ++i) {
            const auto q = key(i);
            std::vector<bytes<V>> got;
            require(grown->find(q, [&](const auto& v) { got.push_back(v); }) ==
                        expected.contains(q),
                    "all-W grown membership");
            if constexpr (V)
                require(got == (expected.contains(q) ? expected.at(q) : std::vector<bytes<V>>{}),
                        "all-W grown value");
        }
        verify();
    }
    // A native borrowed reader trusts the domain, while its checked facade and
    // guards reject noncanonical keys. Geometry covers every b without allocation.
    for (unsigned bits = 0; bits <= W; ++bits) {
        const auto g = geometry::at(W, bits);
        require(g.w == W && g.k == W - bits && g.b == bits, "all-W geometry coordinates");
        if (g.k <= 9)
            require(g.capacity == (__uint128_t{1} << W), "bitmap domain capacity");
    }
    std::cout << W << ',' << V << ',' << Multi << ',' << K << '\n';
}
int main() {
    try {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            ((width<WIDTH_START + I, 0, write_strategy::mixed>(),
              width<WIDTH_START + I, 8, write_strategy::mixed>(),
              width<WIDTH_START + I, 64, write_strategy::mixed>(),
              width<WIDTH_START + I, 0, write_strategy::existing>(),
              width<WIDTH_START + I, 8, write_strategy::existing>()),
             ...);
        }(std::make_index_sequence<8>{});
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
