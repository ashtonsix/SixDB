#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/composition.h>
#include <ikea/tuplepack/author/execution.h>
#include <ikea/tuplepack/author/maintenance.h>
#include <array>
#include <cassert>
#include <cstdio>

namespace tp = ikea::tuplepack;
struct signature_law {
    static constexpr bool needs_before = false, needs_after = true;
    unsigned signature = 0;
    void observe(std::size_t row,
                 std::tuple<std::uint64_t, std::uint64_t, std::uint64_t> values) noexcept {
        assert(row == 0);
        auto [a, b, c] = values;
        // Private output: the owner applies/publishes it under its own protocol.
        signature = (1u << (a % 7)) | (1u << (b % 7)) | (1u << (c % 7));
    }
};
int main() {
    const std::array<tp::code, 3> codes{{{0, 0, 1}, {0, 1, 7}, {1, 0, 8}}};
    const std::array<tp::code, 1> replacement_codes{{{2, 3, 1}}};
    auto format = *tp::layout::make(2, codes);
    auto replacement = *tp::layout::make(3, replacement_codes);
    std::array<tp::byte, 2> original{0, 41};
    std::array<tp::byte, 3> child_bytes{};
    auto old_view = *tp::view::bind(format, original, 1, 2);
    auto new_view = *tp::view::bind(replacement, child_bytes, 1, 3);
    const std::array<tp::byte, 1> a_map{0}, b_map{1}, c_map{2};
    auto old_a_writer = *tp::writer<8>::make(format, a_map);
    auto new_a_writer = *tp::writer<8>::make(replacement, a_map);
    auto b_writer = *tp::writer<8>::make(format, b_map);
    auto old_a = *tp::bind_writer(old_a_writer, old_view);
    auto new_a = *tp::bind_writer(new_a_writer, new_view);
    auto b = *tp::bind_writer(b_writer, old_view);
    auto old_parent = *tp::composition::bind_group(old_a, b);
    auto new_parent = *tp::composition::bind_group(new_a, b);
    // The logical input contract is unchanged, though a child has new extent,
    // bit offset and owner. Binding does not move existing representation bytes.
    static_assert(
        std::is_same_v<decltype(old_parent)::input_type, decltype(new_parent)::input_type>);
    auto ar = *tp::reader<8>::make(replacement, a_map);
    auto br = *tp::reader<8>::make(format, b_map);
    auto cr = *tp::reader<8>::make(format, c_map);
    auto ao = *tp::bind_reader(ar, new_view), bo = *tp::bind_reader(br, old_view),
         co = *tp::bind_reader(cr, old_view);
    tp::composition::projection observe_row(ao, bo, co);
    signature_law law;
    tp::observation maintenance(observe_row, law);
    using maintenance_type = decltype(maintenance);
    tp::erased_mutation<decltype(new_parent)::input_type, maintenance_type> call(new_parent);
    const decltype(new_parent)::input_type input{1, 19};
    std::array<ikea::owner_write, 2> records;
    ikea::source_write_journal effects{records};
    assert(call.replace(0, {&input, 1}, effects, tp::selection::all(), maintenance));
    assert(law.signature == ((1u << 1) | (1u << (19 % 7)) | (1u << (41 % 7))));
    assert(records[0].source == &new_view && records[1].source == &old_view);
    assert((original[0] & 1) == 0); // old child A is no longer a mutation target
    std::puts("TuplePack composition: replaced child, unchanged logical input, complete signature "
              "projection");
}
