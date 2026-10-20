#include <ikea/hashtable.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace ht = ikea::hashtable;
using Value = ht::bytes<8>;
constexpr std::uint64_t seed = 713;
constexpr auto plan = ht::geometry::for_population(64, 64, 850);
static_assert(plan.b == 4 && plan.capacity == 128);

Value value(std::uint64_t n) {
    Value result;
    std::memcpy(result.data(), &n, sizeof(n));
    return result;
}
std::uint64_t number(const Value& bytes) {
    std::uint64_t result;
    std::memcpy(&result, bytes.data(), sizeof(result));
    return result;
}
struct Row {
    std::uint64_t key;
    Value value;
};

// This is caller scheduling, not a table policy. No prepared probe survives a
// mutation: the future home's hint is issued now, and the operation resolves
// the key against current coherent backing when it reaches the head.
template <unsigned Distance, class Owner, class Item, class Key, class Operation>
void independent(const Owner& owner, std::span<const Item> requests, Key key, Operation operation) {
    auto hint = [&](std::size_t i) { owner.prefetch(owner.prepare(key(requests[i]))); };
    if constexpr (Distance)
        for (std::size_t i = 0; i < std::min<std::size_t>(Distance, requests.size()); ++i)
            hint(i);
    for (std::size_t i = 0; i < requests.size(); ++i) {
        if constexpr (Distance)
            if (requests.size() - i > Distance)
                hint(i + Distance);
        operation(requests[i]);
    }
}
template <unsigned Distance, ht::write_strategy Strategy, class Owner>
std::size_t write_batch(Owner& owner, std::span<const Row> rows) {
    std::size_t inserted = 0;
    independent<Distance>(
        owner, rows, [](const Row& row) { return row.key; },
        [&](const Row& row) { inserted += owner.template insert<Strategy>(row.key, row.value); });
    return inserted;
}
struct ReadResult {
    std::uint64_t hits = 0, keys = 0, values = 0;
    bool operator==(const ReadResult&) const = default;
};
template <unsigned Distance, class Owner>
ReadResult read_batch(const Owner& owner, std::span<const std::uint64_t> requests) {
    ReadResult result;
    independent<Distance>(
        owner, requests, [](auto key) { return key; },
        [&](auto key) {
            const bool hit = owner.find(key, [&](const Value& v) { result.values += number(v); });
            result.hits += hit;
            result.keys += hit ? key : 0;
        });
    return result;
}

template <unsigned Distance, class Owner> void consumer(bool collide) {
    // 64 possible distinct keys, not the number of offered operations. A role
    // with a smaller proven domain should choose that W instead of truncating.
    const typename Owner::mix_type mix{seed};
    const auto key = [&](unsigned i) {
        return collide ? mix.recover(0, i) : std::uint64_t(i) * 0x9e3779b97f4a7c15ULL;
    };
    Owner owner(seed);
    std::array<std::uint64_t, 64> expected{};
    std::array<bool, 64> present{};
    auto check = [&] {
        unsigned population = 0;
        for (unsigned i = 0; i < 64; ++i) {
            unsigned calls = 0;
            const bool found = owner.find(key(i), [&](const Value& v) {
                ++calls;
                ht::require(number(v) == expected[i], "complete value oracle");
            });
            ht::require(found == present[i] && calls == unsigned(present[i]), "membership oracle");
            population += present[i];
        }
        ht::require(owner.size() == population, "cardinality oracle");
        owner.validate();
    };
    std::vector<Row> rows;
    for (unsigned i = 0; i < 32; ++i) {
        rows.push_back({key(i), value(i + 1)});
        expected[i] = i + 1;
        present[i] = true;
    }
    ht::require(write_batch<Distance, ht::write_strategy::mixed>(owner, rows) == 32,
                "construction");
    check();
    rows.clear();
    for (unsigned i = 0; i < 64; ++i) {
        rows.push_back({key(i), value(1000 + i)});
        expected[i] = 1000 + i;
        present[i] = true;
    }
    ht::require(write_batch<Distance, ht::write_strategy::mixed>(owner, rows) == 32,
                "half-new upsert");
    check();
    for (unsigned i = 0; i < 64; ++i) {
        rows[i].value = value(2000 + i);
        expected[i] = 2000 + i;
    }
    ht::require(write_batch<Distance, ht::write_strategy::existing>(owner, rows) == 0,
                "precomputed existing replacement");
    check();

    // The existing strategy also handles missing keys; it is not an unchecked
    // caller assertion. Removing/refilling a spilled key exercises Swiss paths.
    ht::require(owner.erase(key(63)), "erase before existing-strategy insertion");
    ht::require(write_batch<Distance, ht::write_strategy::existing>(
                    owner, std::span<const Row>(rows).last(1)) == 1,
                "existing strategy miss");
    check();
    if (collide)
        ht::require(owner.spill_size() >= 56, "deliberate overflow fixture");

    std::array<std::uint64_t, 65> requests;
    for (unsigned i = 0; i < requests.size(); ++i)
        requests[i] = key(i % 2 ? i % 64 : 64 + i); // Known hits and absent identities.
    for (unsigned count : {0u, 1u, 7u, 31u, 32u, 33u, 64u, 65u}) {
        ReadResult expected_read;
        for (unsigned q = 0; q < count; ++q)
            // Independent identity oracle, not another call to find().
            for (unsigned i = 0; i < 64; ++i)
                if (requests[q] == key(i)) {
                    ++expected_read.hits;
                    expected_read.keys += key(i);
                    expected_read.values += expected[i];
                }
        ht::require(read_batch<Distance>(owner, std::span<const std::uint64_t>(requests).first(count)) ==
                        expected_read,
                    "mixed read, empty and distance-tail oracle");
    }
    ht::require(write_batch<Distance, ht::write_strategy::mixed>(owner, {}) == 0,
                "empty write batch");
    // Duplicate-heavy coalescing uses RMW. Its key/value dependency remains in
    // the ordinary call; it is not made independent by wrapping it in stages.
    Owner scratch(seed);
    std::array<std::uint64_t, 64> totals{};
    for (unsigned i = 0; i < 256; ++i) {
        const auto rank = i % 64, amount = 1 + i % 7;
        totals[rank] += amount;
        scratch.modify_or_insert(key(rank), value(0), [&](Value& v, bool) {
            v = value(number(v) + amount);
        });
    }
    ht::require(scratch.size() == 64, "distinct coalescing population");
    for (unsigned i = 0; i < 64; ++i)
        ht::require(scratch.find(key(i), [&](const Value& v) {
                        ht::require(number(v) == totals[i], "coalesced value oracle");
                    }),
                    "coalesced key");
    scratch.validate();
}
template <unsigned E, ht::search_kernel Kernel> void run() {
    using Owner = ht::table<64, plan.b, 8, E, Kernel>;
    consumer<0, Owner>(false);
    consumer<32, Owner>(false);
    consumer<0, Owner>(true);
    consumer<32, Owner>(true);
}
int main(int argc, char** argv) {
    try {
        if (argc == 1)
            run<64 - plan.b, ht::search_kernel::generic>();
        else {
            ht::require(argc == 3, "optional arguments: CPU ISA, compatible with build and machine");
            ht::bind_measured_encoding<64 - plan.b>(
                8, argv[1], argv[2], [](auto encoding, auto kernel, ht::encoding_choice) {
                    run<decltype(encoding)::value, decltype(kernel)::value>();
                });
        }
        std::cout << "population sizing, direct/PF32 reads and writes, overflow, tails and RMW passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
