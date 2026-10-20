#include <ikea/boundlist.h>
#include "../streampack/implementation_orbital.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <source_location>
#include <thread>
#include <vector>

namespace om = streampack_implementation::orbital_material;
namespace sp = ikea::streampack;
namespace oa = orbital::allocation;
template <class R> auto take(R&& r, std::source_location at = std::source_location::current()) {
    if (!r) { std::fprintf(stderr, "%s:%u: failed result%d\n", at.file_name(), at.line(), int(r.error())); std::abort(); }
    return std::move(*r);
}
struct runtime {
    loom::Worker worker;
    orbital::Ref<oa::Arena> arena;
    runtime() {
        assert(loom::adopt(worker));
        arena = take(oa::open_arena(64ull << 20));
        assert(loom::configure(worker, arena, 0));
    }
    ~runtime() {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;) {
            const auto p = loom::shutdown(worker);
            if (p.kind() == orbital::Progress::Kind::done) break;
            assert(p.kind() != orbital::Progress::Kind::failed && std::chrono::steady_clock::now() < until);
            loom::poll(worker, {256, 0}); std::this_thread::yield();
        }
        assert(!arena->charged_bytes.load()); loom::relinquish(worker);
    }
};
struct row {
    unsigned reservation;
    std::vector<sp::byte> bytes;
    bool live = true;
};
row fact(unsigned r, const sp::layout& format) {
    const auto reservation = format.fixed() ? format.fixed_width() : r % 11 == 0 ? 0 : 12 + r % 23;
    const auto length = format.has_used() ? (r * 7) % (reservation + 1) : reservation;
    row result{reservation, std::vector<sp::byte>(length)};
    for (unsigned b = 0; b < length; ++b) result.bytes[b] = sp::byte((r * 31 + b * 17) % 251);
    return result;
}
using image = std::array<std::vector<sp::byte>, 4>;
template <class V> image snapshot(const V& view) {
    image result;
    const auto planes = view.windows().planes();
    for (unsigned i = 0; i < 4; ++i) result[i].assign(planes[i].begin(), planes[i].end());
    return result;
}
void replay(image before, const sp::view& source, const ikea::source_write_journal& effects) {
    const auto planes = source.windows().planes();
    for (const auto e : effects.entries()) {
        assert(e.source == &source && e.bytes.plane < 4);
        const auto p = e.bytes.plane;
        assert(e.bytes.offset <= planes[p].size() && e.bytes.size <= planes[p].size() - e.bytes.offset);
        std::copy_n(planes[p].begin() + e.bytes.offset, e.bytes.size, before[p].begin() + e.bytes.offset);
    }
    assert(before == snapshot(source));
}
void verify(const sp::const_view& view, const std::vector<row>& facts) {
    assert(view.validate() && view.size() == facts.size());
    sp::author::dispatch(view, [&](auto a) {
        using A = decltype(a);
        namespace sc=ikea::boundlist::sidecar;
        const sc::author::access<A::family, typename A::width_type, typename A::offset_type,
            sp::const_view> m(view);
        static_assert(sizeof(m)==2*sizeof(void*) && sizeof(a)==2*sizeof(void*));
        unsigned extent = 0;
        std::vector<unsigned> owner;
        std::vector<unsigned> visited;
        for (unsigned r = 0; r < facts.size(); ++r) {
            const auto& expected = facts[r];
            const auto actual = m.locate(r);
            assert(actual.offset == extent && actual.reservation == expected.reservation);
            assert(m.live(r) == expected.live);
            if (expected.live) {
                assert(std::ranges::equal(view.windows().payload.subspan(actual.offset,expected.bytes.size()), expected.bytes));
                // Value read windows live above geometry; geometry windows
                // include R even when Direct values can use just start + U.
                std::vector<sp::bytes> current;
                a.visit_value_metadata(r, [&](auto p) { current.push_back(p); });
                assert(!current.empty() || (view.format().fixed() && !view.format().has_used() && !view.format().has_tombstones()));
            }
            owner.insert(owner.end(), expected.reservation, r);
            extent += expected.reservation;
        }
        assert(view.extent() == extent);
        for (unsigned q = 0; q < extent; ++q) {
            const auto r = m.reverse(q).rank;
            assert(r == owner[q]);
            assert(m.live(r) == facts[r].live); // Physical reverse keeps retirement and slack.
        }
        assert(!sp::reverse(view,extent));
        m.range(0, facts.size(), [&](unsigned r, sc::location p) {
            const auto value=view.windows().payload.subspan(p.offset, facts[r].bytes.size());
            assert(facts[r].live && std::ranges::equal(value, facts[r].bytes)); visited.push_back(r);
        });
        std::vector<unsigned> expected;
        for (unsigned r = 0; r < facts.size(); ++r) if (facts[r].live) expected.push_back(r);
        assert(visited == expected);
    });
}

void history(runtime& rt, sp::description description, om::Placement allocation_placement) {
    auto format = take(om::prepare_format(description));
    auto* memory = std::pmr::new_delete_resource(); // Catalog metadata; bytes use actual Loom Allocations.
    om::Store store(*memory);
    constexpr orbital::ObjectId object{0x424e44, 1}, sibling{0x424e44, 2};
    std::vector<row> original;
    {
        auto draft = take(om::Draft::create(rt.worker, format, *memory, 16384, allocation_placement));
        std::array<ikea::owner_write, 4096> journal{};
        ikea::source_write_journal effects{journal};
        for (unsigned r = 0; r < 15; ++r) {
            const auto before = snapshot(draft.write()); effects.used = 0;
            original.push_back(fact(r, format->layout));
            assert(sp::append(draft.write(), original.back().bytes, original.back().reservation, effects));
            replay(before, draft.write(), effects); draft.holders()[r] = r;
        }
        verify(draft.write().as_const(), original);
        auto other = take(om::Draft::create(rt.worker, format, *memory, 16384, allocation_placement));
        for (unsigned r = 0; r < original.size(); ++r) {
            assert(sp::append(other.write(), original[r].bytes, original[r].reservation, effects));
            other.holders()[r] = r;
        }
        om::Store::Scope basis(store);
        const std::array<om::Store::Change, 2> changes{{{object, take(draft.capture(store))}, {sibling, take(other.capture(store))}}};
        om::Store::Prepared prepared(store);
        assert(store.prepare(basis, changes, prepared) && store.install(prepared));
    }
    om::Store::Scope retained(store);
    const auto selected = retained.bind(object);
    const auto old = om::read(selected);
    const auto old_image = snapshot(old);
    auto current = original;
    {
        auto draft = take(om::Draft::clone(rt.worker, selected, 16384, allocation_placement));
        std::array<ikea::owner_write, 4096> journal{};
        ikea::source_write_journal effects{journal};
        // Cross15/16/17 with the old reader still allowed to load the entire
        // partial group. A new logical slot does not make old padding writable.
        for (unsigned r = 15; r < 33; ++r) {
            const auto before = snapshot(draft.write()); effects.used = 0;
            current.push_back(fact(r, format->layout));
            assert(sp::append(draft.write(), current.back().bytes, current.back().reservation, effects));
            replay(before, draft.write(), effects); draft.holders()[r] = r;
        }
        {
            const auto before = snapshot(draft.write()); effects.used = 0;
            auto replacement = current[2].bytes;
            if (!replacement.empty()) replacement[0] ^= sp::byte{0x5a};
            if (format->layout.has_used() && !replacement.empty()) replacement.pop_back();
            assert(sp::replace(draft.write(), 2, replacement, effects));
            current[2].bytes = std::move(replacement); replay(before, draft.write(), effects);
        }
        if (format->layout.has_tombstones()) {
            const auto before = snapshot(draft.write()); effects.used = 0;
            assert(sp::retire(draft.write(), 17, effects)); current[17].live = false;
            draft.holders()[17] = om::absent;
            replay(before, draft.write(), effects);
        }
        verify(draft.write().as_const(), current);
        assert(om::publish(store, retained, object, take(draft.capture(store))));
    }
    {
        om::Store::Scope fresh(store);
        verify(om::read(fresh.bind(object)), current);
        verify(om::read(fresh.bind(sibling)), original);
    }
    verify(old, original); assert(snapshot(old) == old_image);
    if (!format->layout.fixed()) {
        auto draft = take(om::Draft::clone(rt.worker, store.borrow().bind(object), 16384, allocation_placement));
        std::array<ikea::owner_write, 4096> journal{};
        ikea::source_write_journal effects{journal};
        const auto before = snapshot(draft.write());
        assert(sp::soft_pack(draft.write(), true, effects));
        for (auto& r : current) {
            r.reservation = r.live ? unsigned(r.bytes.size()) : 0;
            if (!r.live) r.bytes.clear();
        }
        replay(before, draft.write(), effects); verify(draft.write().as_const(), current);
        om::Store::Scope basis(store);
        assert(om::publish(store, basis, object, take(draft.capture(store))));
    }
    // Repartition/reorder and repair external ranks as one owner publication.
    {
        om::Store::Scope basis(store);
        const auto source = om::read(basis.bind(object));
        auto dst = take(om::Draft::create(rt.worker, format, *memory, 16384, allocation_placement));
        std::vector<std::uint16_t> order;
        for (unsigned r = current.size(); r--;) if (current[r].live && r % 5) order.push_back(r);
        std::array<ikea::owner_write, 8192> journal{};
        ikea::source_write_journal effects{journal};
        const auto before = snapshot(dst.write());
        assert(sp::transfer(source, order, dst.write(), !format->layout.fixed(), effects));
        std::vector<row> folded;
        for (unsigned i = 0; i < order.size(); ++i) {
            auto r = current[order[i]];
            if (!format->layout.fixed()) r.reservation = r.bytes.size();
            folded.push_back(std::move(r)); dst.holders()[order[i]] = i;
        }
        replay(before, dst.write(), effects); verify(dst.write().as_const(), folded);
        assert(om::publish(store, basis, object, take(dst.capture(store))));
        verify(om::read(store.borrow().bind(object)), folded);
        const auto map = om::holders(store.borrow().bind(object));
        for (unsigned i = 0; i < order.size(); ++i) assert(map[order[i]] == i);
    }
    store.drain(); store = {};
    verify(old, original); assert(snapshot(old) == old_image);
    verify(om::read(retained.bind(sibling)), original); // First discovery after replacement and Store destruction.
}

int main() {
    runtime rt;
    unsigned cases = 0;
    for (const auto family : {sp::geometry::direct, sp::geometry::width})
        for (const auto placement : {sp::placement::grouped, sp::placement::separated})
            for (const auto allocation : {om::Placement::grouped, om::Placement::separate})
                for (unsigned maximum : {255, 4095}) for (bool full : {false, true}) for (bool immortal : {false, true})
                    for (const auto fixed : {std::optional<std::uint16_t>{}, std::optional<std::uint16_t>{0}, std::optional<std::uint16_t>{8}}) {
                        history(rt, {family, placement, std::uint16_t(maximum), maximum * 256, fixed, full, immortal}, allocation);
                        ++cases;
                    }
    std::printf("%u actual Loom/Orbital histories: metadata-only read projection, exact source effects, padded tails, retained old/fresh/late readers, soft packing and transfer/remaps passed.\n", cases);
}
