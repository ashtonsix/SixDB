#include <ikea/hashtable/erased_table.h>
#include <iostream>
using namespace ikea::hashtable;
template <unsigned V> bytes<V> value(unsigned i) {
    bytes<V> v{};
    for (unsigned j = 0; j < V; ++j)
        v[j] = std::byte(i * 17 + j * 31);
    return v;
}
template <unsigned V> void staged_capture() {
    using D = erased_table<32, V>;
    auto owner = D::template explicit_encoding<2>(23);
    permutation<32, 30> mix{23};
    for (unsigned i = 0; i < 97; ++i) {
        owner.insert(mix.recover(0, i), value<V>(i));
    }
    const auto before = owner.storage();
    require(before.assigned && before.assigned <= before.accessible &&
                before.accessible <= before.reserved,
            "descriptor storage extents");
    require(bool(owner.relinquish_surplus()), "descriptor logical trim");
    auto captured = owner.template read_native<2>();
    auto dependent = captured.borrow();
    auto lookahead = captured.template borrow<staged_values::lookahead8>();
    const std::byte* held_value = nullptr;
    if constexpr (V) {
        const auto match = dependent.get_borrowed(mix.recover(0, 0));
        require(match.found, "borrow before descriptor rebuild");
        held_value = match.address;
    }
    static_assert(sizeof(dependent) == sizeof(void*) && sizeof(lookahead) == sizeof(void*));
    owner.template rebuild_explicit<3>();
    for (unsigned i = 0; i < 97; ++i)
        owner.erase(mix.recover(0, i));
    if constexpr (V)
        require(std::memcmp(held_value, value<V>(0).data(), V) == 0,
                "captured owner retains borrowed address across descriptor replacement");
    auto verify = [&](auto view) {
        for (unsigned i = 0; i < 200; ++i) {
            auto get = [&]<lookup_hint Hint>() {
                const auto match = view.template get<Hint>(mix.recover(0, i));
                require(match.found == (i < 97) &&
                            match.value == (i < 97 ? value<V>(i) : bytes<V>{}),
                        "hinted capture keeps old backing after rebuild/erase");
                if constexpr (V) {
                    const auto borrowed = view.template get_borrowed<Hint>(mix.recover(0, i));
                    require(borrowed.found == (i < 97) &&
                                std::memcmp(borrowed.address,
                                            (i < 97 ? value<V>(i) : bytes<V>{}).data(), V) == 0,
                            "retained native borrowed bytes and readable miss");
                }
            };
            get.template operator()<lookup_hint::predictable>();
            get.template operator()<lookup_hint::unpredictable>();
            typename decltype(view)::staged_probe probe;
            view.prepare_staged(probe, mix.recover(0, i), [](auto, auto) {});
            bool found = false;
            unsigned rank = 0, steps = 0;
            while (!view.advance(
                probe, found,
                [&](const auto& v) {
                    require(v == value<V>(i + rank++), "retained staged complete value/order");
                },
                [](auto, auto) {}))
                require(++steps < 4096, "retained staged termination");
            require(found == (i < 97), "retained staged membership");
            require(rank == (V && i < 97 ? 1 : 0), "retained staged multiplicity");
        }
    };
    verify(dependent);
    verify(lookahead);
}
int main() {
    try {
        staged_capture<0>();
        staged_capture<8>();
        staged_capture<64>();

        {
            bool refused = false;
            try {
                table<64, 64, 0> impossible(0);
            } catch (const std::length_error&) {
                refused = true;
            }
            require(refused, "b64 geometry refuses without shift wrap or allocation");
            refused = false;
            try {
                table<64, 60, 8> impossible(0);
            } catch (const std::length_error&) {
                refused = true;
            }
            require(refused, "value allocation extent cannot wrap");
        }
        for (const auto& profile : detail::profiles::encoding_profiles)
            for (unsigned k = 0; k <= 64; ++k) {
                const auto choice =
                    measured_encoding(k, profile.value_bytes, profile.cpu, profile.isa);
                require(bool(choice), "profile coverage");
                const auto g = geometry::at(64, 64 - k);
                require(choice->width >= k && choice->width <= g.maximum_encoding,
                        "fixed capacity encoding bound");
                require(choice->kernel == profile.kernel[k], "selected measured kernel");
            }
        require(measured_encoding(30, 8, "granite-rapids", "avx512")->kernel ==
                    search_kernel::exact30,
                "retained Granite independent-reader binding");
        require(!measured_encoding(30, 16, "zen5", "avx512"),
                "unmeasured value surface stays unknown");
        require(!measured_encoding(30, 8, "zen5", "unknown-isa"), "unmeasured ISA stays unknown");
        require(bool(measured_encoding(58, 8, "zen5", "avx2")), "measured AVX2 coverage");
        require(bool(measured_encoding(30, 8, "neoverse-v2", "neon")), "measured NEON coverage");
        {
            using D = erased_table<64, 8>;
            auto map = D::measured<6>(19, "zen5", "avx512");
            require(map.encoding() == 64 && map.bucket_bits() == 6, "corrected K58 selection");
            auto key = [](word i) { return i * 0x9e3779b97f4a7c15ULL; };
            for (unsigned i = 0; i < 500; ++i)
                require(map.insert(key(i), value<8>(i)), "map insertion");
            require(map.needs_growth(), "85% growth signal");
            const auto old = map.read();
            const auto functions = map.functions();
            map.rebuild_measured<7>("zen5", "avx512");
            require(map.functions() != functions && !map.needs_growth(),
                    "rebind specialization and capacity");
            for (unsigned i = 0; i < 500; ++i) {
                require(map.read().find(
                            key(i),
                            [&](const auto& v) { require(v == value<8>(i), "grown map value"); }),
                        "grown map key");
                require(old.find(key(i),
                                 [&](const auto& v) {
                                     require(v == value<8>(i), "retained reader value");
                                 }),
                        "retained reader key");
            }
            map.erase(key(9));
            require(!map.read().find(key(9), [](auto) {}), "new map erased");
            require(old.find(key(9), [](auto) {}), "old backing retained");
            const auto* before = map.functions();
            bool rejected = false;
            try {
                map.rebuild_measured<8>("unknown", "avx512");
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected && map.functions() == before && map.size() == 499,
                    "refused rebuild preserves owner");
            map.validate();
        }
        {
            using D = erased_table<40, 8>;
            auto map = D::explicit_encoding<9, 32>(31);
            for (unsigned i = 0; i < 6000; ++i)
                map.insert(word(i) * 104729 & mask<40>, value<8>(i));
            map.rebuild_explicit<10, 30>();
            for (unsigned i = 0; i < 6000; ++i)
                require(map.read().find(word(i) * 104729 & mask<40>,
                                        [&](const auto& v) {
                                            require(v == value<8>(i),
                                                    "96-to-64-byte bucket repack");
                                        }),
                        "repacked key");
            map.validate();
        }
        {
            using D = erased_table<16, 0>;
            auto set = D::measured<3>(17, "zen5", "avx512");
            for (unsigned i = 0; i < 500; ++i)
                set.insert(i * 129u & 65535);
            set.rebuild_measured<4>("zen5", "avx512");
            for (unsigned i = 0; i < 500; ++i)
                require(set.read().find(i * 129u & 65535, [](auto) {}), "set rebuild key");
            set.validate();
        }
        std::cout
            << "descriptor/profile selection, repacking, sets/maps and retained readers passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
