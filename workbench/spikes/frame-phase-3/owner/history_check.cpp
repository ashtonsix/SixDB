// The complete owner-composition witness. No Frame kernels or catalog cold
// definitions live here: bind through binding.cpp and link material.cpp.
#include "binding.h"
#include "material.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <source_location>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

namespace fo = frame3_owner;
using u64 = std::uint64_t;
using u128 = unsigned __int128;
namespace {
unsigned checks{};
void check(bool ok, const char *message, std::source_location at = std::source_location::current()) {
    ++checks;
    if (ok) return;
    std::fprintf(stderr, "%s:%u: %s\n", at.file_name(), at.line(), message);
    std::abort();
}
template <class T, class E> T take(std::expected<T, E> result,
    std::source_location at = std::source_location::current()) {
    check(bool(result), "unexpected refusal", at);
    return std::move(*result);
}
struct Memory final : std::pmr::memory_resource {
    std::size_t live{};
    void *do_allocate(std::size_t n, std::size_t align) override {
        void *p = std::pmr::new_delete_resource()->allocate(n, align);
        live += n;
        return p;
    }
    void do_deallocate(void *p, std::size_t n, std::size_t align) override {
        check(live >= n, "material resource accounting underflow");
        live -= n;
        std::pmr::new_delete_resource()->deallocate(p, n, align);
    }
    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
};
constexpr orbital::ObjectId object{0x4652414d4533, 81}, unrelated{0x4652414d4533, 82};
constexpr std::uint64_t schema = 0x553634;
constexpr unsigned group_rows = 4096;
using Bytes = std::vector<std::byte>;
struct Payload {
    std::weak_ptr<const Bytes> lifetime;
};
std::vector<Payload> payloads;
orbital::PartSource fresh(orbital::PartId id, Bytes image) {
    auto backing = std::make_shared<const Bytes>(std::move(image));
    payloads.push_back({backing});
    return {id, *backing, backing, nullptr};
}
std::span<const std::byte> group(fo::Selection selected, unsigned g) {
    return {take(selected.part(g)).address(), take(selected.control().extent(g))};
}
fo::description program(bool bounded) {
    fo::description d;
    d.nodes = {{fo::node_kind::pack, static_cast<unsigned>(fo::pack_preset::compact), 0, {}}};
    if (bounded) {
        d.nodes.push_back({fo::node_kind::bound, 0, 0, {{0, fo::coordinate::identity}}});
        d.root = 1;
    }
    return d;
}
fo::description program(fo::Selection selected) {
    return take(fo::decode(selected.control().meaning().pipeline));
}
fo::Store::Owner build(fo::Store &store, Memory &memory, orbital::ObjectId id,
    std::span<const u64> values, bool bounded, fo::fitting_policy policy = {}) {
    const auto description = program(bounded);
    frame3::column built(take(fo::make_tree(description, policy)), values);
    std::vector<Bytes> images;
    fo::interpretation meaning{take(fo::encode(description)), values.size(), {}, schema, id};
    for (unsigned g = 0; g < built.root().groups_in(0); ++g) {
        images.push_back(built.image(0, g));
        meaning.group_extents.push_back(images.back().size());
    }
    auto metadata = take(fo::interpretation_owner::make(memory, std::move(meaning)));
    std::vector<orbital::PartSource> sources;
    for (unsigned g = 0; g < images.size(); ++g)
        sources.push_back(fresh(metadata.control().id(g), std::move(images[g])));
    return take(fo::capture(store, metadata, sources));
}
fo::Store::Owner replace_group(fo::Store &store, Memory &memory, fo::Selection basis,
    unsigned g, Bytes image, std::size_t rows) {
    auto meaning = basis.control().meaning();
    meaning.rows = rows;
    meaning.group_extents[g] = image.size();
    auto metadata = take(fo::interpretation_owner::make(memory, std::move(meaning)));
    std::vector<orbital::PartSource> sources;
    for (unsigned i = 0; i < basis.part_count(); ++i) {
        auto source = i == g ? fresh(metadata.control().id(i), std::move(image)) : take(fo::reuse(basis, i));
        source.id = metadata.control().id(i);
        sources.push_back(std::move(source));
    }
    return take(fo::capture(store, metadata, sources));
}
// The caller supplies the exact Scope used to derive the work. Never replace
// it with a fresh Scope merely because preparation has finished.
void publish(fo::Store &store, const fo::Store::Scope &basis, orbital::ObjectId id,
    const fo::Store::Owner &owner) {
    fo::Store::Prepared packet(store);
    const std::array changes{fo::Store::Change{id, owner}};
    check(bool(store.prepare(basis, changes, packet)), "publication preparation refused");
    check(bool(store.install(packet)), "publication installation refused");
}
std::unique_ptr<fo::binding> attach(fo::Selection selected) {
    const auto description = program(selected);  // This transient decoded tree may now die.
    std::vector<fo::borrowed_group> groups;
    for (unsigned g = 0; g < selected.part_count(); ++g) groups.push_back({g, group(selected, g)});
    return take(fo::binding::attach(description, selected.control().meaning().rows, groups));
}
u128 sum(std::span<const u64> values) {
    u128 result = 0;
    for (u64 x : values) result += x;
    return result;
}
void answers(const fo::binding &bound, std::span<const u64> values) {
    check(bound.size() == values.size(), "binding lost selected logical extent");
    std::vector<u64> decoded(values.size());
    check(bool(bound.read(0, decoded)), "full read refused");
    check(std::equal(values.begin(), values.end(), decoded.begin()), "read differs from original-row oracle");
    const auto blocks = frame3::first_n(static_cast<unsigned>((values.size() + 255) / 256));
    check(take(bound.sum(blocks)) == sum(values), "exact UInt128 SUM differs from direct rows");
    for (u64 threshold : {u64{0}, u64{1007}, std::numeric_limits<u64>::max()}) {
        u64 count = 0;
        for (u64 x : values) count += x >= threshold;
        check(take(bound.count(ikea::comparison::greater_equal, blocks, threshold)) == count,
            "COUNT differs from direct rows");
    }
    // Fixed independent points, including duplicates, endpoints and the tail.
    for (unsigned r : {0u, 3u, 3u, 5u, 255u, 4095u, static_cast<unsigned>(values.size() - 1)})
        check(take(bound.get(r)) == values[r], "point differs from direct rows");
}
fo::preparation_budget budget(std::span<const std::byte> source, std::size_t commands) {
    return {true, source.size(), commands, 64, 1024 * 1024, true};
}
std::size_t serialized_bytes(fo::Selection selected) {
    std::size_t result = 0;
    for (unsigned g = 0; g < selected.part_count(); ++g) result += group(selected, g).size();
    return result;
}
std::size_t capacity_of(const std::byte *address) {
    for (const auto &note : payloads)
        if (auto backing = note.lifetime.lock(); backing && backing->data() == address)
            return backing->capacity();
    check(false, "selected payload has no live allocation witness");
    return 0;
}
std::size_t old_extra_capacity(fo::Selection old, fo::Selection current) {
    // This fixture supplies one independent vector allocation per group;
    // retained sources share that exact allocation. No owner is retained here.
    std::size_t result = 0;
    for (unsigned g = 0; g < old.part_count(); ++g) {
        const auto *address = group(old, g).data();
        bool shared = false;
        for (unsigned h = 0; h < current.part_count(); ++h) shared |= address == group(current, h).data();
        if (!shared) result += capacity_of(address);
    }
    return result;
}
bool same_sources(fo::Selection a, fo::Selection b) {
    if (a.part_count() != b.part_count() || a.control().meaning().rows != b.control().meaning().rows ||
        a.control().meaning().pipeline != b.control().meaning().pipeline) return false;
    for (unsigned g = 0; g < a.part_count(); ++g)
        if (group(a, g).data() != group(b, g).data() || group(a, g).size() != group(b, g).size()) return false;
    return true;
}
void refusals(fo::Selection selected, std::span<const fo::command> commands,
    fo::fitting_policy policy, std::span<const u64> original) {
    const auto d = program(selected);
    const auto source = group(selected, 0);
    const Bytes before(source.begin(), source.end());
    auto denied = budget(source, commands.size());
    denied.source_readable = false;
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    const auto allocation = ((source.size() + page - 1) / page) * page;
    void *guard = mmap(nullptr, allocation, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(guard != MAP_FAILED, "guard allocation failed");
    auto result = fo::prepare_group(d, original.size(), 0,
        {static_cast<const std::byte *>(guard), source.size()}, commands, denied, policy);
    check(!result && result.error() == fo::binding_error::authority, "denied source was touched or admitted");
    check(munmap(guard, allocation) == 0, "guard release failed");
    for (unsigned kind = 0; kind < 5; ++kind) {
        auto resources = budget(source, commands.size());
        if (kind == 0) --resources.source_copy_bytes;
        if (kind == 1) --resources.commands;
        if (kind == 2) resources.effect_records = 0;
        if (kind == 3) resources.output_bytes = 0;
        if (kind == 4) resources.allow_rebuild = false; // Late refusal after private earlier commands.
        result = fo::prepare_group(d, original.size(), 0, source, commands, resources, policy);
        const auto error = kind == 2 ? fo::binding_error::effects :
                           kind == 4 ? fo::binding_error::unsupported : fo::binding_error::resources;
        check(!result && result.error() == error, "private admission refusal has wrong result");
        check(std::equal(source.begin(), source.end(), before.begin()), "refusal changed immutable source");
        answers(*attach(selected), original);
    }
    // Original full-part coordinates survive partial attachment: group zero
    // has no supplied address at all; only the partial last group is attached.
    const std::array only{fo::borrowed_group{1, group(selected, 1)}};
    const auto partial = take(fo::binding::attach(d, original.size(), only));
    check(take(partial->get(group_rows)) == original[group_rows], "available point lost original row coordinates");
    check(!partial->get(3), "absent group was read as a constant");
    frame3::bitset256 last;
    last.set(16), last.set(17);
    check(take(partial->sum(last)) == sum(original.subspan(group_rows)), "partial group query lost original block coordinates");
    check(take(partial->count(ikea::comparison::greater_equal, last, 0)) == original.size() - group_rows,
        "partial group COUNT has wrong extent");
    std::array<u64, 2> out{55, 66};
    const auto read = partial->read(group_rows - 1, out);
    check(!read && read.error() == fo::binding_error::unavailable && out == std::array<u64, 2>{55, 66},
        "cross-group read wrote output before complete availability admission");
    auto blocks = last;
    blocks.set(0);
    check(!partial->sum(blocks) && !partial->count(ikea::comparison::greater_equal, blocks, 0),
        "coarse read silently dropped absent blocks");
    frame3::block_rows_set fine;
    fine[17].set(255); // Last block has only fifteen live rows in this fixture.
    frame3::bitset256 last_block;
    last_block.set(17);
    const auto invalid_fine = partial->sum_selected(last_block, fine);
    check(!invalid_fine && invalid_fine.error() == fo::binding_error::range,
        "fine bits beyond the last partial block were admitted");
}
} // namespace

int main() {
    Memory memory;
    std::size_t copied{}, emitted{}, published{}, reused{}, retained{};
    {
        fo::Store store(memory);
        std::vector<u64> original(group_rows + 271);
        for (std::size_t r = 0; r < original.size(); ++r) original[r] = 1000 + (r / 256) * 32 + r % 8;
        const fo::fitting_policy policy{0, 64, 2};
        {
            const auto initial = build(store, memory, object, original, true, policy);
            fo::Store::Scope empty(store);
            publish(store, empty, object, initial);
        } // Factory description, builder, metadata owner and capture sources have all died.
        fo::Store::Scope first(store);
        const auto old = first.bind(object);
        auto old_reader = attach(old);
        const auto initial_description = old.control().meaning().pipeline;
        check(old.part_count() == 2, "fixture requires exactly two groups");
        answers(*old_reader, original);
        const auto source = group(old, 0);
        const Bytes source_before(source.begin(), source.end());
        const std::array commands{
            fo::command{3, u64{1} << 62}, fo::command{3, (u64{1} << 62) + 1},
            fo::command{5, std::numeric_limits<u64>::max()}, fo::command{6, (u64{1} << 62) + 2},
            fo::command{3, original[3]}, fo::command{6, original[6]}, fo::command{0, 0}};
        auto expected = original;
        for (auto c : commands) expected[c.row] = c.value;
        const __int128 contribution = static_cast<__int128>(sum(expected)) - static_cast<__int128>(sum(original));
        refusals(old, commands, policy, original);
        check(std::equal(source.begin(), source.end(), source_before.begin()), "refusal suite changed source");
        __int128 published_contribution = 0;
        std::size_t stale_payload;
        {
            auto prepared = take(fo::prepare_group(program(old), original.size(), 0, source, commands,
                budget(source, commands.size()), policy));
            check(prepared.rebuilds != 0, "ordered temporary outliers failed to exhaust slack");
            check(prepared.contribution == contribution, "ordered batch contribution differs from final-row oracle");
            const auto changed = std::span(expected).first(group_rows);
            check(prepared.sum == sum(changed) && prepared.min == *std::min_element(changed.begin(), changed.end()) &&
                prepared.max == *std::max_element(changed.begin(), changed.end()), "private summary differs from row oracle");
            check(prepared.min == 0 && prepared.max == std::numeric_limits<u64>::max() && prepared.sum > std::numeric_limits<u64>::max(),
                "full UInt64 and UInt128 witness is ineffective");
            copied += prepared.copied_bytes; emitted += prepared.new_bytes;
            stale_payload = payloads.size();
            auto candidate = replace_group(store, memory, old, 0, std::move(prepared.image), original.size());
            // Another publication can occur during Frame's private preparation.
            const std::array<u64, 2> other_values{7, 8};
            const auto other = build(store, memory, unrelated, other_values, false);
            fo::Store::Scope unrelated_basis(store);
            publish(store, unrelated_basis, unrelated, other);
            fo::Store::Prepared stale(store);
            const std::array changes{fo::Store::Change{object, candidate}};
            const auto refused = store.prepare(first, changes, stale);
            check(!refused && refused.error() == orbital::Error::stale, "original preparation Scope was silently replaced");
            check(!store.install(stale) && stale.disposition() == orbital::PublicationState::empty,
                "unaccepted stale work installed");
            check(same_sources(old, store.borrow().bind(object)) && published_contribution == 0,
                "private or stale preparation exposed changes or contributions");
            answers(*attach(store.borrow().bind(object)), original);
        }
        check(payloads[stale_payload].lifetime.expired(), "discarded stale afterimage retained its private payload");
        // Explicit retry: the fresh Scope is named, and its actual source is
        // revalidated before rerunning preparation. No hidden publication helper.
        fo::Store::Scope retry_basis(store);
        const auto retry_source = retry_basis.bind(object);
        check(same_sources(old, retry_source), "unrelated publication changed the source to be retried");
        auto prepared = take(fo::prepare_group(program(retry_source), original.size(), 0, group(retry_source, 0),
            commands, budget(group(retry_source, 0), commands.size()), policy));
        check(prepared.contribution == contribution && prepared.rebuilds != 0, "retry changed ordered command semantics");
        copied += prepared.copied_bytes; emitted += prepared.new_bytes;
        const auto installed_bytes = prepared.new_bytes;
        const auto private_issued = prepared.issued_bytes;
        {
            auto candidate = replace_group(store, memory, retry_source, 0, std::move(prepared.image), original.size());
            const auto private_reader = attach(candidate.selection());
            answers(*private_reader, expected);
            check(group(candidate.selection(), 1).data() == group(old, 1).data(), "untouched group copied into private work");
            fo::Store::Prepared packet(store), excluded(store);
            const std::array changes{fo::Store::Change{object, candidate}};
            check(bool(store.prepare(retry_basis, changes, packet)), "retry publication preparation refused");
            const auto competing = store.prepare(retry_basis, changes, excluded);
            check(!competing && competing.error() == orbital::Error::unavailable, "accepted packet failed to exclude another publication");
            check(published_contribution == 0 && private_issued != 0, "private effects leaked before installation");
            answers(*attach(retry_basis.bind(object)), original);
            check(bool(store.install(packet)), "retry failed to install");
            published_contribution += prepared.contribution; // Public summaries advance only here.
            published += installed_bytes; reused += group(old, 1).size();
        }
        fo::Store::Scope updated(store);
        const auto after_update = updated.bind(object);
        answers(*attach(after_update), expected);
        check(static_cast<__int128>(sum(original)) + published_contribution == static_cast<__int128>(sum(expected)),
            "installed contribution and data disagree");
        check(group(after_update, 1).data() == group(old, 1).data() && group(after_update, 0).data() != group(old, 0).data(),
            "batch publication did not replace exactly its group");
        retained = old_extra_capacity(old, after_update);
        check(retained == capacity_of(group(old, 0).data()), "old-reader physical backing account is wrong");
        check(std::equal(source.begin(), source.end(), source_before.begin()), "successful preparation mutated original image");
        answers(*old_reader, original);

        // Append extends only the last group. The initial implementation has no
        // owner append command: build the equivalent new image through shared
        // Frame code, then publish its changed last group and reuse group zero.
        auto grown = expected;
        for (unsigned i = 0; i < 300; ++i) grown.push_back(9000 + i % 13);
        {
            const auto full = build(store, memory, object, grown, true, policy);
            const auto last = group(full.selection(), 1);
            auto appended = replace_group(store, memory, after_update, 1, Bytes(last.begin(), last.end()), grown.size());
            published += last.size(); reused += group(after_update, 0).size();
            publish(store, updated, object, appended);
        }
        fo::Store::Scope appended_scope(store);
        const auto appended = appended_scope.bind(object);
        check(appended.control().meaning().rows == grown.size() && old.control().meaning().rows == original.size(),
            "append changed an older logical interpretation");
        check(group(appended, 0).data() == group(after_update, 0).data(), "append copied untouched group zero");
        answers(*attach(appended), grown);
        answers(*attach(updated.bind(object)), expected);
        answers(*old_reader, original);

        // Conversion uses a complete logical rebuild into pack-only, never a
        // reinterpretation of existing bytes. Adoption preserves logical time.
        {
            auto target = take(store.conversion(appended_scope, object));
            const auto converted = build(store, memory, object, grown, false);
            answers(*attach(converted.selection()), grown); // Exact equivalence is the converter's proof.
            const auto cut = appended_scope.cut();
            check(bool(store.adopt(target, converted)), "equivalent pack-only conversion refused");
            check(store.borrow().cut == cut, "pure conversion advanced logical time");
            const auto alternative = take(appended_scope.bind_alternative(object));
            check(!take(fo::binding_recipe(program(alternative))).bounded &&
                take(fo::binding_recipe(program(appended))).bounded, "conversion lost independent interpretation identities");
            answers(*attach(alternative), grown);
            published += serialized_bytes(alternative);
            retained = old_extra_capacity(old, alternative);
        } // Conversion builders, descriptions and owners have gone.
        const auto alternative = take(appended_scope.bind_alternative(object));
        answers(*attach(alternative), grown);
        check(first.bind(object).control().meaning().pipeline == initial_description,
            "later pipeline conversion changed old material interpretation");
        answers(*attach(first.bind(object)), original);
        answers(*old_reader, original);
        std::printf("Frame owner history: %u checks; successful_private_source_copy_bytes=%zu emitted_group_bytes=%zu "
                    "published_fresh_image_bytes=%zu reused_image_bytes=%zu old_scope_extra_capacity_bytes=%zu\n",
                    checks, copied, emitted, published, reused, retained);
        std::puts("Payload accounting uses actual vector capacities for old/current set difference; "
                  "material metadata, allocator/shared-control overhead and failed-attempt work are excluded. "
                  "Append/conversion use full rebuilds; these are correctness witnesses, not optimized timings.");
    }
    for (const auto &p : payloads) check(p.lifetime.expired(), "payload survived all owners and Scopes");
    check(memory.live == 0, "material/catalog storage survived fixture teardown");
    std::printf("Frame owner history complete: %u checks\n", checks);
}
