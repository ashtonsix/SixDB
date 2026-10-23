#include "phase_construction.h"
#include "phase_owner.h"
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <chrono>
#include <random>
#include <optional>
#include <new>
#include <source_location>
#include <thread>
#include <vector>

namespace p = frame_lifecycle::phase;
namespace {
void need(bool ok, const char* why) { if (!ok) { std::fprintf(stderr, "%s\n", why); std::abort(); } }
p::GroupData data(unsigned width, bool nullable) {
    p::GroupData out; std::mt19937_64 random(713);
    const auto maximum = width == 64 ? UINT64_MAX : (p::u64{1} << width) - 1;
    for (unsigned i = 0; i < p::rows_per_group; ++i) {
        out.value[i] = width == 64 ? random() : (p::u64{1} << 63) + (random() & maximum);
        if (nullable) out.set(i, {out.value[i], i % 37 != 0, i % 19 != 0});
    }
    out.value[1] = width == 64 ? 0 : p::u64{1} << 63;
    out.value[2] = width == 64 ? UINT64_MAX : (p::u64{1} << 63) + maximum;
    return out;
}
p::QueryResult expected(const p::GroupData& d, unsigned mask, p::u64 threshold) {
    p::QueryResult result;
    for (unsigned i = 0; i < p::rows_per_group; ++i) if (mask >> (i / 256) & 1) {
        const auto row = d.row(i); result.present += row.present;
        if (row.contributes()) { result.sum += row.value; result.count += row.value >= threshold; }
    }
    return result;
}
template<class Base>
unsigned verify(const Base& base, const std::vector<std::byte>& values,
                const std::vector<std::byte>& flags, const p::GroupData& original) {
    p::Bytes v{values.data(), values.size()}, f{flags.data(), flags.size()};
    need(p::admit(base, v, f), "initialized base refused");
    unsigned checks = 0;
    for (unsigned i = 0; i < p::rows_per_group; ++i) {
        const auto row = p::point(base, v, f, i), wanted = original.row(i);
        need(row.present == wanted.present && row.valid == wanted.valid &&
             (!row.contributes() || row.value == wanted.value), "point differs from original values/flags");
        ++checks;
    }
    for (bool reference : {false,true}) {
    if (reference && std::is_same_v<Base,p::NativeBase>) continue;
    p::QueryCache cache(1729,reference); // Deliberately different: query must rebind its RHS.
    for (auto threshold : {p::u64{0}, original.value[1], original.value[77], UINT64_MAX})
        for (auto mask : {0u, 65535u, 0xaaaau, 0x00ffu, 0x8001u}) {
            const auto result = p::query(base, v, f, mask, threshold, cache);
            need(result == expected(original, mask, threshold), "complete masked SUM/COUNT/presence differs");
            ++checks;
        }
    }
    auto broken_flags = flags;
    broken_flags[p::flag_bytes] = std::byte{255}; broken_flags[0] = std::byte{0};
    need(!p::admit(base, v, {broken_flags.data(), broken_flags.size()}), "invalid validity subset admitted");
    return checks;
}
unsigned encoded(const p::GroupData& original, frame_encoding::encode_options options) {
    p::EncodedConstruction built(original, options);
    std::vector<std::byte> values(built.base.header.bytes), flags(p::flag_bytes * 2);
    built.copy_values(values); p::copy_flags(original, flags);
    unsigned count = verify(built.base, values, flags, original);
    auto current = original;
    std::vector<p::OverrideRow> entries;
    for (auto position : {0u, 7u, 255u, 256u, 1023u, 4095u}) {
        const auto before = original.row(position);
        p::LogicalRow after{position % 2 ? UINT64_MAX : 0, position % 3 != 0, position % 5 != 0};
        after.valid &= after.present;
        entries.push_back({std::uint16_t(position), before, after}); current.set(position, after);
    }
    const p::Overlay metadata{std::uint16_t(entries.size()), built.base.header.identity};
    std::vector<std::byte> overlay(p::Overlay::bytes(metadata.count));
    p::copy_overlay(entries, overlay, metadata.basis); const p::Bytes extra{overlay.data(), overlay.size()};
    need(p::admit(metadata, extra), "well-formed overlay refused");
    p::QueryCache cache(17);
    for (auto threshold : {p::u64{0}, original.value[77], UINT64_MAX})
        for (auto mask : {0u, 65535u, 0xaaaau, 0x00ffu, 0x8001u}) {
            need(p::query_overlay(built.base, {values.data(), values.size()}, {flags.data(), flags.size()},
                    metadata, extra, mask, threshold, cache) == expected(current, mask, threshold),
                 "coalesced logical correction differs from latest rows"); ++count;
        }
    auto duplicate = overlay;
    std::memcpy(duplicate.data() + 10, duplicate.data() + 8, 2);
    need(!p::admit(metadata, {duplicate.data(), duplicate.size()}), "duplicate overlay position admitted");
    return count;
}
struct Pages {
    std::byte* address; std::size_t bytes;
    explicit Pages(std::size_t needed) {
        const auto page = std::size_t(sysconf(_SC_PAGESIZE)); bytes = (needed + page - 1) / page * page;
        address = static_cast<std::byte*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)); need(address != MAP_FAILED, "mmap failed");
    }
    void protect(int access) { need(!mprotect(address, bytes, access), "mprotect failed"); }
    ~Pages() { mprotect(address, bytes, PROT_READ | PROT_WRITE); munmap(address, bytes); }
};
void protected_none() {
    auto original = data(16, false); p::EncodedConstruction built(original);
    Pages metadata(sizeof(p::EncodedBase)), values(built.base.header.bytes), flags(p::flag_bytes * 2);
    auto* m = std::construct_at(reinterpret_cast<p::EncodedBase*>(metadata.address), built.base);
    built.copy_values({values.address, built.base.header.bytes});
    p::copy_flags(original, {flags.address, p::flag_bytes * 2}); p::QueryCache cache(0);
    metadata.protect(PROT_NONE); values.protect(PROT_NONE); flags.protect(PROT_NONE);
    need(p::query(*m, {values.address, built.base.header.bytes}, {flags.address, p::flag_bytes * 2},
                  0, 0, cache) == p::QueryResult{}, "none query accessed source");
    const auto child = fork(); need(child >= 0, "fork failed");
    if (!child) { (void)p::query(*m, {values.address, built.base.header.bytes},
        {flags.address, p::flag_bytes * 2}, 1, 0, cache); _exit(0); }
    int status; need(waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
        (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS), "positive source read did not fault");
    metadata.protect(PROT_READ | PROT_WRITE); std::destroy_at(m);
}

using p::take; using p::Memory; using p::Runtime; using p::base;
template<class C> void history() {
    namespace alloc = orbital::allocation;
    using Group = typename decltype(C{}.group)::value_type;
    using Store = orbital::ObjectStore<p::Physical<C>>;
    constexpr unsigned slots = p::groups * Group::parts;
    Runtime runtime; Memory memory;
    Store store(memory); C control; control.memory = &memory;
    std::array<p::GroupData, 2> original{data(16, true), data(64, false)};
    const auto publish = [&](const typename Store::Owner& owner) {
        typename Store::Prepared packet(store);
        const std::array<typename Store::Change, 1> changes{{{p::object, owner}}};
        { typename Store::Scope basis(store); take(store.prepare(basis, changes, packet)); }
        take(store.install(packet));
    };
    {
        std::array<alloc::Allocation, slots> owned;
        std::array<alloc::Hold, slots> holds;
        std::array<orbital::PartSource, slots> sources;
        for (unsigned g = 0; g < 2; ++g) {
            auto& target = base(control.group[g]);
            const auto install = [&](const auto& construction) {
                target = construction.base;
                const auto i = g * Group::parts;
                owned[i] = runtime.allocate(target.header.bytes);
                holds[i] = take(alloc::hold(owned[i], {0, owned[i].size}));
                construction.copy_values({owned[i].address, owned[i].size});
            };
            if constexpr (std::is_same_v<Group, p::NativeBase>) install(p::NativeConstruction(original[g]));
            else install(p::EncodedConstruction(original[g], {.delta_exceptions=true,
                .dictionary=frame_encoding::dictionary_order::frequency, .dictionary_every_part=true}));
            const auto f = g * Group::parts + 1;
            owned[f] = runtime.allocate(p::flag_bytes * 2);
            holds[f] = take(alloc::hold(owned[f], {0, owned[f].size}));
            p::copy_flags(original[g], {owned[f].address, owned[f].size});
            if constexpr (Group::parts == 3) {
                auto& extra = control.group[g].changes;
                extra.basis = target.header.identity;
                owned[f + 1] = runtime.allocate(p::Overlay::bytes(0));
                holds[f + 1] = take(alloc::hold(owned[f + 1], {0, owned[f + 1].size}));
                p::copy_overlay({}, {owned[f + 1].address, owned[f + 1].size}, extra.basis);
            }
        }
        for (unsigned i = 0; i < slots; ++i)
            sources[i] = {control.id(i), alloc::bytes(owned[i]), {}, &owned[i]};
        const auto owner = take(store.capture(control, sources)); publish(owner);
    }
    std::optional<typename Store::Scope> old(std::in_place, store);
    const auto before = old->bind(p::object);
    auto current_data = original; current_data[0].set(7, {0, true, false});
    auto next = control;
    {
        std::array<orbital::PartSource, slots> sources;
        for (unsigned i = 0; i < slots; ++i) sources[i] = take(orbital::source(before, i));
        alloc::Allocation fresh; alloc::Hold hold;
        if constexpr (Group::parts == 3) {
            next.group[0].changes.count = 1;
            fresh = runtime.allocate(p::Overlay::bytes(1));
            hold = take(alloc::hold(fresh, {0, fresh.size}));
            const std::array entry{p::OverrideRow{7, original[0].row(7), current_data[0].row(7)}};
            p::copy_overlay(entry, {fresh.address, fresh.size}, next.group[0].changes.basis);
            sources[2] = {next.id(2), alloc::bytes(fresh), {}, &fresh};
        } else {
            auto& h = base(next.group[0]).header;
            const auto old_header = h; h = p::summarize(current_data[0]);
            h.bytes = old_header.bytes; h.base = old_header.base; h.native_bytes = old_header.native_bytes;
            fresh = runtime.allocate(p::flag_bytes * 2); hold = take(alloc::hold(fresh, {0, fresh.size}));
            p::copy_flags(current_data[0], {fresh.address, fresh.size});
            sources[1] = {next.id(1), alloc::bytes(fresh), {}, &fresh};
        }
        const auto owner = take(store.capture(next, sources)); publish(owner);
        need(!fresh, "publication failed to transfer fresh allocation");
    }
    std::optional<typename Store::Scope> now(std::in_place, store);
    const auto after = now->bind(p::object);
    need(take(before.part(0)).address() == take(after.part(0)).address(),
         "NULL-only update copied the value plane");
    const auto evaluate = [&](auto selected, const auto& wanted) {
        auto c = selected.control(); p::QueryCache cache(0);
        for (unsigned g = 0; g < 2; ++g) {
            const auto n = g * Group::parts;
            const auto v = take(selected.part(n)), f = take(selected.part(n + 1));
            const p::Bytes values{v.address(), base(c.group[g]).header.bytes};
            const p::Bytes flags{f.address(), p::flag_bytes * 2};
            for (auto threshold : {p::u64{0}, wanted[g].value[77], UINT64_MAX}) {
                p::QueryResult result;
                if constexpr (Group::parts == 3) {
                    const auto x = take(selected.part(n + 2));
                    result = p::query_overlay(c.group[g].frozen, values, flags, c.group[g].changes,
                        {x.address(), p::Overlay::bytes(c.group[g].changes.count)}, UINT16_MAX, threshold, cache);
                } else result = p::query(c.group[g], values, flags, UINT16_MAX, threshold, cache);
                need(result == expected(wanted[g], UINT16_MAX, threshold), "published/retained image differs");
            }
        }
    };
    evaluate(before, original); evaluate(after, current_data); evaluate(before, original);
    old.reset(); store.drain(); evaluate(after, current_data);
    // Folding is a physical alternative of the same logical version. Primary
    // and folded reads must both remain exact, but their dependency bundles
    // are not interchangeable merely because their Object version is equal.
    {
        auto converted = next;
        std::array<orbital::PartSource, slots> sources;
        for (unsigned i = 0; i < slots; ++i) sources[i] = take(orbital::source(after, i));
        std::array<alloc::Allocation, Group::parts> fresh;
        std::array<alloc::Hold, Group::parts> holds;
        const auto install = [&](const auto& construction) {
            base(converted.group[0]) = construction.base;
            fresh[0] = runtime.allocate(construction.base.header.bytes);
            holds[0] = take(alloc::hold(fresh[0], {0, fresh[0].size}));
            construction.copy_values({fresh[0].address, fresh[0].size});
        };
        if constexpr (std::is_same_v<Group, p::NativeBase>) install(p::NativeConstruction(current_data[0]));
        else install(p::EncodedConstruction(current_data[0], {.delta_exceptions=true}));
        fresh[1] = runtime.allocate(p::flag_bytes * 2);
        holds[1] = take(alloc::hold(fresh[1], {0, fresh[1].size}));
        p::copy_flags(current_data[0], {fresh[1].address, fresh[1].size});
        if constexpr (Group::parts == 3) {
            auto& folded = converted.group[0];
            folded.changes = {0, folded.frozen.header.identity};
            auto mixed = folded; mixed.changes = next.group[0].changes;
            need(!mixed.valid(), "equal-version bases accepted an old correction bundle");
            fresh[2] = runtime.allocate(p::Overlay::bytes(0));
            holds[2] = take(alloc::hold(fresh[2], {0, fresh[2].size}));
            p::copy_overlay({}, {fresh[2].address, fresh[2].size}, folded.changes.basis);
            const auto old_extra = take(after.part(2));
            need(!p::admit({next.group[0].changes.count, folded.changes.basis},
                          {old_extra.address(), p::Overlay::bytes(next.group[0].changes.count)}),
                 "old overlay payload admitted against a folded base");
        }
        for (unsigned i = 0; i < Group::parts; ++i)
            sources[i] = {converted.id(i), alloc::bytes(fresh[i]), {}, &fresh[i]};
        const auto owner = take(store.capture(converted, sources));
        const auto target = take(store.conversion(*now, p::object));
        take(store.adopt(target, owner));
        const auto alternative = take(now->bind_alternative(p::object));
        need(alternative.realization != after.realization, "fold did not install an alternative");
        evaluate(alternative, current_data); evaluate(after, current_data);
    }
    std::array<orbital::Part<C>*, Group::parts> escaped{};
    {
        auto bound = take(store.bound_alternative(*now, p::object));
        for (unsigned i = 0; i < Group::parts; ++i)
            escaped[i] = take(bound.export_part(i, orbital::PartRetention::selected_copy));
    }
    now.reset(); store = {};
    need(orbital::binding(escaped[0]).version == 2, "physical fold changed the logical version");
    const auto& c = escaped[0]->control;
    const auto& frozen = base(c.group[0]);
    const p::Bytes values{orbital::view(escaped[0]).address(), frozen.header.bytes};
    const p::Bytes flags{orbital::view(escaped[1]).address(), p::flag_bytes * 2};
    need(base(escaped[1]->control.group[0]).header.identity == frozen.header.identity,
         "escaped validity and values belong to different bases");
    p::QueryCache cache(77); p::QueryResult result;
    if constexpr (Group::parts == 3) {
        const auto x = escaped[2]; const auto extra = x->control.group[0].changes;
        need(extra.basis == frozen.header.identity, "escaped overlay belongs to a different base");
        result = p::query_overlay(frozen, values, flags, extra,
            {orbital::view(x).address(), p::Overlay::bytes(extra.count)}, UINT16_MAX, 77, cache);
    } else result = p::query(frozen, values, flags, UINT16_MAX, 77, cache);
    need(result == expected(current_data[0], UINT16_MAX, 77), "escaped logical bundle lost meaning");
    for (auto* part : escaped) orbital::release(part);
    need(!memory.live, "escaped bundle left catalog/interpretation metadata live");
}
unsigned nullable_construction() {
    unsigned checks=0;const auto source=data(64,true);
    for (unsigned cp=0;cp<4;++cp) for (auto order:{p::fe::dictionary_order::none,p::fe::dictionary_order::sorted,
            p::fe::dictionary_order::frequency}) for (auto fill:{p::fe::null_fill::predictor,p::fe::null_fill::zero}) {
        p::EncodedConstruction built(source,{.delta_checkpoints=p::fe::checkpoints(cp),.dictionary=order,
            .dictionary_every_part=order!=p::fe::dictionary_order::none},fill);
        std::vector<std::byte> values(built.base.header.bytes),flags(p::flag_bytes*2);
        built.copy_values(values);p::copy_flags(source,flags);checks+=verify(built.base,values,flags,source);
    }
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));const auto bytes=(sizeof(p::GroupData)+page-1)/page*page;
    void* memory=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    need(memory!=MAP_FAILED,"nullable construction mmap failed");
    auto* hidden=::new(memory) p::GroupData;hidden->valid.fill(0);
    need(!(sizeof(hidden->value)%page) && mprotect(memory,sizeof(hidden->value),PROT_NONE)==0,"NULL input protection failed");
    for (auto fill:{p::fe::null_fill::predictor,p::fe::null_fill::zero}) {
        p::EncodedConstruction built(*hidden,{},fill);
        std::vector<std::byte> values(built.base.header.bytes),flags(p::flag_bytes*2);
        built.copy_values(values);p::copy_flags(*hidden,flags);p::QueryCache cache(0);
        const auto answer=p::query(built.base,{values.data(),values.size()},{flags.data(),flags.size()},UINT16_MAX,0,cache);
        need(answer==p::QueryResult{0,0,p::rows_per_group},"all-NULL owner construction lost presence");++checks;
    }
    need(mprotect(memory,sizeof(hidden->value),PROT_READ|PROT_WRITE)==0,"NULL input restore failed");
    hidden->~GroupData();munmap(memory,bytes);return checks;
}
}
int main() {
    unsigned checks = 0;
    for (bool nullable : {false, true}) {
        for (unsigned width : {8u, 16u, 32u, 64u}) {
            auto source = data(width, nullable);
            for (bool raw : {false, true}) {
                p::NativeConstruction built(source, raw);
                std::vector<std::byte> values(built.base.header.bytes), flags(p::flag_bytes * 2);
                built.copy_values(values); p::copy_flags(source, flags);
                checks += verify(built.base, values, flags, source);
            }
        }
        auto source = data(64, nullable);
        for (auto order : {frame_encoding::dictionary_order::none, frame_encoding::dictionary_order::sorted,
                           frame_encoding::dictionary_order::frequency})
            checks += encoded(source, {.delta_exceptions = true, .dictionary = order,
                                      .dictionary_every_part = order != frame_encoding::dictionary_order::none});
    }
    checks+=nullable_construction();protected_none();
    history<p::EncodedLayout>(); history<p::OverlayLayout>(); history<p::NativeLayout>();
    std::printf("%u phase material checks pass: native8/16/32/64, exact u128, separate presence/validity,"
                " original coarse masks, dictionary/RHS cache changes, overlay corrections and protected none.\n", checks);
}
