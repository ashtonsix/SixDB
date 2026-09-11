#include <ikea/seriespack/composition.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace sp = ikea::seriespack;
namespace composition = sp::composition;

namespace {

// A recording vocabulary for the ordinary authored function. These tokens are
// deliberately unlike the scalar values below; neither is a native ABI proposal.
struct recording_ops {
    enum class operation { body, tail, head, join, less, sum };
    struct token { std::size_t id; };
    struct node {
        operation op;
        std::size_t lhs{};
        std::size_t rhs{};
        unsigned detail{};
        std::uint64_t constant{};
    };
    std::vector<node> nodes;
    const sp::index_range& expected_rows;
    const sp::selection& expected_active;
    const void* expected_source;

    token append(node value) {
        nodes.push_back(value);
        return {nodes.size() - 1};
    }
    template<class Leaf>
    token leaf(operation op, const Leaf& part, const sp::index_range& rows,
               const sp::selection& active, unsigned detail) {
        assert(&rows == &expected_rows);
        assert(&active == &expected_active);
        assert(&part.source == expected_source);
        return append({op, 0, 0, detail});
    }
    template<class Leaf>
    token read_body(const Leaf& part, const sp::index_range& rows,
                    const sp::selection& active) {
        return leaf(operation::body, part, rows, active, Leaf::bit_width);
    }
    template<class Leaf>
    token read_tail(const Leaf& part, const sp::index_range& rows,
                    const sp::selection& active) {
        return leaf(operation::tail, part, rows, active, Leaf::bit_width);
    }
    template<class Leaf>
    token read_head(const Leaf& part, const sp::index_range& rows,
                    const sp::selection& active) {
        return leaf(operation::head, part, rows, active, Leaf::plane);
    }
    template<unsigned LowBits> token join(token high, token low) {
        static_assert(LowBits > 0 && LowBits < 64);
        return append({operation::join, high.id, low.id, LowBits});
    }
    token unsigned_less(token values, std::uint64_t cutoff, const sp::selection& active) {
        assert(&active == &expected_active);
        return append({operation::less, values.id, 0, 0, cutoff});
    }
    token sum(token values, token keep, sp::modulo_u64_sum) {
        return append({operation::sum, values.id, keep.id});
    }
};

void recorded_shared_expansion() {
    struct symbolic_source {} source;
    using format = sp::format<28, sp::preset::filter, 16>;
    auto expression = composition::describe<format>(source);
    sp::index_range rows{37, 41};
    auto active = sp::selection::all();
    recording_ops ops{{}, rows, active, &source};
    constexpr auto cutoff = std::uint64_t{1} << 40; // Wider than the stored domain.
    auto result = composition::selected_sum(ops, expression, rows, active, cutoff);
    using operation = recording_ops::operation;
    const auto count = [&](operation op) {
        return std::ranges::count_if(ops.nodes, [=](const auto& node) { return node.op == op; });
    };
    assert(count(operation::body) == 1);
    assert(count(operation::tail) == 1);
    assert(count(operation::head) == 2);
    assert(count(operation::join) == 3);
    assert(count(operation::less) == 1);
    assert(count(operation::sum) == 1);

    const auto& sum = ops.nodes[result.id];
    const auto& predicate = ops.nodes[sum.rhs];
    assert(sum.op == operation::sum && predicate.op == operation::less);
    assert(sum.lhs == predicate.lhs); // Both consumers reuse the same values node.
    assert(predicate.constant == cutoff);
    const auto& values = ops.nodes[sum.lhs];
    const auto& heads = ops.nodes[values.lhs];
    const auto& payload = ops.nodes[values.rhs];
    assert(values.op == operation::join && values.detail == 12);
    assert(heads.op == operation::join && heads.detail == 8);
    assert(payload.op == operation::join && payload.detail == 4);
    assert(ops.nodes[heads.lhs].detail == 0 && ops.nodes[heads.rhs].detail == 1);
    assert(ops.nodes[payload.lhs].op == operation::body);
    assert(ops.nodes[payload.rhs].op == operation::tail);
}

using component = std::array<std::uint64_t, 128>;
struct synthetic_source {
    component body{};
    component tail{};
    std::array<component, 2> heads{};
};
struct replacement_body {
    static constexpr unsigned bit_width = 8;
    const component& values;
};
struct replacement_tail {
    static constexpr unsigned bit_width = 4;
    const component& values;
};

template<std::size_t N> struct scalar_values {
    std::array<std::uint64_t, N> values{};
    std::array<std::size_t, N> positions{};
};
template<std::size_t N> struct scalar_predicate {
    std::array<bool, N> keep{};
    std::array<std::size_t, N> positions{};
};

// Synthetic components isolate semantic composition from byte layout/codecs.
// The fragment is an ordinary value, and the predicate keeps original positions.
struct scalar_ops {
    unsigned body_reads{};
    unsigned tail_reads{};
    unsigned head_reads{};

    template<class Rows>
    auto load(const component& input, const Rows& rows, const sp::selection& active) {
        scalar_values<Rows::lanes> result;
        for (std::size_t lane = 0; lane < Rows::lanes; ++lane) {
            auto index = rows.index(lane);
            assert(index < input.size());
            result.positions[lane] = index;
            // Inactive values have no semantic promise, even if the synthetic
            // storage happens to contain a plausible value there.
            result.values[lane] = active.contains(index) ? input[index] : UINT64_MAX;
        }
        return result;
    }
    template<class Format, class Rows>
    auto read_body(const composition::body_ref<Format, synthetic_source>& part,
                   const Rows& rows, const sp::selection& active) {
        ++body_reads;
        return load(part.source.body, rows, active);
    }
    template<class Format, class Rows>
    auto read_tail(const composition::tail_ref<Format, synthetic_source>& part,
                   const Rows& rows, const sp::selection& active) {
        ++tail_reads;
        return load(part.source.tail, rows, active);
    }
    template<class Format, unsigned Plane, class Rows>
    auto read_head(const composition::head_ref<Format, Plane, synthetic_source>& part,
                   const Rows& rows, const sp::selection& active) {
        ++head_reads;
        return load(part.source.heads[Plane], rows, active);
    }
    template<class Rows>
    auto read_body(const replacement_body& part, const Rows& rows, const sp::selection& active) {
        ++body_reads;
        return load(part.values, rows, active);
    }
    template<class Rows>
    auto read_tail(const replacement_tail& part, const Rows& rows, const sp::selection& active) {
        ++tail_reads;
        return load(part.values, rows, active);
    }
    template<unsigned LowBits, std::size_t N>
    auto join(const scalar_values<N>& high, const scalar_values<N>& low) {
        static_assert(LowBits > 0 && LowBits < 64);
        assert(high.positions == low.positions);
        scalar_values<N> result{{}, high.positions};
        for (std::size_t lane = 0; lane < N; ++lane)
            result.values[lane] = (high.values[lane] << LowBits) | low.values[lane];
        return result;
    }
    template<std::size_t N>
    auto unsigned_less(const scalar_values<N>& values, std::uint64_t cutoff,
                       const sp::selection& active) {
        scalar_predicate<N> result{{}, values.positions};
        for (std::size_t lane = 0; lane < N; ++lane)
            result.keep[lane] = active.contains(values.positions[lane]) && values.values[lane] < cutoff;
        return result;
    }
    template<std::size_t N>
    std::uint64_t sum(const scalar_values<N>& values, const scalar_predicate<N>& predicate,
                      sp::modulo_u64_sum) {
        assert(values.positions == predicate.positions);
        std::uint64_t result = 0;
        for (std::size_t lane = 0; lane < N; ++lane)
            if (predicate.keep[lane]) result += values.values[lane];
        return result;
    }
};

// The owner has admitted these head spans at their original row origin. The
// source has neither payload storage nor a full SeriesPack view; its first
// head may also be unavailable while the second is inspected independently.
struct admitted_heads {
    std::size_t origin;
    std::array<std::span<const std::uint8_t>, 2> heads;
};
template<std::size_t N> struct head_values {
    using element_type = std::uint8_t;
    std::array<element_type, N> values{};
    std::array<std::size_t, N> positions{};
};
struct head_ops {
    template<class Format, unsigned Plane, class Rows>
    auto read_head(const composition::head_ref<Format, Plane, admitted_heads>& part,
                   const Rows& rows, const sp::selection& active) {
        static_assert(std::remove_cvref_t<decltype(part)>::bit_width == 8);
        const auto bytes = part.source.heads[Plane];
        head_values<Rows::lanes> result;
        for (std::size_t lane = 0; lane < Rows::lanes; ++lane) {
            const auto index = rows.index(lane);
            assert(index >= part.source.origin && index - part.source.origin < bytes.size());
            result.positions[lane] = index;
            if (active.contains(index)) result.values[lane] = bytes[index - part.source.origin];
        }
        return result;
    }
    template<std::size_t N>
    auto unsigned_less(const head_values<N>& values, std::uint64_t cutoff,
                       const sp::selection& active) {
        scalar_predicate<N> result{{}, values.positions};
        for (std::size_t lane = 0; lane < N; ++lane)
            result.keep[lane] = active.contains(values.positions[lane]) && values.values[lane] < cutoff;
        return result;
    }
    template<std::size_t N>
    std::uint64_t sum(const head_values<N>& values, const scalar_predicate<N>& predicate,
                      sp::modulo_u64_sum) {
        assert(values.positions == predicate.positions);
        std::uint64_t result = 0;
        for (std::size_t lane = 0; lane < N; ++lane)
            if (predicate.keep[lane]) result += values.values[lane];
        return result;
    }
};

template<sp::unsigned_element UInt>
struct positional_sink {
    std::span<UInt> output;
    std::size_t origin;
    const sp::selection& expected_active;

    template<unsigned Bits, class Values, class Rows>
    void store(const Values& values, const Rows& rows, const sp::selection& active) {
        static_assert(Bits <= sizeof(UInt) * 8);
        assert(&active == &expected_active);
        for (std::size_t lane = 0; lane < Rows::lanes; ++lane) {
            const auto index = rows.index(lane);
            assert(values.positions[lane] == index);
            if (active.contains(index)) {
                assert(index >= origin && index - origin < output.size());
                output[index - origin] = static_cast<UInt>(values.values[lane]);
            }
        }
    }
};

void projected_head_without_payload() {
    using format = sp::format<60, sp::preset::filter, 16>;
    std::array<std::uint8_t, 16> bytes{};
    bytes[2] = 1; bytes[3] = 2; bytes[10] = 128; bytes[11] = 255;
    const admitted_heads source{32, {std::span<const std::uint8_t>{}, std::span<const std::uint8_t>{bytes}}};
    const auto expression = composition::describe<format>(source);
    static_assert(decltype(expression.head1)::bit_width == 8);
    const std::array<std::uint64_t, 1> words{(1u << 2) | (1u << 10) | (1u << 11)};
    auto active = sp::selection::bitmap(32, 16, std::span(words));
    const sp::index_range range{34, 44};
    assert(active.validate(range));
    composition::lane_coordinates<composition::two_run_lanes<2, 8>> rows{34};
    head_ops ops; // This vocabulary has no payload reads or joins.
    const auto values = composition::read(ops, expression.head1, rows, active);
    static_assert(std::is_same_v<typename decltype(values)::element_type, std::uint8_t>);
    assert((values.positions == std::array<std::size_t, 4>{34, 35, 42, 43}));
    for (std::uint64_t cutoff : {UINT64_C(256), UINT64_C(257), UINT64_MAX})
        assert(composition::selected_sum(ops, expression.head1, rows, active, cutoff) == 384);
    assert(composition::selected_sum(ops, expression.head1, rows, active, 255) == 129);
    assert(composition::selected_sum(ops, expression.head1, rows, active, 128) == 1);
    assert(composition::selected_sum(ops, expression.head1, rows, active, 0) == 0);

    // A projected head admits a byte sink even inside a 60-bit format. Preserve
    // holes and inactive rows without reading the unavailable payload/head0.
    std::array<std::uint8_t, 12> output;
    output.fill(0xa5);
    positional_sink<std::uint8_t> sink{output, 33, active};
    composition::materialize(ops, expression.head1, rows, active, sink);
    for (std::size_t i = 0; i < output.size(); ++i) {
        const auto index = 33 + i;
        const auto expected = index == 34 ? 1 : index == 42 ? 128 : index == 43 ? 255 : 0xa5;
        assert(output[i] == expected);
    }

    recording_ops recording{{}, range, active, &source};
    const auto result = composition::selected_sum(recording, expression.head1, range, active, 257);
    using operation = recording_ops::operation;
    assert(recording.nodes.size() == 3);
    const auto& sum = recording.nodes[result.id];
    const auto& predicate = recording.nodes[sum.rhs];
    const auto& head = recording.nodes[sum.lhs];
    assert(sum.op == operation::sum && predicate.op == operation::less);
    assert(sum.lhs == predicate.lhs && predicate.constant == 257);
    assert(head.op == operation::head && head.detail == 1);
}

void mapped_selection_and_leaf_replacement() {
    using format = sp::format<12, sp::preset::bulk>;
    synthetic_source source;
    source.body[34] = 0x10; source.tail[34] = 1;
    source.body[35] = 0x23; source.tail[35] = 4;
    source.body[42] = 0xff; source.tail[42] = 15;
    source.body[43] = 0x40; source.tail[43] = 0;
    const auto expression = composition::describe<format>(source);
    composition::lane_coordinates<composition::two_run_lanes<2, 8>> rows{34};
    const std::array<std::uint64_t, 1> words{(1u << 2) | (1u << 10) | (1u << 11)};
    auto active = sp::selection::bitmap(32, 32, std::span(words));
    assert(active.validate({34, 44}));
    scalar_ops ops;
    // Cutoffs above 4095 are legal; truncating 5000 to 12 bits would drop rows.
    assert(composition::selected_sum(ops, expression, rows, active, 5000) == 0x1500);
    assert(composition::selected_sum(ops, expression, rows, active, 2000) == 0x501);
    auto values = composition::read(ops, expression, rows, active);
    assert((values.positions == std::array<std::size_t, 4>{34, 35, 42, 43}));

    component other_body{};
    component other_tail{};
    other_body[34] = 1; other_tail[34] = 2;
    other_body[42] = 2; other_tail[42] = 1;
    other_body[43] = 3; other_tail[43] = 15;
    using payload = composition::payload_expression<12, replacement_body, replacement_tail>;
    const composition::value_expression<12, 0, payload,
        decltype(expression.head0), decltype(expression.head1)> replaced{
        {{other_body}, {other_tail}}, expression.head0, expression.head1};
    // The enclosing selected_sum is unchanged when both deeply nested leaves
    // have unrelated types and storage, but the same bit/coordinate contracts.
    assert(composition::selected_sum(ops, replaced, rows, active, 5000) == 0x72);

    std::array<std::uint16_t, 12> output;
    output.fill(0xa5a5);
    positional_sink<std::uint16_t> sink{output, 33, active};
    composition::materialize(ops, replaced, rows, active, sink);
    for (std::size_t i = 0; i < output.size(); ++i) {
        const auto index = 33 + i;
        const auto expected = index == 34 ? 0x12 : index == 42 ? 0x21 : index == 43 ? 0x3f : 0xa5a5;
        assert(output[i] == expected);
    }
}

void independent_sources_preserve_values() {
    synthetic_source first, second;
    first.body[70] = 10; first.body[71] = 20;
    second.body[70] = 100; second.body[71] = 200;
    auto a = composition::describe<sp::format<8>>(first);
    auto b = composition::describe<sp::format<8>>(second);
    composition::lane_coordinates<composition::ordered_lanes<2>> rows{70};
    auto active = sp::selection::all();
    scalar_ops ops;
    auto earlier = composition::read(ops, a, rows, active);
    auto later = composition::read(ops, b, rows, active);
    auto keep_earlier = ops.unsigned_less(earlier, 15, active);
    assert(ops.sum(earlier, keep_earlier, sp::modulo_u64_sum{}) == 10);
    assert(ops.sum(earlier, ops.unsigned_less(earlier, 1000, active), sp::modulo_u64_sum{}) == 30);
    assert(ops.sum(later, ops.unsigned_less(later, 1000, active), sp::modulo_u64_sum{}) == 300);
    assert(ops.body_reads == 2);
}

void domain_edges_and_head_only() {
    composition::lane_coordinates<composition::ordered_lanes<2>> rows{9};
    auto active = sp::selection::all();
    synthetic_source source;
    scalar_ops ops;
    source.body[9] = UINT64_MAX;
    source.body[10] = UINT64_C(1) << 63;
    auto full = composition::describe<sp::format<64>>(source);
    assert(composition::selected_sum(ops, full, rows, active, UINT64_MAX) == (UINT64_C(1) << 63));
    auto all_values = composition::read(ops, full, rows, active);
    scalar_predicate<2> all{{true, true}, all_values.positions};
    assert(ops.sum(all_values, all, sp::modulo_u64_sum{}) == (UINT64_C(1) << 63) - 1);
    assert(ops.tail_reads == 0 && ops.head_reads == 0);

    // Full-width values also join safely from 16 head bits and 48 low bits.
    source.heads[0][9] = 0xff; source.heads[1][9] = 0xff;
    source.body[9] = UINT64_C(0xffffffffffff);
    source.heads[0][10] = 0x80; source.heads[1][10] = 0;
    source.body[10] = 7;
    auto headed_full = composition::describe<sp::format<64, sp::preset::filter, 16>>(source);
    assert(composition::selected_sum(ops, headed_full, rows, active, UINT64_MAX) ==
           (UINT64_C(1) << 63) + 7);

    source.heads[0][9] = 0x12; source.heads[1][9] = 0x34;
    source.heads[0][10] = 0xff; source.heads[1][10] = 0xfe;
    scalar_ops head_ops;
    auto heads8 = composition::describe<sp::format<8, sp::preset::filter, 8>>(source);
    assert(composition::selected_sum(head_ops, heads8, rows, active, 256) == 0x111);
    auto heads16 = composition::describe<sp::format<16, sp::preset::filter, 16>>(source);
    assert(composition::selected_sum(head_ops, heads16, rows, active, 65536) == 0x11232);
    assert(head_ops.body_reads == 0 && head_ops.tail_reads == 0 && head_ops.head_reads == 3);
}

void selection_admission() {
    assert(sp::validate_range({7, 7}, 7));
    assert(sp::validate_range({8, 7}, 10).error() == sp::error::invalid_range);
    assert(sp::validate_range({0, 11}, 10).error() == sp::error::invalid_range);
    assert(sp::selection::all().validate({8, 7}).error() == sp::error::invalid_range);
    const std::array<std::uint64_t, 2> words{1, 1};
    auto selected = sp::selection::bitmap(100, 65, std::span(words));
    assert(selected.validate({100, 165}));
    assert(selected.contains(100) && !selected.contains(101) && selected.contains(164));
    assert(selected.validate({99, 100}).error() == sp::error::invalid_selection);
    assert(selected.validate({100, 166}).error() == sp::error::invalid_selection);
    auto short_words = sp::selection::bitmap(100, 65, std::span(words).first(1));
    assert(short_words.validate({100, 101}).error() == sp::error::invalid_selection);
    auto overflow = sp::selection::bitmap(std::numeric_limits<std::size_t>::max(), 1,
                                         std::span(words).first(1));
    assert(overflow.validate({0, 0}).error() == sp::error::invalid_selection);
    auto empty = sp::selection::bitmap(41, 0, {});
    assert(empty.validate({41, 41}));
    const std::array<std::uint64_t, 1> none{0};
    auto no_rows = sp::selection::bitmap(9, 2, std::span(none));
    synthetic_source source;
    auto expression = composition::describe<sp::format<12>>(source);
    composition::lane_coordinates<composition::ordered_lanes<2>> rows{9};
    scalar_ops ops;
    assert(composition::selected_sum(ops, expression, rows, no_rows, UINT64_MAX) == 0);
}

template<class Source>
concept describes_temporary = requires(Source source) { composition::describe(std::move(source)); };

void static_view_references() {
    using format = sp::format<12, sp::preset::bulk>;
    using view_type = sp::static_const_view<format>;
    static_assert(!describes_temporary<view_type>);
    alignas(32) std::array<std::byte, format::payload::tile_bytes> bytes{};
    const auto view = view_type::assume_valid(20, {{std::span(bytes), bytes.size()}, {}});
    const auto expression = composition::describe(view);
    static_assert(decltype(expression)::bit_width == 12);
    static_assert(std::is_same_v<decltype(expression.payload.body.source), const view_type&>);
    assert(&expression.payload.body.source == &view);
    assert(&expression.payload.tail.source == &view);
    auto dynamic = view.as_dynamic();
    assert(dynamic.layout() == format::layout && dynamic.size() == 20);
    assert(dynamic.placement().payload.bytes.data() == bytes.data());
}

} // namespace

int main() {
    recorded_shared_expansion();
    projected_head_without_payload();
    mapped_selection_and_leaf_replacement();
    independent_sources_preserve_values();
    domain_edges_and_head_only();
    selection_admission();
    static_view_references();
    std::puts("SeriesPack composition contracts passed");
}
