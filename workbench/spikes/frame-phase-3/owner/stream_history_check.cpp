// Real separated size/relative-end interval/checkpoint streams through the public material seam.
// Updates here rebuild shared Frame stages for correctness, not a fast writer.
#include "material.h"
#include "stream_binding.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <source_location>
#include <span>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace fo = frame3_owner;
namespace {
using u64 = frame3::u64;
using u128 = frame3::u128;
using Bytes = std::vector<std::byte>;
constexpr unsigned rows = 4367;
constexpr std::uint64_t logical_schema = 0x553634;
std::size_t checks{};
void check(bool condition, const char *what,
    std::source_location where = std::source_location::current()) {
    ++checks;
    if (condition) return;
    std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), what);
    std::abort();
}
template <class T, class E> T take(std::expected<T, E> result,
    std::source_location where = std::source_location::current()) {
    check(bool(result), "unexpected refusal", where);
    return std::move(*result);
}
struct Memory final : std::pmr::memory_resource {
    std::size_t live{};
    void *do_allocate(std::size_t bytes, std::size_t alignment) override {
        void *result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        live += bytes;
        return result;
    }
    void do_deallocate(void *pointer, std::size_t bytes, std::size_t alignment) override {
        check(live >= bytes, "material PMR accounting underflow");
        live -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
};
struct image {
    fo::description program;
    std::array<Bytes, 3> parts;  // root groups0/1 and checkpoint group0
    std::vector<fo::stream_manifest> streams;
};
image build(unsigned G, fo::node_kind interval_kind, std::span<const u64> values) {
    image out;
    out.program.nodes = {
        {fo::node_kind::pack, static_cast<std::uint16_t>(fo::pack_preset::compact), 0, {}},
        {fo::node_kind::bound, 0, 0, {{0, fo::coordinate::identity}}},
        {fo::node_kind::pack, static_cast<std::uint16_t>(fo::pack_preset::compact), 0, {}},
        {fo::node_kind::bound, 0, 0, {{2, fo::coordinate::identity}}},
        {interval_kind, static_cast<std::uint16_t>(G), 0,
            {{1, fo::coordinate::identity}, {3, fo::coordinate::checkpoints}}}};
    out.program.root = 4;
    frame3::column encoded(take(fo::make_tree(out.program)), values);
    bool root_found = false, checkpoint_found = false;
    for (const auto &[path, stream] : encoded.streams()) {
        if (path.empty()) {
            check(stream->rows(0) == values.size() && stream->groups_in(0) == 2, "wrong root stream domain");
            out.parts[0] = encoded.image(*stream, 0, 0);
            out.parts[1] = encoded.image(*stream, 0, 1);
            root_found = true;
        } else {
            check(path == "checkpoints" && stream->rows(0) == (values.size() + G - 1) / G && stream->groups_in(0) == 1,
                "wrong checkpoint stream domain/identity");
            out.parts[2] = encoded.image(*stream, 0, 0);
            checkpoint_found = true;
        }
    }
    check(root_found && checkpoint_found, "shared Frame did not emit both streams");
    out.streams = {{4, values.size(), {{0, 0, out.parts[0].size()}, {1, 0, out.parts[1].size()}}},
                   {3, (values.size() + G - 1) / G, {{2, 0, out.parts[2].size()}}}};
    const std::array extents{out.parts[0].size(), out.parts[1].size(), out.parts[2].size()};
    check(bool(fo::validate_manifest(out.program, values.size(), extents, out.streams)), "builder manifest refused");
    return out;
}
using Lifetime = std::weak_ptr<const Bytes>;
orbital::PartSource fresh(orbital::PartId id, Bytes bytes, Lifetime &lifetime) {
    auto owner = std::make_shared<const Bytes>(std::move(bytes));
    lifetime = owner;
    return {id, *owner, owner, nullptr};
}
std::span<const std::byte> part(fo::Selection selected, unsigned i) {
    return {take(selected.part(i)).address(), take(selected.control().extent(i))};
}
fo::Store::Owner capture(fo::Store &store, Memory &memory, orbital::ObjectId object,
    image value, std::array<Lifetime, 3> &lifetimes, fo::Selection previous = {}) {
    fo::interpretation meaning{take(fo::encode(value.program)), rows, {}, logical_schema, object, std::move(value.streams)};
    for (const auto &bytes : value.parts) meaning.part_extents.push_back(bytes.size());
    auto metadata = take(fo::interpretation_owner::make(memory, std::move(meaning)));
    std::array<orbital::PartSource, 3> sources;
    for (unsigned i = 0; i < sources.size(); ++i) {
        if (previous && i == 0) {
            const auto old = part(previous, 0);
            check(std::equal(old.begin(), old.end(), value.parts[0].begin(), value.parts[0].end()),
                "root group0 unexpectedly changed during rebuild");
            sources[i] = take(fo::reuse(previous, i));
        } else sources[i] = fresh(metadata.control().id(i), std::move(value.parts[i]), lifetimes[i]);
        sources[i].id = metadata.control().id(i);
    }
    return take(fo::capture(store, metadata, sources));
}
void publish(fo::Store &store, const fo::Store::Scope &basis, orbital::ObjectId object, const fo::Store::Owner &owner) {
    fo::Store::Prepared packet(store);
    const std::array changes{fo::Store::Change{object, owner}};
    check(bool(store.prepare(basis, changes, packet)), "packet preparation failed");
    check(bool(store.install(packet)), "atomic Representation publication failed");
}
std::vector<fo::borrowed_part> parts(fo::Selection selected) {
    std::vector<fo::borrowed_part> out;
    for (unsigned i = 0; i < selected.part_count(); ++i) out.push_back({i, part(selected, i)});
    return out;
}
std::unique_ptr<fo::stream_binding> attach(fo::Selection selected) {
    const auto &meaning = selected.control().meaning();
    const auto d = take(fo::decode(meaning.pipeline));
    const std::array available{fo::section_key{4, 0}, fo::section_key{4, 1}, fo::section_key{3, 0}};
    return take(fo::stream_binding::attach(d, meaning.rows, meaning.part_extents, meaning.streams, parts(selected), available));
}
struct answer { u128 sum{}, selected_sum{}; u64 count{}; };
answer oracle(std::span<const u64> values, u64 threshold) {
    answer out;
    for (unsigned r = 0; r < values.size(); ++r) {
        out.sum += values[r];
        out.count += values[r] >= threshold;
        if (r % 5 == 1) out.selected_sum += values[r];
    }
    return out;
}
void verify(const fo::stream_binding &bound, std::span<const u64> values, u64 threshold) {
    const auto want = oracle(values, threshold);
    check(bound.size() == values.size(), "retained logical row extent changed");
    std::vector<u64> decoded(values.size());
    check(bool(bound.read(0, decoded)) && std::equal(decoded.begin(), decoded.end(), values.begin()),
        "full read differs from original-row oracle");
    for (unsigned r = 0; r < values.size(); ++r) check(take(bound.get(r)) == values[r], "point differs from original-row oracle");
    const auto blocks = frame3::first_n(static_cast<unsigned>((values.size() + 255) / 256));
    frame3::block_rows_set selected{};
    for (unsigned r = 0; r < values.size(); ++r) if (r % 5 == 1) selected[r / 256].set(r % 256);
    check(take(bound.sum(blocks)) == want.sum, "interval SUM differs from original-row oracle");
    check(take(bound.count(ikea::comparison::greater_equal, blocks, threshold)) == want.count,
        "interval COUNT differs from original-row oracle");
    check(take(bound.sum_selected(blocks, selected)) == want.selected_sum,
        "selected interval SUM differs from original-row oracle");
}
void missing_checkpoint(fo::Selection selected) {
    const auto &meaning = selected.control().meaning();
    const auto d = take(fo::decode(meaning.pipeline));
    auto supplied = parts(selected);
    const long page = ::sysconf(_SC_PAGESIZE);
    check(page > 0, "page-size query failed");
    const auto bytes = ((meaning.part_extents[2] + page - 1) / page) * page;
    void *unreadable = ::mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(unreadable != MAP_FAILED, "checkpoint guard mapping failed");
    supplied[2].bytes = {static_cast<const std::byte *>(unreadable), meaning.part_extents[2]};
    {
        const std::array roots_only{fo::section_key{4, 0}, fo::section_key{4, 1}};
        const auto incomplete = take(fo::stream_binding::attach(d, rows, meaning.part_extents, meaning.streams, supplied, roots_only));
        std::array<u64, 3> output{0xaaaa, 0xbbbb, 0xcccc};
        const auto before = output;
        const auto refused = incomplete->read(4095, output);
        check(!refused && refused.error() == fo::binding_error::unavailable && output == before,
            "missing checkpoint changed output before refusal");
        check(!incomplete->get(4096), "missing checkpoint answered a point");
        check(!incomplete->sum(frame3::first_n((rows + 255) / 256)), "missing checkpoint answered SUM");
        const std::array wrong{fo::section_key{4, 0}, fo::section_key{4, 1}, fo::section_key{3, 1}};
        check(!fo::stream_binding::attach(d, rows, meaning.part_extents, meaning.streams, supplied, wrong),
            "wrong checkpoint-group availability was admitted");
    }
    check(::munmap(unreadable, bytes) == 0, "checkpoint guard cleanup failed");
}
void history(unsigned G, fo::node_kind interval_kind) {
    Memory memory;
    std::array<Lifetime, 3> original_lifetime, updated_lifetime;
    std::vector<u64> original(rows);
    for (unsigned r = 0; r < rows; ++r) original[r] = 1000000 + u64{17} * r + r % 7;
    auto updated = original;
    // A checkpoint row: repair its checkpoint and dependent size/relative-end fields.
    updated[4096] += 5;
    const u64 threshold = original[4096] + 2;
    const auto old_answer = oracle(original, threshold), new_answer = oracle(updated, threshold);
    check(new_answer.sum - old_answer.sum == 5 && new_answer.count == old_answer.count + 1,
        "forcing mutation did not change the expected answers");
    const orbital::ObjectId object{0x53545245414d,
        (u64{G} << 8) | static_cast<u64>(interval_kind)};
    {
        fo::Store store(memory);
        {
            fo::Store::Scope absent(store);
            const auto initial = capture(store, memory, object, build(G, interval_kind, original), original_lifetime);
            publish(store, absent, object, initial);
        }
        fo::Store::Scope old(store);
        const auto previous = old.bind(object);
        const auto old_binding = attach(previous); // temporary decoded description/Part wrappers are gone.
        verify(*old_binding, original, threshold);
        check(previous.control().meaning().streams[1].rows == (rows + G - 1) / G,
            "material lost checkpoint-stream cardinality");
        {
            const auto next = capture(store, memory, object, build(G, interval_kind, updated), updated_lifetime, previous);
            verify(*old_binding, original, threshold); // Preparing both new sections does not publish either.
            publish(store, old, object, next);
        }
        fo::Store::Scope now(store);
        const auto current = now.bind(object);
        check(old.cut() == 1 && now.cut() == 2, "SET plus rebuild did not make one logical version");
        check(part(previous, 0).data() == part(current, 0).data(), "untouched root section was not physically reused");
        check(part(previous, 1).data() != part(current, 1).data() && part(previous, 2).data() != part(current, 2).data(),
            "two changed sections were not replaced together");
        verify(*old_binding, original, threshold);
        verify(*attach(old.bind(object)), original, threshold);
        verify(*attach(current), updated, threshold);
        check(!original_lifetime[1].expired() && !original_lifetime[2].expired(), "old reader lost a dependent stream");
        missing_checkpoint(current);
        const __int128 contribution = static_cast<__int128>(updated[4096]) - static_cast<__int128>(original[4096]);
        check(static_cast<__int128>(old_answer.sum) + contribution == static_cast<__int128>(new_answer.sum),
            "logical contribution differs from original-row SUM change");
    }
    for (const auto &lifetime : original_lifetime) check(lifetime.expired(), "old physical stream leaked");
    for (const auto &lifetime : updated_lifetime) check(lifetime.expired(), "new physical stream leaked");
    check(memory.live == 0, "stream interpretation/catalog PMR storage leaked");
}

void conversion_history(unsigned G, fo::node_kind source_kind, fo::node_kind target_kind) {
    Memory memory;
    std::array<Lifetime, 3> source_lifetime, target_lifetime;
    std::vector<u64> values(rows);
    for (unsigned r = 0; r < rows; ++r) values[r] = 1000000 + u64{17} * r + r % 7;
    const auto threshold = values[4096];
    const orbital::ObjectId object{0x434f4e56535452,
        (u64{G} << 16) | (static_cast<u64>(source_kind) << 8) | static_cast<u64>(target_kind)};
    {
        fo::Store store(memory);
        {
            fo::Store::Scope absent(store);
            const auto initial = capture(store, memory, object, build(G, source_kind, values), source_lifetime);
            publish(store, absent, object, initial);
        } // Source builders, description and metadata owner have all died.
        fo::Store::Scope old(store);
        const auto previous = old.bind(object);
        const auto old_pipeline = previous.control().meaning().pipeline;
        const auto old_reader = attach(previous);
        verify(*old_reader, values, threshold);
        {
            auto target = take(store.conversion(old, object));
            check(fo::same_material(target.source(), previous), "conversion target lost its exact source material");
            // Full shared-stage rebuild, not byte reinterpretation or an optimized converter.
            const auto converted = capture(store, memory, object, build(G, target_kind, values), target_lifetime);
            const auto candidate_reader = attach(converted.selection());
            verify(*candidate_reader, values, threshold); // Equivalence is independently proved before adoption.
            const auto blocks = frame3::first_n((rows + 255) / 256);
            const __int128 logical_contribution = static_cast<__int128>(take(candidate_reader->sum(blocks))) -
                static_cast<__int128>(take(old_reader->sum(blocks)));
            check(logical_contribution == 0, "pure conversion changed the logical SUM contribution");
            check(bool(store.adopt(target, converted)), "equivalent interval-form conversion refused");
            check(store.borrow().cut == old.cut(), "pure interval-form conversion advanced logical time");
        } // Target, builder, description, capture sources and replacement Owner have all died.

        const auto alternative = take(old.bind_alternative(object));
        const auto &meaning = alternative.control().meaning();
        const auto d = take(fo::decode(meaning.pipeline));
        check(d.nodes[d.root].kind == target_kind && d.nodes[d.root].parameter == G &&
            take(fo::encode(d)) == meaning.pipeline && meaning.pipeline != old_pipeline,
            "converted material lost its target-kind canonical description");
        check(meaning.rows == rows && meaning.streams.size() == 2 &&
            meaning.streams[0].root_node == 4 && meaning.streams[0].rows == rows && meaning.streams[0].groups.size() == 2 &&
            meaning.streams[1].root_node == 3 && meaning.streams[1].rows == (rows + G - 1) / G &&
            meaning.streams[1].groups.size() == 1 &&
            bool(fo::validate_manifest(d, meaning.rows, meaning.part_extents, meaning.streams)),
            "converted root/checkpoint manifests lost their coherent logical domains");
        verify(*attach(alternative), values, threshold);
        missing_checkpoint(alternative);
        check(previous.control().meaning().pipeline == old_pipeline &&
            take(fo::decode(old_pipeline)).nodes[4].kind == source_kind &&
            old.bind(object).control().meaning().pipeline == old_pipeline,
            "same-version conversion rewrote the old source interpretation");
        verify(*old_reader, values, threshold);
        verify(*attach(old.bind(object)), values, threshold);
        fo::Store::Scope current(store);
        check(current.cut() == old.cut() && old.cut() == 1, "conversion made a logical publication");
        check(fo::same_material(take(current.bind_alternative(object)), alternative),
            "same-cut alternative lookup lost the converted material");
        for (const auto &lifetime : source_lifetime) check(!lifetime.expired(), "old conversion reader lost source payload");
        for (const auto &lifetime : target_lifetime) check(!lifetime.expired(), "adopted conversion lost target payload");
    }
    for (const auto &lifetime : source_lifetime) check(lifetime.expired(), "source conversion stream leaked");
    for (const auto &lifetime : target_lifetime) check(lifetime.expired(), "target conversion stream leaked");
    check(memory.live == 0, "same-version interval conversion leaked material/catalog PMR storage");
}

void metadata_remap() {
    Memory memory;
    {
        fo::Store store(memory);
        const orbital::ObjectId object{0x4d4150524f4f46, 1};
        fo::description d;
        d.nodes = {{fo::node_kind::pack, 0, 0, {}}, {fo::node_kind::bound, 0, 0, {{0, fo::coordinate::identity}}}};
        d.root = 1;
        std::vector<u64> values(8192, 100);
        std::fill(values.begin() + 4096, values.end(), 200);
        frame3::column encoded(take(fo::make_tree(d)), values);
        auto a = encoded.image(0, 0), b = encoded.image(0, 1);
        check(a.size() == b.size() && a != b, "remap witness needs equal-sized, distinct valid groups");
        fo::interpretation meaning{take(fo::encode(d)), 8192, {a.size(), b.size()}, logical_schema, object,
            {{1, 8192, {{0, 0, a.size()}, {1, 0, b.size()}}}}};
        const std::array available{fo::section_key{1, 0}, fo::section_key{1, 1}};
        auto reader = [&](fo::Selection selected) {
            const auto &m = selected.control().meaning();
            return take(fo::stream_binding::attach(take(fo::decode(m.pipeline)), m.rows,
                m.part_extents, m.streams, parts(selected), available));
        };
        auto metadata = take(fo::interpretation_owner::make(memory, meaning));
        std::array<Lifetime, 2> lifetimes;
        const std::array sources{fresh(metadata.control().id(0), std::move(a), lifetimes[0]),
                                 fresh(metadata.control().id(1), std::move(b), lifetimes[1])};
        const auto initial = take(fo::capture(store, metadata, sources));
        { fo::Store::Scope absent(store); publish(store, absent, object, initial); }
        fo::Store::Scope old(store);
        const auto previous = old.bind(object);
        check(fo::same_material(previous, previous) && !fo::same_material(previous, {}), "material basis presence law");
        check(take(reader(previous)->get(0)) == 100, "old remap value is wrong");
        const std::array commands{fo::command{0, 150}};
        const auto prepared = take(fo::prepare_group(d, 8192, 0, part(previous, 0), commands,
            {true, part(previous, 0).size(), 1, 64, 1024 * 1024, true}));
        check(prepared.contribution == 50, "old preparation did not observe its original basis");

        std::swap(meaning.streams[0].groups[0], meaning.streams[0].groups[1]);
        check(bool(fo::validate_manifest(d, meaning.rows, meaning.part_extents, meaning.streams)), "valid swapped manifest refused");
        auto remapped = take(fo::interpretation_owner::make(memory, std::move(meaning)));
        const std::array retained{take(fo::reuse(previous, 0)), take(fo::reuse(previous, 1))};
        const auto next = take(fo::capture(store, remapped, retained));
        check(take(reader(next.selection())->get(0)) == 200, "checked decoder did not follow the valid remap");
        publish(store, old, object, next); // Metadata alone changes the logical rows.
        fo::Store::Prepared stale(store);
        const std::array stale_changes{fo::Store::Change{object, initial}};
        const auto refused = store.prepare(old, stale_changes, stale);
        check(!refused && refused.error() == orbital::Error::stale, "old preparation Scope escaped stale refusal");
        fo::Store::Scope now(store);
        const auto current = now.bind(object);
        check(now.cut() == old.cut() + 1, "metadata remap did not make a logical version");
        for (unsigned i = 0; i < 2; ++i)
            check(part(previous, i).data() == part(current, i).data() && part(previous, i).size() == part(current, i).size(),
                "metadata-only witness changed physical bytes/extent");
        check(previous.control().meaning().pipeline == current.control().meaning().pipeline &&
            previous.control().meaning().rows == current.control().meaning().rows, "remap changed shallow basis fields");
        check(!fo::same_material(previous, current), "fresh Scope reused a proof across a changed stream manifest");
        check(take(reader(old.bind(object))->get(0)) == 100 && take(reader(current)->get(0)) == 200,
            "metadata-only publication lost coherent old/current values");
    }
    check(memory.live == 0, "metadata-only remap leaked material");
}
} // namespace

int main() {
    for (const auto kind : {fo::node_kind::intervals_sizes, fo::node_kind::intervals_relative_ends}) {
        history(16, kind);
        history(64, kind);
        const auto other = kind == fo::node_kind::intervals_sizes ?
            fo::node_kind::intervals_relative_ends : fo::node_kind::intervals_sizes;
        conversion_history(16, kind, other);
        conversion_history(64, kind, other);
    }
    metadata_remap();
    std::printf("Frame stream history: %zu checks pass; real G16/G64 sizes/relative_ends, atomic two-section replacement, same-version bidirectional form conversion, retained old readers and valid metadata-remap revalidation; full-rebuild correctness only\n", checks);
}
