#include "implementation_orbital.h"
#include "implementation_loom.h"
#include <orbital/detail/object/representation.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>
#include <thread>
#include <vector>
#include <unistd.h>

namespace om = streampack_implementation::orbital_material;
namespace lc = streampack_implementation::loom_consumer;
namespace sp = ikea::streampack;
namespace oa = orbital::allocation;
using Clock = std::chrono::steady_clock;

namespace {
void check(bool ok, const char* reason,
    std::source_location where = std::source_location::current()) {
    if (ok) return;
    std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), reason);
    std::abort();
}

template <class R> auto take(R&& result, const char* reason) {
    check(bool(result), reason);
    return std::move(*result);
}

struct Runtime {
    loom::Worker worker;
    orbital::Ref<oa::Arena> arena;
    Runtime() {
        check(bool(loom::adopt(worker)), "adopt Worker");
        arena = take(oa::open_arena(64 * 1024 * 1024), "open Arena");
        check(bool(loom::configure(worker, arena, 0)), "configure Worker");
    }
    ~Runtime() {
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        for (;;) {
            const auto p = loom::shutdown(worker);
            if (p.kind() == orbital::Progress::Kind::done) break;
            check(p.kind() != orbital::Progress::Kind::failed && Clock::now() < deadline,
                "shutdown must drain physical storage");
            loom::poll(worker, {256, 0});
            std::this_thread::yield();
        }
        check(!orbital::worker::pending(worker.lane), "shutdown leaves no Messages");
        check(arena->charged_bytes.load() == 0, "shutdown refunds Arena");
        loom::relinquish(worker);
    }
};

struct Row {
    std::uint64_t key;
    unsigned length, reservation, next;
    bool retired{};
};

// Application facts precede encoding; neither the point nor chain oracle decodes
// StreamPack metadata or obtains expected bytes from a selected Representation.
std::vector<Row> rows(unsigned n, unsigned salt = 0) {
    std::vector<Row> result;
    for (unsigned r = 0; r < n; ++r)
        result.push_back({UINT64_C(0x1234567800000000) + salt * 1000 + r,
            12 + (r + salt) % 7, 24 + (r + salt) % 5, (r + 1) % n});
    return result;
}

std::vector<sp::byte> encode(const Row& row) {
    std::vector<sp::byte> result(row.length);
    for (unsigned i = 0; i < row.length; ++i) result[i] = sp::byte((row.key + i * 19) % 251);
    if (row.length >= 8) std::memcpy(result.data(), &row.key, 8);
    if (row.length >= 12) std::memcpy(result.data() + row.length - 4, &row.next, 4);
    return result;
}

void fill(om::Draft& draft, std::span<const Row> input) {
    std::array<ikea::journal::entry, 16> ignored_records; ikea::journal ignored{ignored_records};
    for (const auto& row : input) {
        const auto data = encode(row);
        ignored.used = 0;
        check(bool(sp::append(draft.write(), data, row.reservation, ignored)), "append application row");
    }
    for (unsigned r = 0; r < input.size(); ++r)
        if (input[r].retired) { ignored.used = 0; check(bool(sp::retire(draft.write(), r, ignored)), "retire application row"); }
}

void expect(const sp::const_view& source, std::span<const Row> expected) {
    check(bool(source.validate()) && source.size() == expected.size(), "admitted count and SIMD tail");
    for (unsigned r = 0; r < expected.size(); ++r) {
        const auto value = sp::value(source, r);
        if (expected[r].retired) {
            check(!value && value.error() == sp::error::retired, "retired rank refuses");
        } else {
            const auto data = encode(expected[r]);
            check(value && std::ranges::equal(*value, data), "selected bytes match independent row");
        }
    }
    const auto outside = sp::value(source, unsigned(expected.size()));
    check(!outside && outside.error() == sp::error::range, "rank boundary refuses");
}

using Snapshot = std::array<std::vector<sp::byte>, 4>;
template <class V> Snapshot snapshot(const V& source) {
    Snapshot result;
    const auto planes = source.windows().planes();
    for (unsigned p = 0; p < planes.size(); ++p) result[p].assign(planes[p].begin(), planes[p].end());
    return result;
}

void replay(Snapshot before, const sp::view& source, const ikea::journal& effects) {
    const auto planes = source.windows().planes();
    for (const auto entry : effects.entries()) {
        check(entry.source == &source && entry.plane < 4, "effect resolves actual named source");
        const auto p = entry.plane;
        const auto offset = entry.offset, size = entry.size;
        check(offset <= planes[p].size() && size <= planes[p].size() - offset, "effect is bounded by Part");
        std::copy_n(planes[p].begin() + offset, size, before[p].begin() + offset);
    }
    check(before == snapshot(source), "issued afterimages reproduce all resulting bytes");
}

om::FormatOwner format(sp::geometry geometry = sp::geometry::sizes,
    sp::placement placement = sp::placement::grouped) {
    return take(om::prepare_format({geometry, placement, 255, sp::slot_capacity * 255, {}, false, false}),
        "prepare shared format");
}

constexpr orbital::ObjectId object{9, 1}, undiscovered{9, 2};

void publication(Runtime& runtime, om::Placement placement) {
    om::Store store(*std::pmr::new_delete_resource());
    const auto f = format();
    const auto original = rows(15), other = rows(3, 7);
    {
        om::Store::Scope basis(store);
        auto a = take(om::Draft::create(runtime.worker, f, *std::pmr::new_delete_resource(), 2048, placement), "create a");
        auto b = take(om::Draft::create(runtime.worker, f, *std::pmr::new_delete_resource(), 2048, placement), "create b");
        fill(a, original); fill(b, other);
        const std::array<om::Store::Change, 2> changes{{
            {object, take(a.capture(store), "capture a")},
            {undiscovered, take(b.capture(store), "capture b")}}};
        om::Store::Prepared prepared(store);
        check(bool(store.prepare(basis, changes, prepared)) && bool(store.install(prepared)), "initial publication");
    }
    om::Store::Scope old(store);
    auto selected = old.bind(object);
    const auto old_view = om::read(selected);
    const auto old_bytes = snapshot(old_view);
    auto current_rows = original;
    current_rows[4].key += 400; current_rows[4].length = 21;
    auto next = take(om::Draft::clone(runtime.worker, selected, 2048, placement), "clone selected body");
    std::array<ikea::journal::entry, 16> ignored_records; ikea::journal ignored{ignored_records};
    check(bool(sp::replace(next.write(), 4, encode(current_rows[4]), ignored)), "replace private clone");
    auto addition = rows(2, 19);
    for (const auto row : addition) {
        ignored.used = 0;
        check(bool(sp::append(next.write(), encode(row), row.reservation, ignored)), "cross partial sixteen group");
        current_rows.push_back(row);
    }
    check(bool(om::publish(store, old, object, take(next.capture(store), "capture changed clone"))), "publish replacement");
    {
        om::Store::Scope current(store);
        expect(om::read(current.bind(object)), current_rows);
        expect(om::read(current.bind(undiscovered)), other);
    }
    store.drain();
    expect(old_view, original);
    check(snapshot(old_view) == old_bytes, "publication preserves all old SIMD-readable bytes");
    expect(om::read(old.bind(undiscovered)), other); // First discovery after publication and cleanup.
    store = {};
    expect(om::read(old.bind(object)), original);
    expect(om::read(old.bind(undiscovered)), other);
}

struct Escaped {
    std::array<orbital::Part<om::Control>*, 4> parts{};
    ~Escaped() { for (auto* part : parts) if (part) orbital::release(part); }
};

void escaping_parts(Runtime& runtime, om::Placement placement) {
    Escaped exports;
    const auto original = rows(17, 5);
    Snapshot saved;
    {
        auto f = format();
        om::Store store(*std::pmr::new_delete_resource());
        {
            auto draft = take(om::Draft::create(runtime.worker, f, *std::pmr::new_delete_resource(), 2048, placement), "create escape");
            fill(draft, original);
            om::Store::Scope basis(store);
            check(bool(om::publish(store, basis, object, take(draft.capture(store), "capture escape"))), "publish escape");
        }
        {
            om::Store::Scope scope(store);
            const auto bound = take(store.bound(scope, object), "catalog binds export identity");
            saved = snapshot(om::read(bound.selection()));
            for (unsigned p = 0; p < 4; ++p) {
                exports.parts[p] = take(bound.export_part(p, p % 2 ? orbital::PartRetention::selected_copy :
                    orbital::PartRetention::shared_control), "export retained Part");
                check(orbital::binding(exports.parts[p]).object == object &&
                    orbital::binding(exports.parts[p]).version == 1, "export retains catalog label");
            }
        }
        store = {};
        f.reset();
    }
    std::array<sp::bytes, 4> planes;
    for (unsigned p = 0; p < 4; ++p) {
        const auto* part = exports.parts[p];
        planes[p] = {orbital::view(part).address(), saved[p].size()};
        check(std::ranges::equal(planes[p], saved[p]), "escaped Part survives Store and original format owner");
        check(part->control.original(part->index) == p, "selected copy retains original plane identity");
    }
    const auto& l = exports.parts[1]->control.format->layout;
    const auto source = sp::const_view::attach_unchecked(l, {planes[0], planes[1], planes[2], planes[3]});
    expect(source, original);
}

void boundary_and_refusal(Runtime& runtime) {
    for (auto geometry : {sp::geometry::absolute_ends, sp::geometry::sizes})
        for (auto placement : {sp::placement::grouped, sp::placement::separated}) {
            const auto f = format(geometry, placement);
            auto draft = take(om::Draft::create(runtime.worker, f, *std::pmr::new_delete_resource(), 2048), "create boundary");
            auto input = rows(33, 2);
            std::array<ikea::journal::entry, 512> entries{};
            ikea::journal effects{entries};
            for (unsigned n = 0; n < input.size(); ++n) {
                const auto before = snapshot(draft.write());
                effects.used = 0;
                check(bool(sp::append(draft.write(), encode(input[n]), input[n].reservation, effects)), "append 15/16/17/31/32/33");
                replay(before, draft.write(), effects);
                expect(draft.write().as_const(), std::span(input).first(n + 1));
            }
            auto refusal = [&](auto command, sp::error expected_error) {
                const auto before = snapshot(draft.write());
                entries.fill({&draft, 3, 99, 101});
                const auto prior = entries;
                effects.used = 1;
                const auto result = command();
                check(!result && result.error() == expected_error && effects.used == 1,
                    "refusal leaves effect cursor intact");
                check(snapshot(draft.write()) == before, "refusal preserves every plane");
                for (unsigned i = 0; i < entries.size(); ++i)
                    check(entries[i].source == prior[i].source &&
                        entries[i].plane == prior[i].plane &&
                        entries[i].offset == prior[i].offset &&
                        entries[i].size == prior[i].size,
                        "refusal preserves every effect slot, including preexisting output");
            };
            refusal([&] { return sp::replace(draft.write(), 33, encode(input[0]), effects); }, sp::error::range);
            const std::array<sp::byte, 29> too_large{};
            refusal([&] { return sp::replace(draft.write(), 0, too_large, effects); }, sp::error::value);
            refusal([&] { return sp::replace(draft.write(), 0, draft.write().windows().control.first(1), effects); }, sp::error::overlap);
            effects.storage = std::span(entries).first(2);
            refusal([&] { return sp::replace(draft.write(), 0, encode(input[0]), effects); }, sp::error::effects);
            refusal([&] { return sp::soft_pack(draft.write(), true, effects); }, sp::error::effects);
        }

    const auto zero = take(om::prepare_format({sp::geometry::absolute_ends, sp::placement::grouped,
        0, 0, 0, true, true}), "prepare count-only format");
    auto z = take(om::Draft::create(runtime.worker, zero, *std::pmr::new_delete_resource(), 0), "allocate count-only object");
    std::array<ikea::journal::entry, 1> entry{};
    ikea::journal effects{entry};
    check(bool(sp::append(z.write(), {}, 0, effects)) && effects.used == 1,
        "maximum-zero append writes only count");
    check(entry[0].source == &z.write() && entry[0].plane == unsigned(sp::plane::control), "count has named control effect");
    om::Store store(*std::pmr::new_delete_resource());
    om::Store::Scope basis(store);
    check(bool(om::publish(store, basis, object, take(z.capture(store), "capture zero payload"))), "publish zero payload");
    const auto view = om::read(store.borrow().bind(object));
    check(view.size() == 1 && view.extent() == 0 && sp::value(view, 0)->empty() && view.validate(), "zero payload stays readable");

    auto packed = take(om::Draft::create(runtime.worker, format(), *std::pmr::new_delete_resource(), 64), "create soft pack");
    const std::array bytes{sp::byte{7}, sp::byte{8}};
    std::array<ikea::journal::entry, 16> ignored_records; ikea::journal ignored{ignored_records};
    check(bool(sp::append(packed.write(), bytes, 8, ignored)), "append slack reservation");
    const auto before = snapshot(packed.write());
    std::array<ikea::journal::entry, 16> writes{};
    ikea::journal log{writes};
    check(bool(sp::soft_pack(packed.write(), true, log)), "soft pack trailing slack");
    replay(before, packed.write(), log);
    check(packed.write().extent() == 2 && packed.write().size() == 1 &&
        snapshot(packed.write())[1] == before[1], "single-rank packing preserves count and checkpoint");
    check(std::ranges::any_of(log.entries(), [](auto e) { return e.plane == unsigned(sp::plane::control) &&
        e.offset == 2 && e.size == 4; }), "extent-only control effect survives integration");
}

void transfer_and_repair(Runtime& runtime) {
    const auto f = format();
    auto original = rows(20, 13);
    original[3].retired = original[14].retired = true;
    om::Store store(*std::pmr::new_delete_resource());
    {
        auto draft = take(om::Draft::create(runtime.worker, f, *std::pmr::new_delete_resource(), 2048), "create transfer source");
        fill(draft, original);
        draft.holders()[0] = 3;
        const auto address = draft.address(0);
        const auto before_capture = snapshot(draft.write());
        const auto invalid_holder = draft.capture(store);
        check(!invalid_holder && invalid_holder.error() == orbital::Error::invalid_argument &&
            draft.source && draft.allocations[0] && draft.address(0) == address &&
            snapshot(draft.write()) == before_capture, "refused capture preserves complete private Draft");
        for (unsigned i = 0; i < 12; ++i) draft.holders()[i] = i % 3 ? 2 : 17;
        draft.holders()[12] = 8;
        om::Store::Scope basis(store);
        check(bool(om::publish(store, basis, object, take(draft.capture(store), "capture transfer source"))), "publish transfer source");
    }
    om::Store::Scope old(store);
    const auto selection = old.bind(object);
    const auto source = om::read(selection);
    const auto before = snapshot(source);
    auto dst = take(om::Draft::create(runtime.worker, f, *std::pmr::new_delete_resource(), 2048, om::Placement::separate), "create transfer destination");
    std::vector<std::uint16_t> order;
    for (unsigned i = unsigned(original.size()); i--;) if (!original[i].retired && i != 8) order.push_back(i);
    std::array<std::uint32_t, 64> remap;
    remap.fill(om::absent);
    for (unsigned n = 0; n < order.size(); ++n) remap[order[n]] = n;
    const auto dst_before = snapshot(dst.write());
    std::array<ikea::journal::entry, 512> entries{};
    ikea::journal effects{entries};
    entries[0] = {&source, 0, 1, 2};
    effects.used = 1;
    const std::array<std::uint16_t, 2> invalid_order{2, 3};
    const auto refused = sp::transfer(source, invalid_order, dst.write(), true, effects);
    check(!refused && refused.error() == sp::error::retired && effects.used == 1 &&
        entries[0].source == &source && entries[0].plane == 0 &&
        entries[0].offset == 1 && entries[0].size == 2 &&
        snapshot(source) == before && snapshot(dst.write()) == dst_before,
        "refused transfer preserves both physical selections and prior effects");
    effects.used = 0;
    check(bool(sp::transfer(source, order, dst.write(), true, effects)), "transfer permuted retained live ranks");
    std::vector<Row> current;
    for (unsigned n = 0; n < order.size(); ++n) {
        auto row = original[order[n]];
        row.next = remap[row.next]; row.reservation = row.length;
        current.push_back(row);
        check(bool(sp::replace(dst.write(), n, encode(row), effects)), "repair embedded successor in owner transaction");
    }
    const auto old_holders = om::holders(selection);
    for (unsigned h = 0; h < old_holders.size(); ++h)
        dst.holders()[h] = old_holders[h] == om::absent ? om::absent : remap[old_holders[h]];
    replay(dst_before, dst.write(), effects);
    check(bool(om::publish(store, old, object, take(dst.capture(store), "capture payload and repaired holders together"))), "publish remapped block");
    const auto fresh = store.borrow().bind(object);
    expect(om::read(fresh), current);
    for (unsigned h = 0; h < old_holders.size(); ++h)
        check(om::holders(fresh)[h] == (old_holders[h] == om::absent ? om::absent : remap[old_holders[h]]), "external aliases and absent ranks repaired");
    check(om::holders(fresh)[12] == om::absent && om::holders(fresh)[0] == om::holders(fresh)[3], "omitted holder and aliased holders observable");
    expect(source, original);
    check(snapshot(source) == before, "hard transfer and repair preserve retained source");
}

void growth(Runtime& runtime) {
    const auto f = format();
    auto draft = take(om::Draft::create(runtime.worker, f, *std::pmr::new_delete_resource(), 96,
        om::Placement::separate, 16), "create private growable planes");
    auto input = rows(3);
    fill(draft, input);
    const auto full = snapshot(draft.write());
    std::array<ikea::journal::entry, 8> writes{};
    writes[0] = {&draft, 3, 7, 9};
    ikea::journal log{writes, 1};
    const auto no_room = sp::append(draft.write(), encode(input[1]), input[1].reservation, log);
    check(!no_room && no_room.error() == sp::error::capacity && snapshot(draft.write()) == full &&
        log.used == 1 && writes[0].source == &draft && writes[0].plane == 3 &&
        writes[0].offset == 7 && writes[0].size == 9,
        "payload exhaustion cannot alter partial-group metadata or prior effects");
    auto* first = draft.address(0);
    const auto in_place = oa::capacity(draft.allocations[0]);
    check(bool(draft.grow(in_place, 16)) && draft.address(0) == first, "growth within capacity retains address");
    expect(draft.write().as_const(), input);
    const auto held = snapshot(draft.write());
    auto hold = take(loom::hold(draft.allocations[0], {0, draft.allocations[0].size}), "hold actual payload before relocation");
    const auto relocation = std::max(std::size_t(4096), oa::reserved_bytes(draft.allocations[0]) + 64);
    const auto refused = draft.grow(relocation, 16);
    check(!refused && refused.error() == orbital::Error::conflict && draft.address(0) == first,
        "held relocation refuses without pointer change");
    check(snapshot(draft.write()) == held, "held refusal preserves all bindings and bytes");
    oa::release(hold);
    check(bool(draft.grow(relocation, 33)) && draft.address(0) != first, "unheld private relocation rebinds");
    check(draft.write().windows().payload.data() == draft.address(0) &&
        draft.write().windows().lengths.data() == draft.address(2), "bindings use grown addresses");
    expect(draft.write().as_const(), input);
    const auto rest = rows(30, 9);
    std::array<ikea::journal::entry, 16> ignored_records; ikea::journal ignored{ignored_records};
    for (const auto row : rest) {
        ignored.used = 0;
        check(bool(sp::append(draft.write(), encode(row), row.reservation, ignored)), "append through expanded length groups");
        input.push_back(row);
    }
    expect(draft.write().as_const(), input);

    auto empty = take(om::Draft::create(runtime.worker, f, *std::pmr::new_delete_resource(), 0,
        om::Placement::separate, 16), "create empty payload for later growth");
    check(!empty.allocations[0], "empty payload needs no initial physical backing");
    check(bool(empty.grow(64, 16)) && empty.allocations[0], "first payload growth obtains backing");
    const auto first_rows = rows(1, 5);
    fill(empty, first_rows);
    expect(empty.write().as_const(), first_rows);
    om::Store store(*std::pmr::new_delete_resource());
    auto owner = take(empty.capture(store), "capture first grown payload");
    const auto again = empty.capture(store);
    const auto after_capture = empty.grow(128, 16);
    check(!again && again.error() == orbital::Error::invalid_argument &&
        !after_capture && after_capture.error() == orbital::Error::invalid_argument,
        "capture consumes mutable Draft and repeated operations refuse");
}

om::Store::Owner shared_capture(om::Draft& draft, std::size_t payload_bytes) {
    auto extents = draft.extents;
    extents[0] = payload_bytes;
    auto& memory = *std::pmr::new_delete_resource();
    const om::Control control{&memory, draft.format.get(), extents, om::Placement::grouped};
    auto retained = std::make_shared<oa::Hold>(take(
        oa::hold(draft.allocations[0], {0, draft.offsets[0] + payload_bytes}),
        "retain exact shared five-plane footprint"));
    std::array<orbital::PartSource, 5> sources;
    for (unsigned p = 0; p < sources.size(); ++p)
        sources[p] = {control.id(p), {draft.address(p), extents[p]}, retained, nullptr};
    return om::Store::Owner::take(take(orbital::capture_representation(memory, control,
        orbital::PartOwners::grouped_shared, sources), "capture shared physical Hold"));
}

void live_tail(Runtime& runtime, sp::geometry geometry, orbital::PartRetention retention) {
    const auto f = take(om::prepare_format({.family = geometry, .maximum = 4095,
        .byte_limit = 256 * 4095}), "prepare tail-reclaim schema");
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    const auto old_payload = 8 * page;
    auto draft = take(om::Draft::create(runtime.worker, f, *std::pmr::new_delete_resource(),
        old_payload, om::Placement::grouped), "create deliberately touched speculative span");
    const auto input = rows(256, 17);
    fill(draft, input);
    const auto used = draft.write().extent();
    check(used < old_payload, "rank exhaustion precedes byte exhaustion");
    // This sentinel is initialized readable storage of the old interpretation,
    // not a new logical StreamPack value. It exposes premature physical discard.
    std::fill(draft.address(0) + used, draft.address(0) + old_payload, sp::byte{0x6d});
    auto& allocation = draft.allocations[0];
    const auto original_address = allocation.address;
    const auto original_capacity = oa::capacity(allocation);
    const auto original_reservation = oa::reserved_bytes(allocation);
    const auto wanted = draft.offsets[0] + used;
    om::Store store(*std::pmr::new_delete_resource());
    auto old_owner = shared_capture(draft, old_payload);
    { om::Store::Scope basis(store);
      check(bool(om::publish(store, basis, object, old_owner)), "publish old oversized footprint"); }
    std::optional<om::Store::Scope> old_scope;
    old_scope.emplace(store);
    const auto bound = take(store.bound(*old_scope, object), "bind old physical interpretation");
    auto* escaped = take(bound.export_part(0, retention),
        "export payload with old full footprint");
    auto prefix_owner = shared_capture(draft, used);
    check(bool(om::publish(store, *old_scope, object, prefix_owner)), "publish useful-prefix interpretation");
    prefix_owner = {};
    old_owner = {};
    check(bool(oa::relinquish_tail(allocation, wanted)), "relinquish unused writer tail");
    check(allocation.size == wanted, "writer access contracts before backing maintenance");
    auto narrowed = draft.write().windows();
    narrowed.payload = narrowed.payload.first(used);
    draft.source = take(sp::view::attach(f->layout, narrowed), "rebind contracted native windows");
    draft.extents[0] = used;
    const auto settle = [&](std::size_t bound, bool reduced) {
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        for (;;) {
            const auto progress = oa::trim_cold(*runtime.worker.memory, 0);
            check(progress.kind() != orbital::Progress::Kind::failed, "tail maintenance failed");
            if (progress.kind() == orbital::Progress::Kind::done) break;
            check(Clock::now() < deadline, "tail maintenance did not settle");
            loom::poll(runtime.worker, {256, 0});
            std::this_thread::yield();
        }
        const auto capacity = oa::capacity(allocation);
        if (!reduced) check(capacity >= bound, "old reader still protects its full footprint");
        else {
            const auto rounded = (bound + page - 1) / page * page;
            check(capacity >= rounded && capacity <= 2 * rounded && capacity < original_capacity,
                "old tail returns while prefix reader remains, within endpoint-bin bound");
        }
    };
    check(allocation.address == original_address && oa::reserved_bytes(allocation) == original_reservation,
        "logical relinquishment preserves address and VA");
    expect(om::read(old_scope->bind(object)), input);
    expect(om::read(store.borrow().bind(object)), input);
    auto escaped_tail = [&] {
        const auto* payload = orbital::view(escaped).address();
        check(std::all_of(payload + used, payload + old_payload,
            [](auto v) { return v == sp::byte{0x6d}; }), "escaped old tail remains readable and intact");
    };
    settle((draft.offsets[0] + old_payload + page - 1) / page * page, false);
    escaped_tail();
    old_scope.reset(); store.drain();
    settle((draft.offsets[0] + old_payload + page - 1) / page * page, false);
    escaped_tail();
    orbital::release(escaped); escaped = nullptr;
    const auto before_return = runtime.arena->charged_bytes.load();
    settle(wanted, true);
    check(runtime.arena->charged_bytes.load() < before_return, "confirmed tail return refunds backing charge");
    expect(om::read(store.borrow().bind(object)), input);
    check(allocation.address == original_address, "reclaim preserves useful native addresses");
    store = {};
    const auto wanted_old = draft.offsets[0] + old_payload;
    check(bool(oa::grow(allocation, {.bytes = wanted_old, .alignment = 64})), "regrow within retained VA");
    std::fill(allocation.address + wanted, allocation.address + wanted_old, sp::byte{0x42});
    check(allocation.address == original_address && oa::reserved_bytes(allocation) == original_reservation,
        "regrowth reuses the same retained address reservation");
    expect(draft.write().as_const(), input);
}

lc::Outcome expected_chain(std::span<const Row> rows, lc::Request request) {
    lc::Outcome out{request.identity * 0x9e3779b97f4a7c15, request.rank, 0, 1, lc::Code::ok};
    for (unsigned step = 0; step < request.steps; ++step) {
        if (out.rank >= rows.size()) { out.code = lc::Code::rank; break; }
        const auto& row = rows[out.rank];
        if (row.retired) { out.code = lc::Code::retired; break; }
        if (row.length < 12) { out.code = lc::Code::short_value; break; }
        out.digest = std::rotl(out.digest, 9) ^ row.key ^
            (std::uint64_t(row.length) * 0x94d049bb133111eb) ^ out.rank;
        ++out.consumed; out.rank = row.next;
        if (out.rank >= rows.size()) { out.code = lc::Code::invalid_link; break; }
    }
    return out;
}

void loom_publication(Runtime& runtime) {
    om::Store store(*std::pmr::new_delete_resource());
    const auto f = format();
    const auto original = rows(33, 31);
    {
        auto draft = take(om::Draft::create(runtime.worker, f, *std::pmr::new_delete_resource(), 4096), "create Loom source");
        fill(draft, original);
        om::Store::Scope basis(store);
        check(bool(om::publish(store, basis, object, take(draft.capture(store), "capture Loom source"))), "publish Loom source");
    }
    om::Store::Scope old(store);
    const auto selected = old.bind(object);
    const std::array views{om::read(selected)};
    const auto before = snapshot(views[0]);
    auto changed = original;
    changed[0].key += 9000; changed[0].next = 17;
    auto draft = take(om::Draft::clone(runtime.worker, selected), "clone pending Loom source");
    std::array<ikea::journal::entry, 16> ignored_records; ikea::journal ignored{ignored_records};
    check(bool(sp::replace(draft.write(), 0, encode(changed[0]), ignored)), "edit private pending successor");
    auto replacement = take(draft.capture(store), "seal replacement before native region");
    struct Hook {
        om::Store& store;
        const om::Store::Scope& basis;
        const om::Store::Owner& replacement;
        std::span<const Row> rows;
        bool published{};
        static void visit(void* p, std::uint64_t, std::uint64_t) noexcept {
            auto& h = *static_cast<Hook*>(p);
            if (h.published) return;
            check(bool(om::publish(h.store, h.basis, object, h.replacement)), "publish at returned native boundary");
            om::Store::Scope fresh(h.store);
            expect(om::read(fresh.bind(object)), h.rows);
            h.published = true;
        }
    } hook{store, old, replacement, changed};
    std::vector<lc::Request> requests;
    for (unsigned i = 0; i < 41; ++i) requests.push_back({0, i % 33, i % 11 ? 23u + i % 19 : 0u, 100 + i});
    const auto report = lc::assess(runtime.worker, views, requests,
        {lc::Route::ring, 4, 2, 8, 1, true, false}, Hook::visit, &hook);
    check(hook.published && report.outcomes.size() == requests.size(), "pending population crosses publication");
    for (unsigned i = 0; i < requests.size(); ++i)
        check(report.outcomes[i] == expected_chain(original, requests[i]), "old Scope drives all pending successor reads");
    check(snapshot(views[0]) == before && report.samples.size() == 1 &&
        report.samples[0].completed == requests.size(), "complete Loom drain preserves retained four-plane footprint");
    store.drain();
    expect(views[0], original);
    expect(om::read(store.borrow().bind(object)), changed);
}
} // namespace

int main() {
    Runtime runtime;
    for (auto placement : {om::Placement::grouped, om::Placement::separate}) {
        publication(runtime, placement);
        escaping_parts(runtime, placement);
    }
    boundary_and_refusal(runtime);
    transfer_and_repair(runtime);
    growth(runtime);
    loom_publication(runtime);
    for (auto geometry : {sp::geometry::absolute_ends, sp::geometry::sizes})
        for (auto retention : {orbital::PartRetention::shared_control, orbital::PartRetention::selected_copy})
            live_tail(runtime, geometry, retention);
    std::puts("StreamPack Orbital check: publication, escaping Parts, effects, remap, growth, live tail and Loom passed");
}
