#include "material.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <random>
#include <source_location>
#include <thread>

namespace fm = frame_material_witness;
namespace fe = frame_encoding;
namespace alloc = orbital::allocation;
namespace {
void need(bool ok, const char* why, std::source_location where = std::source_location::current()) {
    if (ok) return;
    std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), why);
    std::abort();
}
template<class T> T take(orbital::Result<T> value,
                        std::source_location where = std::source_location::current()) {
    need(bool(value), "unexpected refusal", where);
    if constexpr (!std::is_void_v<T>) return std::move(*value);
}
struct Memory final : std::pmr::memory_resource {
    std::size_t live{}, peak{}, calls{}, limit = SIZE_MAX;
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (bytes > limit || live > limit - bytes) throw std::bad_alloc();
        auto* p = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        live += bytes; peak = std::max(peak, live); ++calls;
        return p;
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        need(live >= bytes, "metadata disposed more than once");
        live -= bytes;
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};
struct Fixture {
    orbital::worker::Lane lane;
    orbital::Ref<alloc::Arena> arena;
    alloc::Pool* pool{};
    Fixture() {
        take(orbital::worker::adopt(lane));
        arena = take(alloc::open_arena(32 * 1024 * 1024));
        pool = take(alloc::add_pool(*arena, lane));
    }
    alloc::Allocation create(std::size_t body, unsigned padding) {
        return take(alloc::allocate(*pool, {.bytes = body, .alignment = 64,
            .readable_padding = padding, .initialization = alloc::Initialization::zero}));
    }
    ~Fixture() {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;) {
            const auto progress = alloc::close(*pool);
            need(progress.kind() != orbital::Progress::Kind::failed, "Pool close refused");
            if (progress.kind() == orbital::Progress::Kind::done) break;
            need(std::chrono::steady_clock::now() < end, "live body prevented Pool close");
            orbital::worker::poll(lane, 32); std::this_thread::yield();
        }
        need(!arena->charged_bytes.load() && !orbital::worker::pending(lane), "stranded physical charge");
        take(alloc::remove_pool(*pool));
        arena.reset(); orbital::worker::relinquish(lane);
    }
};
using Values = std::vector<fe::u64>;
struct Scenario {
    const char* name;
    fe::kind special;
    fe::checkpoints checkpoints = fe::checkpoints::native;
    fe::dictionary_order dictionary = fe::dictionary_order::none;
    bool descending = false;
};
Values values(const Scenario& s, unsigned seed, unsigned version) {
    std::mt19937_64 rng(seed);
    Values out(fm::region_blocks * fe::block_rows);
    for (auto& value : out) value = rng();
    if (s.dictionary != fe::dictionary_order::none) {
        const fe::u64 base = (fe::u64(version) << 56) + seed;
        for (auto& value : out) value = base + (rng() & ((UINT64_C(1) << 55) - 1));
        for (unsigned i = 0; i < 256; ++i) {
            const unsigned code = i < 160 ? 17 : i % 31;
            out[i] = base + code * ((UINT64_C(1) << 40) + 37);
            out[256 + i] = base + 9; // Raw constant: coding has equal cost, not a win.
        }
        return out;
    }
    for (unsigned i = 0; i < 256; ++i) {
        switch (s.special) {
        case fe::kind::arrival: need(false, "arrival is outside this witness"); break;
        case fe::kind::constant: out[i] = 17 + version; break;
        case fe::kind::runs: out[i] = 100 + version * 10 + (i / 64) * 10; break;
        case fe::kind::packed: out[i] = 1000 + version * 100000 + (rng() & 8191); break;
        case fe::kind::delta:
            out[i] = 1000000 + version * 111111 + (s.descending ? -1000LL : 1000LL) * i + i % 7;
            break;
        case fe::kind::patched: out[i] = version * 3 + i % 3; break;
        case fe::kind::delta_patched: out[i] = 1000000 + version * 111111 + 1000 * i + i % 7; break;
        }
    }
    if (s.special == fe::kind::patched) out[73] = (UINT64_C(1) << 48) + version;
    if (s.special == fe::kind::delta_patched) {
        out[64] = (UINT64_C(1) << 48) + version;
        out[192] = (UINT64_C(1) << 45) + version;
    }
    return out;
}

// Temporary encoding inputs are destroyed before any retained-reader checks.
// Only copied pointer-free metadata and actual native bytes enter the Object.
struct EncodedRegion {
    fe::column staging;
    fm::Region metadata;
    EncodedRegion(const Values& original, const Scenario& s, unsigned first,
                  bool successor = false) {
        auto order = s.dictionary;
        if (successor && order != fe::dictionary_order::none)
            order = order == fe::dictionary_order::sorted ? fe::dictionary_order::frequency
                                                         : fe::dictionary_order::sorted;
        staging = fe::encode(original, {.delta_checkpoints = s.checkpoints, .delta_exceptions = true,
            .dictionary = order, .dictionary_every_part = order != fe::dictionary_order::none,
            .bind_decoders = false});
        auto& r = metadata;
        r.block_count = unsigned(staging.blocks.size());
        r.first_position = first; r.readable_padding = 32; r.checkpoints = s.checkpoints;
        need(r.block_count == fm::region_blocks, "unexpected encoded region population");
        if (!staging.parts.empty() && staging.parts[0].count) {
            const auto d = fe::dictionary_of(staging, 0);
            r.dictionary = {d.min, d.edition, 0, d.count, unsigned(d.bytes()), d.width, d.order};
            r.body_bytes = unsigned(d.bytes());
        }
        unsigned coded = 0, raw = 0;
        for (unsigned i = 0; i < r.block_count; ++i) {
            const auto k = fe::borrow(staging, i);
            const auto extent = fe::extent_of(k.d, k.delta_checkpoints, k.body, k.grant);
            need(extent.has_value() && extent->read_grant_bytes <= k.grant && fe::admits(k),
                 "encoder produced an inadmissible block");
            auto& b = r.blocks[i];
            b.descriptor = k.d; b.descriptor.offset = 0;
            b.offset = r.body_bytes; b.body_bytes = unsigned(extent->body_bytes);
            b.grant_bytes = unsigned(extent->read_grant_bytes); b.rows = k.rows; b.coded = k.coded;
            b.dictionary_edition = k.coded ? k.dictionary.edition : fe::dictionary_identity{};
            r.body_bytes += b.grant_bytes;
            coded += k.coded; raw += !k.coded;
        }
        if (order == fe::dictionary_order::none)
            need(r.blocks[0].descriptor.type == s.special, "fixture did not exercise requested body kind");
        else need(r.dictionary.count && coded && raw, "fixture lost mixed raw/coded part coverage");
        need(r.valid(), "constructed metadata is invalid");
    }
    void copy_into(std::byte* destination) const {
        const auto& r = metadata;
        if (r.dictionary.count) {
            const auto d = fe::dictionary_of(staging, 0);
            std::memcpy(destination + r.dictionary.offset, d.plane, r.dictionary.grant_bytes);
        }
        for (unsigned i = 0; i < r.block_count; ++i) {
            const auto& b = r.blocks[i];
            if (b.grant_bytes) std::memcpy(destination + b.offset, fe::borrow(staging, i).body, b.grant_bytes);
        }
        need(fm::attach(r, {destination, r.bytes()}).has_value(), "copied native image refused admission");
    }
};
struct Query {
    fe::u64 threshold;
    fe::code_sum sum;
    fe::code_count count;
    explicit Query(fe::u64 t) : threshold(t), count(t) {}
};
void expect(const fm::BorrowedRegion& ready, const Values& original, Query& query) {
    const auto& r = *ready.interpretation;
    need(r.rows() == original.size(), "interpretation changed population");
    for (unsigned row = 0; row < r.rows(); ++row)
        need(ready.point(row) == original[row], "point differs from original-value oracle");
    Values decoded(r.rows()); ready.range(0, decoded);
    need(decoded == original, "decode differs from original-value oracle");
    std::array<fe::u64, 279> partial{}; ready.range(7, partial);
    need(std::equal(partial.begin(), partial.end(), original.begin() + 7), "partial range differs");
    for (unsigned b = 0; b < r.block_count; ++b) {
        const auto k = ready.block(b);
        const auto first = original.begin() + b * fe::block_rows, limit = first + fe::block_rows;
        fe::u128 wanted_sum = 0;
        for (auto value = first; value != limit; ++value) wanted_sum += *value;
        need(fe::block_sum(k) == wanted_sum, "borrowed logical SUM differs");
        need((k.coded ? query.sum(k) : fe::stored_sum(k)) == wanted_sum, "dictionary SUM cache used stale edition");
        const auto [low, high] = std::minmax_element(first, limit);
        for (auto threshold : {fe::u64{0}, *low, *low + (*high - *low) / 2, *high, UINT64_MAX}) {
            const auto wanted = std::count_if(first, limit, [&](auto value) { return value >= threshold; });
            need(fe::block_count_ge(k, threshold) == fe::u64(wanted), "borrowed logical COUNT differs");
        }
        const auto wanted = std::count_if(first, limit, [&](auto value) { return value >= query.threshold; });
        need((k.coded ? query.count.count(k) : fe::block_count_ge(k, query.threshold)) == fe::u64(wanted),
             "dictionary COUNT cache used stale edition");
    }
}
void expect(fm::Selection selected, const std::array<Values, 2>& original, Query& query) {
    need(selected && selected.part_count() == 2, "wrong visible region count");
    const auto control = selected.control();
    unsigned position = 0;
    for (unsigned i = 0; i < 2; ++i) {
        const auto& r = control.region(i);
        need(r.first_position == position, "logical coordinates changed");
        const auto body = take(selected.part(i));
        const auto ready = fm::attach(r, {body.address(), r.bytes()});
        need(ready.has_value(), "selected Part refused attach");
        expect(*ready, original[i], query);
        for (unsigned p = 0; p < r.readable_padding; ++p)
            need(body.address()[r.bytes() + p] == std::byte{0}, "sealed readable padding changed");
        position += unsigned(original[i].size());
    }
}
constexpr orbital::ObjectId object{0x4652414d45, 1};
void publish(fm::Store& store, const fm::Store::Owner& owner) {
    fm::Store::Prepared packet(store);
    const std::array<fm::Store::Change, 1> changes{{{object, owner}}};
    { fm::Store::Scope basis(store); take(store.prepare(basis, changes, packet)); }
    take(store.install(packet));
}
void report(const Scenario& scenario, const char* mode, const char* phase, Fixture& fixture,
            const Memory& memory, const Query& query, std::size_t capacity, std::size_t body, std::size_t copied) {
    const auto s = alloc::statistics(*fixture.pool);
    std::printf("{\"case\":\"%s\",\"checkpoints\":\"%s\",\"dictionary\":\"%s\",\"mode\":\"%s\",\"phase\":\"%s\","
        "\"provenance_owner_capacity_bytes\":%zu,\"provenance_owner_body_bytes\":%zu,\"native_payload_copy_bytes\":%zu,"
        "\"metadata_live_bytes\":%zu,\"metadata_peak_bytes\":%zu,\"query_cache_dynamic_bytes\":%zu,\"pool_mapped_bytes\":%llu,\"pool_idle_bytes\":%llu,"
        "\"pool_metadata_bytes\":%llu,\"arena_charged_bytes\":%llu}\n", scenario.name,
        fe::checkpoint_name(scenario.checkpoints), fe::dictionary_name(scenario.dictionary), mode, phase,
        capacity, body, copied, memory.live, memory.peak,
        (query.sum.values.capacity() + query.count.truth.capacity()) * sizeof(fe::u64),
        (unsigned long long)s.mapped_bytes,
        (unsigned long long)s.idle_bytes, (unsigned long long)s.metadata_bytes,
        (unsigned long long)fixture.arena->charged_bytes.load());
}
std::uint64_t nonidle(Fixture& fixture) {
    const auto s = alloc::statistics(*fixture.pool);
    need(s.idle_bytes <= s.mapped_bytes, "invalid public Pool accounting");
    return s.mapped_bytes - s.idle_bytes;
}
void attach_refusals(const fm::Region& r, alloc::Allocation& fresh) {
    auto bad = r; --bad.blocks[0].rows;
    need(!fm::attach(bad, alloc::bytes(fresh)) && fresh, "invalid row count consumed fresh ownership");
    unsigned nonempty = 0;
    while (!r.blocks[nonempty].grant_bytes) ++nonempty;
    bad = r; --bad.blocks[nonempty].grant_bytes;
    need(!fm::attach(bad, alloc::bytes(fresh)) && fresh, "short block grant consumed fresh ownership");
    if (r.dictionary.count) {
        bad = r; ++bad.dictionary.edition.edition;
        need(!fm::attach(bad, alloc::bytes(fresh)) && fresh, "wrong dictionary edition admitted");
        bad = r; --bad.dictionary.grant_bytes;
        need(!fm::attach(bad, alloc::bytes(fresh)) && fresh, "short dictionary grant admitted");
    }
    for (unsigned i = 0; i < r.block_count; ++i) {
        const auto& b = r.blocks[i];
        if (b.descriptor.type != fe::kind::patched && b.descriptor.type != fe::kind::delta_patched) continue;
        auto* width = fresh.address + b.offset + fe::fixed_body_bytes(b.descriptor, r.checkpoints) - 1;
        const auto saved = *width; *width = std::byte{0};
        need(!fm::attach(r, alloc::bytes(fresh)) && fresh, "malformed patch tail admitted");
        *width = saved;
    }
    need(fm::attach(r, alloc::bytes(fresh)).has_value(), "refusal damaged valid fresh contents");
}
void exercise(const Scenario& scenario, bool grouped) {
    Fixture fixture; Memory memory; fm::Store store(memory);
    const auto original = std::array{values(scenario, 713, 0), values(scenario, 714, 0)};
    const auto changed = std::array{original[0], values(scenario, 715, 1)};
    Query query(scenario.dictionary == fe::dictionary_order::none ? original[1][17] : UINT64_C(1) << 56);
    fm::Control old_control{&memory};
    old_control.owners = grouped ? orbital::PartOwners::grouped_allocation
                                : orbital::PartOwners::independent_allocation;
    const char* mode = grouped ? "same-owner-slices" : "independent-owners";
    std::array<const std::byte*, 2> addresses{};
    std::size_t old_capacity{}, first_capacity{}, old_body{}, copied{};
    {
        const std::array images{EncodedRegion(original[0], scenario, 0),
            EncodedRegion(original[1], scenario, unsigned(original[0].size()))};
        for (unsigned i = 0; i < 2; ++i) old_control.regions[i] = images[i].metadata;
        if (scenario.dictionary != fe::dictionary_order::none) {
            const auto a = old_control.regions[0].dictionary.edition;
            const auto b = old_control.regions[1].dictionary.edition;
            // Use honest column-factory identities: equal local part indices,
            // distinct images. Fixture-global editions would conceal collisions.
            need(a.edition == b.edition && a.scope != b.scope,
                 "cross-column cache regression lost equal-part-index coverage");
        }
        std::array<alloc::Allocation, 2> allocations;
        const auto offset = (old_control.regions[0].bytes() + old_control.padding(0) + 63) / 64 * 64;
        if (grouped) {
            old_body = offset + old_control.regions[1].bytes();
            allocations[0] = fixture.create(old_body, old_control.padding(1));
            old_capacity = first_capacity = alloc::capacity(allocations[0]);
            addresses = {allocations[0].address, allocations[0].address + offset};
        } else {
            for (unsigned i = 0; i < 2; ++i) {
                allocations[i] = fixture.create(old_control.regions[i].bytes(), old_control.padding(i));
                old_capacity += alloc::capacity(allocations[i]); old_body += allocations[i].size;
                addresses[i] = allocations[i].address;
            }
            first_capacity = alloc::capacity(allocations[0]);
        }
        std::array<orbital::PartSource, 2> parts;
        std::array<alloc::Hold, 2> grants;
        for (unsigned i = 0; i < 2; ++i) {
            auto& allocation = allocations[grouped ? 0 : i];
            images[i].copy_into(const_cast<std::byte*>(addresses[i]));
            copied += images[i].metadata.bytes();
            grants[i] = take(alloc::hold(allocation,
                {std::size_t(addresses[i] - allocation.address), old_control.regions[i].bytes() + old_control.padding(i)}));
            parts[i] = {old_control.id(i), {addresses[i], old_control.regions[i].bytes()}, {}, &allocation};
        }
        const auto owner = take(store.capture(old_control, parts));
        need(!allocations[0] && !allocations[1], "fresh ownership not consumed exactly once");
        publish(store, owner);
    } // Encoding vectors, construction Holds and caller Owners are all gone.
    std::optional<fm::Store::Scope> old(std::in_place, store);
    const auto old_selection = old->bind(object);
    expect(old_selection, original, query);
    fm::Control next_control = old_control;
    next_control.owners = orbital::PartOwners::independent_allocation;
    std::size_t fresh_capacity{};
    {
        const EncodedRegion image(changed[1], scenario, unsigned(changed[0].size()), true);
        next_control.regions[1] = image.metadata;
        if (scenario.dictionary != fe::dictionary_order::none)
            need(image.metadata.dictionary.edition != old_control.regions[1].dictionary.edition,
                 "changed dictionary reused old interpretation identity");
        auto fresh = fixture.create(image.metadata.bytes(), image.metadata.readable_padding);
        fresh_capacity = alloc::capacity(fresh);
        image.copy_into(fresh.address); copied += image.metadata.bytes();
        const auto unchanged_metadata = memory.live;
        attach_refusals(image.metadata, fresh);
        need(memory.live == unchanged_metadata, "non-owning attach allocated persistent metadata");
        std::array parts{take(orbital::source(old_selection, 0)), orbital::PartSource{
            next_control.id(1), alloc::bytes(fresh), {}, &fresh}};
        need(fm::attach(next_control.regions[0], parts[0].body).has_value(), "retained source lost admission");
        auto bad = next_control; ++bad.regions[0].readable_padding;
        auto refused = store.capture(bad, parts);
        need(!refused && refused.error() == orbital::Error::invalid_argument && fresh &&
             memory.live == unchanged_metadata, "padding refusal changed source ownership");
        memory.limit = memory.live;
        refused = store.capture(next_control, parts); memory.limit = SIZE_MAX;
        need(!refused && refused.error() == orbital::Error::capacity && fresh &&
             memory.live == unchanged_metadata, "metadata refusal changed fresh ownership");
        const auto owner = take(store.capture(next_control, parts));
        need(!fresh, "successor did not consume fresh allocation"); publish(store, owner);
    }
    std::optional<fm::Store::Scope> current(std::in_place, store);
    const auto selected = current->bind(object);
    need(take(selected.part(0)).address() == addresses[0], "unchanged region was copied");
    need(take(selected.part(1)).address() != addresses[1], "changed region aliases old version");
    expect(selected, changed, query); expect(old_selection, original, query); expect(selected, changed, query);
    report(scenario, mode, "old-reader-held", fixture, memory, query, old_capacity + fresh_capacity,
           old_body + next_control.regions[1].bytes(), copied);
    const auto held = nonidle(fixture); old.reset(); store.drain();
    const auto released = nonidle(fixture);
    need(grouped ? released == held : released < held, "sibling retention did not match owner grain");
    expect(selected, changed, query);
    report(scenario, mode, "old-reader-released", fixture, memory, query, first_capacity + fresh_capacity,
           (grouped ? old_body : old_control.regions[0].bytes()) + next_control.regions[1].bytes(), copied);
    std::array<orbital::Part<fm::Control>*, 2> escaped{};
    {
        auto bound = take(store.bound(*current, object));
        for (unsigned i = 0; i < 2; ++i) escaped[i] = take(bound.export_part(i, orbital::PartRetention::selected_copy));
    }
    current.reset(); store = {};
    for (unsigned i = 0; i < 2; ++i) {
        const auto* part = escaped[i]; const auto& r = part->control.region(0);
        need(orbital::binding(part).object == object && orbital::binding(part).version == 2,
             "escape inherited an old version label");
        need(part->control.part_count() == 1 && r.first_position == i * original[0].size(),
             "selected copy lost interpretation or coordinates");
        const auto ready = fm::attach(r, {orbital::view(part).address(), r.bytes()});
        need(ready.has_value(), "escaped dictionary or body lost admission"); expect(*ready, changed[i], query);
    }
    orbital::release(escaped[1]);
    const auto& r = escaped[0]->control.region(0);
    expect(*fm::attach(r, {orbital::view(escaped[0]).address(), r.bytes()}), changed[0], query);
    report(scenario, mode, "escaped-region-only", fixture, memory, query, first_capacity,
           grouped ? old_body : old_control.regions[0].bytes(), copied);
    orbital::release(escaped[0]);
    need(!memory.live && nonidle(fixture) == 0, "final release retained metadata or physical uses");
    report(scenario, mode, "all-released", fixture, memory, query, 0, 0, copied);
}
} // namespace

int main() {
    std::vector<Scenario> cases{{"constant", fe::kind::constant}, {"packed", fe::kind::packed},
        {"runs", fe::kind::runs}, {"patched", fe::kind::patched},
        {"delta-descending", fe::kind::delta, fe::checkpoints::native, fe::dictionary_order::none, true}};
    for (auto mode : {fe::checkpoints::native, fe::checkpoints::packed, fe::checkpoints::every64, fe::checkpoints::none}) {
        cases.push_back({"delta", fe::kind::delta, mode});
        cases.push_back({"delta-patched", fe::kind::delta_patched, mode});
    }
    cases.push_back({"dictionary-sorted", fe::kind::packed, fe::checkpoints::native, fe::dictionary_order::sorted});
    cases.push_back({"dictionary-frequency", fe::kind::packed, fe::checkpoints::native, fe::dictionary_order::frequency});
    for (const auto& scenario : cases) { exercise(scenario, false); exercise(scenario, true); }
    std::printf("PASS: %zu scenarios, both native ownership modes, actual borrowed Frame bodies, dictionary editions, old/current/escaped readers and refusals\n", cases.size());
}
