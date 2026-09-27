#pragma once
#include <ikea/bec256/author/native.h>
#include <arm_neon.h>
#include <array>
#include <cstdint>

namespace v3_spike {
namespace bc = ikea::bec256;
using block = bc::native::block;
using pair = bc::native::pair;
using consumer = std::uint64_t (*)(const bc::source&, const bc::source&, const bc::byte*, const bc::byte*);
struct candidate { const char* name; consumer consume; bool sve; };
extern const std::array<candidate, 8> candidates;
std::uint64_t consume_sve(const bc::source&, const bc::source&, const bc::byte*, const bc::byte*);
std::uint64_t consume_sve_interleaved(const bc::source&, const bc::source&, const bc::byte*, const bc::byte*);

[[gnu::always_inline]] inline std::uint64_t finish(pair value, const bc::byte* qa, const bc::byte* qb) {
    return bc::native::population(bc::native::intersection(
        value, bc::native::join(bc::native::load(qa), bc::native::load(qb))));
}

template <unsigned Method>
[[gnu::always_inline]] inline uint8x16_t lookup256(const std::uint8_t* table, uint8x16_t indices) {
    if constexpr (Method == 0)
        return bc::detail::neon::lookup256(table, indices);
    else if constexpr (Method == 1) {
        // Eight 32-byte tables instead of four 64-byte tables.
        uint8x16_t parts[8];
        auto lookup = [&](unsigned offset) {
            auto table_part = vld1q_u8_x2(table + offset);
            return vqtbl2q_u8(table_part, vsubq_u8(indices, vdupq_n_u8(offset)));
        };
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            ((parts[I] = lookup(32 * I)), ...);
        }(std::make_index_sequence<8>{});
        return vorrq_u8(vorrq_u8(vorrq_u8(parts[0], parts[1]), vorrq_u8(parts[2], parts[3])),
                       vorrq_u8(vorrq_u8(parts[4], parts[5]), vorrq_u8(parts[6], parts[7])));
    } else {
        auto a = vqtbl4q_u8(vld1q_u8_x4(table), indices);
        a = vqtbx4q_u8(a, vld1q_u8_x4(table + 64), vsubq_u8(indices, vdupq_n_u8(64)));
        a = vqtbx4q_u8(a, vld1q_u8_x4(table + 128), vsubq_u8(indices, vdupq_n_u8(128)));
        return vqtbx4q_u8(a, vld1q_u8_x4(table + 192), vsubq_u8(indices, vdupq_n_u8(192)));
    }
}

[[gnu::always_inline]] inline uint8x16x4_t load_scratch(const bc::source& input) {
    if (input.storage().size() >= 64)
        return vld1q_u8_x4(reinterpret_cast<const std::uint8_t*>(input.storage().data()));
    alignas(64) std::uint8_t scratch[64]{};
    if (input.bytes()) std::memcpy(scratch, input.storage().data(), input.bytes());
    return vld1q_u8_x4(scratch);
}

// Decoder factoring copied from the maintained NEON decoder at 03f0152.
// Keep the tree/bit-extraction bodies shared; vary only entry loading, table
// decomposition, and whether the two independent trees advance together.
struct tree {
    uint8x16x4_t body;
    unsigned pos;
    uint16x8_t root;
};
[[gnu::always_inline]] inline tree start(uint8x16x4_t body, const bc::source& input) {
    const unsigned population = input.population();
    unsigned n = std::bit_width(std::min(population, 256 - population));
    unsigned first = input.bytes() ? std::to_integer<unsigned>(input.storage()[0]) : 0;
    unsigned left = (first & ((1u << n) - 1)) + (population > 128 ? population - 128 : 0);
    auto p2 = vsetq_lane_u16(left, vdupq_n_u16(0), 0);
    return {body, n, vsetq_lane_u16(population - left, p2, 1)};
}
template <unsigned Method>
[[gnu::always_inline]] inline block leaves(tree& t, uint16x8x2_t p16) {
    using namespace bc::detail::neon;
    auto first = split<8>(t.body, p16.val[0], t.pos);
    auto second = split<8>(t.body, p16.val[1], t.pos);
    auto b0 = vcombine_u8(vmovn_u16(first.val[0]), vmovn_u16(first.val[1]));
    auto b1 = vcombine_u8(vmovn_u16(second.val[0]), vmovn_u16(second.val[1]));
    auto widths = vld1q_u8(bc::detail::byte_width.data());
    auto n0 = vqtbl1q_u8(widths, b0), n1 = vqtbl1q_u8(widths, b1);
    auto prefix0 = prefix16bytes(n0), prefix1 = prefix16bytes(n1);
    auto relative0 = vaddq_u8(vsubq_u8(prefix0, n0), vdupq_n_u8(t.pos % 8));
    auto relative1 = vaddq_u8(vsubq_u8(prefix1, n1),
                              vdupq_n_u8(t.pos % 8 + vgetq_lane_u8(prefix0, 15)));
    auto r0 = ranks16(t.body, relative0, n0, t.pos / 8);
    auto r1 = ranks16(t.body, relative1, n1, t.pos / 8);
    auto base = vld1q_u8(bc::detail::byte_base.data());
    auto s0 = vaddq_u8(vqtbl1q_u8(base, b0), r0);
    auto s1 = vaddq_u8(vqtbl1q_u8(base, b1), r1);
    return {{v3_spike::lookup256<Method>(bc::detail::codes.value.data(), s0),
             v3_spike::lookup256<Method>(bc::detail::codes.value.data(), s1)}};
}
template <unsigned Method>
[[gnu::always_inline]] inline block decode_loaded(tree t) {
    using namespace bc::detail::neon;
    auto p4 = split<64>(t.body, t.root, t.pos).val[0];
    auto p8 = split<32>(t.body, p4, t.pos).val[0];
    return leaves<Method>(t, split<16>(t.body, p8, t.pos));
}
template <bool Constrain = false>
[[gnu::always_inline]] inline pair interleaved(tree a, tree b) {
    using namespace bc::detail::neon;
    auto a4 = split<64>(a.body, a.root, a.pos).val[0];
    auto b4 = split<64>(b.body, b.root, b.pos).val[0];
    if constexpr (Constrain) asm("" : "+w"(a4), "+w"(b4));
    auto a8 = split<32>(a.body, a4, a.pos).val[0];
    auto b8 = split<32>(b.body, b4, b.pos).val[0];
    if constexpr (Constrain) asm("" : "+w"(a8), "+w"(b8));
    auto a16 = split<16>(a.body, a8, a.pos);
    auto b16 = split<16>(b.body, b8, b.pos);
    if constexpr (Constrain)
        asm("" : "+w"(a16.val[0]), "+w"(a16.val[1]), "+w"(b16.val[0]), "+w"(b16.val[1]));
    return bc::native::join(leaves<0>(a, a16), leaves<0>(b, b16));
}
} // namespace v3_spike
