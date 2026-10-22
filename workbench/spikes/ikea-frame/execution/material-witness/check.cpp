#include "material.h"
#include "../../encoding/material.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <random>
#include <source_location>
#include <thread>

namespace fm = frame_material_witness;
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
using Values = std::vector<std::uint64_t>;
Values values(const fm::Region& r, unsigned seed) {
    std::mt19937_64 rng(seed);
    const auto mask = (UINT64_C(1) << r.width) - 1;
    Values out(r.count);
    for (auto& value : out) value = r.base + (rng() & mask);
    out.front() = r.base; out.back() = r.base + mask;
    return out;
}
void encode(const fm::Region& r, const Values& original, std::byte* target) {
    Values residuals;
    for (auto value : original) residuals.push_back(value - r.base);
    frame_encoding::pack(r.width, residuals, reinterpret_cast<std::uint8_t*>(target));
}
void expect(const fm::Region& r, orbital::View body, const Values& original) {
    need(r.count == original.size(), "interpretation changed population");
    for (unsigned row = 0; row < r.count; ++row)
        need(fm::point(r, body, row) == original[row], "point differs from original-value oracle");
    Values decoded(r.count);
    fm::range(r, body, 0, decoded);
    need(decoded == original, "range differs from original-value oracle");
    std::array<std::uint64_t, 21> partial{};
    fm::range(r, body, 7, partial);
    need(std::equal(partial.begin(), partial.end(), original.begin() + 7), "partial range differs");
    for (unsigned i = 0; i < r.readable_padding; ++i)
        need(body.address()[r.bytes() + i] == std::byte{0}, "sealed readable padding changed");
}
void expect(fm::Selection selected, const std::array<Values, 2>& original) {
    need(selected && selected.part_count() == 2, "wrong visible region count");
    const auto control = selected.control();
    unsigned position = 0;
    for (unsigned i = 0; i < 2; ++i) {
        need(control.region(i).first_position == position, "logical coordinates changed");
        expect(control.region(i), take(selected.part(i)), original[i]);
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
void report(const char* mode, const char* phase, Fixture& fixture, const Memory& memory,
            std::size_t retained_capacity, std::size_t retained_body, std::size_t replacement_bytes) {
    const auto s = alloc::statistics(*fixture.pool);
    std::printf("{\"mode\":\"%s\",\"phase\":\"%s\",\"provenance_owner_capacity_bytes\":%zu,"
        "\"provenance_owner_body_bytes\":%zu,\"successor_encoded_plane_bytes\":%zu,\"payload_copy_bytes\":0,\"metadata_live_bytes\":%zu,"
        "\"metadata_peak_bytes\":%zu,\"pool_mapped_bytes\":%llu,\"pool_idle_bytes\":%llu,"
        "\"pool_metadata_bytes\":%llu,\"arena_charged_bytes\":%llu}\n", mode, phase,
        retained_capacity, retained_body, replacement_bytes, memory.live, memory.peak,
        (unsigned long long)s.mapped_bytes, (unsigned long long)s.idle_bytes,
        (unsigned long long)s.metadata_bytes, (unsigned long long)fixture.arena->charged_bytes.load());
}
std::uint64_t nonidle(Fixture& fixture) {
    const auto s = alloc::statistics(*fixture.pool);
    need(s.idle_bytes <= s.mapped_bytes, "invalid public Pool accounting");
    return s.mapped_bytes - s.idle_bytes;
}

void exercise(bool grouped) {
    Fixture fixture;
    Memory memory;
    fm::Store store(memory);
    fm::Control old_control{&memory};
    old_control.regions = {{{1000, 4096, 13, 0, 32}, {UINT64_C(1) << 40, 4096, 9, 4096, 32}}};
    old_control.owners = grouped ? orbital::PartOwners::grouped_allocation
                                : orbital::PartOwners::independent_allocation;
    const std::array original{values(old_control.regions[0], 713), values(old_control.regions[1], 714)};
    const char* mode = grouped ? "same-owner-slices" : "independent-owners";
    std::array<const std::byte*, 2> addresses{};
    std::size_t old_capacity{}, first_capacity{}, old_body{};
    {
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
                old_capacity += alloc::capacity(allocations[i]);
                old_body += allocations[i].size;
                addresses[i] = allocations[i].address;
            }
            first_capacity = alloc::capacity(allocations[0]);
        }
        std::array<orbital::PartSource, 2> parts;
        std::array<alloc::Hold, 2> grants;
        for (unsigned i = 0; i < 2; ++i) {
            auto& allocation = allocations[grouped ? 0 : i];
            encode(old_control.regions[i], original[i], const_cast<std::byte*>(addresses[i]));
            const auto offset_in_owner = std::size_t(addresses[i] - allocation.address);
            grants[i] = take(alloc::hold(allocation,
                {offset_in_owner, old_control.regions[i].bytes() + old_control.padding(i)}));
            parts[i] = {old_control.id(i), {addresses[i], old_control.regions[i].bytes()}, {}, &allocation};
        }
        const auto owner = take(store.capture(old_control, parts));
        need(!allocations[0] && !allocations[1], "fresh ownership not consumed exactly once");
        publish(store, owner);
    } // Construction Holds and the caller's owner end before lifetime observations.
    std::optional<fm::Store::Scope> old(std::in_place, store);
    const auto old_selection = old->bind(object);
    expect(old_selection, original);
    fm::Control next_control = old_control;
    next_control.owners = orbital::PartOwners::independent_allocation;
    next_control.regions[1] = {UINT64_C(1) << 48, 4096, 21, 4096, 32};
    const std::array changed{original[0], values(next_control.regions[1], 715)};
    std::size_t fresh_capacity{};
    {
        auto fresh = fixture.create(next_control.regions[1].bytes(), next_control.padding(1));
        fresh_capacity = alloc::capacity(fresh);
        encode(next_control.regions[1], changed[1], fresh.address);
        std::array parts{take(orbital::source(old_selection, 0)), orbital::PartSource{
            next_control.id(1), alloc::bytes(fresh), {}, &fresh}};
        const auto unchanged_metadata = memory.live;
        auto bad = next_control;
        ++bad.regions[0].readable_padding;
        auto refused = store.capture(bad, parts);
        need(!refused && refused.error() == orbital::Error::invalid_argument && fresh &&
             memory.live == unchanged_metadata, "padding refusal changed sources or metadata");
        memory.limit = memory.live;
        refused = store.capture(next_control, parts);
        memory.limit = SIZE_MAX;
        need(!refused && refused.error() == orbital::Error::capacity && fresh &&
             memory.live == unchanged_metadata, "metadata refusal changed fresh ownership");
        const auto owner = take(store.capture(next_control, parts));
        need(!fresh, "successor did not consume fresh allocation");
        publish(store, owner);
    }
    std::optional<fm::Store::Scope> current(std::in_place, store);
    const auto selected = current->bind(object);
    need(take(selected.part(0)).address() == addresses[0], "unchanged plane was copied");
    need(take(selected.part(1)).address() != addresses[1], "changed plane aliases old version");
    expect(old_selection, original); expect(selected, changed);
    const auto replacement_bytes = next_control.regions[1].bytes();
    report(mode, "old-reader-held", fixture, memory, old_capacity + fresh_capacity,
           old_body + replacement_bytes, replacement_bytes);
    const auto held = nonidle(fixture);
    old.reset();
    store.drain();
    const auto released = nonidle(fixture);
    need(grouped ? released == held : released < held, "physical sibling retention did not match owner grain");
    expect(selected, changed);
    report(mode, "old-reader-released", fixture, memory, first_capacity + fresh_capacity,
           (grouped ? old_body : old_control.regions[0].bytes()) + replacement_bytes, replacement_bytes);
    auto* escaped = [&] {
        auto bound = take(store.bound(*current, object));
        return take(bound.export_part(0, orbital::PartRetention::selected_copy));
    }();
    need(orbital::binding(escaped).object == object && orbital::binding(escaped).version == 2,
         "escape inherited an old version label");
    current.reset();
    store = {};
    expect(escaped->control.region(0), orbital::view(escaped), original[0]);
    need(escaped->control.part_count() == 1, "selected copy kept whole interpretation");
    report(mode, "escaped-region-only", fixture, memory, first_capacity,
           grouped ? old_body : old_control.regions[0].bytes(), replacement_bytes);
    orbital::release(escaped);
    need(!memory.live && nonidle(fixture) == 0, "final release retained metadata or physical uses");
    report(mode, "all-released", fixture, memory, 0, 0, replacement_bytes);
}
} // namespace

int main() {
    exercise(false);
    exercise(true);
    std::puts("PASS: actual encoded FOR native material, retained source reuse, old/current/escaped readers and independent/grouped owner lifetimes");
}
