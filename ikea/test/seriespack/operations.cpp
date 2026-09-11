#include <ikea/seriespack/operations.h>
#include <ikea/seriespack/point.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

using namespace ikea::seriespack;

namespace {

std::uint64_t width_mask(unsigned width) {
    return width == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
}

struct buffer {
    explicit buffer(std::size_t size = 0) : allocation(size + 63, std::byte{0xa5}) {
        const auto base = reinterpret_cast<std::uintptr_t>(allocation.data());
        bytes = std::span(allocation).subspan((32 - base % 32) % 32, size);
    }
    std::vector<std::byte> allocation;
    std::span<std::byte> bytes;
};

struct fixture {
    fixture(description layout, std::size_t count, bool gaps = true)
        : layout(layout), n(count), t(tile_values(layout)), b(tile_bytes(layout)),
          payload_stride(b + (gaps ? 32 : 0)), head_stride(t + (gaps ? 17 : 0)),
          extents(*required_extents(layout, n, payload_stride, {head_stride, head_stride})),
          payload(extents.payload_envelope), head0(extents.head_envelopes[0]),
          head1(extents.head_envelopes[1]),
          placement{{payload.bytes, payload_stride},
                    {{{head0.bytes, head_stride}, {head1.bytes, head_stride}}}},
          view(*mutable_view::attach(layout, n, placement)) {}

    description layout;
    std::size_t n, t, b, payload_stride, head_stride;
    extent_info extents;
    buffer payload, head0, head1;
    basic_placement<std::byte> placement;
    mutable_view view;
};

struct snapshot {
    explicit snapshot(const fixture& f)
        : payload(f.payload.allocation), head0(f.head0.allocation), head1(f.head1.allocation) {}
    std::vector<std::byte> payload, head0, head1;
    void unchanged(const fixture& f) const {
        assert(payload == f.payload.allocation);
        assert(head0 == f.head0.allocation);
        assert(head1 == f.head1.allocation);
    }
};

bool owned(const_view view, const std::byte* byte) {
    const auto address = reinterpret_cast<std::uintptr_t>(byte);
    const auto t = tile_values(view.layout());
    const auto tiles = view.size() / t + (view.size() % t != 0);
    const auto in_plane = [&](basic_plane<const std::byte> plane, std::size_t width) {
        if (width == 0 || tiles == 0) return false;
        const auto base = reinterpret_cast<std::uintptr_t>(plane.bytes.data());
        if (address < base) return false;
        const auto relative = address - base;
        return relative / plane.stride < tiles && relative % plane.stride < width;
    };
    if (in_plane(view.placement().payload, tile_bytes(view.layout()))) return true;
    for (unsigned h = 0; h < view.layout().head_bits / 8; ++h)
        if (in_plane(view.placement().heads[h], t)) return true;
    return false;
}

bool covered(const std::byte* byte, const effect_output& effects, std::size_t first = 0) {
    const auto address = reinterpret_cast<std::uintptr_t>(byte);
    for (std::size_t i = first; i < effects.size; ++i) {
        const auto base = reinterpret_cast<std::uintptr_t>(effects.storage[i].data);
        if (address >= base && address - base < effects.storage[i].size) return true;
    }
    return false;
}

void check_coverage(const_view view, const effect_output& effects, std::size_t first = 0) {
    assert(effects.size <= effects.storage.size());
    const auto t = tile_values(view.layout());
    const auto tiles = view.size() / t + (view.size() % t != 0);
    const auto byte_count = tiles * (tile_bytes(view.layout()) + t * (view.layout().head_bits / 8));
    for (std::size_t i = first; i < effects.size; ++i) {
        const auto span = effects.storage[i];
        assert(span.size <= byte_count);
        for (std::size_t byte = 0; byte < span.size; ++byte) assert(owned(view, span.data + byte));
    }
}

// Byte differences cannot reveal stores of an unchanged value. This checks
// coverage of every changed byte and excludes all foreign/gap bytes; exact
// issued-store footprints belong to the physical implementation's own checks.
void check_changes(const_view view, std::span<const std::byte> before,
                   std::span<const std::byte> after, const effect_output& effects,
                   std::size_t first = 0) {
    assert(before.size() == after.size());
    for (std::size_t i = 0; i < after.size(); ++i) {
        if (before[i] != after[i]) {
            assert(owned(view, after.data() + i));
            assert(covered(after.data() + i, effects, first));
        }
    }
}

void check_changes(const fixture& f, const snapshot& before, const effect_output& effects,
                   std::size_t first = 0) {
    check_coverage(f.view.as_const(), effects, first);
    check_changes(f.view.as_const(), before.payload, f.payload.allocation, effects, first);
    check_changes(f.view.as_const(), before.head0, f.head0.allocation, effects, first);
    check_changes(f.view.as_const(), before.head1, f.head1.allocation, effects, first);
}

void effect_storage_unchanged(std::span<const byte_span> before,
                              std::span<const byte_span> after) {
    assert(before.size() == after.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        assert(before[i].data == after[i].data);
        assert(before[i].size == after[i].size);
    }
}

void ordinary_operations() {
    std::byte prefix_byte{};
    for (unsigned width = 1; width <= 64; ++width) {
        for (unsigned heads : {0u, 8u, 16u}) {
            if (heads > width) continue;
            for (auto recipe : {preset::compact, preset::bulk, preset::bulk_arm,
                                preset::filter, preset::filter_arm}) {
                const auto layout = resolve(width, recipe, heads);
                if (!layout) continue;
                fixture f(*layout, tile_values(*layout) + 3);
                const auto maximum = width_mask(width);
                std::vector<std::uint64_t> values(f.n);
                for (std::size_t i = 0; i < f.n; ++i)
                    values[i] = (std::uint64_t(i) * 0x9e3779b97f4a7c15ULL) & maximum;
                values.front() = 0;
                values.back() = maximum;
                const auto capacity = encode_effect_capacity(f.view.as_const());
                assert(capacity);
                std::vector<byte_span> spans(*capacity + 1, {&prefix_byte, 1});
                effect_output effects{spans, 1};
                const snapshot before(f);
                assert(encode(f.view, input_values{std::span(values)}, &effects, execution_target::scalar));
                assert(spans.front().data == &prefix_byte && spans.front().size == 1);
                check_changes(f, before, effects, 1);
                for (std::size_t i = 0; i < f.n; ++i) assert(get(f.view.as_const(), i) == values[i]);

                // Encoding initializes complete tiles. An explicitly enlarged
                // view of that initialized capacity observes canonical zeros.
                const auto padded = const_view::attach(*layout, f.extents.capacity, as_const(f.placement));
                assert(padded);
                for (std::size_t i = f.n; i < f.extents.capacity; ++i) assert(get(*padded, i) == 0);

                std::vector<std::uint64_t> decoded(f.n, ~std::uint64_t{0});
                assert(decode(f.view.as_const(), {1, f.n - 1}, output_values{std::span(decoded)},
                              execution_target::scalar));
                for (std::size_t i = 1; i < f.n - 1; ++i) assert(decoded[i - 1] == values[i]);
                assert(decoded[f.n - 2] == ~std::uint64_t{0});

                const auto index = f.n / 2;
                const auto set_capacity = write_effect_capacity(f.view.as_const(), {index, index + 1});
                assert(set_capacity);
                spans.resize(*set_capacity + 1);
                effects = {spans, 1};
                const snapshot before_set(f);
                values[index] ^= maximum;
                assert(set(f.view, index, values[index], &effects));
                check_changes(f, before_set, effects, 1);

                const auto bound = bind_reader(f.view.as_const(), execution_target::scalar);
                assert(bound && bound->target() == execution_target::scalar);
                assert(bound->source().layout() == *layout && bound->source().size() == f.n);
                bound->decode({0, f.n}, output_values{std::span(decoded)});
                assert(decoded == values);
                for (std::size_t i = 0; i < f.n; ++i) assert(bound->get(i) == values[i]);
            }
        }
    }
}

void checked_rejection() {
    fixture f({12, 0, geometry::local8}, 19);
    std::vector<std::uint64_t> input(f.n, 123);
    assert(encode(f.view, input_values{std::span(input)}, nullptr, execution_target::scalar));
    const auto cap = encode_effect_capacity(f.view.as_const());
    const auto write_cap = write_effect_capacity(f.view.as_const(), {0, f.n});
    assert(cap && write_cap);
    std::byte marker{};
    std::vector<byte_span> spans(std::max(*cap, *write_cap) + 1, {&marker, 17});
    effect_output effects{spans, 1};
    const auto original_effects = spans;
    const snapshot before(f);
    const auto unchanged = [&] {
        before.unchanged(f);
        assert(effects.size == 1);
        effect_storage_unchanged(original_effects, spans);
    };

    input.back() = 4096; // Reject after examining earlier valid values.
    auto status = encode(f.view, input_values{std::span(input)}, &effects, execution_target::scalar);
    assert(!status && status.error() == error::invalid_value);
    unchanged();
    status = write(f.view, {0, f.n}, input_values{std::span(input)}, selection::all(), &effects);
    assert(!status && status.error() == error::invalid_value);
    unchanged();
    status = set(f.view, 2, 4096, &effects);
    assert(!status && status.error() == error::invalid_value);
    unchanged();
    input.back() = 123;

    status = encode(f.view, input_values{std::span(input).first(f.n - 1)}, &effects,
                    execution_target::scalar);
    assert(!status);
    unchanged();
    status = write(f.view, {5, 4}, input_values{std::span(input)}, selection::all(), &effects);
    assert(!status && status.error() == error::invalid_range);
    unchanged();
    status = set(f.view, f.n, 123, &effects);
    assert(!status && status.error() == error::invalid_range);
    assert(!get(f.view.as_const(), f.n));
    unchanged();

    const std::array<std::uint64_t, 1> mask{1};
    for (const auto bad : {selection::bitmap(1, f.n, mask), selection::bitmap(0, f.n, {}),
                           selection::bitmap(std::numeric_limits<std::size_t>::max(), 2, mask)}) {
        status = write(f.view, {0, f.n}, input_values{std::span(input)}, bad, &effects);
        assert(!status && status.error() == error::invalid_selection);
        assert(!write_effect_capacity(f.view.as_const(), {0, f.n}, bad));
        unchanged();
    }

    effect_output no_capacity{std::span(spans).first(1), 1};
    status = encode(f.view, input_values{std::span(input)}, &no_capacity, execution_target::scalar);
    assert(!status && status.error() == error::insufficient_effect_storage);
    assert(no_capacity.size == 1);
    unchanged();
    status = set(f.view, 0, 17, &no_capacity);
    assert(!status && status.error() == error::insufficient_effect_storage);
    unchanged();
    status = write(f.view, {0, f.n}, input_values{std::span(input)}, selection::all(), &no_capacity);
    assert(!status && status.error() == error::insufficient_effect_storage);
    unchanged();

    std::vector<std::uint8_t> narrow(f.n, 0xa5);
    status = decode(f.view.as_const(), {0, f.n}, output_values{std::span(narrow)}, execution_target::scalar);
    assert(!status && status.error() == error::invalid_output_type);
    assert(std::ranges::all_of(narrow, [](auto value) { return value == 0xa5; }));
    std::vector<std::uint16_t> short_output(f.n - 1, 0xa5a5);
    status = decode(f.view.as_const(), {0, f.n}, output_values{std::span(short_output)}, execution_target::scalar);
    assert(!status);
    status = decode(f.view.as_const(), {0, f.n + 1}, output_values{std::span(short_output)}, execution_target::scalar);
    assert(!status && status.error() == error::invalid_range);
    assert(std::ranges::all_of(short_output, [](auto value) { return value == 0xa5a5; }));
    unchanged();
}

void selected_coordinates() {
    fixture f({9, 0, geometry::local8}, 25);
    std::vector<std::uint64_t> values(f.n, 7);
    assert(encode(f.view, input_values{std::span(values)}, nullptr, execution_target::scalar));
    // Bit zero names row10, and input zero names row14. The selected input
    // slots are 0,2,5; compacted slots 0,1,2 would produce different results.
    const std::array<std::uint64_t, 1> words{(1u << 4) | (1u << 6) | (1u << 9)};
    const auto selected = selection::bitmap(10, 12, words);
    std::array<std::uint64_t, 6> input{31, 99999, 77, 99999, 99999, 511};
    const auto capacity = write_effect_capacity(f.view.as_const(), {14, 20}, selected);
    assert(capacity);
    std::vector<byte_span> spans(*capacity);
    effect_output effects{spans, 0};
    const snapshot before(f);
    assert(write(f.view, {14, 20}, input_values{std::span(input)}, selected, &effects));
    values[14] = 31;
    values[16] = 77;
    values[19] = 511;
    check_changes(f, before, effects);
    for (std::size_t i = 0; i < f.n; ++i) assert(get(f.view.as_const(), i) == values[i]);

    const snapshot after(f);
    effects.size = 0;
    const auto original_effects = spans;
    const auto original_count = effects.size;
    input[5] = 512;
    const auto status = write(f.view, {14, 20}, input_values{std::span(input)}, selected, &effects);
    assert(!status && status.error() == error::invalid_value);
    after.unchanged(f);
    assert(effects.size == original_count);
    effect_storage_unchanged(original_effects, spans);

    const std::array<std::uint64_t, 1> none{0};
    const auto empty = selection::bitmap(10, 12, none);
    assert(write_effect_capacity(f.view.as_const(), {14, 20}, empty) == 0);
    assert(write(f.view, {14, 20}, input_values{std::span(input)}, empty));
    after.unchanged(f); // Invalid inputs at entirely unselected positions are irrelevant.

    fixture crossing({7, 0, geometry::local8}, 80);
    std::array<std::uint8_t, 80> initial{};
    assert(encode(crossing.view, input_values{std::span(initial)}, nullptr, execution_target::scalar));
    const std::array<std::uint64_t, 2> crossing_words{std::uint64_t{1} << 61, 1u | (1u << 6)};
    const auto crossing_selection = selection::bitmap(5, 75, crossing_words);
    const std::array<std::uint64_t, 10> crossing_input{31, 999, 999, 63, 999, 999, 999, 999, 999, 127};
    assert(write(crossing.view, {66, 76}, input_values{std::span(crossing_input)}, crossing_selection));
    initial[66] = 31;
    initial[69] = 63;
    initial[75] = 127;
    for (std::size_t i = 0; i < initial.size(); ++i) assert(get(crossing.view.as_const(), i) == initial[i]);
}

void alias_rejection() {
    fixture f({8, 0, geometry::local8}, 16, false);
    std::array<std::uint8_t, 16> input{};
    assert(encode(f.view, input_values{std::span(input)}, nullptr, execution_target::scalar));
    const snapshot before(f);
    auto alias_input = std::span(reinterpret_cast<const std::uint8_t*>(f.payload.bytes.data()), f.n);
    assert(!encode(f.view, input_values{alias_input}, nullptr, execution_target::scalar));
    assert(!write(f.view, {0, f.n}, input_values{alias_input}));
    auto alias_output = std::span(reinterpret_cast<std::uint8_t*>(f.payload.bytes.data()), f.n);
    assert(!decode(f.view.as_const(), {0, f.n}, output_values{alias_output}, execution_target::scalar));
    before.unchanged(f);

    // The bitmap is a real uint64 object; the destination borrows its byte
    // representation. Rejection precedes mutation of this still-live mask.
    std::array<std::uint64_t, 8> owner{5};
    const auto owner_before = owner;
    auto view = mutable_view::attach({8, 0, geometry::local8}, 8,
                                     {{std::as_writable_bytes(std::span(owner)), 8}, {}});
    assert(view);
    const auto mask = selection::bitmap(0, 8, std::span(owner).first(1));
    assert(!write(*view, {0, 8}, input_values{std::span(input).first(8)}, mask));
    assert(owner == owner_before);

    std::array<byte_span, 16> spans{};
    // size_t and uint64_t are the same type on supported Linux targets. Using
    // this member as the word avoids inventing overlapping object lifetimes.
    static_assert(std::is_same_v<std::size_t, std::uint64_t>);
    spans[0].size = 1;
    const auto spans_before = spans;
    effect_output effects{spans, 0};
    const auto effect_mask = selection::bitmap(0, 8, std::span(&spans[0].size, 1));
    assert(!write(f.view, {0, 8}, input_values{std::span(input).first(8)}, effect_mask, &effects));
    assert(effects.size == 0);
    effect_storage_unchanged(spans_before, spans);
    before.unchanged(f);

    // Effect descriptors themselves cannot live in the encoded destination.
    const auto descriptor_view = mutable_view::attach({8, 0, geometry::local8}, 8,
        {{std::as_writable_bytes(std::span(spans)), 8}, {}});
    assert(descriptor_view);
    assert(!encode(*descriptor_view, input_values{std::span(input).first(8)}, &effects,
                   execution_target::scalar));
    effect_storage_unchanged(spans_before, spans);
    assert(effects.size == 0);
}

void empty_and_append_views() {
    auto empty = mutable_view::attach({16, 16, geometry::local8}, 0, {});
    assert(empty);
    std::array<std::uint16_t, 0> nothing{};
    effect_output no_effects{{}, 0};
    assert(encode_effect_capacity(empty->as_const()) == 0);
    assert(write_effect_capacity(empty->as_const(), {0, 0}) == 0);
    assert(encode(*empty, input_values{std::span(nothing)}, &no_effects, execution_target::scalar));
    assert(decode(empty->as_const(), {0, 0}, output_values{std::span(nothing)}, execution_target::scalar));
    assert(write(*empty, {0, 0}, input_values{std::span(nothing)}, selection::all(), &no_effects));
    assert(!get(empty->as_const(), 0));
    assert(no_effects.size == 0);

    fixture f({7, 0, geometry::local8}, 10);
    std::array<std::uint8_t, 10> initial{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    assert(encode(f.view, input_values{std::span(initial)}, nullptr, execution_target::scalar));
    const auto retained = f.view.as_const();
    const auto bound = bind_reader(retained, execution_target::scalar);
    assert(bound);
    // This owner is exclusive and quiescent. Appending inside the initialized
    // tile shares physical RMW bytes; old length alone is no concurrency fence.
    const auto staging = mutable_view::attach(f.layout, 13, f.placement);
    assert(staging);
    const std::array<std::uint8_t, 3> appended{100, 101, 102};
    assert(write(*staging, {10, 13}, input_values{std::span(appended)}));
    assert(retained.size() == 10 && bound->source().size() == 10 && staging->size() == 13);
    for (std::size_t i = 0; i < initial.size(); ++i) {
        assert(get(retained, i) == initial[i]);
        assert(bound->get(i) == initial[i]);
    }
    assert(!get(retained, 10));
    for (std::size_t i = 0; i < appended.size(); ++i) assert(get(staging->as_const(), 10 + i) == appended[i]);
    const auto capacity = const_view::attach(f.layout, 16, as_const(f.placement));
    assert(capacity);
    for (std::size_t i = 13; i < 16; ++i) assert(get(*capacity, i) == 0);
}

void interleaved_siblings() {
    // Two stripe tiles interleave payload, two heads and 32B of sibling data.
    alignas(32) std::array<std::byte, 512> owner;
    owner.fill(std::byte{0xa5});
    const auto before = owner;
    const auto bytes = std::span(owner);
    basic_placement<std::byte> placement{
        {bytes, 256}, {{{bytes.subspan(96), 256}, {bytes.subspan(160), 256}}}};
    const auto view = mutable_view::attach({28, 16, geometry::striped}, 67, placement);
    assert(view);
    std::array<std::uint32_t, 67> input{};
    for (std::size_t i = 0; i < input.size(); ++i) input[i] = static_cast<std::uint32_t>(i * 917345);
    const auto capacity = encode_effect_capacity(view->as_const());
    assert(capacity);
    std::vector<byte_span> spans(*capacity);
    effect_output effects{spans, 0};
    assert(encode(*view, input_values{std::span(input)}, &effects, execution_target::scalar));
    check_coverage(view->as_const(), effects);
    check_changes(view->as_const(), before, owner, effects);
    for (std::size_t tile = 0; tile < 2; ++tile)
        for (std::size_t i = 224; i < 256; ++i) assert(owner[tile * 256 + i] == std::byte{0xa5});
    for (std::size_t i = 0; i < input.size(); ++i) assert(get(view->as_const(), i) == input[i]);
}

void narrower_inputs() {
    // Input types constrain actual values and may be narrower than the stored
    // domain. Output types must still be able to represent the complete domain.
    for (const auto layout : {description{12, 0, geometry::local8},
                              description{64, 0, geometry::local8},
                              description{28, 16, geometry::striped}}) {
        fixture f(layout, 19);
        std::array<std::uint8_t, 19> input{};
        for (std::size_t i = 0; i < input.size(); ++i) input[i] = static_cast<std::uint8_t>(i * 13);
        for (auto target : {execution_target::scalar, execution_target::automatic}) {
            assert(encode(f.view, input_values{std::span(input)}, nullptr, target));
            for (std::size_t i = 0; i < input.size(); ++i) assert(get(f.view.as_const(), i) == input[i]);
        }
        const std::array<std::uint8_t, 6> replacements{201, 202, 203, 204, 205, 206};
        const std::array<std::uint64_t, 1> words{(1u << 2) | (1u << 5)};
        const auto selected = selection::bitmap(3, 6, words);
        assert(write(f.view, {3, 9}, input_values{std::span(replacements)}, selected));
        input[5] = replacements[2];
        input[8] = replacements[5];
        for (std::size_t i = 0; i < input.size(); ++i) assert(get(f.view.as_const(), i) == input[i]);
        std::array<std::uint8_t, 19> output{};
        const auto rejected = decode(f.view.as_const(), {0, 19}, output_values{std::span(output)},
                                     execution_target::scalar);
        assert(!rejected && rejected.error() == error::invalid_output_type);
        if (layout.head_bits != 0) {
            for (auto byte : f.head0.bytes) assert(byte == std::byte{0});
            for (std::size_t i = 0; i < f.n; ++i)
                assert(f.head1.bytes[i] == std::byte{0});
        }
    }
}

void bound_construction() {
    std::byte prefix_byte{};
    for (const auto layout : {description{5, 0, geometry::local8},
                              // Narrow upstream bytes exercise head extraction
                              // at both ends of the sub-byte shift range.
                              description{9, 8, geometry::local8},
                              description{15, 8, geometry::striped},
                              description{17, 16, geometry::striped},
                              description{21, 16, geometry::striped},
                              description{23, 16, geometry::local8},
                              description{31, 16, geometry::striped},
                              description{64, 0, geometry::local8},
                              description{16, 16, geometry::local8}}) {
        const auto t = tile_values(layout);
        for (const auto n : {t, t + 3}) {
            fixture actual(layout, n, n != t);
            fixture reference(layout, n, n != t);
            std::vector<std::uint8_t> narrow(n);
            std::vector<std::uint64_t> wide(n);
            for (std::size_t i = 0; i < n; ++i) {
                narrow[i] = static_cast<std::uint8_t>((i * 17) & width_mask(layout.width));
                wide[i] = (std::uint64_t(i + 1) * 0x9e3779b97f4a7c15ULL) & width_mask(layout.width);
            }
            const auto capacity = encode_effect_capacity(actual.view.as_const());
            assert(capacity);
            std::vector<byte_span> spans(*capacity + 1);
            spans.front() = {&prefix_byte, 1};
            for (auto target : {execution_target::scalar, execution_target::automatic,
                                execution_target::avx2, execution_target::avx512,
                                execution_target::neon}) {
                const auto encoder = bind_encoder(actual.view, target);
                if (!encoder) {
                    assert(target != execution_target::scalar && target != execution_target::automatic);
                    assert(encoder.error() == error::unsupported);
                    continue;
                }
                assert(encoder->target() != execution_target::automatic);
                if (target != execution_target::automatic) assert(encoder->target() == target);
                assert(encoder->destination().layout() == layout);
                assert(encoder->destination().size() == n);
                assert(encoder->destination().placement().payload.bytes.data() == actual.payload.bytes.data());

                // Both arrays, the destination and effect capacity are proven
                // before invoking this trusted binding. Reuse it for a narrow
                // source and then a wider carrier with fitting actual values.
                const auto exercise = [&](input_values input) {
                    assert(encode(reference.view, input, nullptr, execution_target::scalar));
                    effect_output effects{spans, 1};
                    const snapshot before(actual);
                    encoder->encode(input, &effects);
                    assert(spans.front().data == &prefix_byte && spans.front().size == 1);
                    const auto compare = [&](std::span<const std::byte> got,
                                             std::span<const std::byte> expected,
                                             const char* plane) {
                        assert(got.size() == expected.size());
                        for (std::size_t i = 0; i != got.size(); ++i) if (got[i] != expected[i]) {
                            std::fprintf(stderr,
                                "bound construction: k=%u h=%u geometry=%u n=%zu target=%u "
                                "resolved=%u input=%u plane=%s offset=%zu got=%02x expected=%02x\n",
                                layout.width, layout.head_bits, unsigned(layout.storage), n,
                                unsigned(target), unsigned(encoder->target()), unsigned(input.width),
                                plane, i, std::to_integer<unsigned>(got[i]),
                                std::to_integer<unsigned>(expected[i]));
                            std::abort();
                        }
                    };
                    compare(actual.payload.bytes, reference.payload.bytes, "payload");
                    compare(actual.head0.bytes, reference.head0.bytes, "head0");
                    compare(actual.head1.bytes, reference.head1.bytes, "head1");
                    check_changes(actual, before, effects, 1);
                    const auto padded = const_view::attach(layout, actual.extents.capacity,
                                                           as_const(actual.placement));
                    assert(padded);
                    for (std::size_t i = n; i < actual.extents.capacity; ++i) assert(get(*padded, i) == 0);
                };
                exercise(input_values{std::span(narrow)});
                exercise(input_values{std::span(wide)});
            }
            const auto unavailable = bind_encoder(actual.view, static_cast<execution_target>(99));
            assert(!unavailable && unavailable.error() == error::unsupported);
        }
    }
}

void bound_point_policies() {
    for (const auto layout : {description{5, 0, geometry::striped},
                              description{7, 0, geometry::striped},
                              description{15, 0, geometry::striped},
                              description{21, 16, geometry::striped},
                              description{15, 8, geometry::striped},
                              description{31, 16, geometry::striped},
                              description{56, 0, geometry::local8},
                              description{64, 0, geometry::local8}}) {
        fixture f(layout, tile_values(layout) + 7);
        std::vector<std::uint64_t> values(f.n);
        for (std::size_t i = 0; i < f.n; ++i)
            values[i] = (std::uint64_t(i + 11) * 0x9e3779b97f4a7c15ULL) & width_mask(layout.width);
        values.front() = 0;
        values.back() = width_mask(layout.width);
        assert(encode(f.view, input_values{std::span(values)}, nullptr, execution_target::scalar));
        for (auto target : {execution_target::scalar, execution_target::automatic,
                            execution_target::avx2, execution_target::avx512,
                            execution_target::neon}) {
            const auto arithmetic = bind_reader(f.view.as_const(), target, point_reader::arithmetic);
            const auto offsets = bind_reader(f.view.as_const(), target, point_reader::constant_offsets);
            assert(arithmetic.has_value() == offsets.has_value());
            if (!arithmetic) {
                assert(target != execution_target::scalar && target != execution_target::automatic);
                assert(arithmetic.error() == error::unsupported && offsets.error() == error::unsupported);
                continue;
            }
            assert(arithmetic->point_strategy() == point_reader::arithmetic);
            assert(offsets->point_strategy() == point_reader::constant_offsets);
            assert(arithmetic->target() == offsets->target());
            assert(arithmetic->target() != execution_target::automatic);
            if (target != execution_target::automatic) assert(arithmetic->target() == target);
            for (std::size_t i = 0; i < f.n; ++i) {
                assert(arithmetic->get(i) == values[i]);
                assert(offsets->get(i) == values[i]);
            }
            // Point policy remains independent of the selected bulk target.
            std::vector<std::uint64_t> arithmetic_output(f.n - 5), offsets_output(f.n - 5);
            arithmetic->decode({3, f.n - 2}, output_values{std::span(arithmetic_output)});
            offsets->decode({3, f.n - 2}, output_values{std::span(offsets_output)});
            assert(arithmetic_output == offsets_output);
            assert(std::ranges::equal(arithmetic_output, std::span(values).subspan(3, f.n - 5)));
        }
        const auto invalid_policy = bind_reader(f.view.as_const(), execution_target::scalar,
                                                static_cast<point_reader>(99));
        assert(!invalid_policy && invalid_policy.error() == error::unsupported);
        const auto unavailable = bind_reader(f.view.as_const(), static_cast<execution_target>(99));
        assert(!unavailable && unavailable.error() == error::unsupported);
    }
}

template<class Format>
void static_points() {
    fixture f(Format::layout, Format::payload::tile_values + 3);
    std::vector<std::uint64_t> values(f.n);
    for (std::size_t i = 0; i < f.n; ++i)
        values[i] = (std::uint64_t(i + 1) * 0x9e3779b97f4a7c15ULL) & width_mask(Format::layout.width);
    assert(encode(f.view, input_values{std::span(values)}, nullptr, execution_target::scalar));
    const auto narrowed = specialize<Format>(f.view);
    assert(narrowed);
    const auto view = *narrowed;
    static_assert(std::is_same_v<decltype(trusted_get(view, 0)), typename Format::scalar_type>);
    std::byte prior_byte{};
    std::array<byte_span, 8> records{};
    records[0] = {&prior_byte, 1};
    for (std::size_t i = 0; i < f.n; ++i) {
        assert(trusted_get(view.as_const(), i) == values[i]);
        assert(trusted_get<point_reader::constant_offsets>(view, i) == values[i]);
        const auto capacity = write_effect_capacity(f.view.as_const(), {i, i + 1});
        assert(capacity && *capacity + 1 <= records.size());
        effect_output effects{records, 1};
        const snapshot before(f);
        values[i] ^= width_mask(Format::layout.width);
        trusted_set(view, i, values[i], &effects);
        assert(records[0].data == &prior_byte && records[0].size == 1);
        check_changes(f, before, effects, 1);
        assert(trusted_get(view, i) == values[i]);
    }
    for (std::size_t i = 0; i < f.n; ++i) assert(get(f.view.as_const(), i) == values[i]);
    trusted_set(view, 0, 0); // Omitted effects is a usable standalone path.
    assert(trusted_get(view, 0) == 0);
}

} // namespace

int main() {
    ordinary_operations();
    checked_rejection();
    selected_coordinates();
    alias_rejection();
    empty_and_append_views();
    interleaved_siblings();
    narrower_inputs();
    bound_construction();
    bound_point_policies();
    static_points<format<5>>();
    static_points<format<5, preset::bulk>>();
    static_points<format<31, preset::filter_arm, 16>>();
    static_points<format<64>>();
    static_points<format<16, preset::filter, 16>>();
}
