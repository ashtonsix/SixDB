#include <ikea/seriespack/view.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

using namespace ikea::seriespack;

namespace {

struct aligned_buffer {
    explicit aligned_buffer(std::size_t size) : allocation(size + 31) {
        const auto address = reinterpret_cast<std::uintptr_t>(allocation.data());
        bytes = std::span(allocation).subspan((32 - address % 32) % 32, size);
    }
    std::vector<std::byte> allocation;
    std::span<std::byte> bytes;
};

template<unsigned K>
void static_descriptions() {
    using compact = format<K>;
    using arm = format<K, preset::bulk_arm>;
    static_assert(compact::layout == description{K, 0, geometry::local8});
    static_assert(arm::layout == *resolve(K, preset::bulk_arm));
    static_assert(sizeof(typename compact::scalar_type) * 8 >= K);
    static_assert(sizeof(static_mutable_view<compact>) ==
                  sizeof(std::size_t) + sizeof(basic_placement<std::byte>));
    static_assert(sizeof(static_mutable_view<compact>) < sizeof(mutable_view));
    aligned_buffer storage(K);
    const auto attached = static_mutable_view<compact>::attach(8, {{storage.bytes, K}, {}});
    assert(attached);
    assert(attached->size() == 8);
    assert(attached->layout() == compact::layout);
    const auto erased = attached->as_dynamic();
    assert(erased.layout() == compact::layout);
    assert(erased.placement().payload.bytes.data() == storage.bytes.data());
    assert(attached->as_const().as_dynamic().size() == 8);
    static_assert(std::is_same_v<decltype(attached->as_const()), static_const_view<compact>>);
}

template<unsigned... I>
void all_static_descriptions(std::integer_sequence<unsigned, I...>) {
    (static_descriptions<I + 1>(), ...);
}

void descriptions_and_extents() {
    static_assert(payload_layout<0>::tile_values == 8);
    static_assert(payload_layout<0>::tile_bytes == 0);
    static_assert(payload_layout<56>::body_bytes == 7);
    static_assert(payload_layout<56>::tile_bytes == 56);
    static_assert(payload_layout<15, geometry::striped>::tile_values == 256);
    static_assert(payload_layout<15, geometry::striped>::tile_bytes == 480);
    static_assert(static_format<20, geometry::striped>::layout.storage == geometry::striped);
    static_assert(format<24, preset::compact, 16>::payload::width == 8);
    static_assert(format<8, preset::filter, 8>::payload::width == 0);
    static_assert(required_extents({12, 0, geometry::striped}, 65, 128)->payload_envelope == 224);
    assert(!validate({0, 0, geometry::local8}));
    assert(!validate({65, 0, geometry::local8}));
    assert(!validate({7, 8, geometry::local8}));
    assert(!validate({32, 1, geometry::local8}));
    assert(!validate({32, 24, geometry::local8}));
    assert(!validate({8, 0, geometry::striped}));
    assert(!validate({8, 8, geometry::striped}));
    assert(!validate({8, 0, static_cast<geometry>(99)}));
    assert(validate({8, 0, geometry::local8, wire_version}));
    assert(!validate({8, 0, geometry::local8, wire_version + 1}));
    assert(required_extents({8, 0, geometry::local8, wire_version + 1}, 0, 0).error() ==
           error::invalid_description);
    assert(!resolve(8, static_cast<preset>(99)));
    assert(!resolve(8, preset::filter));
    assert(!resolve(8, preset::filter_arm));

    for (unsigned width = 1; width <= 64; ++width) {
        for (unsigned heads : {0u, 8u, 16u}) {
            if (heads > width) continue;
            for (preset choice : {preset::compact, preset::bulk, preset::bulk_arm,
                                  preset::filter, preset::filter_arm}) {
                if (heads == 0 && (choice == preset::filter || choice == preset::filter_arm)) continue;
                const auto layout = resolve(width, choice, heads);
                assert(layout);
                const auto w = width - heads;
                const bool mixed = choice != preset::compact;
                const bool arm = choice == preset::bulk_arm || choice == preset::filter_arm;
                const bool stripes = mixed && ((w >= 1 && w <= 7) || w == 12 || w == 20 ||
                                               (arm && (w == 10 || w == 14 || w == 15)));
                assert(layout->storage == (stripes ? geometry::striped : geometry::local8));
            }
            for (auto storage : {geometry::local8, geometry::striped}) {
                const auto checked = validate({width, heads, storage});
                if (!checked) continue;
                const auto layout = *checked;
                const auto t = tile_values(layout);
                const auto b = tile_bytes(layout);
                for (std::size_t n : {std::size_t{0}, std::size_t{1}, t - 1, t, t + 1,
                                      std::size_t{65537}}) {
                    const auto extents = required_extents(layout, n, b, {t, t});
                    assert(extents);
                    const auto m = n / t + (n % t != 0);
                    assert(extents->tiles == m && extents->capacity == m * t);
                    assert(extents->payload_envelope == m * b);
                    assert(extents->payload_bytes == m * b);
                    assert(extents->payload_bytes + extents->head_bytes[0] +
                           extents->head_bytes[1] == m * t * width / 8);
                    aligned_buffer payload(extents->payload_envelope);
                    aligned_buffer head0(extents->head_envelopes[0]);
                    aligned_buffer head1(extents->head_envelopes[1]);
                    basic_placement<std::byte> placement{
                        {payload.bytes, b}, {{{head0.bytes, t}, {head1.bytes, t}}}};
                    const auto view = mutable_view::attach(layout, n, placement);
                    assert(view && view->size() == n && view->layout() == layout);
                    assert(view->as_const().placement().payload.bytes.data() == payload.bytes.data());
                    if (extents->payload_envelope != 0) {
                        placement.payload.bytes = payload.bytes.first(payload.bytes.size() - 1);
                        const auto short_view = mutable_view::attach(layout, n, placement);
                        assert(!short_view && short_view.error() == error::insufficient_storage);
                    }
                }
            }
        }
    }

    const auto max = std::numeric_limits<std::size_t>::max();
    assert(required_extents({7, 0, geometry::local8}, max, 7).error() == error::overflow);
    assert(required_extents({64, 0, geometry::local8}, max - 7, 64).error() == error::overflow);
    assert(required_extents({7, 0, geometry::local8}, 17, max).error() == error::overflow);
    assert(required_extents({7, 0, geometry::local8}, 1, 6).error() == error::invalid_stride);
    assert(required_extents({12, 0, geometry::striped}, 1, 97).error() == error::invalid_stride);
    assert(required_extents({16, 8, geometry::local8}, 1, 8, {7, 0}).error() == error::invalid_stride);
    assert(mutable_view::attach({12, 0, geometry::striped}, 0, {}));
    assert(mutable_view::attach({64, 16, geometry::local8}, 0, {}));

    alignas(32) std::array<std::byte, 128> bytes{};
    const auto misaligned = mutable_view::attach({12, 0, geometry::striped}, 1,
                                                {{std::span(bytes).subspan(1), 96}, {}});
    assert(!misaligned && misaligned.error() == error::misaligned_storage);
}

bool brute_overlap(std::size_t a, std::size_t as, std::size_t aw,
                   std::size_t b, std::size_t bs, std::size_t bw, std::size_t m) {
    for (std::size_t i = 0; i < m; ++i)
        for (std::size_t j = 0; j < m; ++j)
            if (a + i * as < b + j * bs + bw && b + j * bs < a + i * as + aw) return true;
    return false;
}

void specialization() {
    using compact12 = format<12>;
    using bulk12 = format<12, preset::bulk>;
    alignas(32) std::array<std::byte, 96> bytes{};
    auto runtime = mutable_view::attach(compact12::layout, 8, {{bytes, 12}, {}});
    assert(runtime);
    auto typed = specialize<compact12>(*runtime);
    assert(typed && typed->size() == runtime->size());
    assert(typed->placement().payload.bytes.data() == bytes.data());
    assert(!specialize<bulk12>(*runtime)); // Same logical values, different wire.
    assert((!specialize<format<20, preset::compact, 8>>(*runtime)));
    auto read_only = specialize<compact12>(runtime->as_const());
    static_assert(std::is_same_v<typename decltype(read_only)::value_type::byte_type,
                                 const std::byte>);
    assert(read_only && read_only->placement().payload.stride == 12);
}

void occupied_ranges() {
    alignas(32) std::array<std::byte, 1024> bytes{};
    const auto storage = std::span(bytes);
    // Payload and both heads occupy three disjoint streams in the same owner;
    // every envelope overlaps the others, including final-tile zero slack.
    basic_placement<std::byte> interleaved{
        {storage.subspan(0, 52), 24},
        {{{storage.subspan(4, 56), 24}, {storage.subspan(12, 56), 24}}}};
    assert(mutable_view::attach({20, 16, geometry::local8}, 17, interleaved));
    interleaved.heads[1].bytes = storage.subspan(11, 56);
    const auto overlap = mutable_view::attach({20, 16, geometry::local8}, 17, interleaved);
    assert(!overlap && overlap.error() == error::overlapping_storage);

    // Independently compare admission with byte-interval enumeration. Unequal
    // and relatively-prime strides exercise later tile collisions as well as
    // valid gaps; swapping offsets checks both address orderings.
    for (std::size_t m = 1; m <= 7; ++m) {
        for (std::size_t ps = 4; ps <= 23; ++ps) {
            for (std::size_t hs = 8; hs <= 25; ++hs) {
                for (std::size_t offset = 0; offset <= 55; ++offset) {
                    for (bool reverse : {false, true}) {
                        const auto p = reverse ? offset : 0;
                        const auto h = reverse ? 0 : offset;
                        basic_placement<std::byte> placement{
                            {storage.subspan(p), ps}, {{{storage.subspan(h), hs}, {}}}};
                        const auto admitted = mutable_view::attach({12, 8, geometry::local8}, m * 8,
                                                                   placement);
                        const bool collision = brute_overlap(p, ps, 4, h, hs, 8, m);
                        assert(admitted.has_value() == !collision);
                        if (collision) assert(admitted.error() == error::overlapping_storage);
                    }
                }
            }
        }
    }
}

} // namespace

int main() {
    all_static_descriptions(std::make_integer_sequence<unsigned, 64>{});
    descriptions_and_extents();
    occupied_ranges();
    specialization();
}
