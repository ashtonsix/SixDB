#include <ikea/seriespack/presets.h>
#include <ikea/seriespack/view.h>
#include "../support.h"
#include <algorithm>
#include <limits>
#include <vector>
namespace sp = ikea::seriespack;
namespace {
using byte = std::byte;
// Independent schema bytes; no implementation framing/load/store helpers.
std::array<byte, 8> wire(const sp::layout_value& law) {
    return {byte{'S'}, byte{'P'}, byte{2}, byte{3}, byte{0},
            byte(law.width), byte(law.heads), byte(unsigned(law.storage))};
}
} // namespace

int main() {
    using L = sp::preset_layout<12>;
    static_assert(std::is_same_v<L, sp::layout<12>>);
    static_assert(std::is_same_v<sp::preset_layout<20, sp::preset::bulk_x86, 8>,
                                 sp::layout<20, sp::geometry::striped, 8>>);
    static_assert(std::is_same_v<sp::preset_layout<20, sp::preset::compact, 8>,
                                 sp::layout<20, sp::geometry::local, 8>>);
    static_assert(std::is_same_v<sp::preset_layout<20, sp::preset::bulk_arm>,
                                 sp::layout<20, sp::geometry::striped>>);
    constexpr auto expected = sp::layout_value::of<L>();
    constexpr std::array<byte, 8> golden{byte{'S'}, byte{'P'}, byte{2}, byte{3}, byte{0},
                                         byte{12}, byte{0}, byte{0}};
    std::array<byte, 16> output;
    output.fill(byte{0xa5});
    const auto encoded = expected.encode(output);
    IKEA_CHECK(encoded && *encoded == 8 && expected.encoded_size() == 8);
    IKEA_CHECK(std::equal(golden.begin(), golden.end(), output.begin()));
    IKEA_CHECK(std::ranges::all_of(std::span(output).subspan(8),
                                   [](byte b) { return b == byte{0xa5}; }));
    const auto decoded = sp::layout_value::decode(golden);
    IKEA_CHECK(decoded && *decoded == expected);

    // Counts/actual strides are parent placement. Two different realizations
    // use exactly the same serialized law; the law does not store either count.
    std::array<byte, 256> storage{};
    const auto a = sp::view<L>::attach(*decoded, 16, {{{storage, 64}, {}, {}}});
    const auto b = sp::view<L>::attach(*decoded, 24, {{{storage, 80}, {}, {}}});
    IKEA_CHECK(a && b && a->size() == 16 && b->size() == 24);
    IKEA_CHECK(a->stream(0).stride == 64 && b->stream(0).stride == 80);
    IKEA_CHECK(!sp::view<sp::layout<13>>::attach(*decoded, 16, {{{storage, 64}, {}, {}}}));
    IKEA_CHECK(!sp::view<L>::attach(*decoded, 16, {{{storage, 11}, {}, {}}}));
    IKEA_CHECK(!sp::view<L>::attach(*decoded, std::numeric_limits<std::size_t>::max(),
                                   {{{storage, 64}, {}, {}}}));

    const std::array<sp::layout_value, 8> laws{{
        {1, 0, sp::geometry::local}, {64, 0, sp::geometry::local},
        {8, 8, sp::geometry::local}, {16, 16, sp::geometry::local},
        {20, 8, sp::geometry::striped}, {36, 16, sp::geometry::striped},
        {15, 0, sp::geometry::striped}, {64, 16, sp::geometry::local},
    }};
    for (unsigned i = 0; i < laws.size(); ++i) {
        ikea_test::scope scenario{"layout law", i};
        const auto golden_case = wire(laws[i]);
        const auto encoded_case = laws[i].encode(output);
        IKEA_CHECK(encoded_case && std::equal(golden_case.begin(), golden_case.end(), output.begin()));
        const auto recovered = sp::layout_value::decode(golden_case);
        IKEA_CHECK(recovered && *recovered == laws[i]);
    }
    for (unsigned at = 0; at < golden.size(); ++at) {
        auto bad = golden;
        bad[at] = byte{255};
        IKEA_CHECK(!sp::layout_value::decode(bad));
    }
    for (std::size_t length = 0; length <= golden.size() + 8; ++length) {
        if (length == golden.size()) continue;
        std::vector<byte> input(golden.begin(), golden.end());
        input.resize(length);
        IKEA_CHECK(!sp::layout_value::decode(input));
    }
    for (std::size_t length = 0; length < golden.size(); ++length) {
        output.fill(byte{0xa5});
        const auto refused = expected.encode(std::span(output).first(length));
        IKEA_CHECK(!refused && refused.error() == sp::error::capacity);
        IKEA_CHECK(std::ranges::all_of(output, [](byte b) { return b == byte{0xa5}; }));
    }
    const std::array<sp::layout_value, 7> invalid{{
        {0, 0, sp::geometry::local}, {65, 0, sp::geometry::local},
        {8, 1, sp::geometry::local}, {8, 16, sp::geometry::local},
        {12, 0, sp::geometry(7)}, {8, 0, sp::geometry::striped},
        {16, 16, sp::geometry::striped},
    }};
    for (const auto law : invalid) {
        output.fill(byte{0xa5});
        const auto refused = law.encode(output);
        IKEA_CHECK(!refused && refused.error() == sp::error::description);
        IKEA_CHECK(std::ranges::all_of(output, [](byte b) { return b == byte{0xa5}; }));
        IKEA_CHECK(!sp::layout_value::decode(wire(law)));
    }
    // Runtime sizing must agree with the independently selected concrete law,
    // including zero-payload heads, partial tiles and checked overflow.
    const auto check_storage = []<class F>() {
        const auto law = sp::layout_value::of<F>();
        for (std::size_t n : {std::size_t{0}, std::size_t{1}, F::tile_rows - 1, F::tile_rows,
                              F::tile_rows + 1, F::tile_rows * 3}) {
            const auto r = law.required_storage(n);
            IKEA_CHECK(r && r->tile_rows == F::tile_rows);
            const auto tiles = n / F::tile_rows + (n % F::tile_rows != 0);
            const std::array<std::size_t,3> per_tile{F::tile_bytes,
                F::heads >= 8 ? F::tile_rows : 0, F::heads == 16 ? F::tile_rows : 0};
            IKEA_CHECK(r->tile_bytes == per_tile);
            for (unsigned p = 0; p < 3; ++p) IKEA_CHECK(r->plane_bytes[p] == tiles * per_tile[p]);
        }
    };
    check_storage.template operator()<sp::layout<1>>();
    check_storage.template operator()<sp::layout<64>>();
    check_storage.template operator()<sp::layout<8,sp::geometry::local,8>>();
    check_storage.template operator()<sp::layout<16,sp::geometry::local,16>>();
    check_storage.template operator()<sp::layout<20,sp::geometry::striped,8>>();
    check_storage.template operator()<sp::layout<31,sp::geometry::striped,16>>();
    for (const auto law : invalid) IKEA_CHECK(!law.required_storage(0));
    const auto overflow = sp::layout_value{64,0,sp::geometry::local}.required_storage(std::numeric_limits<std::size_t>::max());
    IKEA_CHECK(!overflow && overflow.error() == sp::error::overflow);
    // The former SP v1 count/stride descriptor is not a headless layout law.
    std::array<byte, 40> old{};
    old[0] = byte{'S'}; old[1] = byte{'P'}; old[2] = byte{1};
    IKEA_CHECK(!sp::layout_value::decode(old));
}
