#include <ikea2/seriespack/presets.h>
#include "../support.h"
namespace sp = ikea2::seriespack;
void check_representations() {
    using F = sp::preset_format<12>;
    static_assert(std::is_same_v<F, sp::format<12>>);
    static_assert(std::is_same_v<sp::preset_format<20, sp::preset::bulk_x86, 8>,
                                 sp::format<20, sp::geometry::striped, 8>>);
    static_assert(std::is_same_v<sp::preset_format<20, sp::preset::compact, 8>,
                                 sp::format<20, sp::geometry::local, 8>>);
    static_assert(std::is_same_v<sp::preset_format<20, sp::preset::bulk_arm, 0>,
                                 sp::format<20, sp::geometry::striped, 0>>);
    static_assert(
        sp::valid(sp::describe_preset<sp::preset_format<15, sp::preset::bulk_arm>>(70000)));
    const auto expected = sp::describe_preset<F>(16, sp::tile_spacing::cacheline);
    const auto encoded = sp::encode_descriptor(expected);
    IKEA2_CHECK(encoded);
    sp::descriptor_bytes golden{'S', 'P', 1, 0, 12, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 64};
    IKEA2_CHECK(*encoded == golden);
    const auto decoded = sp::decode_descriptor(golden);
    IKEA2_CHECK(decoded && *decoded == expected);
    alignas(64) std::array<std::uint8_t, 128> bytes{};
    const auto recovered = sp::attach_representation<F, std::uint8_t>(*decoded, {{bytes, {}, {}}});
    IKEA2_CHECK(recovered);
    IKEA2_CHECK(sp::describe_storage(*recovered) == expected);
    IKEA2_CHECK(
        (!sp::attach_representation<sp::format<13>, std::uint8_t>(*decoded, {{bytes, {}, {}}})));
    for (unsigned at : {0, 2, 3, 4, 5, 6, 7, 24}) {
        auto bad = golden;
        bad[at] = 255;
        IKEA2_CHECK(!sp::decode_descriptor(bad));
    }
    IKEA2_CHECK(!sp::decode_descriptor(std::span(golden).first(39)));
    auto overflow = expected;
    overflow.count = UINT64_MAX;
    overflow.strides[0] = UINT64_MAX;
    IKEA2_CHECK(!sp::encode_descriptor(overflow));
}
