#include "fusion.h"
#include "byte_route.h"
#include <random>

namespace tuple_composition_probe {
namespace {
// Explicit compiled stages provide the separate-operation control. The same
// transform bodies are used by generic inline and constant-specialized cases.
[[gnu::noinline]] TUPLE_CC native_packet right(native_packet v, const shuffle& s) {
    return transform<false>(v, s);
}
[[gnu::noinline]] TUPLE_CC native_packet left(native_packet v, const shuffle& s) {
    return transform<true>(v, s);
}
[[gnu::always_inline]] native_packet separate_decode(native_packet v, const recipe& p) {
    const auto a = right(v, p.read[0].operation);
    const auto b = right(v, p.read[1].operation);
    return either(left(a, p.assembly[0]), left(b, p.assembly[1]));
}
[[gnu::always_inline]] native_packet algebraic_decode(native_packet v, const recipe& p) {
    const auto zero = native_detail::zero16();
    auto out = native_detail::join(zero, zero, zero, zero);
    for (const auto& t : p.decoded) {
        if (t.identity) return v;
        out = either(out, t.left ? transform<true>(v, t.operation) : transform<false>(v, t.operation));
    }
    return out;
}
struct known_controls {
    std::array<shuffle, 2> read{}, assembly{}, write{};
    std::array<byte, 64> preserve{};
};
// AOT specialization witness, not a runtime compiler. The fixture still has
// identical packet inputs and the same whole-operation endpoint as A/B/D.
constexpr known_controls known(bool reordered, bool partial) {
    known_controls c;
    c.preserve.fill(255);
    for (unsigned i = 0; i < 64; ++i) {
        const unsigned w = 1 + i % 7;
        const unsigned offset = reordered ? (17 * i + 3) % 64 : i;
        for (unsigned p = 0; p < 2; ++p) {
            const unsigned width = p ? 8 - w : w;
            const unsigned physical_shift = reordered ? (p ? 0 : 8 - w) : (p ? w : 0);
            const unsigned semantic_shift = p ? w : 0;
            auto& r = c.read[p]; r.index[i] = offset; r.shift[i] = -int(physical_shift); r.mask[i] = (1u << width) - 1;
            auto& a = c.assembly[p]; a.index[i] = i; a.shift[i] = semantic_shift; a.mask[i] = ((1u << width) - 1) << semantic_shift;
            if (!partial || !p || i % 3 == 0) {
                auto& s = c.write[p]; s.index[offset] = i; s.shift[offset] = physical_shift; s.mask[offset] = ((1u << width) - 1) << physical_shift;
                c.preserve[offset] &= byte(~s.mask[offset]);
            }
        }
    }
    for (unsigned p = 0; p < 2; ++p) {
        finish_controls(c.read[p], false); finish_controls(c.assembly[p], true); finish_controls(c.write[p], true);
    }
    return c;
}
template <bool Reordered, bool Partial>
TUPLE_CC std::uint64_t constants(const recipe&, raw_view view, native_packet low, native_packet high) {
    static constexpr auto c = known(Reordered, Partial);
    auto decode = [](native_packet v) {
        return either(transform<true>(transform<false>(v, c.read[0]), c.assembly[0]),
                      transform<true>(transform<false>(v, c.read[1]), c.assembly[1]));
    };
    const auto old = load(view);
    const auto before = sum_native(decode(old));
    const auto updated = either(both(old, load_packet(c.preserve.data())),
        either(transform<true>(low, c.write[0]), transform<true>(high, c.write[1])));
    const auto delta = sum_native(decode(updated)) - before;
    store(view, updated);
    return delta;
}
TUPLE_CC std::uint64_t separate(const recipe& p, raw_view view, native_packet low, native_packet high) {
    const auto old = load(view);
    const auto before = sum_native(separate_decode(old, p));
    const auto preserve = both(load_packet(p.write[0].preserve.data()), load_packet(p.write[1].preserve.data()));
    const auto updated = either(both(old, preserve), either(left(low, p.write[0].rounds[0]), left(high, p.write[1].rounds[0])));
    const auto delta = sum_native(separate_decode(updated, p)) - before;
    store(view, updated);
    return delta;
}
TUPLE_CC std::uint64_t algebraic(const recipe& p, raw_view view, native_packet low, native_packet high) {
    const auto old = load(view);
    const auto before = sum_native(algebraic_decode(old, p));
    const auto preserve = both(load_packet(p.write[0].preserve.data()), load_packet(p.write[1].preserve.data()));
    const auto updated = either(both(old, preserve), either(transform<true>(low, p.write[0].rounds[0]), transform<true>(high, p.write[1].rounds[0])));
    const auto delta = sum_native(algebraic_decode(updated, p)) - before;
    store(view, updated);
    return delta;
}
// Complete fallback for the caller's 128-code assembly contract. Multiple
// codes from one input packet may contribute to a single physical byte.
TUPLE_CC std::uint64_t general(const recipe& p, raw_view view, native_packet low, native_packet high) {
    auto decode = [&](native_packet v) {
        return either(transform<true>(transform<false>(v,p.read[0].operation),p.assembly[0]),
                      transform<true>(transform<false>(v,p.read[1].operation),p.assembly[1]));
    };
    const auto old = load(view);
    const auto before = sum_native(decode(old));
    const auto preserve = both(load_packet(p.write[0].preserve.data()),load_packet(p.write[1].preserve.data()));
    auto updated = both(old,preserve);
    for (unsigned r = 0; r < p.write[0].round_count; ++r) updated = either(updated,transform<true>(low,p.write[0].rounds[r]));
    for (unsigned r = 0; r < p.write[1].round_count; ++r) updated = either(updated,transform<true>(high,p.write[1].rounds[r]));
    const auto delta = sum_native(decode(updated)) - before;
    store(view,updated);
    return delta;
}
TUPLE_CC std::uint64_t normalized(const recipe& p, raw_view view, native_packet low, native_packet high) {
    const auto& route = *p.byte_decoder;
    const auto old = load(view);
    const auto before = sum_native(apply_bytes(old, route));
    const auto preserve = both(load_packet(p.write[0].preserve.data()), load_packet(p.write[1].preserve.data()));
    const auto updated = either(both(old, preserve),
        either(shift_mask<true>(permute_full(low, p.write[0].rounds[0]), p.write[0].rounds[0]),
               shift_mask<true>(permute_full(high, p.write[1].rounds[0]), p.write[1].rounds[0])));
    const auto delta = sum_native(apply_bytes(updated, route)) - before;
    store(view, updated);
    return delta;
}
} // namespace
operation bind_operation(const recipe& p) {
    if (p.byte_decoder && p.write[0].round_count == 1 && p.write[1].round_count == 1) return normalized;
    return general;
}
operation select(execution e, bool reordered, bool partial) {
    switch (e) {
        case execution::separate: return separate;
        case execution::generic: return replace_and_sum_delta;
        case execution::algebraic: return algebraic;
        case execution::normalized: return normalized;
        case execution::constants:
            if (reordered) return partial ? constants<true, true> : constants<true, false>;
            return partial ? constants<false, true> : constants<false, false>;
    }
    std::abort();
}
const char* name(execution e) {
    switch (e) {
        case execution::separate: return "separate";
        case execution::generic: return "generic";
        case execution::constants: return "constants";
        case execution::algebraic: return "algebraic";
        case execution::normalized: return "normalized";
    }
    std::abort();
}
void check_routes() {
    std::mt19937 random(0x512);
    // Independent random bit mappings exercise more than the two benchmark
    // fixtures: arbitrary zero bits, permutations, mixed left/right shifts.
    for (unsigned trial = 0; trial < 256; ++trial) {
        bit_routes bits;
        for (auto& bit : bits) bit = random() % 9 ? random() % 512 : -1;
        auto terms = lower(bits);
        bytes64 source{}, expected{}, actual{};
        for (auto& b : source) b = random();
        for (unsigned b = 0; b < 512; ++b)
            if (bits[b] >= 0) expected[b / 8] |= ((source[bits[b] / 8] >> (bits[b] % 8)) & 1) << (b % 8);
        const auto input = load_packet(source.data());
        auto result = load_packet(actual.data());
        for (const auto& t : terms) result = either(result, t.identity ? input :
            t.left ? transform<true>(input, t.operation) : transform<false>(input, t.operation));
        store_packet(actual.data(), result);
        require(actual == expected, "general bit-route lowering");
    }
    for (unsigned trial = 0; trial < 256; ++trial) {
        bit_routes bits;
        bytes64 source{}, expected{}, actual{};
        for (auto& v : source) v = random();
        for (unsigned i = 0; i < 64; ++i) {
            const unsigned source_byte = random() % 64, rotation = random() % 8;
            for (unsigned b = 0; b < 8; ++b) bits[i * 8 + b] = source_byte * 8 + (b + rotation) % 8;
            expected[i] = byte((unsigned(source[source_byte]) >> rotation) | (unsigned(source[source_byte]) << (8 - rotation)));
        }
        auto plan = recognize_bytes(bits);
        require(plan.supported, "recognize general byte rotation/permutation");
        store_packet(actual.data(), apply_bytes(load_packet(source.data()), plan));
        require(actual == expected, "byte rotation/permutation lowering");
        bits[0] = -1;
        require(!recognize_bytes(bits).supported, "reject incomplete byte normal form");
    }
    std::puts("256 byte-rotation/permutation lowering cases passed");
    std::puts("256 independent bit-route lowering cases passed");
}
} // namespace tuple_composition_probe
