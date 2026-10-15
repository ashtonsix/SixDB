#include "packed_key_baseline.h"

#include <absl/container/internal/hashtable_debug.h>
#include <absl/container/internal/hashtablez_sampler.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <new>
#include <set>
#include <sys/mman.h>
#include <unistd.h>

using namespace hashtable_spike;

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}

std::uint64_t random_word(std::uint64_t& state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

template <unsigned Bytes> void check_key() {
    using Key = exact_unsigned_key<Bytes>;
    constexpr auto maximum = Key::maximum;
    constexpr auto high = std::uint64_t{1} << (Bytes * 8 - 1);
    const std::array<std::uint64_t, 9> values{
        0, 1, 127, 128, 255, high - 1, high, maximum - 1, maximum};
    for (const auto value : values) {
        const Key key(value);
        require(key.value() == value, "key reconstruction");
        for (unsigned i = 0; i < Bytes; ++i)
            require(key.bytes[i] == ((value >> (8 * i)) & 255), "key physical byte");
        const auto reference = absl::Hash<std::uint64_t>{}(value);
        require(absl::Hash<Key>{}(key) == reference, "default hash logical value");
        require(exact_unsigned_hash<Bytes>{}(key) == reference, "stored transparent hash");
        require(exact_unsigned_hash<Bytes>{}(value) == reference, "word transparent hash");
        require(exact_unsigned_equal<Bytes>{}(key, value), "stored/word equality");
        require(exact_unsigned_equal<Bytes>{}(value, key), "word/stored equality");
        require(!exact_unsigned_equal<Bytes>{}(value ^ 1, key), "word mismatch");
    }
    const auto page_size = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    auto* pages = static_cast<unsigned char*>(::mmap(nullptr, page_size * 2,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    require(pages != MAP_FAILED, "guard allocation");
    require(::mprotect(pages + page_size, page_size, PROT_NONE) == 0, "guard protect");
    auto* edge = ::new (pages + page_size - Bytes) Key(maximum);
    asm volatile("" : : : "memory"); // Force the guarded reads to survive optimization.
    require(edge->value() == maximum, "guarded key decode");
    require(absl::Hash<Key>{}(*edge) == absl::Hash<std::uint64_t>{}(maximum),
            "guarded default hash");
    require(exact_unsigned_hash<Bytes>{}(*edge) == absl::Hash<std::uint64_t>{}(maximum),
            "guarded transparent hash");
    require(exact_unsigned_equal<Bytes>{}(*edge, maximum), "guarded equality");
    edge->~Key();
    require(::munmap(pages, page_size * 2) == 0, "guard release");
}

template <unsigned Bits, class Value, bool Transparent> void check_map() {
    using Map = packed_abseil_map<Bits, Value, Transparent>;
    constexpr auto mask = Map::key_type::maximum;
    Map table;
    std::map<std::uint64_t, Value> expected;
    table.reserve(127);
    const auto validate = [&] {
        require(table.size() == expected.size(), "map size");
        std::map<std::uint64_t, Value> visited;
        table.each([&](auto key, auto value) {
            require(visited.emplace(key, value).second, "map unique iteration");
        });
        require(visited == expected, "map iteration values");
        for (const auto& [key, value] : expected) {
            std::uint64_t output = ~std::uint64_t{};
            require(table.get(key, output) && output == value, "map full-value lookup");
        }
    };
    const std::array<std::uint64_t, 6> keys{0, 1, mask >> 1, (mask >> 1) + 1, mask - 1, mask};
    const std::array<std::uint64_t, 6> values{0, 1, 0x8000, 0xffff,
        0x8000000000000000ULL, 0xffffffffffffffffULL};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        require(table.insert_or_assign(keys[i], values[i]), "boundary insert");
        expected[keys[i]] = static_cast<Value>(values[i]);
    }
    validate();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        require(!table.insert_or_assign(keys[i], values[values.size() - i - 1]), "boundary replace");
        expected[keys[i]] = static_cast<Value>(values[values.size() - i - 1]);
    }
    validate();
    for (const auto key : keys) {
        require(table.erase(key), "boundary erase");
        require(!table.erase(key), "boundary repeated erase");
        std::uint64_t output = 0x0123456789abcdefULL;
        require(!table.get(key, output) && output == 0x0123456789abcdefULL, "unchanged miss");
        expected.erase(key);
    }
    std::uint64_t state = 0x0192837465abcdefULL;
    for (unsigned i = 0; i < 16384; ++i) {
        // A 64-key hot domain forces meaningful replacement/removal on every K.
        const auto random = random_word(state);
        const auto key = (i % 3 ? random : (random & 63)) & mask;
        switch (i % 4) {
        case 0: case 1: {
            const auto value = random_word(state);
            const bool inserted = expected.insert_or_assign(key, static_cast<Value>(value)).second;
            require(table.insert_or_assign(key, value) == inserted, "map mutation result");
            break;
        }
        case 2: require(table.erase(key) == (expected.erase(key) != 0), "map erase result"); break;
        default: {
            const auto found = expected.find(key);
            std::uint64_t output = 0xfedcba9876543210ULL;
            require(table.get(key, output) == (found != expected.end()), "map get result");
            require(output == (found == expected.end() ? 0xfedcba9876543210ULL : found->second),
                    "map get value/unchanged miss");
        }
        }
        if (i % 1024 == 0) validate();
    }
    validate();
    require(table.allocation_stats().allocation_calls > 0, "map allocator observed");
    require(table.allocation_stats().peak_bytes >= table.allocation_stats().current_bytes,
            "map allocation high water");
    std::printf("map,%u,%u,%u,%zu,%zu,%zu,%zu,%zu\n", Bits, unsigned(sizeof(Value) * 8),
        unsigned(Transparent), Map::value_type_bytes, Map::ordinary_object_bytes, table.size(),
        table.allocation_stats().current_bytes, table.allocation_stats().peak_bytes);
    table.clear(); expected.clear(); validate();

    // Independent lifetime/accounting check includes allocator rebinds and all
    // deallocation after growing from SOO into allocated control/slot storage.
    allocation_counters counters;
    {
        typename Map::counted_table_type raw(0, typename Map::hasher{}, typename Map::key_equal{},
                                             typename Map::allocator_type(counters));
        const auto check_bytes = [&] {
            require(absl::container_internal::AllocatedByteSize(raw) == counters.current_bytes,
                    "map byte hook matches allocator counter");
        };
        check_bytes();
        raw.insert_or_assign(std::uint8_t(7), Value(9));
        check_bytes();
        raw.reserve(129);
        check_bytes();
        raw.insert_or_assign(typename Map::key_type(mask), static_cast<Value>(~std::uint64_t{}));
        require(raw.find(std::uint64_t(mask)) != raw.end(), "mixed map native lookup");
        raw.insert_or_assign(std::uint16_t(19), static_cast<Value>(7));
        require(raw.find(std::uint32_t(19))->second == Value(7), "mixed map integer lookup");
        require(raw.find(typename Map::key_type(19))->second == Value(7), "mixed map key lookup");
        require(raw.erase(std::uint8_t(19)) == 1, "mixed map erase");
        for (std::uint64_t i = 0; i < 129; ++i) raw.insert_or_assign(i, static_cast<Value>(i * 701));
        check_bytes();
        require(counters.current_bytes > 0 && counters.allocation_calls > 0, "map rebind allocation");
        raw.clear(); check_bytes();
        raw.reserve(17); check_bytes();
    }
    require(counters.current_bytes == 0 && counters.allocation_calls == counters.deallocation_calls,
            "map rebind deallocation");
}

template <unsigned Bits, bool Transparent> void check_set() {
    using Set = packed_abseil_set<Bits, Transparent>;
    constexpr auto mask = Set::key_type::maximum;
    Set table;
    std::set<std::uint64_t> expected;
    table.reserve(129);
    for (auto key : std::array<std::uint64_t, 6>{0, 1, mask >> 1, (mask >> 1) + 1, mask - 1, mask}) {
        require(table.insert(key), "set boundary insert");
        require(!table.insert(key), "set duplicate insert");
        require(table.contains(key), "set boundary lookup");
        require(table.erase(key) && !table.erase(key) && !table.contains(key), "set boundary erase");
    }
    std::uint64_t state = 0xfedcba981234567ULL;
    for (unsigned i = 0; i < 16384; ++i) {
        const auto random = random_word(state);
        const auto key = (i % 3 ? random : (random & 63)) & mask;
        switch (i % 4) {
        case 0: case 1: require(table.insert(key) == expected.insert(key).second, "set insert"); break;
        case 2: require(table.erase(key) == (expected.erase(key) != 0), "set erase"); break;
        default: require(table.contains(key) == expected.contains(key), "set lookup"); break;
        }
    }
    std::set<std::uint64_t> visited;
    table.each([&](auto key) { require(visited.insert(key).second, "set unique iteration"); });
    require(visited == expected && table.size() == expected.size(), "set iteration/size");
    std::printf("set,%u,0,%u,%zu,%zu,%zu,%zu,%zu\n", Bits, unsigned(Transparent),
        Set::value_type_bytes, Set::ordinary_object_bytes, table.size(),
        table.allocation_stats().current_bytes, table.allocation_stats().peak_bytes);
    table.clear(); require(table.size() == 0, "set clear");
    allocation_counters counters;
    {
        typename Set::counted_table_type raw(0, typename Set::hasher{}, typename Set::key_equal{},
                                             typename Set::allocator_type(counters));
        const auto check_bytes = [&] {
            require(absl::container_internal::AllocatedByteSize(raw) == counters.current_bytes,
                    "set byte hook matches allocator counter");
        };
        check_bytes();
        raw.emplace(std::uint8_t(7));
        check_bytes();
        raw.reserve(129);
        check_bytes();
        raw.emplace(std::uint64_t(mask));
        require(raw.contains(typename Set::key_type(mask)), "mixed set key lookup");
        raw.emplace(typename Set::key_type(19));
        require(raw.contains(std::uint8_t(19)) && raw.contains(std::uint16_t(19)) &&
                raw.contains(std::uint32_t(19)), "mixed set integer lookup");
        require(raw.erase(std::uint64_t(19)) == 1, "mixed set erase");
        for (std::uint64_t i = 0; i < 129; ++i) raw.emplace(i);
        check_bytes();
        require(counters.current_bytes > 0 && counters.allocation_calls > 0, "set rebind allocation");
        raw.clear(); check_bytes();
        raw.reserve(17); check_bytes();
    }
    require(counters.current_bytes == 0 && counters.allocation_calls == counters.deallocation_calls,
            "set rebind deallocation");
}

template <unsigned Bits> void check_width() {
    check_key<Bits / 8>();
    check_map<Bits, std::uint16_t, false>(); check_map<Bits, std::uint16_t, true>();
    check_map<Bits, std::uint64_t, false>(); check_map<Bits, std::uint64_t, true>();
    check_set<Bits, false>(); check_set<Bits, true>();
}
} // namespace

// Stable codegen entry points for comparing ordinary seeded hashing with the
// transparent path. No benchmark loop, fabricated hash or precomputed query.
extern "C" bool packed24_default_get(const packed_abseil_map<24>& map,
                                     std::uint64_t key, std::uint64_t& value) {
    return map.get(key, value);
}
extern "C" bool packed24_transparent_get(const packed_abseil_map<24, std::uint64_t, true>& map,
                                         std::uint64_t key, std::uint64_t& value) {
    return map.get(key, value);
}
extern "C" bool packed56_default_get(const packed_abseil_map<56>& map,
                                     std::uint64_t key, std::uint64_t& value) {
    return map.get(key, value);
}
extern "C" bool packed24_default_contains(const packed_abseil_set<24>& set, std::uint64_t key) {
    return set.contains(key);
}

int main() {
    absl::container_internal::SetHashtablezEnabled(false);
    std::puts("kind,key_bits,value_bits,transparent,value_type_bytes,ordinary_object_bytes,entries,requested_bytes,peak_requested_bytes");
    check_width<8>(); check_width<16>(); check_width<24>(); check_width<32>();
    check_width<40>(); check_width<48>(); check_width<56>(); check_width<64>();
    std::puts("all packed-key baseline checks passed");
}
