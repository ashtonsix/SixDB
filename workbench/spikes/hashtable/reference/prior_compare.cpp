#include "baseline.h"
#include "prior_art/unified_quotient.h"

#include <array>
#include <bitset>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t cardinality = 4096;
constexpr std::size_t query_count = 262144;
constexpr std::size_t domain_size = 65536;
using key_type = std::uint16_t;
using clock_type = std::chrono::steady_clock;

void require(bool condition, const char* reason) {
    if (!condition) {
        std::fprintf(stderr, "prior comparison failed: %s\n", reason);
        std::abort();
    }
}

template <class V> class abseil_typed_map {
    using ordinary_map = absl::flat_hash_map<key_type, V>;
    using allocator = hashtable_spike::counting_allocator<std::pair<const key_type, V>>;
    using table = absl::flat_hash_map<key_type, V, typename ordinary_map::hasher,
                                     typename ordinary_map::key_equal, allocator>;
    hashtable_spike::allocation_counters counters_;
    table table_;

  public:
    static constexpr std::size_t ordinary_object_bytes = sizeof(ordinary_map);
    abseil_typed_map()
        : table_(0, typename ordinary_map::hasher{}, typename ordinary_map::key_equal{},
                 allocator(counters_)) {}
    bool insert_or_assign(key_type key, V value) {
        return table_.insert_or_assign(key, value).second;
    }
    const V* find(key_type key) const {
        const auto found = table_.find(key);
        return found == table_.end() ? nullptr : &found->second;
    }
    bool erase(key_type key) { return table_.erase(key) != 0; }
    void reserve(std::size_t size) { table_.reserve(size); }
    std::size_t size() const { return table_.size(); }
    std::size_t capacity() const { return table_.capacity(); }
    const auto& allocation_stats() const { return counters_; }
};

std::uint64_t value_mix(std::uint64_t value) {
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

template <class V> V pattern(std::uint64_t index) {
    constexpr V maximum = std::numeric_limits<V>::max();
    switch (index & 7u) {
    case 0: return 0;
    case 1: return maximum;
    case 2: return V{1} << (sizeof(V) * 8 - 1);
    case 3: return V{1};
    case 4: return static_cast<V>(UINT64_C(0xaaaaaaaaaaaaaaaa));
    case 5: return static_cast<V>(UINT64_C(0x5555555555555555));
    default: return static_cast<V>(value_mix(index));
    }
}

template <class V> struct oracle {
    std::array<V, domain_size> values{};
    std::bitset<domain_size> present{};
    void set(key_type key, V value) { values[key] = value; present.set(key); }
    void erase(key_type key) { present.reset(key); }
};

template <class Map, class V> void audit(const Map& map, const oracle<V>& expected) {
    require(map.size() == expected.present.count(), "size disagrees with independent oracle");
    for (std::size_t i = 0; i < domain_size; ++i) {
        const V* found = map.find(static_cast<key_type>(i));
        require((found != nullptr) == expected.present[i], "full-domain membership");
        if (found) require(*found == expected.values[i], "full-width value");
    }
}

template <class Prior> void check_case(std::uint32_t buckets) {
    using V = typename Prior::value_type;
    Prior prior(buckets, 713);
    abseil_typed_map<V> abseil;
    abseil.reserve(domain_size);
    oracle<V> expected;
    std::vector<key_type> order(domain_size);
    std::iota(order.begin(), order.end(), key_type{});
    std::mt19937 random(0x947723u);
    std::shuffle(order.begin(), order.end(), random);
    audit(prior, expected);
    audit(abseil, expected);

    // Every key pattern, including 0 and 65535; every uint16 value pattern.
    // Filling the entire domain also exercises overflow promotion and backfill.
    for (key_type key : order) {
        V value = static_cast<V>(value_mix(key));
        if constexpr (sizeof(V) == 2) value = key;
        require(prior.insert_or_assign(key, value), "historical full-domain insert");
        require(abseil.insert_or_assign(key, value), "Abseil full-domain insert");
        expected.set(key, value);
    }
    audit(prior, expected);
    audit(abseil, expected);
    for (key_type key : order) {
        const V value = pattern<V>(key);
        require(!prior.insert_or_assign(key, value), "historical replacement status");
        require(!abseil.insert_or_assign(key, value), "Abseil replacement status");
        expected.set(key, value);
    }
    audit(prior, expected);
    audit(abseil, expected);
    for (std::size_t i = 0; i < order.size(); ++i) {
        const key_type key = order[i];
        require(prior.erase(key) && abseil.erase(key), "erase populated key");
        require(!prior.erase(key) && !abseil.erase(key), "erase absent key");
        expected.erase(key);
        if (i % 4096 == 4095) { audit(prior, expected); audit(abseil, expected); }
    }
    audit(prior, expected);
    audit(abseil, expected);

    // Randomized reuse after draining promoted overflow; checks are never assert().
    for (unsigned step = 0; step < 100000; ++step) {
        const key_type key = static_cast<key_type>(random());
        if (step % 3 != 0) {
            const V value = pattern<V>(random());
            const bool fresh = !expected.present[key];
            require(prior.insert_or_assign(key, value) == fresh, "historical random insert status");
            require(abseil.insert_or_assign(key, value) == fresh, "Abseil random insert status");
            expected.set(key, value);
        } else {
            const bool present = expected.present[key];
            require(prior.erase(key) == present, "historical random erase status");
            require(abseil.erase(key) == present, "Abseil random erase status");
            expected.erase(key);
        }
        if (step % 4096 == 4095) { audit(prior, expected); audit(abseil, expected); }
    }
    audit(prior, expected);
    audit(abseil, expected);
}

void check() {
    check_case<unified_quotient::Byte16x16>(512);
    check_case<unified_quotient::Byte32>(1024);
    check_case<unified_quotient::Byte64>(2048);
    std::puts("checks passed: three unchanged historical maps and typed Abseil; full key domain, "
              "all uint16 values, zero/max/high-bit/alternating/full-width values, replacements, "
              "overflow promotion, swap/backfill deletion, drain and randomized reuse");
}

struct data {
    std::vector<key_type> inserted, absent, hits, misses, mixed, alternate;
    data() {
        std::vector<key_type> domain(domain_size);
        std::iota(domain.begin(), domain.end(), key_type{});
        // Preserve the original benchmark's exact key generator.
        std::mt19937 random(0x1600b17u + cardinality);
        std::shuffle(domain.begin(), domain.end(), random);
        inserted.assign(domain.begin(), domain.begin() + cardinality);
        absent.assign(domain.begin() + cardinality, domain.end());
        alternate.assign(absent.begin(), absent.begin() + cardinality);
        hits.resize(query_count); misses.resize(query_count); mixed.resize(query_count);
        for (std::size_t i = 0; i < query_count; ++i) {
            hits[i] = inserted[(i * 4051u + 17u) % cardinality];
            misses[i] = absent[(i * 7919u + 31u) % absent.size()];
            mixed[i] = i % 10 == 0 ? misses[i] : hits[i];
        }
    }
};

template <class V> std::vector<V> cycle_values(const std::vector<key_type>& keys) {
    std::vector<V> values(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        // A single permutation cycle. The returned low 16 value bits are the
        // next key; higher value bits remain populated for wider typed maps.
        const std::uint64_t upper = value_mix(keys[i]) & ~UINT64_C(65535);
        values[i] = static_cast<V>(upper | keys[(i + 1) % keys.size()]);
    }
    return values;
}

struct sample { double ns; std::uint64_t checksum; };

template <class F> sample timed(std::size_t operations, F&& operation) {
    const auto start = clock_type::now();
    std::uint64_t checksum = operation();
    asm volatile("" : "+r"(checksum) :: "memory");
    const auto end = clock_type::now();
    return {std::chrono::duration<double, std::nano>(end - start).count() / double(operations),
            checksum};
}

template <class Map> std::uint64_t independent(const Map& map,
                                              const std::vector<key_type>& keys,
                                              unsigned rounds) {
    std::uint64_t sum = 0;
    for (unsigned round = 0; round < rounds; ++round)
        for (key_type key : keys) {
            const auto* found = map.find(key);
            sum += found ? static_cast<std::uint64_t>(*found) : UINT64_C(1);
        }
    return sum;
}

template <class Map> std::uint64_t serial(const Map& map, key_type first,
                                         std::size_t operations) {
    key_type key = first;
    for (std::size_t i = 0; i < operations; ++i) {
        const auto* found = map.find(key);
        key = found ? static_cast<key_type>(*found) : key_type{};
        asm volatile("" : "+r"(key) : "r"(found));
    }
    return key;
}

template <class Map, class V> std::uint64_t update(Map& map,
                                                  const std::vector<key_type>& keys,
                                                  const std::vector<V>& values,
                                                  unsigned rounds) {
    std::uint64_t inserted = 0;
    for (unsigned round = 0; round < rounds; ++round)
        for (std::size_t i = 0; i < keys.size(); ++i)
            inserted += map.insert_or_assign(keys[i], values[i]);
    return inserted;
}

template <class Map, class V> std::uint64_t churn(Map& map, const data& input,
                                                const std::vector<V>& first_values,
                                                const std::vector<V>& second_values,
                                                unsigned cycles) {
    std::uint64_t changed = 0;
    for (unsigned cycle = 0; cycle < cycles; ++cycle) {
        for (std::size_t i = 0; i < cardinality; ++i) {
            changed += map.erase(input.inserted[i]);
            changed += map.insert_or_assign(input.alternate[i], second_values[i]);
        }
        for (std::size_t i = 0; i < cardinality; ++i) {
            changed += map.erase(input.alternate[i]);
            changed += map.insert_or_assign(input.inserted[i], first_values[i]);
        }
    }
    return changed;
}

template <class Prior> void compare(const char* label, std::uint32_t buckets,
                                    unsigned rounds, unsigned repetitions) {
    using V = typename Prior::value_type;
    const data input;
    const auto first_values = cycle_values<V>(input.inserted);
    const auto second_values = cycle_values<V>(input.alternate);
    std::vector<V> updates(query_count);
    for (std::size_t i = 0; i < query_count; ++i) updates[i] = pattern<V>(i);
    const std::size_t operations = query_count * rounds;
    const unsigned churn_cycles = static_cast<unsigned>(operations / (2 * cardinality));
    require(operations == std::size_t(churn_cycles) * 2 * cardinality, "whole churn cycles");

    for (unsigned repetition = 0; repetition < repetitions; ++repetition) {
        Prior prior(buckets, 713);
        abseil_typed_map<V> abseil;
        abseil.reserve(cardinality);
        oracle<V> expected;
        auto fill = [&](auto& map) {
            for (std::size_t i = 0; i < cardinality; ++i)
                require(map.insert_or_assign(input.inserted[i], first_values[i]), "benchmark fill");
        };
        if (repetition % 2) { fill(abseil); fill(prior); }
        else { fill(prior); fill(abseil); }
        for (std::size_t i = 0; i < cardinality; ++i)
            expected.set(input.inserted[i], first_values[i]);
        audit(prior, expected); audit(abseil, expected);

        auto emit = [&](bool historical, const char* workload, sample result) {
            const auto statistics = prior.statistics();
            const auto& allocation = abseil.allocation_stats();
            std::printf("16,%zu,%zu,%u,%u,%s,%s,%u,%u,%.6f,%llu,%s,%zu,%zu,%zu,%zu,%u\n",
                sizeof(V) * 8, cardinality, buckets, Prior::slots,
                historical ? label : "abseil", workload, repetition, rounds,
                result.ns, static_cast<unsigned long long>(result.checksum),
                historical ? "modeled_live_payload" : "allocator_requested",
                historical ? std::size_t(statistics.modeled_bytes) : allocation.current_bytes,
                historical ? std::size_t(statistics.resident_modeled_bytes) : allocation.peak_bytes,
                historical ? sizeof(Prior) : abseil_typed_map<V>::ordinary_object_bytes,
                abseil.capacity(), historical ? statistics.overflow_entries : 0);
        };
        auto expected_read_sum = [&](const std::vector<key_type>& keys) {
            std::uint64_t sum = 0;
            for (key_type key : keys)
                sum += expected.present[key] ? static_cast<std::uint64_t>(expected.values[key]) : 1;
            return sum * rounds;
        };
        auto pair = [&](const char* workload, std::uint64_t expected_checksum, auto&& operation) {
            sample p{}, a{};
            auto run = [&](auto& map) {
                // Identical full-workload warmup immediately before each timed arm.
                auto warm = operation(map);
                asm volatile("" : "+r"(warm) :: "memory");
                return timed(operations, [&] { return operation(map); });
            };
            if (repetition % 2) { a = run(abseil); p = run(prior); }
            else { p = run(prior); a = run(abseil); }
            require(p.checksum == expected_checksum && a.checksum == expected_checksum,
                    "operation checksum differs from independent oracle");
            emit(true, workload, p); emit(false, workload, a);
            std::fflush(stdout);
        };
        pair("serial_hit", input.inserted[0],
             [&](auto& map) { return serial(map, input.inserted[0], operations); });
        pair("independent_hit", expected_read_sum(input.hits),
             [&](auto& map) { return independent(map, input.hits, rounds); });
        pair("independent_miss", expected_read_sum(input.misses),
             [&](auto& map) { return independent(map, input.misses, rounds); });
        pair("independent_90hit", expected_read_sum(input.mixed),
             [&](auto& map) { return independent(map, input.mixed, rounds); });
        pair("update", 0, [&](auto& map) { return update(map, input.hits, updates, rounds); });
        for (std::size_t i = 0; i < query_count; ++i) expected.set(input.hits[i], updates[i]);
        audit(prior, expected); audit(abseil, expected);
        pair("erase_insert_pair", 2 * operations, [&](auto& map) {
            return churn(map, input, first_values, second_values, churn_cycles);
        });
        for (std::size_t i = 0; i < cardinality; ++i)
            expected.set(input.inserted[i], first_values[i]);
        audit(prior, expected); audit(abseil, expected);
        pair("post_churn_hit", expected_read_sum(input.hits),
             [&](auto& map) { return independent(map, input.hits, rounds); });
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "check") { check(); return 0; }
    if (argc > 3) {
        std::fprintf(stderr, "usage: hashtable_prior_compare check | [ROUNDS=4] [REPETITIONS=7]\n");
        return 2;
    }
    const unsigned rounds = argc >= 2 ? static_cast<unsigned>(std::stoul(argv[1])) : 4;
    const unsigned repetitions = argc >= 3 ? static_cast<unsigned>(std::stoul(argv[2])) : 7;
    require(rounds > 0 && repetitions > 0, "positive rounds and repetitions");
    std::fprintf(stderr, "Abseil %s; verbatim unified_quotient dc19d09c3bd1bfb36511c5afb10200826a2e3b08a4a492fac33f5d0b5a98109b; "
                         "fixed uint16 key domain, no cache-residence claim; erase_insert_pair is one erase plus one insert; "
                         "historical memory is modeled, not allocated bytes\n", hashtable_spike::abseil_revision);
    std::puts("key_bits,value_bits,n,buckets,slots,table,operation,rep,rounds,ns_per_operation,checksum,"
              "memory_kind,footprint_bytes,resident_model_or_allocator_peak_bytes,ordinary_object_bytes,"
              "abseil_capacity,overflow_entries");
    compare<unified_quotient::Byte16x16>("prior_byte16x16", 512, rounds, repetitions);
    compare<unified_quotient::Byte32>("prior_byte32", 1024, rounds, repetitions);
    compare<unified_quotient::Byte64>("prior_byte64", 2048, rounds, repetitions);
}
