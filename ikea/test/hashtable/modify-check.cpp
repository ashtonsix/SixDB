#include <ikea/hashtable.h>
#include "damage.h"
#include <cstring>
#include <iostream>
#include <map>

namespace ht = ikea::hashtable;
using ht::word;
using ht::mutation_status;
using ht::owner_integration::damage_check;

template <unsigned W, unsigned b, unsigned V, unsigned E = W - b> void check() {
    using Owner = ht::table<W, b, V, E>;
    const word seed = 713 & ht::mask<W>;
    Owner owner(seed);
    ht::permutation<W, W - b> mix{seed};
    const auto key = [&](word q) { return mix.recover(0, q); };
    ht::bytes<V> initial{}, expected{};
    for (unsigned i = 0; i < V; ++i)
        initial[i] = ht::byte(i * 13 + 7);
    auto verify = [&](word k, const auto& value) {
        unsigned calls = 0;
        ht::require(owner.find(k,
                               [&](const auto& actual) {
                                   ++calls;
                                   ht::require(actual == value, "RMW complete value");
                               }),
                    "RMW present");
        ht::require(calls == 1, "RMW value callback");
        owner.validate();
    };
    unsigned updates = 0;
    ht::require(!owner.modify(key(0), [&](auto&) { ++updates; }), "modify absent");
    damage_check absent(owner, key(0));
    ht::require(owner.guarded_modify(key(0), absent, [&](auto&) { ++updates; }) ==
                    mutation_status::absent,
                "guarded modify absent");
    absent.verify(true);
    ht::require(updates == 0, "absent has no updater");
    ht::require(owner.modify_or_insert(key(0), initial,
                                       [&](auto& value, bool present) {
                                           ht::require(!present && value == initial,
                                                       "initial admitted once");
                                           ++updates;
                                           for (auto& x : value)
                                               x ^= 0x51;
                                       }),
                "RMW inserts");
    expected = initial;
    for (auto& x : expected)
        x ^= 0x51;
    verify(key(0), expected);
    ht::require(!owner.modify_or_insert(key(0), initial,
                                        [&](auto& value, bool present) {
                                            ht::require(present && value == expected,
                                                        "existing snapshot");
                                            ++updates;
                                            for (auto& x : value)
                                                x += 3;
                                        }),
                "RMW replaces");
    for (auto& x : expected)
        x += 3;
    verify(key(0), expected);
    const auto fail = [](auto& value) {
        value[0] ^= 255;
        throw std::runtime_error("updater");
    };
    try {
        owner.modify(key(0), fail);
        ht::require(false, "throw required");
    } catch (const std::runtime_error& e) {
        ht::require(std::string_view(e.what()) == "updater", "expected throw");
    }
    verify(key(0), expected);
    for (bool deny_enter : {false, true}) {
        damage_check denied(owner, key(0));
        denied.allow_write = !deny_enter;
        denied.allow_prepare = deny_enter;
        ht::require(owner.guarded_modify(key(0), denied, [&](auto&) { ++updates; }) ==
                        mutation_status::denied,
                    "RMW refusal");
        denied.verify(true);
        ht::require(updates == 2, "denial has no updater");
    }
    damage_check throwing(owner, key(0));
    try {
        owner.guarded_modify(key(0), throwing, fail);
        ht::require(false, "guarded throw required");
    } catch (const std::runtime_error& e) {
        ht::require(std::string_view(e.what()) == "updater", "expected guarded throw");
    }
    throwing.verify(true);
    verify(key(0), expected);
    damage_check damage(owner, key(0));
    ht::require(owner.guarded_modify(key(0), damage,
                                     [&](auto& value) {
                                         ++updates;
                                         for (auto& x : value)
                                             x ^= 0xa7;
                                     }) == mutation_status::updated,
                "guarded RMW");
    damage.verify();
    ht::require(damage.covered_bytes() == V, "existing RMW publishes only actual value bytes");
    ht::require(damage.observations() == (V % 8 == 0 ? 1u : (V + 7) / 8),
                "value plane damage count");
    for (auto& x : expected)
        x ^= 0xa7;
    verify(key(0), expected);
    if constexpr (W < 64) {
        damage_check invalid(owner, word{1} << W);
        ht::require(owner.guarded_modify(word{1} << W, invalid, [&](auto&) { ++updates; }) ==
                        mutation_status::denied,
                    "invalid key refused");
        invalid.verify(true);
    }
    if constexpr (Owner::bitmap && Owner::N > 1) {
        const auto last = key(Owner::N - 1);
        owner.insert(last, initial);
        damage_check edge(owner, last);
        ht::require(owner.guarded_modify(last, edge,
                                         [](auto& v) {
                                             for (auto& x : v)
                                                 x ^= 0x23;
                                         }) == mutation_status::updated,
                    "bitmap final-row RMW");
        edge.verify();
        ht::require(edge.covered_bytes() == V, "bitmap final-row value damage");
        auto value = initial;
        for (auto& x : value)
            x ^= 0x23;
        verify(last, value);
    }
    if constexpr (!Owner::bitmap) {
        for (unsigned q = 1; q < Owner::N; ++q)
            owner.insert(key(q), initial);
        const auto last = key(Owner::N - 1);
        damage_check edge(owner, last);
        ht::require(owner.guarded_modify(last, edge,
                                         [](auto& v) {
                                             for (auto& x : v)
                                                 x ^= 0x23;
                                         }) == mutation_status::updated,
                    "packed final-row RMW");
        edge.verify();
        ht::require(edge.covered_bytes() == V, "packed final-row value damage");
        auto value = initial;
        for (auto& x : value)
            x ^= 0x23;
        verify(last, value);
        const auto incoming = key(Owner::N);
        damage_check capacity(owner, incoming);
        const auto count = updates;
        ht::require(owner.guarded_modify_or_insert(incoming, initial, capacity,
                                                   [&](auto&, bool) { ++updates; }) ==
                        mutation_status::needs_capacity,
                    "RMW capacity separate");
        ht::require(updates == count, "capacity refusal has no updater");
        capacity.verify(true);
        Owner failure(seed);
        for (unsigned q = 0; q < Owner::N; ++q)
            failure.insert(key(q), initial);
        bool failed = false;
        try {
            failure.modify_or_insert(incoming, initial, [](auto& v, bool) {
                v[0] ^= 1;
                throw std::runtime_error("new updater");
            });
        } catch (const std::runtime_error&) {
            failed = true;
        }
        ht::require(failed && failure.size() == Owner::N && !failure.find(incoming, [](auto) {}),
                    "throwing new RMW keeps logical contents");
        failure.validate();
        owner.prepare_absent(incoming);
        damage_check insert(owner, incoming);
        ht::require(owner.guarded_modify_or_insert(incoming, initial, insert,
                                                   [&](auto& v, bool present) {
                                                       ht::require(!present, "new Swiss RMW");
                                                       v[0] ^= 0xee;
                                                   }) == mutation_status::inserted,
                    "prepared Swiss RMW inserts");
        insert.verify();
        auto spilled = initial;
        spilled[0] ^= 0xee;
        verify(incoming, spilled);
        // A primary hole must not hide the existing Swiss entry.
        owner.erase(key(1));
        const auto size = owner.size();
        damage_check swiss(owner, incoming);
        ht::require(owner.guarded_modify_or_insert(incoming, initial, swiss,
                                                   [&](auto& v, bool present) {
                                                       ht::require(
                                                           present && v == spilled,
                                                           "Swiss duplicate after primary hole");
                                                       v.back() ^= 0x63;
                                                   }) == mutation_status::updated,
                    "Swiss RMW updates");
        swiss.verify();
        ht::require(swiss.covered_bytes() == V && owner.size() == size,
                    "Swiss only value/count unchanged");
        spilled.back() ^= 0x63;
        verify(incoming, spilled);
        damage_check refused_new(owner, key(Owner::N + 1));
        refused_new.allow_prepare = false;
        const auto before_updates = updates;
        ht::require(owner.guarded_modify_or_insert(key(Owner::N + 1), initial, refused_new,
                                                   [&](auto&, bool) { ++updates; }) ==
                        mutation_status::denied,
                    "new RMW preparation refusal");
        refused_new.verify(true);
        ht::require(updates == before_updates, "new admission refusal has no updater");
    }
    std::cout << "RMW_PASS," << W << ',' << b << ',' << V << ',' << E << '\n';
}
void descriptor_and_reference() {
    using D = ht::descriptor<24, 8>;
    auto owner = D::explicit_encoding<10, 14>(713, nullptr, ht::write_strategy::existing);
    const ht::bytes<8> zero{};
    unsigned callbacks = 0;
    ht::require(!owner.modify(7, [&](auto&) { ++callbacks; }), "descriptor absent");
    ht::require(owner.modify_or_insert(7, zero,
                                       [&](auto& v, bool present) {
                                           ht::require(!present, "descriptor insertion");
                                           v[0] = 1;
                                           ++callbacks;
                                       }),
                "descriptor inserts");
    owner.with_native_mutation<10, 14>([&](auto& native, auto strategy) {
        static_assert(decltype(strategy)::value == ht::write_strategy::mixed ||
                      decltype(strategy)::value == ht::write_strategy::existing);
        ht::require(decltype(strategy)::value == ht::write_strategy::existing,
                    "batch exposes selected writer");
        for (unsigned i = 0; i < 100; ++i)
            native.modify(7, [](auto& v) { ++v[0]; });
        native.template insert<decltype(strategy)::value>(9, zero);
    });
    auto capture = owner.read_native<10, 14>();
    ht::require(capture.borrow().find(
                    7, [](const auto& v) { ht::require(v[0] == 101, "native RMW loop"); }),
                "descriptor changed");
    ht::require(callbacks == 1, "descriptor updater count");
    bool refused = false;
    try {
        owner.with_native_mutation<11, 14>([](auto&, auto) {});
    } catch (const std::runtime_error&) {
        refused = true;
    }
    ht::require(refused, "mismatched batch refused");
    // The container stores only a caller-owned reference. External append and
    // ownership are intentionally a consumer operation, not an owning multimap.
    ht::table<24, 10, 8> refs(713);
    std::vector<std::vector<unsigned>> blocks;
    std::map<word, std::vector<unsigned>> expected;
    for (unsigned i = 0; i < 300; ++i) {
        const word key = i % 23;
        refs.modify_or_insert(key, zero, [&](auto& v, bool present) {
            word id = 0;
            if (present)
                std::memcpy(&id, v.data(), 8);
            else {
                id = blocks.size();
                blocks.emplace_back();
            }
            blocks[id].push_back(i);
            std::memcpy(v.data(), &id, 8);
        });
        expected[key].push_back(i);
    }
    for (const auto& [key, values] : expected)
        ht::require(refs.find(key,
                              [&](const auto& v) {
                                  word id;
                                  std::memcpy(&id, v.data(), 8);
                                  ht::require(blocks[id] == values,
                                              "complete caller-owned append order");
                              }),
                    "reference membership");
}
int main() {
    try {
        check<1, 1, 1>();
        check<10, 1, 3>();
        check<15, 1, 8>();
        check<16, 1, 9, 16>();
        check<31, 1, 16>();
        check<32, 1, 17>();
        check<64, 0, 65>();
        descriptor_and_reference();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
