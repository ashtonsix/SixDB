#include <ikea/seriespack/presets.h>
#include "../support.h"
#include <vector>
namespace sp = ikea::seriespack;
namespace {
// Describe the wire directly; do not use the implementation's load/store helpers.
sp::descriptor_bytes wire(const sp::representation& r) {
    sp::descriptor_bytes bytes{'S', 'P', 1, std::uint8_t(r.storage == sp::geometry::striped),
                               std::uint8_t(r.width), std::uint8_t(r.heads), 0, 0};
    const std::array fields{r.count, r.strides[0], r.strides[1], r.strides[2]};
    for (unsigned field = 0; field < fields.size(); ++field)
        for (unsigned byte = 0; byte < 8; ++byte)
            bytes[8 + field * 8 + byte] = fields[field] >> (byte * 8);
    return bytes;
}
} // namespace

int main() {
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
    IKEA_CHECK(encoded);
    sp::descriptor_bytes golden{'S', 'P', 1, 0, 12, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 64};
    IKEA_CHECK(*encoded == golden);
    const auto decoded = sp::decode_descriptor(golden);
    IKEA_CHECK(decoded && *decoded == expected);
    alignas(64) std::array<std::uint8_t, 128> bytes{};
    const auto recovered = sp::attach_representation<F, std::uint8_t>(*decoded, {{bytes, {}, {}}});
    IKEA_CHECK(recovered);
    IKEA_CHECK(sp::describe_storage(*recovered) == expected);
    IKEA_CHECK(
        (!sp::attach_representation<sp::format<13>, std::uint8_t>(*decoded, {{bytes, {}, {}}})));
    // Large logical extents exercise persistence without allocating their storage.
    const std::array<sp::representation, 8> cases{{
        {1, 0, sp::geometry::local, 0, {1, 0, 0}},
        {12, 0, sp::geometry::local, (1ull << 32) + 17, {12, 0, 0}},
        {12, 0, sp::geometry::local, 9, {(1ull << 32) + 64, 0, 0}},
        {8, 8, sp::geometry::local, 9, {0, (1ull << 32) + 7, 0}},
        {16, 16, sp::geometry::local, 9, {0, (1ull << 32) + 9, (1ull << 40) + 11}},
        {20, 8, sp::geometry::striped, 257, {128, 192, 0}},
        {1, 0, sp::geometry::local, UINT64_MAX, {1, 0, 0}},
        {8, 0, sp::geometry::local, 16, {UINT64_MAX - 8, 0, 0}},
    }};
    for (unsigned i = 0; i < cases.size(); ++i) {
        ikea_test::scope scenario{"descriptor", i};
        const auto bytes = wire(cases[i]);
        const auto encoded_case = sp::encode_descriptor(cases[i]);
        IKEA_CHECK(encoded_case && *encoded_case == bytes);
        const auto decoded_case = sp::decode_descriptor(bytes);
        IKEA_CHECK(decoded_case && *decoded_case == cases[i]);
    }
    for (unsigned at : {0, 1, 2, 3, 4, 5, 6, 7, 24}) {
        auto bad = golden;
        bad[at] = 255;
        IKEA_CHECK(!sp::decode_descriptor(bad));
    }
    for (std::size_t length = 0; length <= golden.size() + 8; ++length) {
        if (length == golden.size())
            continue;
        ikea_test::scope scenario{"descriptor length", length};
        std::vector<std::uint8_t> bytes(golden.begin(), golden.end());
        bytes.resize(length);
        IKEA_CHECK(!sp::decode_descriptor(bytes));
    }
    auto overflow = expected;
    overflow.count = UINT64_MAX;
    overflow.strides[0] = UINT64_MAX;
    IKEA_CHECK(!sp::encode_descriptor(overflow));
    IKEA_CHECK(!sp::decode_descriptor(wire(overflow)));
    auto insufficient = cases.back();
    ++insufficient.strides[0]; // Exactly one byte beyond the representable extent.
    IKEA_CHECK(!sp::encode_descriptor(insufficient));
    IKEA_CHECK(!sp::decode_descriptor(wire(insufficient)));
}
