#include <ikea/hashtable/table.h>
#include <cstdio>
#include <unordered_map>

using namespace ikea::hashtable;
// Fixture facts supplied by an owner, not production lock tokens. The context
// checks a canonical key, object identity and live physical generation before
// the actual table may store anything.
struct context {
    struct region {
        unsigned plane;
        std::span<const std::byte> storage;
        std::vector<std::byte> old;
        std::vector<std::uint8_t> covered;
    };
    const void* owner = nullptr;
    std::span<const std::byte> (*current)(const void*, unsigned) noexcept = nullptr;
    word key = 0;
    unsigned object = 7, grant_object = 7, generation = 3, lease_generation = 3, budget = 4;
    bool live = true, write = true, exclusive = true, allow_prepare = true, before_ok = true;
    unsigned observations = 0;
    std::vector<region> regions;
    template <class Table> void snapshot(const Table& table, word requested) {
        owner = &table;
        current = [](const void* owner, unsigned id) noexcept {
            return static_cast<const Table*>(owner)->plane_storage(plane(id));
        };
        key = requested;
        observations = 0;
        before_ok = true;
        regions.clear();
        for (auto id : {plane::header, plane::primary, plane::swiss_controls, plane::swiss_tuples}) {
            const auto storage = table.plane_storage(id);
            std::vector<std::byte> old;
            if (!storage.empty()) old.assign(storage.begin(),storage.end());
            regions.push_back({unsigned(id),storage,std::move(old),std::vector<std::uint8_t>(storage.size())});
        }
    }
    template <class Table>
    bool enter(const Table& table, word requested, access_intent intent) const {
        return owner == &table && requested == key && object == grant_object && live &&
               generation == lease_generation && exclusive &&
               (intent == access_intent::read || write);
    }
    region* resolve(const void* source, unsigned id) {
        if (source != owner || !current) return nullptr;
        const auto storage = current(owner,id);
        for (auto& r : regions)
            if (r.plane == id && r.storage.data() == storage.data() &&
                r.storage.size() == storage.size()) return &r;
        return nullptr;
    }
    bool prepare(std::span<const ikea::journal::entry> writes) {
        if (!allow_prepare || writes.size() > budget || generation != lease_generation)
            return false;
        for (const auto& d : writes) {
            const auto* r = resolve(d.source,d.plane);
            if (!r || !d.size || d.offset > r->storage.size() ||
                d.size > r->storage.size() - d.offset) return false;
        }
        return true;
    }
    template<class Table>
    void before(const Table& source, unsigned id, std::size_t offset, std::size_t size) noexcept {
        auto* r = resolve(&source,id);
        if (!r || offset > r->storage.size() || size > r->storage.size() - offset) {
            before_ok = false;
            return;
        }
        for (std::size_t i = offset; i < offset + size; ++i) {
            before_ok &= r->storage[i] == r->old[i];
            r->covered[i] = 1;
        }
        ++observations;
    }
    void verify(bool unchanged = false) const {
        require(before_ok, "damage did not precede stores");
        for (const auto& r : regions)
            for (std::size_t i = 0; i < r.old.size(); ++i)
                if (r.old[i] != r.storage[i])
                    require(!unchanged && r.covered[i], "uncovered or refused store");
        if (unchanged) require(observations == 0, "refusal published damage");
    }
};

template <class Table, write_strategy Writes = write_strategy::mixed> void run() {
    Table table(713);
    static_assert(ikea::effects<context,Table>);
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
    static_assert(ikea::effects<context,Table>);
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
void source_plane_contract() {
    using Table = table<16,8,8,8>;
    static_assert(ikea::effects<context,Table> && ikea::effects<ikea::journal,Table>);
    static_assert(std::is_same_v<typename Table::value_type::value_type,std::byte>);
    static_assert(std::is_same_v<decltype(Table::borrowed_value{}.address),const std::byte*>);
    Table owner(713), other(713);
    context c;
    c.snapshot(owner,7);
    const auto primary = owner.plane_storage(plane::primary);
    require(!primary.empty() && owner.plane_storage(plane::header).size() == sizeof(owner),
            "owner resolves current physical planes");
    std::array<ikea::journal::entry,2> planned{{
        {&owner,unsigned(plane::primary),0,1},
        {&other,unsigned(plane::primary),0,1}}};
    require(!c.prepare(planned), "late wrong source refuses whole preparation");
    c.verify(true);
    planned[1] = {&owner,99,0,1};
    require(!c.prepare(planned), "unknown source plane refuses preparation");
    planned[1] = {&owner,unsigned(plane::primary),primary.size(),1};
    require(!c.prepare(planned), "plane-relative extent is checked");
    planned[1] = {&owner,unsigned(plane::primary),1,std::numeric_limits<std::size_t>::max()};
    require(!c.prepare(planned), "plane span arithmetic does not wrap");
    c.verify(true);
    planned[1] = {&owner,unsigned(plane::header),0,sizeof(owner)};
    require(c.prepare(planned), "same stable owner identifies separate planes");
    ++c.generation;
    require(!c.prepare(planned), "preparation rechecks source generation");
    --c.generation;
    c.budget = 1;
    require(!c.prepare(planned), "all records are budgeted before effects");
    c.verify(true);
    std::array<ikea::journal::entry,2> recorded{};
    ikea::journal effects{recorded};
    effects.before(owner,unsigned(plane::primary),0,1);
    effects.before(owner,unsigned(plane::header),0,sizeof(owner));
    require(effects.entries().size() == 2 && recorded[0].source == &owner &&
                recorded[1].source == &owner && recorded[0].plane != recorded[1].plane,
            "journal retains actual table identity rather than transient preflight state");
    require(Table::effect_capacity_insert() >= planned.size() &&
                Table::effect_capacity_modify() >= Table::effect_capacity_insert(),
            "command capacity is a conservative additional-record bound");
    heap_storage_policy::buffer buffer(64);
    static_assert(std::is_same_v<decltype(buffer.data),std::byte*>);
    const auto refused = buffer.relinquish_tail(128);
    require(!refused && refused.error() == error::range && buffer.size == 64,
            "storage extent failure remains distinct from mutation outcomes");
}
int main() {
    source_plane_contract();
    run<table<16, 4, 2, 12>>();
    run<table<16, 4, 0, 12>>();
    run<table<16, 8, 2, 8>>();
    concentrated();
    run<table<16, 4, 2, 12>, write_strategy::existing>();
    run<table<16, 4, 0, 12>, write_strategy::existing>();
    std::puts("PASS: set/map grants, before-store damage, atomic refusal, bitmap/Swiss");
}
