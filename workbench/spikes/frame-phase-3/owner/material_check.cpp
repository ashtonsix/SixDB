#include "material.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <source_location>
#include <utility>

namespace fo = frame3_owner;
namespace {
void check(bool condition, const char *message,
    std::source_location where = std::source_location::current()) {
    if (condition) return;
    std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), message);
    std::abort();
}
template <class T> T take(orbital::Result<T> value,
    std::source_location where = std::source_location::current()) {
    check(bool(value), "unexpected refusal", where);
    return std::move(*value);
}
struct Memory final : std::pmr::memory_resource {
    std::size_t live{}, allocations{}, allowance = std::numeric_limits<std::size_t>::max();
    void *do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (!allowance) throw std::bad_alloc{};
        if (allowance != std::numeric_limits<std::size_t>::max()) --allowance;
        void *result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        live += bytes;
        ++allocations;
        return result;
    }
    void do_deallocate(void *pointer, std::size_t bytes, std::size_t alignment) override {
        check(live >= bytes && allocations, "resource double release");
        live -= bytes;
        --allocations;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
};

constexpr orbital::ObjectId object{0x4652414d4533, 7};
const std::vector<std::byte> original_pipeline{std::byte{0xf3}, std::byte{1}, std::byte{7}};
fo::interpretation meaning(std::size_t rows = 2, std::uint64_t schema = 41,
    orbital::ObjectId identity = object, unsigned groups = 2) {
    return {original_pipeline, rows, std::vector<std::size_t>(groups, sizeof(std::uint64_t)), schema, identity};
}
using Witness = std::weak_ptr<const void>;
orbital::PartSource fresh(orbital::PartId id, std::uint64_t number, Witness &witness) {
    auto backing = std::make_shared<const std::uint64_t>(number);
    witness = backing;
    return {id, std::as_bytes(std::span(backing.get(), std::size_t{1})), backing, nullptr};
}
orbital::PartSource appended(orbital::PartId id, Witness &witness) {
    auto backing = std::make_shared<const std::array<std::uint64_t, 2>>(
        std::array<std::uint64_t, 2>{20, 99});
    witness = backing;
    return {id, std::as_bytes(std::span(*backing)), backing, nullptr};
}
std::uint64_t read(orbital::View view) {
    check(bool(view), "absent view");
    std::uint64_t result;
    std::memcpy(&result, view.address(), sizeof(result));
    return result;
}
void publish(fo::Store &store, const fo::Store::Owner &owner, orbital::ObjectId identity = object) {
    fo::Store::Scope basis(store);
    fo::Store::Prepared packet(store);
    const std::array changes{fo::Store::Change{identity, owner}};
    check(bool(store.prepare(basis, changes, packet)), "publication refused");
    check(bool(store.install(packet)), "publication did not install");
}

void escaping(orbital::PartRetention policy) {
    Memory memory;
    std::array<Witness, 2> original;
    Witness replacement;
    orbital::Part<fo::Control> *escaped{};
    {
        fo::Store store(memory);
        {
            auto builder = meaning();
            auto metadata = take(fo::interpretation_owner::make(memory, std::move(builder)));
            auto moved = std::move(metadata);
            check(!metadata && bool(moved), "metadata move kept the wrong owner");
            const auto control = moved.control();
            std::array sources{fresh(control.id(0), 10, original[0]), fresh(control.id(1), 20, original[1])};
            const auto owner = take(fo::capture(store, moved, sources));
            publish(store, owner);
        }
        fo::Store::Scope old(store);
        const auto selected = old.bind(object);
        check(selected.control().meaning().pipeline == original_pipeline, "builder lifetime lost pipeline");
        check(selected.control().meaning().rows == 2 && selected.part_count() == 2, "initial interpretation changed");
        auto bound = take(store.bound(old, object));
        escaped = take(bound.export_part(1, policy));
        {
            auto grown = meaning(3);
            grown.part_extents[1] = 16;
            auto next = take(fo::interpretation_owner::make(memory, std::move(grown)));
            std::array sources{take(fo::reuse(selected, 0)), appended(next.control().id(1), replacement)};
            const auto owner = take(fo::capture(store, next, sources));
            check(owner.selection().part_count() == 2, "capture lost group geometry");
            check(take(owner.selection().part(0)).address() == take(selected.part(0)).address(),
                "retained source copied unchanged bytes");
            publish(store, owner);  // Ordinary append changes row count.
        }
        const auto current = store.borrow().bind(object);
        check(current.control().meaning().rows == 3, "logical publication refused new row count");
        check(selected.control().meaning().rows == 2, "append changed old interpretation");
        std::array<std::uint64_t, 2> appended_values;
        std::memcpy(appended_values.data(), take(current.part(1)).address(), sizeof(appended_values));
        check(appended_values == std::array<std::uint64_t, 2>{20, 99} && read(take(selected.part(1))) == 20 &&
            *current.control().extent(1) == 16 && *selected.control().extent(1) == 8,
            "append lost values or old/new extent split");
        check(orbital::binding(escaped).object == object && orbital::binding(escaped).version == 1,
            "escaping Part lost sealed Object/version");
    }
    check(original[0].expired() && !original[1].expired() && replacement.expired(),
        "escaping one group retained sibling/replacement payload");
    check(memory.allocations == 2, "unnecessary catalog or interpretation lifetime survived");
    check(read(orbital::view(escaped)) == 20, "escaping bytes died with Store");
    const auto control = escaped->control;
    check(control.meaning().pipeline == original_pipeline && control.meaning().rows == 2 &&
        control.meaning().logical_schema == 41 && control.meaning().object == object,
        "escaping interpretation died with builder/Store/Scope");
    if (policy == orbital::PartRetention::selected_copy) {
        check(control.part_count() == 1 && control.id(0).low == 1 && *control.extent(0) == 8,
            "selected proof did not project its original group to index zero");
        const auto source = take(fo::reuse(escaped));
        check(source.id == control.id(0) && source.body.size() == 8, "selected Part source lost certified geometry");
    }
    orbital::release(escaped);
    check(original[1].expired() && memory.live == 0 && memory.allocations == 0,
        "final Part release leaked bytes or interpretation");
}

std::size_t interpretation_vector_bytes(const fo::interpretation &meaning) {
    std::size_t bytes = meaning.pipeline.capacity() * sizeof(std::byte) +
        meaning.part_extents.capacity() * sizeof(std::size_t) +
        meaning.streams.capacity() * sizeof(fo::stream_manifest);
    for (const auto &stream : meaning.streams)
        bytes += stream.groups.capacity() * sizeof(fo::section_location);
    return bytes;
}

void manifest_lifetime(orbital::PartRetention policy) {
    Memory memory;
    Witness packed, sibling;
    orbital::Part<fo::Control> *escaped{};
    std::size_t vector_bytes = 0;
    const std::vector<fo::stream_manifest> expected{
        {4, 8192, {{0, 0, 8}, {1, 0, 8}}},
        {1, 512, {{0, 16, 8}}}};
    {
        fo::Store store(memory);
        {
            // This material-only fixture treats payloads as opaque bytes. Node
            // identities/cardinalities and actual Frame decode are checked by
            // the separate manifest/binding fixtures, not this storage adapter.
            auto value = meaning(8192);
            value.part_extents = {24, 8};
            value.streams = expected;
            auto metadata = take(fo::interpretation_owner::make(memory, std::move(value)));
            vector_bytes = interpretation_vector_bytes(metadata.meaning());
            auto bytes = std::make_shared<const std::array<std::uint64_t, 3>>(
                std::array<std::uint64_t, 3>{11, 12, 13});
            packed = bytes;
            const std::array sources{
                orbital::PartSource{metadata.control().id(0), std::as_bytes(std::span(*bytes)), bytes},
                fresh(metadata.control().id(1), 99, sibling)};
            const auto owner = take(fo::capture(store, metadata, sources));
            publish(store, owner);
        }
        fo::Store::Scope scope(store);
        auto selected = take(store.bound(scope, object));
        const auto &meaning = selected.selection().control().meaning();
        check(meaning.streams == expected && meaning.part_extents == std::vector<std::size_t>{24, 8},
            "material lost canonical node/section coordinates");
        escaped = take(selected.export_part(0, policy));
    }
    check(!packed.expired() && sibling.expired(), "co-packed export retained an unrelated physical Part");
    check(memory.allocations == 2 && memory.live != 0,
        "co-packed export retained catalog or duplicate interpretation state");
    const auto &meaning = escaped->control.meaning();
    check(meaning.streams == expected && meaning.pipeline == original_pipeline && meaning.rows == 8192,
        "stream manifest depended on builder/Scope/Store lifetime");
    check(interpretation_vector_bytes(meaning) == vector_bytes && vector_bytes != 0,
        "retained vector-capacity accounting lost nested section metadata");
    const auto first_section = meaning.streams[0].groups[0], checkpoint_section = meaning.streams[1].groups[0];
    check(first_section.owner_part == 0 && checkpoint_section.owner_part == 0 &&
        first_section.offset == 0 && checkpoint_section.offset == 16,
        "manifest silently imposed one stream per Part");
    std::uint64_t first_value, checkpoint;
    const auto *data = orbital::view(escaped).address();
    std::memcpy(&first_value, data + first_section.offset, sizeof(first_value));
    std::memcpy(&checkpoint, data + checkpoint_section.offset, sizeof(checkpoint));
    check(first_value == 11 && checkpoint == 13, "co-packed section addresses changed after teardown");
    // Deduplicate by &Control::meaning() before summing vector capacities across
    // live selections/exports. PMR live includes the shared state header and
    // Orbital metadata; it excludes these ordinary-vector buffers and payloads.
    std::printf("manifest lifetime: pmr_bytes=%zu interpretation_vector_capacity_bytes=%zu (distinct state, payload excluded)\n",
        memory.live, vector_bytes);
    orbital::release(escaped);
    check(packed.expired() && memory.live == 0 && memory.allocations == 0,
        "manifest/payload metadata leaked after final export release");
}

void identity_and_adoption() {
    Memory memory;
    {
        fo::Store store(memory);
        Witness first, second;
        auto metadata = take(fo::interpretation_owner::make(memory, meaning()));
        std::array sources{fresh(metadata.control().id(0), 1, first), fresh(metadata.control().id(1), 2, second)};
        const auto owner = take(fo::capture(store, metadata, sources));
        {
            fo::Store::Scope basis(store);
            fo::Store::Prepared packet(store);
            const std::array wrong{fo::Store::Change{{object.high + 1, object.low}, owner}};
            auto result = store.prepare(basis, wrong, packet);
            check(!result && result.error() == orbital::Error::conflict, "publication ignored full Object identity");
        }
        publish(store, owner);
        fo::Store::Scope basis(store);
        auto target = take(store.conversion(basis, object));
        for (const auto [rows, schema] : {std::pair{3u, 41u}, std::pair{2u, 42u}}) {
            auto changed = take(fo::interpretation_owner::make(memory, meaning(rows, schema)));
            const auto candidate = take(fo::capture(store, changed, sources));
            auto result = store.adopt(target, candidate);
            check(!result && result.error() == orbital::Error::conflict,
                "same-version adopt accepted changed logical extent/schema");
        }
        auto other_object = meaning(2, 41, {object.high + 1, object.low});
        auto other_metadata = take(fo::interpretation_owner::make(memory, std::move(other_object)));
        const auto other = take(fo::capture(store, other_metadata, sources));
        check(!store.adopt(target, other), "adoption ignored full Object identity");

        // A different physical pipeline and group count can preserve the same
        // two logical values: one 16-byte group instead of two 8-byte groups.
        auto repacked = meaning();
        repacked.pipeline.push_back(std::byte{9});
        repacked.part_extents = {16};
        auto converted_metadata = take(fo::interpretation_owner::make(memory, std::move(repacked)));
        auto values = std::make_shared<const std::array<std::uint64_t, 2>>(std::array<std::uint64_t, 2>{1, 2});
        const std::array converted_source{orbital::PartSource{converted_metadata.control().id(0), std::as_bytes(std::span(*values)), values}};
        const auto converted = take(fo::capture(store, converted_metadata, converted_source));
        check(bool(store.adopt(target, converted)), "equivalent physical regrouping was refused");
        check(fo::Store::Scope(store).cut() == basis.cut(), "physical adoption changed logical time");
        const auto alternative = take(basis.bind_alternative(object));
        check(alternative.part_count() == 1 && alternative.control().meaning().pipeline.size() == 4,
            "physical adoption lost its own interpretation");
        std::array<std::uint64_t, 2> decoded;
        std::memcpy(decoded.data(), take(alternative.part(0)).address(), sizeof(decoded));
        check(decoded == *values && read(take(basis.bind(object).part(1))) == 2,
            "independent logical oracle or retained primary failed");
    }
    check(memory.live == 0, "identity/adoption fixture leaked metadata");
}

void refusals() {
    Memory memory, foreign;
    {
        fo::Store store(memory);
        fo::Store foreign_store(foreign);
        fo::interpretation_owner empty;
        check(!empty && !empty.control().valid(memory) && !empty.control().extent(0), "default proof was admitted");
        check(!fo::capture(store, empty, {}), "default metadata captured");
        auto invalid = meaning();
        invalid.pipeline.clear();
        check(!fo::interpretation_owner::make(memory, std::move(invalid)), "missing pipeline admitted");
        Witness first, second;
        auto metadata = take(fo::interpretation_owner::make(memory, meaning()));
        std::array sources{fresh(metadata.control().id(0), 1, first), fresh(metadata.control().id(1), 2, second)};
        const auto retained_live = memory.live, foreign_live = foreign.live;
        auto bad_resource = fo::capture(foreign_store, metadata, sources);
        check(!bad_resource && bad_resource.error() == orbital::Error::invalid_argument &&
            memory.live == retained_live && foreign.live == foreign_live, "bad resource changed ownership");
        auto bad_extent = sources;
        bad_extent[1].body = bad_extent[1].body.first(7);
        auto refused = fo::capture(store, metadata, bad_extent);
        check(!refused && refused.error() == orbital::Error::invalid_argument && memory.live == retained_live,
            "invalid capture failed to unwind its interpretation use");
        check(!first.expired() && !second.expired(), "capture refusal consumed caller backing");

        memory.allowance = 0;
        auto no_metadata = fo::interpretation_owner::make(memory, meaning());
        check(!no_metadata && no_metadata.error() == orbital::Error::capacity && memory.live == retained_live,
            "metadata allocation refusal leaked");
        memory.allowance = 1;  // Temporary metadata succeeds; Representation allocation refuses.
        auto no_capture = fo::capture(store, memory, meaning(), sources);
        check(!no_capture && no_capture.error() == orbital::Error::capacity && memory.live == retained_live,
            "capture convenience leaked temporary metadata after refusal");
        memory.allowance = std::numeric_limits<std::size_t>::max();
        const auto accepted = take(fo::capture(store, metadata, sources));
        check(accepted.selection().control().meaning().pipeline == original_pipeline,
            "refusals corrupted reusable interpretation");
    }
    check(memory.live == 0 && foreign.live == 0, "refusal fixture leaked metadata");
}
}  // namespace

int main() {
    escaping(orbital::PartRetention::shared_control);
    escaping(orbital::PartRetention::selected_copy);
    manifest_lifetime(orbital::PartRetention::shared_control);
    manifest_lifetime(orbital::PartRetention::selected_copy);
    identity_and_adoption();
    refusals();
    std::puts("Frame owner material: public two-TU capture, interpretation/Part lifetime, identity, append/adopt and refusal checks pass");
}
