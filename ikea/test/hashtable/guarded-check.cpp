#include <ikea/hashtable.h>
#define REBUILD_CHECK_NO_MAIN
#include "rebuild-check.cpp"
#include "damage.h"

namespace {
namespace ht = ikea::hashtable;
using ht::word;
using ht::mutation_status;
using ht::owner_integration::damage_check;

template <unsigned W, unsigned Bits, unsigned V, ht::write_strategy Flow, unsigned E = W - Bits>
void check() {
    using Base = ht::table<W, Bits, V, E>;
    using Owner = Base;
    constexpr word seed = 713 & ht::mask<W>;
    for (unsigned pass = 0; pass < (Base::bucket_count == 1 ? 1u : 2u); ++pass) {
        const auto selected_bucket = pass ? Base::bucket_count - 1 : 0;
        Owner owner(seed);
        ht::permutation<W, W - Bits> mix{seed};
        std::map<word, ht::bytes<V>> expected;
        auto key = [&](word q) { return mix.recover(selected_bucket, q); };
        auto verify = [&] { study::verify(static_cast<const Base&>(owner), expected); };
        auto put = [&](word k, unsigned generation) {
            const auto v = study::value<V>(k, generation);
            const bool existed = expected.contains(k);
            damage_check refused(owner, k);
            refused.allow_prepare = false;
            auto rejected = owner.template guarded_insert<Flow>(k, v, refused);
            // A duplicate set can be a genuine no-store early exit.
            ht::require(rejected == mutation_status::denied ||
                            (!V && existed && rejected == mutation_status::unchanged),
                        "guarded refusal status");
            refused.verify(true);
            damage_check damage(owner, k);
            auto result = owner.template guarded_insert<Flow>(k, v, damage);
            ht::require(result == (existed
                                       ? (V ? mutation_status::updated : mutation_status::unchanged)
                                       : mutation_status::inserted),
                        "guarded insertion status");
            damage.verify();
            expected[k] = v;
            verify();
            if constexpr (Flow == ht::write_strategy::mixed && !Base::bitmap)
                if (existed) {
                    const auto state = ht::author::table_access::get(static_cast<Base&>(owner));
                    if (ht::author::table_access::primary_find<Base>(
                            state.primary.data + selected_bucket * Base::stride, mix(k).quotient) !=
                        Base::N)
                        ht::require(damage.covered_bytes() >= Base::stride + sizeof(owner),
                                    "shared idempotent primary/header stores covered");
                }
        };
        auto erase = [&](word k) {
            const bool existed = expected.contains(k);
            damage_check refused(owner, k);
            refused.allow_prepare = false;
            ht::require(owner.guarded_erase(k, refused) ==
                            (existed ? mutation_status::denied : mutation_status::absent),
                        "guarded erase refusal");
            refused.verify(true);
            damage_check damage(owner, k);
            ht::require(owner.guarded_erase(k, damage) ==
                            (existed ? mutation_status::erased : mutation_status::absent),
                        "guarded erase status");
            damage.verify();
            expected.erase(k);
            verify();
        };
        auto v = study::value<V>(key(0), 1);
        damage_check denied(owner, key(0));
        denied.allow_write = false;
        ht::require(owner.template guarded_insert<Flow>(key(0), v, denied) ==
                        mutation_status::denied,
                    "key/physical grant refused");
        denied.verify(true);
        if constexpr (W < 64) {
            damage_check invalid(owner, word{1} << W);
            ht::require(owner.template guarded_insert<Flow>(word{1} << W, v, invalid) ==
                            mutation_status::denied,
                        "noncanonical key refused");
            invalid.verify(true);
        }
        const unsigned primary = Base::bitmap ? std::min(8u, Base::N) : Base::N;
        for (unsigned q = 0; q < primary; ++q)
            put(key(q), q);
        put(key(0), 99);
        erase(key(0));
        put(key(0), 100);
        if constexpr (Base::bitmap)
            for (auto q : {7u, 8u, Base::N - 1})
                if (q < Base::N) {
                    put(key(q), 101);
                    put(key(q), 102);
                    erase(key(q));
                    put(key(q), 103);
                }
        if constexpr (!Base::bitmap) {
            damage_check no_capacity(owner, key(primary));
            ht::require(owner.template guarded_insert<Flow>(key(primary), v, no_capacity) ==
                            mutation_status::needs_capacity,
                        "guarded operation never grows");
            no_capacity.verify(true);
            // Capacity is prepared by ordinary private mutation outside this
            // guarded operation. Every later call obtains a fresh admission.
            const auto state = ht::author::table_access::get(static_cast<Base&>(owner));
            auto* primary_bytes = state.primary.data + selected_bucket * Base::stride;
            owner.insert(key(primary), v);
            expected[key(primary)] = v;
            ht::require(primary_bytes[Base::overflow_offset] == 1, "one live overflow");
            erase(key(primary));
            ht::require(primary_bytes[Base::overflow_offset] == 0,
                        "last known overflow decrements");
            for (unsigned q = primary; q < primary + 14; ++q)
                put(key(q), q);
            damage_check full_swiss(owner, key(primary + 14));
            ht::require(owner.template guarded_insert<Flow>(key(primary + 14), v, full_swiss) ==
                            mutation_status::needs_capacity,
                        "allocated Swiss capacity refusal");
            full_swiss.verify(true);
            for (unsigned q = primary + 14; q < primary + 254; ++q) {
                auto k = key(q);
                auto value = study::value<V>(k, q);
                owner.insert(k, value);
                expected[k] = value;
            }
            ht::require(primary_bytes[Base::overflow_offset] == 254,
                        "known overflow before saturation");
            put(key(primary + 254), 801);
            ht::require(primary_bytes[Base::overflow_offset] == 255, "guarded count saturates");
            erase(key(primary + 254));
            put(key(primary + 254), 802);
            for (unsigned q = primary; q < 600; ++q) {
                auto k = key(q);
                auto value = study::value<V>(k, q);
                owner.insert(k, value);
                expected[k] = value;
            }
            verify();
            ht::require(primary_bytes[Base::overflow_offset] == 255, "sticky overflow fixture");
            put(key(primary + 1), 900);
            erase(key(primary + 2));
            put(key(primary + 2), 901);
            // A primary hole must not hide a Swiss duplicate.
            erase(key(1));
            const auto size = owner.size();
            put(key(primary + 1), 902);
            ht::require(owner.size() == size, "Swiss duplicate behind primary hole");
            if constexpr (V) {
                const auto target = key(primary + 1);
                const ht::byte* tuple = nullptr;
                for (std::size_t slot = 0; slot < state.groups * 16; ++slot)
                    if (state.controls.data[slot] < 128 &&
                        ht::load<Base::key_bytes>(state.slots.data + slot * Base::tuple_bytes) ==
                            target)
                        tuple = state.slots.data + slot * Base::tuple_bytes;
                ht::require(tuple, "Swiss alias fixture");
                ht::bytes<V> before{};
                std::memcpy(before.data(), tuple, V);
                damage_check damage(owner, target);
                ht::require(owner.template guarded_insert<Flow>(
                                target, *reinterpret_cast<const ht::bytes<V>*>(tuple), damage) ==
                                mutation_status::updated,
                            "Swiss overlapping replacement");
                damage.verify();
                expected[target] = before;
                verify();
            }
            put(key(700), 903);
            erase(key(primary + 1));
            ht::require(primary_bytes[Base::overflow_offset] == 255,
                        "unknown overflow never decrements");
            // Snapshot input aliased to key bytes before a fresh key store changes
            // them. The whole primary allocation is a real addressable object.
            if constexpr (V) {
                erase(key(2));
                const auto* p = primary_bytes;
                ht::bytes<V> before{};
                std::memcpy(before.data(), p, V);
                const auto& aliased = *reinterpret_cast<const ht::bytes<V>*>(p);
                damage_check damage(owner, key(701));
                ht::require(owner.template guarded_insert<Flow>(key(701), aliased, damage) ==
                                mutation_status::inserted,
                            "aliased insertion");
                damage.verify();
                expected[key(701)] = before;
                verify();
            }
        }
        erase(key(0));
        erase(key(0));
        std::cout << "GUARDED_PASS," << W << ',' << Bits << ',' << V << ',' << unsigned(Flow) << ','
                  << E << ',' << selected_bucket << '\n';
    }
}
} // namespace

int main() {
    using enum ikea::hashtable::write_strategy;
    check<1, 1, 0, mixed>();
    check<9, 1, 8, existing>();
    check<10, 1, 7, mixed>();
    check<11, 1, 0, mixed>();
    check<12, 1, 8, mixed>();
    check<15, 1, 8, mixed>();
    check<16, 1, 7, existing, 16>();
    check<16, 1, 64, existing>();
    check<31, 1, 64, mixed>();
    check<32, 1, 8, mixed>();
    check<64, 1, 64, mixed>();
    check<64, 0, 8, mixed>();
}
