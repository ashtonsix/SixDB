#include "baseline.h"
#include "prior_art/unified_quotient.h"
#include "support.h"

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
#include <tuple>
#include <vector>

// Bounded bridge to the reproduced historical result, not a general benchmark.
// The historical header is unchanged. In particular its fixed affine mixer,
// backfill policy and modeled (not allocator-counted) footprint remain visible.
namespace {

using word = std::uint64_t;
using key_type = std::uint16_t;
constexpr std::size_t domain_size = 65536;
constexpr std::size_t cardinality = 4096;
using clock_type = std::chrono::steady_clock;

void require(bool condition, const char* reason) {
    if (!condition) {
        std::fprintf(stderr, "historical bridge failed: %s\n", reason);
        std::abort();
    }
}

template <unsigned Bits> using value_type = std::conditional_t<Bits == 16, std::uint16_t, word>;
template <unsigned Bits> constexpr unsigned slots = Bits == 16 ? 16 : 6;
template <unsigned Bits> constexpr unsigned prior_buckets = Bits == 16 ? 512 : 2048;
// Current 80% reserve rounding gives exactly the historical primary bucket count.
// V64 is intentionally over-reserved: this matches the old family, not a preset.
template <unsigned Bits> constexpr std::size_t current_reserve = Bits == 16 ? 4096 : 8192;
template <unsigned Bits> using prior_type =
    std::conditional_t<Bits == 16, unified_quotient::Byte16x16, unified_quotient::Byte64>;

// Every returned value bit participates in the dependent next-key calculation.
// Encoding the next key in low16 alone lets the V64 historical load narrow to
// 16 bits. The fold below deliberately charges the same dependency to every arm.
template <unsigned Bits> [[gnu::always_inline]] key_type next_key(word value) {
    if constexpr (Bits == 64) {
        value ^= value >> 32;
        value ^= value >> 16;
    }
    return static_cast<key_type>(value);
}

template <unsigned Bits> value_type<Bits> cycle_value(key_type key, key_type next) {
    if constexpr (Bits == 16) return next;
    const word upper = hashtable_spike::mix(key + UINT64_C(0x6732ba904df01000)) & ~word{65535};
    return upper | key_type(next ^ next_key<64>(upper));
}

template <unsigned Bits> value_type<Bits> boundary_value(std::size_t index) {
    using V = value_type<Bits>;
    switch (index & 7) {
    case 0: return 0;
    case 1: return std::numeric_limits<V>::max();
    case 2: return V{1} << (Bits - 1);
    case 3: return 1;
    case 4: return static_cast<V>(UINT64_C(0xaaaaaaaaaaaaaaaa));
    case 5: return static_cast<V>(UINT64_C(0x5555555555555555));
    default: return static_cast<V>(hashtable_spike::mix(index));
    }
}

struct footprint {
    const char* kind;
    // Unknown values use -1, not an invented zero. Historical dynamic controls
    // and allocator capacity are not exposed by the unchanged implementation.
    std::int64_t payload, backing_or_resident_model, known_controls;
    std::int64_t allocator_peak, pool_mapped, pool_metadata;
    std::int64_t primary, secondary, overflow, capacity;
    unsigned primary_bucket_count, primary_stride, secondary_stride;
};

template <unsigned Bits> class historical_map {
    prior_type<Bits> map_;

  public:
    static constexpr const char* label = "historical_fixed_affine";
    explicit historical_map(word seed) : map_(prior_buckets<Bits>, static_cast<key_type>(seed)) {}
    [[gnu::always_inline]] bool get(key_type key, word& value) const {
        const auto* found = map_.find(key);
        if (!found) return false;
        value = *found;
        return true;
    }
    [[gnu::always_inline]] bool insert_or_assign(key_type key, value_type<Bits> value) {
        return map_.insert_or_assign(key, value);
    }
    [[gnu::always_inline]] bool erase(key_type key) { return map_.erase(key); }
    std::size_t size() const { return map_.size(); }
    void structure_check() const {}
    footprint memory() const {
        const auto s = map_.statistics();
        return {"modeled_payload_controls_incomplete", std::int64_t(s.modeled_bytes),
                std::int64_t(s.resident_modeled_bytes), sizeof(map_), -1, -1, -1,
                s.primary_entries, 0, s.overflow_entries, -1, s.buckets, 64, 0};
    }
};

template <unsigned Bits> class current_map {
    using table = hashtable_spike::basic_map<false, 3, 16, Bits, false, slots<Bits>>;
    table map_;

  public:
    static constexpr const char* label = "current_packed_strong_mixer";
    current_map(hashtable_spike::oa::Pool& pool, word seed)
        : map_(pool, current_reserve<Bits>, slots<Bits>, false, 1, seed) {
        require((1u << map_.primary_bits()) == prior_buckets<Bits>, "matched primary bucket count");
    }
    [[gnu::always_inline]] bool get(key_type key, word& value) const { return map_.get(key, value); }
    [[gnu::always_inline]] bool insert_or_assign(key_type key, value_type<Bits> value) {
        return map_.insert_or_assign(key, value);
    }
    [[gnu::always_inline]] bool erase(key_type key) { return map_.erase(key); }
    std::size_t size() const { return map_.size(); }
    void structure_check() const { map_.audit(); }
    footprint memory() const {
        const auto m = map_.memory();
        const auto c = map_.counts();
        return {"allocator_requested_plus_controls", std::int64_t(m.payload), std::int64_t(m.capacity),
                std::int64_t(m.catalog + m.plans), -1, std::int64_t(m.pool.mapped_bytes),
                std::int64_t(m.pool.metadata_bytes), std::int64_t(c.primary), std::int64_t(c.secondary),
                std::int64_t(c.overflow), -1, 1u << map_.primary_bits(), map_.primary_stride(),
                map_.secondary_stride()};
    }
};

template <unsigned Bits> class abseil_map {
    using table = hashtable_spike::basic_abseil_map<key_type, value_type<Bits>>;
    table map_;

  public:
    static constexpr const char* label = "ordinary_abseil";
    abseil_map() { map_.reserve(cardinality); }
    [[gnu::always_inline]] bool get(key_type key, word& value) const { return map_.get(key, value); }
    [[gnu::always_inline]] bool insert_or_assign(key_type key, value_type<Bits> value) {
        return map_.insert_or_assign(key, value);
    }
    [[gnu::always_inline]] bool erase(key_type key) { return map_.erase(key); }
    std::size_t size() const { return map_.size(); }
    void structure_check() const {}
    footprint memory() const {
        const auto& a = map_.allocation_stats();
        return {"allocator_requested_plus_controls", std::int64_t(a.current_bytes), -1,
                table::ordinary_object_bytes, std::int64_t(a.peak_bytes), -1, -1,
                -1, -1, -1, std::int64_t(map_.capacity()), 0, 0, 0};
    }
};

template <unsigned Bits> struct oracle {
    std::array<value_type<Bits>, domain_size> values{};
    std::bitset<domain_size> present{};
    void set(key_type key, value_type<Bits> value) { values[key] = value; present.set(key); }
    void erase(key_type key) { present.reset(key); }
    bool get(key_type key, word& value) const {
        if (!present[key]) return false;
        value = values[key];
        return true;
    }
};

template <unsigned Bits, class Map> void audit(const Map& map, const oracle<Bits>& expected) {
    require(map.size() == expected.present.count(), "independent oracle cardinality");
    for (std::size_t i = 0; i < domain_size; ++i) {
        word value = UINT64_C(0x813579abfedc0246);
        const bool found = map.get(static_cast<key_type>(i), value);
        require(found == expected.present[i], "full-domain membership");
        require(value == (found ? word(expected.values[i]) : UINT64_C(0x813579abfedc0246)),
                "full-width value or unchanged miss output");
    }
    map.structure_check();
}

template <unsigned Bits> struct trace {
    std::vector<key_type> first, alternate, misses;
    std::vector<value_type<Bits>> first_values, alternate_values;
    explicit trace(unsigned repetition) {
        std::vector<key_type> domain(domain_size);
        std::iota(domain.begin(), domain.end(), key_type{});
        // Repetition zero retains the original key generator. Each subsequent
        // paired trace changes its shuffle; every arm sees identical inputs.
        std::mt19937 random(0x1600b17u + cardinality + repetition * 0x73821u);
        std::shuffle(domain.begin(), domain.end(), random);
        first.assign(domain.begin(), domain.begin() + cardinality);
        alternate.assign(domain.begin() + cardinality, domain.begin() + 2 * cardinality);
        misses.assign(domain.begin() + 2 * cardinality, domain.begin() + 3 * cardinality);
        first_values.resize(cardinality);
        alternate_values.resize(cardinality);
        for (std::size_t i = 0; i < cardinality; ++i) {
            first_values[i] = cycle_value<Bits>(first[i], first[(i + 1) % cardinality]);
            alternate_values[i] = cycle_value<Bits>(alternate[i], alternate[(i + 1) % cardinality]);
        }
    }
    std::size_t bytes() const {
        return (first.size() + alternate.size() + misses.size()) * sizeof(key_type)
             + (first_values.size() + alternate_values.size()) * sizeof(value_type<Bits>);
    }
};

template <unsigned Bits, class Map> void fill(Map& map, const trace<Bits>& input) {
    for (std::size_t i = 0; i < cardinality; ++i)
        require(map.insert_or_assign(input.first[i], input.first_values[i]), "fresh fill insertion");
}

template <class Map> [[gnu::noinline]] word independent(const Map& map,
                                                     const std::vector<key_type>& keys,
                                                     unsigned passes) {
    word sum = 0;
    for (unsigned pass = 0; pass < passes; ++pass)
        for (key_type key : keys) {
            word value = 0;
            const bool found = map.get(key, value);
            sum += value + word(found);
        }
    return sum;
}

template <unsigned Bits, class Map> [[gnu::noinline]] word serial(const Map& map, key_type first,
                                                               std::size_t operations) {
    key_type key = first;
    word sum = 0;
    for (std::size_t i = 0; i < operations; ++i) {
        word value = 0;
        const bool found = map.get(key, value);
        sum += value + word(found);
        key = next_key<Bits>(value);
        // Keep the recurrence observable, without selecting an architecture-
        // specific load or concealing the full-value fold from code generation.
        asm volatile("" : "+r"(key) : "r"(found));
    }
    return sum ^ key;
}

template <unsigned Bits, class Map> [[gnu::noinline]] word changed_updates(
    Map& map, const trace<Bits>& input, unsigned passes) {
    word inserted = 0;
    for (unsigned pass = 0; pass < passes; ++pass) {
        const auto change = pass % 2 ? value_type<Bits>{0} : std::numeric_limits<value_type<Bits>>::max();
        for (std::size_t i = 0; i < cardinality; ++i)
            inserted += map.insert_or_assign(input.first[i], input.first_values[i] ^ change);
    }
    return inserted;
}

template <unsigned Bits, class Map> [[gnu::noinline]] word turnover(
    Map& map, const trace<Bits>& input, unsigned passes) {
    word changed = 0;
    for (unsigned pass = 0; pass < passes; ++pass) {
        const auto& before = pass % 2 ? input.alternate : input.first;
        const auto& after = pass % 2 ? input.first : input.alternate;
        const auto& values = pass % 2 ? input.first_values : input.alternate_values;
        for (std::size_t i = 0; i < cardinality; ++i) {
            changed += map.erase(before[i]);
            changed += map.insert_or_assign(after[i], values[i]);
        }
    }
    return changed;
}

struct sample { double ns; word checksum; };
template <class F> sample timed(std::size_t operations, F&& operation) {
    const auto start = clock_type::now();
    word sum = operation();
    asm volatile("" : "+r"(sum) :: "memory");
    const auto end = clock_type::now();
    return {std::chrono::duration<double, std::nano>(end - start).count() / double(operations), sum};
}

// All six arm orders repeat over repetitions, with a different starting order
// for each operation. This balances position and adjacency, not just first/last.
constexpr std::array<std::array<unsigned, 3>, 6> arm_orders{{
    {{0, 1, 2}}, {{1, 2, 0}}, {{2, 0, 1}}, {{2, 1, 0}}, {{1, 0, 2}}, {{0, 2, 1}}
}};
template <class Tuple, class F> void ordered(Tuple& maps, unsigned order, F&& operation) {
    unsigned position = 0;
    for (unsigned arm : arm_orders[order % arm_orders.size()]) {
        switch (arm) {
        case 0: operation(std::get<0>(maps), position); break;
        case 1: operation(std::get<1>(maps), position); break;
        case 2: operation(std::get<2>(maps), position); break;
        }
        ++position;
    }
}

template <unsigned Bits> void compare(unsigned passes, unsigned repetitions) {
    for (unsigned rep = 0; rep < repetitions; ++rep) {
        const trace<Bits> input(rep);
        const word seed = hashtable_spike::mix(rep + 773);
        hashtable_spike::pool_owner pool;
        historical_map<Bits> prior(713);
        current_map<Bits> current(*pool.pool, seed);
        abseil_map<Bits> abseil;
        auto maps = std::tie(prior, current, abseil);
        oracle<Bits> expected;
        for (std::size_t i = 0; i < cardinality; ++i) expected.set(input.first[i], input.first_values[i]);
        ordered(maps, rep, [&](auto& map, unsigned) { fill(map, input); audit(map, expected); });

        const std::size_t operations = cardinality * std::size_t(passes);
        unsigned workload_index = 0;
        unsigned cumulative_turnovers = 0;
        auto run = [&](const char* workload, word expected_checksum, bool mutation,
                       unsigned timed_turnovers, auto&& operation) {
            const unsigned this_workload = workload_index++;
            ordered(maps, rep + this_workload, [&](auto& map, unsigned position) {
                using Map = std::remove_reference_t<decltype(map)>;
                const auto before = map.memory();
                // Warm the complete workload separately for each arm immediately
                // before timing. Metadata inspection occurs before warmup and
                // after timing, never between them. Mutation warmup is charged
                // in cumulative_turnovers and also changes physical tier state.
                auto warm = operation(map);
                asm volatile("" : "+r"(warm) :: "memory");
                require(warm == expected_checksum, "warmup independent checksum");
                const auto result = timed(operations, [&] { return operation(map); });
                const auto after = map.memory();
                require(result.checksum == expected_checksum, "timed independent checksum");
                if (mutation) audit(map, expected);
                std::printf("16,%u,%zu,%s,%s,%u,%u,%u,%zu,%.6f,%llu,%zu,%llu,%u,%u,%u,%u,%u,%u,%s,"
                            "%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%u,%u\n",
                    Bits, cardinality, Map::label, workload, rep, position, passes, operations,
                    result.ns, static_cast<unsigned long long>(result.checksum), input.bytes(),
                    static_cast<unsigned long long>(std::string_view(Map::label) == historical_map<Bits>::label ? 713 :
                        std::string_view(Map::label) == current_map<Bits>::label ? seed : 0),
                    slots<Bits>, prior_buckets<Bits>, unsigned(current_reserve<Bits>),
                    after.primary_bucket_count, after.primary_stride, after.secondary_stride, after.kind,
                    static_cast<long long>(after.payload), static_cast<long long>(after.backing_or_resident_model),
                    static_cast<long long>(after.known_controls), static_cast<long long>(after.allocator_peak),
                    static_cast<long long>(after.pool_mapped), static_cast<long long>(after.pool_metadata),
                    static_cast<long long>(after.capacity), static_cast<long long>(before.primary),
                    static_cast<long long>(before.secondary), static_cast<long long>(before.overflow),
                    static_cast<long long>(after.primary), static_cast<long long>(after.secondary),
                    static_cast<long long>(after.overflow), timed_turnovers,
                    cumulative_turnovers + 2 * timed_turnovers);
                std::fflush(stdout);
            });
            cumulative_turnovers += 2 * timed_turnovers;
        };
        run("serial_full_value_hit", serial<Bits>(expected, input.first[0], operations), false, 0,
            [&](auto& map) { return serial<Bits>(map, input.first[0], operations); });
        run("independent_hit", independent(expected, input.first, passes), false, 0,
            [&](auto& map) { return independent(map, input.first, passes); });
        run("independent_miss", 0, false, 0,
            [&](auto& map) { return independent(map, input.misses, passes); });
        run("changed_value_update", 0, true, 0,
            [&](auto& map) { return changed_updates(map, input, passes); });
        run("alternating_erase_insert_pair", 2 * operations, true, passes,
            [&](auto& map) { return turnover(map, input, passes); });
        run("post_turnover_hit", independent(expected, input.first, passes), false, 0,
            [&](auto& map) { return independent(map, input.first, passes); });
        ordered(maps, rep, [&](auto& map, unsigned) { audit(map, expected); });
    }
}

template <unsigned Bits> void check_case() {
    hashtable_spike::pool_owner pool;
    historical_map<Bits> prior(713);
    current_map<Bits> current(*pool.pool, UINT64_C(0x128aac7418873291));
    abseil_map<Bits> abseil;
    auto maps = std::tie(prior, current, abseil);
    oracle<Bits> expected;
    std::vector<key_type> keys(domain_size);
    std::iota(keys.begin(), keys.end(), key_type{});
    std::mt19937 random(0x853720);
    std::shuffle(keys.begin(), keys.end(), random);
    ordered(maps, 0, [&](auto& map, unsigned) { audit(map, expected); });
    for (key_type key : keys) {
        // Exhaust all uint16 values, including 0 and 65535; V64 gives independent
        // full-width boundary and alternating patterns, not just encoded links.
        const auto value = Bits == 16 ? value_type<Bits>(key) : boundary_value<Bits>(key);
        ordered(maps, key, [&](auto& map, unsigned) {
            require(map.insert_or_assign(key, value), "full-domain insertion status");
        });
        expected.set(key, value);
    }
    ordered(maps, 1, [&](auto& map, unsigned) { audit(map, expected); });
    for (key_type key : keys) {
        const auto value = static_cast<value_type<Bits>>(~expected.values[key]);
        ordered(maps, key, [&](auto& map, unsigned) {
            require(!map.insert_or_assign(key, value), "changed replacement status");
        });
        expected.set(key, value);
    }
    ordered(maps, 2, [&](auto& map, unsigned) { audit(map, expected); });
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto key = keys[i];
        ordered(maps, unsigned(i), [&](auto& map, unsigned) {
            require(map.erase(key), "erase present");
            require(!map.erase(key), "erase already absent");
        });
        expected.erase(key);
        if (i % 8192 == 8191) ordered(maps, unsigned(i), [&](auto& map, unsigned) { audit(map, expected); });
    }
    const trace<Bits> input(0);
    for (std::size_t i = 0; i < cardinality; ++i) {
        expected.set(input.first[i], input.first_values[i]);
        require(next_key<Bits>(input.first_values[i]) == input.first[(i + 1) % cardinality],
                "entire value folds to next key");
    }
    // Trace operations are checked without timing. Each doubled mutation is a
    // complete warmup+timed equivalent and leaves the original logical contents.
    ordered(maps, 3, [&](auto& map, unsigned) {
        fill(map, input);
        require(serial<Bits>(map, input.first[0], 2 * cardinality) ==
                serial<Bits>(expected, input.first[0], 2 * cardinality), "full-value serial trace");
        require(independent(map, input.first, 2) == independent(expected, input.first, 2), "hit trace");
        require(independent(map, input.misses, 2) == 0, "miss trace");
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            require(changed_updates(map, input, 4) == 0, "changed update trace");
            audit(map, expected);
            require(turnover(map, input, 4) == 8 * cardinality, "alternating turnover trace");
            audit(map, expected);
        }
    });
    // Flipping any of V64's four 16-bit chunks must change the next key. This
    // independently catches the original narrow-load-friendly trace mistake.
    if constexpr (Bits == 64)
        for (unsigned bit = 0; bit < 64; ++bit)
            require(next_key<64>(word{1} << bit) == key_type(1u << (bit % 16)), "full-value dependency");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "check") {
        check_case<16>();
        check_case<64>();
        std::puts("historical bridge checks passed: all 65536 keys, all uint16 values, full-width "
                  "boundary values, replacements, tier overflow, erase/drain/reuse, full-value "
                  "serial dependency and matched read/update/alternating-turnover traces");
        return 0;
    }
    if (argc > 3) {
        std::fprintf(stderr, "usage: hashtable_historical_bridge check | [PASSES=64] [REPETITIONS=6]\n");
        return 2;
    }
    const unsigned passes = argc >= 2 ? static_cast<unsigned>(std::stoul(argv[1])) : 64;
    const unsigned repetitions = argc >= 3 ? static_cast<unsigned>(std::stoul(argv[2])) : 6;
    require(passes > 0 && passes % 2 == 0 && repetitions > 0, "positive even passes and positive repetitions");
    std::fprintf(stderr,
        "Abseil %s; unchanged unified_quotient dc19d09c3bd1bfb36511c5afb10200826a2e3b08a4a492fac33f5d0b5a98109b; "
        "no residence claim. Serial dependency includes XOR folding all value bits. Independent means "
        "ordinary unbatched loop. One erase_insert_pair is erase+insert. Alternating fixed disjoint banks "
        "measure repeated reuse, not stationary fresh arrivals. Mutation warmup repeats the complete workload; "
        "reported tier populations are before warmup and after timing. Historical payload/capacity is modeled, "
        "dynamic controls unmeasured; no RSS claim or comparable allocator peak. Current reserve deliberately "
        "matches historical primary bucket count; row strides differ. Historical affine versus current strong "
        "mixer is an end-to-end family comparison, not an isolated hash or codec attribution. Seed 0 for Abseil "
        "means unspecified internal ordinary hash seed. Six repetitions balance all six arm orders.\n",
        hashtable_spike::abseil_revision);
    std::puts("key_bits,value_bits,n,table,operation,rep,arm_position,passes,timed_operations,ns_per_operation,"
              "checksum,shared_trace_bytes,seed,historical_slots,historical_buckets,current_reserve,"
              "actual_primary_buckets,actual_primary_stride,actual_secondary_stride,memory_kind,payload_bytes,"
              "backing_capacity_or_resident_model_bytes,known_control_bytes,allocator_peak_bytes,pool_mapped_bytes,"
              "pool_metadata_bytes,abseil_capacity,before_warm_primary,before_warm_secondary,before_warm_overflow,"
              "after_timed_primary,after_timed_secondary,after_timed_overflow,timed_turnovers,total_turnovers_including_warmup");
    compare<16>(passes, repetitions);
    compare<64>(passes, repetitions);
}
