#include "fusion.h"
#include <memory>
#include <random>

namespace tuple_composition_probe {
struct region {
    std::shared_ptr<std::vector<byte>> lease;
    std::size_t offset;
    std::uint64_t id;
};
struct effect { std::uint64_t region_id; std::size_t offset, bytes; };
struct owner {
    std::uint64_t generation = 1, summary = 0;
    std::vector<effect> coverage;
    bool writable = true, visible = false;
    unsigned before_writes = 0;
};
struct pending {
    std::shared_ptr<const recipe> binding;
    std::array<region, 2> regions;
    packets input;
    std::uint64_t generation = 1;
    bool cancelled = false, completed = false;
    operation execute = replace_and_sum_delta;
};
enum class outcome { complete, cancelled, stale, lease, capacity, value, already_complete };

outcome resume(owner& o, pending& request) {
    if (request.completed) return outcome::already_complete;
    if (request.cancelled) return outcome::cancelled;
    if (o.generation != request.generation) return outcome::stale;
    if (!o.writable) return outcome::lease;
    for (const auto& r : request.regions)
        if (!r.lease || r.offset > r.lease->size() || r.lease->size() - r.offset < 32)
            return outcome::lease;
    // This test owner gives distinct allocations distinct IDs. Admission must
    // also reject overlapping destinations within a lease.
    const auto& a = request.regions[0]; const auto& b = request.regions[1];
    if (a.lease == b.lease && (a.id != b.id || (a.offset < b.offset + 32 && b.offset < a.offset + 32)))
        return outcome::lease;
    if (a.lease != b.lease && a.id == b.id) return outcome::lease;
    if (o.coverage.capacity() - o.coverage.size() < 2) return outcome::capacity;
    for (unsigned p = 0; p < 2; ++p)
        for (unsigned i = 0; i < 64; ++i)
            if (request.input[p][i] & request.binding->write[p].invalid_bits[i]) return outcome::value;

    operation call = request.execute;
    asm volatile("" : "+r"(call));
    const auto low = load_packet(request.input[0].data()), high = load_packet(request.input[1].data());
    // No-fail, no-suspend before-write hook after all admission. No baseline
    // image is required by this owner contract: Orbital can COW protected pages.
    ++o.before_writes;
    for (const auto& r : request.regions) o.coverage.push_back({r.id, r.offset, 32});
    const auto delta = call(*request.binding,
                           {a.lease->data() + a.offset, b.lease->data() + b.offset}, low, high);
    o.summary += delta;
    request.completed = true;
    return outcome::complete;
}
bytes64 snapshot(const pending& p) {
    bytes64 out;
    for (unsigned i = 0; i < 2; ++i)
        std::memcpy(out.data() + i * 32, p.regions[i].lease->data() + p.regions[i].offset, 32);
    return out;
}
void publish(owner& o, const pending& p) {
    require(p.completed, "cannot publish an incomplete operation");
    // This model only records ordering. Actual visibility/MVCC is owner work.
    o.visible = true;
}

// Two independently supplied children expose stable code ranks/widths. The
// parent assembles their physical descriptions at binding time; the native
// operation stays unchanged when only the second child is replaced. This is
// cold composition, not a chain of erased child callbacks on every invocation.
void check_nested_substitution() {
    using child = std::array<code,64>;
    auto make_child = [](unsigned base, bool reordered) {
        child result;
        for (unsigned i = 0; i < 32; ++i) {
            const auto w = byte(width(base + i));
            const auto offset = byte(reordered ? (13 * i + 7) % 32 : i);
            result[2*i] = {offset, byte(reordered ? 8-w : 0), w};
            result[2*i+1] = {offset, byte(reordered ? 0 : w), byte(8-w)};
        }
        return result;
    };
    auto parent = [](const child& first, const child& second) {
        std::array<code,128> combined;
        for (unsigned i = 0; i < 64; ++i) {
            if (first[i].offset >= 32 || second[i].offset >= 32)
                return std::expected<recipe,error>(std::unexpected(error::schema));
            combined[i] = first[i]; combined[64+i] = second[i]; combined[64+i].offset += 32;
        }
        return prepare_codes(combined,true);
    };
    auto first = make_child(0,false), old_second = make_child(32,false), new_second = make_child(32,true);
    auto old_parent = parent(first,old_second), new_parent = parent(first,new_second);
    require(bool(old_parent) && bool(new_parent), "resolve independent child descriptions");
    auto split_sources = first;
    std::swap(split_sources[0],split_sources[13]); // equal widths; two contributions per packet/byte
    auto general_parent = parent(split_sources,new_second);
    require(bool(general_parent) && general_parent->write[0].round_count == 2 &&
            general_parent->write[1].round_count == 2 && !general_parent->byte_decoder,
            "legal child substitution needs general writer/decode fallback");
    auto invalid = new_second; --invalid[0].width;
    require(!parent(first,invalid), "reject substitution changing parent's code-width contract");
    for (unsigned i = 0; i < 64; ++i)
        require(old_parent->codes[i].offset == new_parent->codes[i].offset &&
                old_parent->codes[i].shift == new_parent->codes[i].shift, "untouched child descriptor unchanged");
    std::mt19937 random(0xc41d);
    for (const auto* compiled : {&*old_parent,&*new_parent,&*general_parent}) for (unsigned trial = 0; trial < 128; ++trial) {
        bytes64 old_values{}, replacement{}, physical{};
        for (auto& v : old_values) v = random();
        for (auto& v : replacement) v = random();
        auto before = split(old_values), input = split(replacement);
        for (unsigned p = 0; p < 2; ++p)
            reference_write({64,compiled->codes},compiled->maps[p],physical.data(),before[p]);
        auto lease = std::make_shared<std::vector<byte>>(physical.begin(),physical.end());
        auto binding = std::make_shared<recipe>(*compiled);
        pending request{binding,{{{lease,0,71},{lease,32,71}}},input};
        request.execute = bind_operation(*binding);
        owner o; o.summary = sum(old_values); o.coverage.reserve(2);
        // Old/new parents remain usable for different segments. Rebinding
        // alone never reinterprets the old segment's stored representation.
        require(resume(o,request) == outcome::complete, "nested replacement ordinary operation");
        for (unsigned i = 0; i < 64; ++i) if (i%3) input[1][i] = before[1][i];
        const auto wanted = assemble(input);
        auto actual = snapshot(request); packets decoded;
        for (unsigned p = 0; p < 2; ++p) decoded[p] = reference_read({64,compiled->codes},compiled->maps[p],actual.data());
        require(assemble(decoded) == wanted && o.summary == sum(wanted), "nested child substitution preserves semantics/effects");
    }
    std::puts("384 nested child-substitution mutations passed with old/new representations coexisting");
}

void check() {
    std::mt19937 random(0x12864);
    unsigned completed = 0;
    check_routes();
    check_nested_substitution();
    for (auto e : {execution::separate, execution::generic, execution::constants, execution::algebraic, execution::normalized})
    for (bool partial : {false, true}) for (bool reordered : {false, true}) {
        auto binding = std::make_shared<recipe>(prepare(reordered, partial));
        for (bool planes : {false, true}) for (unsigned trial = 0; trial < 128; ++trial) {
            bytes64 old_values, new_values;
            for (auto& v : old_values) v = random();
            for (auto& v : new_values) v = random();
            const auto old_packets = split(old_values);
            bytes64 physical{};
            for (unsigned p = 0; p < 2; ++p)
                reference_write({64, binding->codes}, binding->maps[p], physical.data(), old_packets[p]);
            auto first = std::make_shared<std::vector<byte>>(planes ? 48 : 80, 0xa7);
            auto second = planes ? std::make_shared<std::vector<byte>>(48, 0xa7) : first;
            const std::size_t second_offset = planes ? 8 : 40;
            std::memcpy(first->data() + 8, physical.data(), 32);
            std::memcpy(second->data() + second_offset, physical.data() + 32, 32);
            auto invocation_binding = std::make_shared<recipe>(*binding);
            pending request{invocation_binding, {{{first, 8, 19}, {second, second_offset, planes ? 27u : 19u}}}, split(new_values)};
            request.execute = select(e, reordered, partial);
            auto expected_packets = request.input;
            if (partial) for (unsigned i = 0; i < 64; ++i)
                if (i % 3) expected_packets[1][i] = old_packets[1][i];
            const auto expected_values = assemble(expected_packets);
            owner o; o.summary = sum(old_values); o.coverage.reserve(2);
            auto expect_unchanged = [&] {
                require(snapshot(request) == physical && o.summary == sum(old_values) &&
                        o.coverage.empty() && o.before_writes == 0 && !request.completed,
                        "admission failure changed data, summary or effects");
            };
            // The error is in packet TWO: the first must not have been applied.
            const unsigned invalid_position = partial ? 3 * (trial % 22) : trial % 64;
            request.input[1][invalid_position] |= 1u << (8 - width(invalid_position));
            require(resume(o, request) == outcome::value, "second-packet width admission");
            expect_unchanged(); request.input = split(new_values);
            request.cancelled = true;
            require(resume(o, request) == outcome::cancelled, "cancellation before entry");
            expect_unchanged(); request.cancelled = false;
            ++o.generation;
            require(resume(o, request) == outcome::stale, "generation recheck on resume");
            expect_unchanged(); --o.generation;
            o.writable = false;
            require(resume(o, request) == outcome::lease, "writable ownership admission");
            expect_unchanged(); o.writable = true;
            o.coverage = {};
            std::vector<effect>().swap(o.coverage);
            require(resume(o, request) == outcome::capacity, "effect capacity admission");
            expect_unchanged(); o.coverage.reserve(2);
            auto saved_region = request.regions[1];
            request.regions[1] = request.regions[0];
            require(resume(o, request) == outcome::lease, "overlapping views rejected");
            request.regions[1] = saved_region; expect_unchanged();
            // A suspended request owns the input packets, binding and leases.
            // Drop other handles before resumption; the hot kernel sees none of
            // the shared_ptr, cancellation, journal or generation machinery.
            first.reset(); second.reset(); invocation_binding.reset();
            require(request.binding.use_count() == 1, "pending operation alone retains its binding");
            require(resume(o, request) == outcome::complete, "complete operation");
            const auto after = snapshot(request);
            packets decoded;
            for (unsigned p = 0; p < 2; ++p)
                decoded[p] = reference_read({64, binding->codes}, binding->maps[p], after.data());
            require(assemble(decoded) == expected_values && o.summary == sum(expected_values), "substitution, preservation and summary");
            require(o.coverage.size() == 2 && o.before_writes == 1 && !o.visible, "issued spans before publication");
            for (unsigned p = 0; p < 2; ++p) {
                const auto& r = request.regions[p]; const auto& e = o.coverage[p];
                require(e.region_id == r.id && e.offset == r.offset && e.bytes == 32, "qualified effect coordinates");
                for (unsigned i = 0; i < 8; ++i)
                    require((*r.lease)[i] == 0xa7 && (*r.lease)[r.lease->size() - 1 - i] == 0xa7,
                            "preserved allocation guards");
            }
            request.cancelled = true;
            require(resume(o, request) == outcome::already_complete && snapshot(request) == after &&
                    o.coverage.size() == 2, "late cancellation retains completed work without replay");
            publish(o, request);
            require(o.visible && o.summary == sum(expected_values), "summary ready before publication");
            ++completed;
        }
    }
    std::printf("%u compound mutations: 128 codes, full/partial unions, uint16 assembly/summary, reordered layout, split leases, "
                "whole-operation admission, cancellation, resumption and publication ordering passed\n", completed);
}
} // namespace tuple_composition_probe

void check_tuple_composition() { tuple_composition_probe::check(); }
