#include <ikea/seriespack.h>
#include <ikea/seriespack/point.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <vector>

namespace {
namespace sp = ikea::seriespack;

std::size_t descriptions{}, placements{}, reads{}, writes{};

[[noreturn]] void fail(const char* what, sp::description d, std::size_t n,
                       std::size_t index = 0) {
    std::fprintf(stderr, "SeriesPack static point: %s, k=%u h=%u geometry=%u n=%zu index=%zu\n",
                 what, d.width, d.head_bits, unsigned(d.storage), n, index);
    std::abort();
}

std::uint64_t random_bits() {
    static std::uint64_t state = 0x36ccfa51792d403bULL;
    state ^= state << 13;
    state ^= state >> 7;
    return state ^= state << 17;
}

constexpr std::uint64_t mask(unsigned k) {
    return k == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << k) - 1;
}

struct alignas(64) cache_line {
    std::array<std::byte, 64> bytes;
    bool operator==(const cache_line&) const = default;
};
static_assert(sizeof(cache_line) == 64);

// Aligned backing buffers preserve identical byte offsets when copied. The
// placement owns complete tiles only; every prefix, gap and suffix is canaried.
struct fixture {
    sp::description layout;
    std::size_t n, values, tiles;
    std::array<std::size_t, 3> tile_bytes{}, strides{}, offsets{}, envelopes{};
    std::array<std::vector<cache_line>, 3> buffers;

    fixture(sp::description d, std::size_t size, bool strided)
        : layout(d), n(size), values(sp::tile_values(d)),
          tiles(size / values + (size % values != 0)) {
        tile_bytes[0] = sp::tile_bytes(d);
        for (unsigned h = 0; h < d.head_bits / 8; ++h) tile_bytes[h + 1] = values;
        for (unsigned p = 0; p < 3; ++p) {
            strides[p] = tile_bytes[p] + (strided ?
                (p == 0 && d.storage == sp::geometry::striped ? 32 : 11 + p * 2) : 0);
            offsets[p] = p == 0 && d.storage == sp::geometry::striped ? 64 : 65 + p * 3;
            envelopes[p] = tile_bytes[p] == 0 || tiles == 0 ? 0
                : (tiles - 1) * strides[p] + tile_bytes[p];
            buffers[p].resize((offsets[p] + envelopes[p] + 127) / 64);
            std::fill_n(raw(p), buffers[p].size() * 64, std::byte{0xd7});
            for (std::size_t t = 0; t < tiles; ++t)
                std::fill_n(raw(p) + offsets[p] + t * strides[p], tile_bytes[p], std::byte{0});
        }
    }
    std::byte* raw(unsigned p) { return reinterpret_cast<std::byte*>(buffers[p].data()); }
    const std::byte* raw(unsigned p) const {
        return reinterpret_cast<const std::byte*>(buffers[p].data());
    }
    std::span<std::byte> plane(unsigned p) {
        return envelopes[p] == 0 ? std::span<std::byte>{}
            : std::span<std::byte>{raw(p) + offsets[p], envelopes[p]};
    }
    sp::basic_placement<std::byte> placement() {
        return {{plane(0), strides[0]}, {{{plane(1), strides[1]}, {plane(2), strides[2]}}}};
    }
    bool owns(unsigned p, std::size_t offset) const {
        if (tile_bytes[p] == 0 || offset < offsets[p]) return false;
        const auto relative = offset - offsets[p];
        return relative < envelopes[p] && relative % strides[p] < tile_bytes[p];
    }
};

struct effect_fixture {
    inline static std::byte prefix{};
    std::array<sp::byte_span, 16> records{};
    sp::effect_output output{records, 1};
    effect_fixture() { records[0] = {&prefix, 1}; }

    void check(const fixture& after, const fixture& before, std::size_t index) const {
        if (output.size < 1 || output.size > records.size() ||
            records[0].data != &prefix || records[0].size != 1)
            fail("effect prefix/capacity", after.layout, after.n, index);
        for (std::size_t r = output.size; r < records.size(); ++r)
            if (records[r].data != nullptr || records[r].size != 0)
                fail("effect suffix changed", after.layout, after.n, index);
        for (std::size_t r = 1; r < output.size; ++r) {
            if (records[r].size == 0) fail("empty effect", after.layout, after.n, index);
            for (std::size_t b = 0; b < records[r].size; ++b) {
                const auto address = reinterpret_cast<std::uintptr_t>(records[r].data + b);
                bool owned = false;
                for (unsigned p = 0; p < 3; ++p) {
                    const auto base = reinterpret_cast<std::uintptr_t>(after.raw(p));
                    if (address >= base && address - base < after.buffers[p].size() * 64)
                        owned |= after.owns(p, address - base);
                }
                if (!owned) fail("effect includes foreign byte", after.layout, after.n, index);
            }
        }
        for (unsigned p = 0; p < 3; ++p)
            for (std::size_t b = 0; b < after.buffers[p].size() * 64; ++b) {
                if (after.raw(p)[b] == before.raw(p)[b]) continue;
                bool covered = false;
                const auto address = reinterpret_cast<std::uintptr_t>(after.raw(p) + b);
                for (std::size_t r = 1; r < output.size; ++r) {
                    const auto begin = reinterpret_cast<std::uintptr_t>(records[r].data);
                    covered |= address >= begin && address - begin < records[r].size;
                }
                if (!covered) fail("changed byte missing effect", after.layout, after.n, index);
            }
    }
};

template<class Format>
void check_placement(std::size_t n, bool strided) {
    fixture actual(Format::layout, n, strided);
    auto attached = sp::static_mutable_view<Format>::attach(n, actual.placement());
    if (!attached) fail("static attach", Format::layout, n);
    auto view = *attached;
    std::vector<std::uint64_t> values(n);
    for (auto& value : values) value = random_bits() & mask(Format::layout.width);
    values.front() = mask(Format::layout.width);
    if (n > 1) values.back() = 0;
    if (!sp::encode(view.as_dynamic(), sp::input_values{std::span(values)}, nullptr,
                    sp::execution_target::scalar)) fail("canonical construction", Format::layout, n);
    fixture expected = actual;
    auto expected_view = sp::mutable_view::attach(Format::layout, n, expected.placement());
    if (!expected_view) fail("expected attach", Format::layout, n);

    for (std::size_t i = 0; i < n; ++i) {
        if (sp::trusted_get(view, i) != values[i] ||
            sp::trusted_get<sp::point_reader::constant_offsets>(view.as_const(), i) != values[i])
            fail("known value mismatch", Format::layout, n, i);
        reads += 2;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const auto value = random_bits() & mask(Format::layout.width);
        const fixture before = actual;
        effect_fixture effects;
        sp::trusted_set(view, i, value, &effects.output);
        if (!sp::set(*expected_view, i, value)) fail("checked point mutation", Format::layout, n, i);
        if (actual.buffers != expected.buffers)
            fail("mutation differs / unselected bytes changed", Format::layout, n, i);
        effects.check(actual, before, i);
        values[i] = value;
        if (sp::trusted_get(view.as_const(), i) != value ||
            sp::trusted_get<sp::point_reader::constant_offsets>(view, i) != value)
            fail("post-mutation value mismatch", Format::layout, n, i);
        reads += 2;
        ++writes;
        if (i % 7 == 0) {
            sp::trusted_set(view, i, 0); // Compile-time omitted effects path.
            if (!sp::set(*expected_view, i, 0) || actual.buffers != expected.buffers)
                fail("mutation with omitted effects differs", Format::layout, n, i);
            values[i] = 0;
            ++writes;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (sp::trusted_get(view, i) != values[i] ||
            sp::trusted_get<sp::point_reader::constant_offsets>(view, i) != values[i])
            fail("unselected logical value changed", Format::layout, n, i);
        reads += 2;
    }
    ++placements;
}

template<unsigned K, unsigned H, sp::geometry G>
void check_format() {
    using F = sp::static_format<K, G, H>;
    constexpr auto T = F::payload::tile_values;
    for (auto n : {std::size_t{1}, T - 1, T, T + 3, 2 * T - 1})
        for (bool strided : {false, true}) check_placement<F>(n, strided);
    ++descriptions;
}

template<unsigned K, unsigned H>
void check_head() {
    check_format<K, H, sp::geometry::local8>();
    if constexpr (sp::supports_stripes(K - H)) check_format<K, H, sp::geometry::striped>();
}

// Direct physical witnesses deliberately bypass only the public static wrapper.
// They are disassembly controls, not the expected-value oracle above. Each pair
// has the same concrete view/argument/result ABI and admitted facts.
template<class F, sp::point_reader Reader>
[[gnu::always_inline]] inline typename F::scalar_type direct_get(
    const sp::static_const_view<F>& v, std::size_t i) {
    constexpr auto W = sp::payload_width(F::layout), H = F::layout.head_bits;
    constexpr auto G = F::layout.storage;
    constexpr auto T = F::payload::tile_values;
    const auto tile = i / T, local = i % T;
    const auto& p = v.placement();
    std::uint64_t value = 0;
    if constexpr (W != 0) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(p.payload.bytes.data() + tile * p.payload.stride);
        if constexpr (Reader == sp::point_reader::arithmetic) value = sp::detail::get_arithmetic<W, G>(bytes, local);
        else value = sp::detail::get<W, G>(bytes, local);
    }
    if constexpr (H != 0) {
        auto head = std::to_integer<std::uint64_t>(p.heads[0].bytes[tile * p.heads[0].stride + local]);
        if constexpr (H == 16) head = (head << 8) |
            std::to_integer<std::uint64_t>(p.heads[1].bytes[tile * p.heads[1].stride + local]);
        value |= head << W;
    }
    return static_cast<typename F::scalar_type>(value);
}

using local12 = sp::static_format<12, sp::geometry::local8>;
using striped5 = sp::static_format<5, sp::geometry::striped>;
using striped7 = sp::static_format<7, sp::geometry::striped>;
using striped15 = sp::static_format<15, sp::geometry::striped>;
using headed60 = sp::static_format<60, sp::geometry::local8, 16>;
using local56 = sp::static_format<56, sp::geometry::local8>;
using local64 = sp::static_format<64, sp::geometry::local8>;
} // namespace

#define IKEA_POINT_WITNESSES(Name) \
extern "C" [[gnu::used, gnu::retain, gnu::noinline, gnu::visibility("default")]] Name::scalar_type \
seriespack_static_##Name##_arithmetic(const sp::static_const_view<Name>& v, std::size_t i) { \
    return sp::trusted_get(v, i); \
} \
extern "C" [[gnu::used, gnu::retain, gnu::noinline, gnu::visibility("default")]] Name::scalar_type \
seriespack_direct_##Name##_arithmetic(const sp::static_const_view<Name>& v, std::size_t i) { \
    return direct_get<Name, sp::point_reader::arithmetic>(v, i); \
} \
extern "C" [[gnu::used, gnu::retain, gnu::noinline, gnu::visibility("default")]] Name::scalar_type \
seriespack_static_##Name##_offsets(const sp::static_const_view<Name>& v, std::size_t i) { \
    return sp::trusted_get<sp::point_reader::constant_offsets>(v, i); \
} \
extern "C" [[gnu::used, gnu::retain, gnu::noinline, gnu::visibility("default")]] Name::scalar_type \
seriespack_direct_##Name##_offsets(const sp::static_const_view<Name>& v, std::size_t i) { \
    return direct_get<Name, sp::point_reader::constant_offsets>(v, i); \
}
IKEA_POINT_WITNESSES(local12)
IKEA_POINT_WITNESSES(striped5)
IKEA_POINT_WITNESSES(striped7)
IKEA_POINT_WITNESSES(striped15)
IKEA_POINT_WITNESSES(headed60)
IKEA_POINT_WITNESSES(local56)
IKEA_POINT_WITNESSES(local64)
#undef IKEA_POINT_WITNESSES

int main() {
    sp::detail::static_for<64>([](auto i) {
        constexpr unsigned K = i + 1;
        check_head<K, 0>();
        if constexpr (K >= 8) check_head<K, 8>();
        if constexpr (K >= 16) check_head<K, 16>();
    });
    std::printf("SeriesPack static point: %zu descriptions, %zu placements, %zu reads, %zu writes passed\n",
                descriptions, placements, reads, writes);
}
