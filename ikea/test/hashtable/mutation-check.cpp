#include <ikea/hashtable/table.h>
#include <cstdio>
#include <unordered_map>

using namespace ikea::hashtable;
// Fixture facts supplied by an owner, not production lock tokens. The context
// checks a canonical key, object identity and live physical generation before
// the actual table may store anything.
struct context {
    struct region {
        mutation_source source;
        std::vector<byte> old, covered;
    };
    const void* owner = nullptr;
    word key = 0;
    unsigned object = 7, grant_object = 7, generation = 3, lease_generation = 3, budget = 4;
    bool live = true, write = true, exclusive = true, allow_prepare = true, before_ok = true;
    unsigned observations = 0;
    std::vector<region> regions;
    template <class Table> void snapshot(const Table& table, word requested) {
        owner = &table;
        key = requested;
        observations = 0;
        before_ok = true;
        regions.clear();
        auto add = [&](owner_part part, const byte* base, std::size_t extent) {
            regions.push_back(
                {{owner, part, base, extent}, {base, base + extent}, std::vector<byte>(extent)});
        };
        add(owner_part::header, reinterpret_cast<const byte*>(&table), sizeof(table));
        table.allocations([&](const char* name, const byte* base, std::size_t extent) {
            const auto label = std::string_view(name);
            const auto part = label == "primary"               ? owner_part::primary
                              : label == "swiss_controls"      ? owner_part::swiss_controls
                              : label == "singleton_bags"      ? owner_part::singleton_bags
                              : label == "small_variable_bags" ? owner_part::small_variable_bags
                              : label == "large_variable_bags" ? owner_part::large_variable_bags
                                                               : owner_part::swiss_tuples;
            add(part, base, extent);
        });
    }
    template <class Table>
    bool enter(const Table& table, word requested, access_intent intent) const {
        return owner == &table && requested == key && object == grant_object && live &&
               generation == lease_generation && exclusive &&
               (intent == access_intent::read || write);
    }
    region* resolve(mutation_source source) {
        for (auto& r : regions)
            if (r.source.owner == source.owner && r.source.part == source.part &&
                r.source.base == source.base && r.source.extent == source.extent)
                return &r;
        return nullptr;
    }
    bool prepare(std::span<const mutation_damage> writes) {
        if (!allow_prepare || writes.size() > budget)
            return false;
        for (auto d : writes)
            if (!resolve(d.source) || d.span.plane || d.span.offset > d.source.extent ||
                d.span.size > d.source.extent - d.span.offset)
                return false;
        return true;
    }
    void before(mutation_source source, ikea::byte_write span) noexcept {
        auto* r = resolve(source);
        if (!r) {
            before_ok = false;
            return;
        }
        for (std::size_t i = span.offset; i < span.offset + span.size; ++i) {
            before_ok &= source.base[i] == r->old[i];
            r->covered[i] = 1;
        }
        ++observations;
    }
    void verify(bool unchanged = false) const {
        require(before_ok, "damage did not precede stores");
        for (const auto& r : regions)
            for (std::size_t i = 0; i < r.old.size(); ++i)
                if (r.old[i] != r.source.base[i])
                    require(!unchanged && r.covered[i], "uncovered or refused store");
        if (unchanged)
            require(observations == 0, "refusal published damage");
    }
};

template <class Table, write_strategy Writes = write_strategy::mixed> void run() {
    Table table(713);
    context c;
    std::unordered_map<word, word> expected;
    auto insert = [&](word key, word value) {
        typename Table::value_type bytes{};
        if constexpr (bytes.size())
            store<bytes.size()>(bytes.data(), value);
        c.snapshot(table, key);
        auto result = table.template guarded_insert<Writes>(key, bytes, c);
        if (result == mutation_status::needs_capacity) {
            c.verify(true);
            table.prepare_absent(key);
            c.snapshot(table, key);
            result = table.template guarded_insert<Writes>(key, bytes, c);
        }
        require(result == (expected.contains(key) ? (bytes.size() ? mutation_status::updated
                                                                  : mutation_status::unchanged)
                                                  : mutation_status::inserted),
                "insert status");
        c.verify();
        expected[key] = value;
    };
    auto erase = [&](word key) {
        c.snapshot(table, key);
        const auto result = table.guarded_erase(key, c);
        require(result ==
                    (expected.contains(key) ? mutation_status::erased : mutation_status::absent),
                "erase status");
        c.verify(result == mutation_status::absent);
        expected.erase(key);
    };
    // Independent key/value oracle, repeated updates, misses, deletion and
    // tombstone reuse. Counter and packed-plane stores are included in coverage.
    for (word i = 0; i < 1200; ++i)
        insert((i * 317) & mask<16>, i * 19);
    for (word i = 0; i < 1200; i += 3)
        erase((i * 317) & mask<16>);
    for (word i = 0; i < 1200; i += 5)
        insert((i * 317) & mask<16>, i + 7);
    table.validate();
    require(table.size() == expected.size(), "oracle population");
    for (auto [key, value] : expected) {
        c.snapshot(table, key);
        word seen = 0;
        const auto result = table.guarded_find(key, c, [&](const auto& bytes) {
            if constexpr (bytes.size())
                seen = load<bytes.size()>(bytes.data());
        });
        require(result == lookup_status::found, "guarded read");
        if constexpr (std::tuple_size_v<typename Table::value_type>)
            require(seen == value, "guarded value");
        c.verify(true);
    }
    typename Table::value_type value{};
    // Wrong object/key/intent/lifetime/physical generation and journal refusal
    // must leave both allocations and owner metadata unchanged.
    c.snapshot(table, 42);
    c.grant_object = 8;
    require(table.guarded_insert(42, value, c) == mutation_status::denied, "wrong object");
    c.verify(true);
    c.grant_object = 7;
    c.snapshot(table, 42);
    require(table.guarded_insert(43, value, c) == mutation_status::denied, "wrong key");
    c.verify(true);
    c.snapshot(table, 42);
    c.write = false;
    require(table.guarded_insert(42, value, c) == mutation_status::denied, "read-only grant");
    c.verify(true);
    c.write = true;
    c.snapshot(table, 42);
    c.live = false;
    require(table.guarded_insert(42, value, c) == mutation_status::denied, "expired grant");
    c.verify(true);
    c.live = true;
    c.snapshot(table, 42);
    c.exclusive = false;
    require(table.guarded_insert(42, value, c) == mutation_status::denied,
            "missing packed-byte exclusion");
    c.verify(true);
    c.exclusive = true;
    c.snapshot(table, 42);
    c.lease_generation = 2;
    require(table.guarded_insert(42, value, c) == mutation_status::denied, "stale generation");
    c.verify(true);
    c.lease_generation = 3;
    const word present = expected.begin()->first;
    c.snapshot(table, present);
    c.budget = 0;
    require(table.guarded_erase(present, c) == mutation_status::denied, "journal capacity");
    c.verify(true);
    c.budget = 4;
    c.snapshot(table, present);
    c.allow_prepare = false;
    require(table.guarded_erase(present, c) == mutation_status::denied, "preservation refusal");
    c.verify(true);
    c.allow_prepare = true;
}
void concentrated() {
    using Table = table<20, 4, 8, 16>;
    Table table(713);
    context c;
    typename Table::mix_type mix{713};
    std::vector<word> keys;
    for (word q = 0; q < 420; ++q) {
        const auto key = mix.recover(3, q);
        keys.push_back(key);
        table.prepare_absent(key);
        c.snapshot(table, key);
        require(table.guarded_insert(key, bytes<8>{}, c) == mutation_status::inserted,
                "concentrated insert");
        c.verify();
    }
    table.validate();
    for (auto key : keys) {
        c.snapshot(table, key);
        require(table.guarded_erase(key, c) == mutation_status::erased, "sticky erase");
        c.verify();
    }
    table.validate();
    require(table.size() == 0, "sticky empty");
    c.snapshot(table, keys.back());
    require(table.guarded_find(keys.back(), c, [](auto) {}) == lookup_status::absent,
            "sticky lookup");
    c.verify(true);
    c.snapshot(table, 1u << 20);
    require(table.guarded_erase(1u << 20, c) == mutation_status::denied, "invalid W key");
    c.verify(true);
}
int main() {
    run<table<16, 4, 2, 12>>();
    run<table<16, 4, 0, 12>>();
    run<table<16, 8, 2, 8>>();
    concentrated();
    run<table<16, 4, 2, 12>, write_strategy::existing>();
    run<table<16, 4, 0, 12>, write_strategy::existing>();
    std::puts("PASS: set/map grants, before-store damage, atomic refusal, bitmap/Swiss");
}
