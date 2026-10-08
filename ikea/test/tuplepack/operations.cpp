#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/composition.h>
#include <ikea/tuplepack/author/maintenance.h>
#include <algorithm>
#include <array>
#include <vector>
#include <cassert>
#include <cstdio>

using namespace ikea::tuplepack;
struct exact_signature {
    static constexpr bool needs_before = false, needs_after = true;
    std::array<unsigned, 3> signatures{};
    unsigned calls = 0;
    // Deliberately overlapping contributions: clearing the removed code's
    // bits would lose another field's witness. Recompute all dependencies.
    void observe(std::size_t row, std::uint64_t values) noexcept {
        signatures[row] = (1u << (byte(values) % 7)) | (1u << (byte(values >> 8) % 7)) |
                          (1u << (byte(values >> 16) % 7));
        ++calls;
    }
};
struct invalidate {
    static constexpr bool needs_before = false, needs_after = false;
    std::uint64_t rows = 0;
    void observe(std::size_t row) noexcept {
        rows |= 1ULL << row;
    }
};
namespace tp = ikea::tuplepack;
using tp::byte;

void check_construction_packets(unsigned count) {
    const unsigned unit = (count + 1) / 2, stride = unit + 5;
    constexpr unsigned rows = 3, offset = 3;
    std::array<tp::code, 128> codes;
    for (unsigned i = 0; i < count; ++i)
        codes[i] = {byte(i / 2), byte(i % 2 * 4), 3};
    auto layout = tp::layout::make(unit, std::span(codes).first(count));
    assert(layout);
    auto plan = tp::constructor::make(*layout);
    assert(plan && plan->code_count() == count);
    // End the allocation exactly at the last unit; gaps and prefix remain owned
    // by somebody else. Poison ignored input slots, including the empty packet.
    std::vector<byte> storage(offset + (rows - 1) * stride + unit, 0xa6);
    auto view = tp::view::bind(*layout, storage, rows, stride, offset);
    assert(view);
    auto op = tp::bind_constructor(*plan, *view);
    assert(op);
    std::array<tp::construction_input, rows> values;
    for (unsigned row = 0; row < rows; ++row) {
        values[row].fill(255);
        for (unsigned i = 0; i < count; ++i)
            values[row][i] = (i * 5 + row * 3) % 8;
        assert(plan->accepts(values[row]));
    }
    std::array<ikea::owner_write, rows> records;
    ikea::source_write_journal effects{records};
    const auto before = storage;
    auto bad = values;
    bad.back()[count - 1] = 8;
    auto result = op->initialize(0, bad, effects);
    assert(!result && result.error() == tp::error::value && !effects.used && storage == before);
    ikea::source_write_journal short_effects{std::span(records).first(rows - 1)};
    result = op->initialize(0, values, short_effects);
    assert(!result && result.error() == tp::error::capacity && !short_effects.used && storage == before);
    result = op->initialize(1, values, effects);
    assert(!result && result.error() == tp::error::range && !effects.used && storage == before);
    assert(op->initialize(0, values, effects));
    auto expected = before;
    for (unsigned row = 0; row < rows; ++row) {
        const auto start = offset + row * stride;
        std::fill_n(expected.begin() + start, unit, 0);
        for (unsigned i = 0; i < count; ++i)
            expected[start + i / 2] |= byte(((i * 5 + row * 3) % 8) << (i % 2 * 4));
        const auto& effect = effects.storage[row];
        assert(effect.source == &*view && effect.bytes.plane == 0 &&
               effect.bytes.offset == start && effect.bytes.size == unit);
    }
    assert(storage == expected && effects.used == rows);
    std::fill(storage.begin(), storage.end(), 0xa6);
    struct Coverage {
        const tp::view& destination;
        std::span<const byte> bytes;
        unsigned next = 0, stride, unit;
        void before(const tp::view& source, ikea::byte_write write) {
            assert(&source == &destination && write.offset == offset + next * stride &&
                   write.plane == 0 && write.size == unit);
            for (unsigned i = 0; i < unit; ++i)
                assert(bytes[write.offset + i] == 0xa6);
            ++next;
        }
    } coverage{*view, storage, 0, stride, unit};
    op->initialize_unchecked(0, values, coverage);
    assert(coverage.next == rows && storage == expected);
}

int main() {
    for (auto count : {1u, 20u, 63u, 64u, 65u, 127u, 128u})
        check_construction_packets(count);
    // Independent finite-interval reference for the shared cold overlap proof.
    for (unsigned sa = 1; sa < 12; ++sa)
        for (unsigned sb = 1; sb < 12; ++sb)
            for (unsigned ba = 1; ba <= sa; ++ba)
                for (unsigned bb = 1; bb <= sb; ++bb)
                    for (unsigned delta = 0; delta < 24; ++delta) {
                        const ikea::detail::occupied_run a{100, sa, ba, 3},
                            b{100 + delta, sb, bb, 4};
                        bool wanted = false;
                        for (unsigned i = 0; i < a.count; ++i)
                            for (unsigned j = 0; j < b.count; ++j)
                                wanted |= a.base + i * sa < b.base + j * sb + bb &&
                                          b.base + j * sb < a.base + i * sa + ba;
                        assert(ikea::detail::overlaps(a, b) == wanted);
                        assert(ikea::detail::overlaps(b, a) == wanted);
                    }
    const std::array<code, 3> codes{{{0, 0, 3}, {0, 3, 3}, {1, 0, 8}}};
    auto format = layout::make(2, codes);
    assert(format);
    std::array<byte, 14> storage;
    storage.fill(0xd5);
    auto placement = view::bind(*format, storage, 3, 4, 1);
    assert(placement);
    const std::array<byte, 1> ma{0}, mb{1};
    const std::array<byte, 3> all{0, 1, 2};
    auto wa = writer<8>::make(*format, ma), wb = writer<8>::make(*format, mb);
    auto rd = reader<8>::make(*format, all);
    assert(wa && wb && rd);
    auto a = bind_writer(*wa, *placement), b = bind_writer(*wb, *placement);
    auto read = bind_reader(*rd, *placement);
    assert(a && b && read);
    auto group = composition::bind_group(*a, *b);
    assert(group);
    auto duplicate = composition::bind_group(*a, *a);
    assert(!duplicate && duplicate.error() == error::overlap);
    std::array<ikea::owner_write, 12> entries;
    ikea::source_write_journal effects{entries};
    exact_signature signature;
    observation observe(*read, signature);
    using input = decltype(group)::value_type::input_type;
    const std::array<input, 3> input_rows{input{1, 2}, input{3, 4}, input{5, 6}};
    auto original = storage;
    auto bad = input_rows;
    std::get<1>(bad.back()) = 8;
    assert(!group->replace(0, bad, effects, selection::all(), observe));
    assert(storage == original && effects.used == 0 && signature.calls == 0);
    ikea::source_write_journal short_effects{std::span(entries).first(5)};
    assert(!group->replace(0, input_rows, short_effects, selection::all(), observe));
    assert(storage == original && signature.calls == 0);
    assert(group->replace(0, input_rows, effects, selection::all(), observe));
    assert(signature.calls == 3);
    for (unsigned row = 0; row < 3; ++row) {
        const auto value = read->get_unchecked(row);
        assert(byte(value) == 1 + row * 2 && byte(value >> 8) == 2 + row * 2 &&
               byte(value >> 16) == 0xd5);
        assert(signature.signatures[row] ==
               ((1u << ((1 + row * 2) % 7)) | (1u << ((2 + row * 2) % 7)) | (1u << (0xd5 % 7))));
        assert((storage[1 + row * 4] & 0xc0) == (original[1 + row * 4] & 0xc0));
    }
    for (auto effect : effects.entries()) {
        assert(effect.source == &*placement && effect.bytes.plane == 0 && effect.bytes.size == 1);
        assert((effect.bytes.offset - 1) % 4 == 0);
    }
    auto empty_reader = reader<8>::make(*format, {});
    assert(empty_reader);
    auto empty_op = bind_reader(*empty_reader, *placement);
    assert(empty_op);
    invalidate invalidation;
    observation no_values(*empty_op, invalidation);
    effects.used = 0;
    std::array<std::uint64_t, 1> selected{0b101};
    assert(group->replace(0, input_rows, effects, selection::bits(0, selected), no_values));
    assert(invalidation.rows == 0b101);
    // A two-level write retains whole-call admission and final-row observation.
    auto nested = composition::bind_group(*group);
    assert(nested);
    using nested_input = decltype(nested)::value_type::input_type;
    effects.used = 0;
    assert(nested->set(1, nested_input{input{7, 0}}, effects, observe));
    assert(byte(read->get_unchecked(1)) == 7);
    // Full construction admits both packets before clearing any caller bytes.
    std::array<code, 128> many;
    for (unsigned i = 0; i < 128; ++i)
        many[i] = {byte(i / 2), byte(i % 2 * 4), 4};
    auto large = layout::make(64, many);
    assert(large);
    std::array<byte, 66> raw;
    raw.fill(0xaa);
    auto large_view = view::bind(*large, raw, 1, 64, 1);
    assert(large_view);
    auto build = constructor::make(*large);
    assert(build);
    auto initialize = bind_constructor(*build, *large_view);
    assert(initialize);
    construction_input values;
    values.fill(5);
    values[127] = 16;
    effects.used = 0;
    assert(!initialize->initialize(0, {&values, 1}, effects));
    for (auto x : raw)
        assert(x == 0xaa);
    assert(effects.used == 0);
    values[127] = 6;
    assert(initialize->initialize(0, {&values, 1}, effects));
    assert(raw.front() == 0xaa && raw.back() == 0xaa && raw[64] == 0x65);
    for (unsigned i = 1; i < 64; ++i)
        assert(raw[i] == 0x55);
    assert(effects.used == 1 && entries[0].bytes.offset == 1 && entries[0].bytes.size == 64);
    std::array<byte, 3> other_raw{0xaa, 0xaa, 0xaa};
    auto other_view = *view::bind(*format, other_raw, 1, 2);
    auto other_plan = *constructor::make(*format);
    auto other_build = *bind_constructor(other_plan, other_view);
    auto both = composition::bind_group(*initialize, other_build);
    assert(both);
    using both_input = decltype(both)::value_type::input_type;
    construction_input short_values{};
    short_values[0] = 8;
    effects.used = 0;
    auto before_large = raw;
    assert(!both->set(0, both_input{values, short_values}, effects));
    assert(raw == before_large && other_raw[0] == 0xaa && effects.used == 0);
    short_values[0] = 3;
    short_values[1] = 4;
    short_values[2] = 91;
    assert(both->set(0, both_input{values, short_values}, effects));
    assert(other_raw[0] == (3 | (4 << 3)) && other_raw[1] == 91 && other_raw[2] == 0xaa);
    std::puts(
        "TuplePack operation admission, complete maintenance, nesting and construction passed");
}
