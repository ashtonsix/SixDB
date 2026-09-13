#include "algebra.h"
#include <cstring>

namespace bec_study {
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
namespace {
// Erase metadata refills, not the decoder cross-product. Each input keeps its
// own frame and frontier. Returning the frame through memory is an intentional
// once-per-refill seam; extraction and the two-input Boolean kernel stay inline.
class metadata_reader {
    const directory &d_;
    entry (*point_)(const directory &, unsigned);
    frame (*refill_)(const directory &, unsigned, unsigned) = nullptr;
    unsigned group_ = ~0u;
    frame frame_{};
    template <layout L> void choose(resolution r) {
        point_ = &point<L>;
        if constexpr (!absolute(L)) {
            if (r == resolution::native16)
                refill_ = &native_frame<L>;
            else if (r == resolution::buffered16)
                refill_ = [](const directory &d, unsigned first, unsigned base) {
                    unsigned p[16], n[16];
                    d.buffered(first, p, n);
                    return make_frame(p, n, base);
                };
        }
    }

  public:
    metadata_reader(const directory &d, resolution r) : d_(d) {
        switch (d.kind()) {
        case layout::direct:
            choose<layout::direct>(r);
            break;
        case layout::direct32:
            choose<layout::direct32>(r);
            break;
        case layout::tuple_absolute:
            choose<layout::tuple_absolute>(r);
            break;
        case layout::tuple_checkpoint:
            choose<layout::tuple_checkpoint>(r);
            break;
        case layout::series_local:
            choose<layout::series_local>(r);
            break;
        case layout::series_scan:
            choose<layout::series_scan>(r);
            break;
        case layout::tuple_folded:
            choose<layout::tuple_folded>(r);
            break;
        case layout::series_folded_local:
            choose<layout::series_folded_local>(r);
            break;
        case layout::series_folded_scan:
            choose<layout::series_folded_scan>(r);
            break;
        }
    }
    [[gnu::always_inline]] entry get(unsigned i) {
        if (d_.kind() == layout::direct32)
            return point<layout::direct32>(d_, i);
        if (d_.kind() == layout::direct)
            return point<layout::direct>(d_, i);
        if (!refill_)
            return point_(d_, i);
        const auto group = i & ~15u;
        if (group != group_) {
            const auto base = group % d_.checkpoint() == 0            ? d_.checkpoint_offset(group)
                              : group_ != ~0u && group == group_ + 16 ? frame_.next
                                                                      : point_(d_, group).offset;
            frame_ = refill_(d_, group, base);
            group_ = group;
        }
        return frame_.at(i % 16);
    }
};
struct cell {
    const bc::byte *data;
    unsigned population; // 257 means plain, with no population evidence.
};
alignas(64) constexpr bc::plain_block zero{};
alignas(64) constexpr bc::plain_block full = [] {
    bc::plain_block value;
    value.fill(bc::byte{255});
    return value;
}();
[[gnu::always_inline]] bc::native::block load(cell c) {
    if (!c.population)
        return bc::native::load(zero.data());
    if (c.population == 256)
        return bc::native::load(full.data());
    return c.population == 257 ? bc::native::load(c.data)
                               : bc::native::decode_unchecked(c.data, c.population);
}
[[gnu::always_inline]] bc::native::pair load(cell a, cell b) {
    if (a.population > 0 && a.population < 256 && b.population > 0 && b.population < 256)
        return bc::native::decode_pair_unchecked(a.data, a.population, b.data, b.population);
    return bc::native::join(load(a), load(b));
}
template <boolean_op Op> [[gnu::always_inline]] void evidence(cell &a, cell &b) {
    if constexpr (Op == boolean_op::intersection) {
        if (!a.population || !b.population)
            a = b = {nullptr, 0};
    } else if (a.population == 256 || b.population == 256)
        a = b = {nullptr, 256};
}
template <boolean_op Op>
[[gnu::always_inline]] bc::native::pair combine(bc::native::pair a, bc::native::pair b) {
    if constexpr (Op == boolean_op::intersection)
        return bc::native::intersection(a, b);
    else
        return bc::native::set_union(a, b);
}
template <boolean_op Op>
[[gnu::always_inline]] bc::native::block combine_halves(bc::native::pair values) {
    auto a = bc::native::part<0>(values), b = bc::native::part<1>(values);
#if defined(IKEA_BEC256_AVX512)
    if constexpr (Op == boolean_op::intersection)
        return _mm256_and_si256(a, b);
    else
        return _mm256_or_si256(a, b);
#else
    if constexpr (Op == boolean_op::intersection)
        return {{vandq_u8(a.val[0], b.val[0]), vandq_u8(a.val[1], b.val[1])}};
    else
        return {{vorrq_u8(a.val[0], b.val[0]), vorrq_u8(a.val[1], b.val[1])}};
#endif
}
template <boolean_op Op, output_kind Output, unsigned Grain>
void apply(operand_view a, operand_view b, const std::uint64_t *active, bc::destination &output,
           ikea::source_write_journal &effects) {
    const auto n = a.bits.size();
    assert(n == b.bits.size() && output.storage.size() >= n * 32);
    assert(effects.remaining() >= (Output == output_kind::complete ? 1 : n));
    // Complete output has one known contiguous footprint, including zeros at
    // inactive ordinals. Record it once instead of repeating journal work for each store.
    if constexpr (Output == output_kind::complete)
        effects.before(output, {0, 0, n * 32});
    std::optional<metadata_reader> ma, mb;
    if (a.metadata)
        ma.emplace(*a.metadata, a.lookup);
    if (b.metadata)
        mb.emplace(*b.metadata, b.lookup);
    auto resolve = [] [[gnu::always_inline]] (operand_view input, auto &metadata, unsigned i) {
        if (!metadata)
            return cell{input.bits.plain.data() + 32 * i, 257};
        const auto e = metadata->get(i);
        return cell{input.bits.body.data() + e.offset, e.population};
    };
    auto store = [&] [[gnu::always_inline]] (unsigned i, bc::native::block bits) {
        if constexpr (Output == output_kind::selected_only)
            effects.before(output, {0, i * 32, 32});
        bc::native::store(output.storage.data() + i * 32, bits);
    };
    unsigned pending = n;
    cell previous_a{}, previous_b{};
    for (unsigned i = 0; i < n; ++i) {
        if (active && !((active[i / 64] >> (i % 64)) & 1)) {
            if constexpr (Output == output_kind::complete)
                store(i, bc::native::load(zero.data()));
            continue;
        }
        auto ca = resolve(a, ma, i), cb = resolve(b, mb, i);
        evidence<Op>(ca, cb);
        if constexpr (Grain == 1) {
            // The decoder's two inputs are operands at ONE output ordinal.
            store(i, combine_halves<Op>(load(ca, cb)));
        } else {
            if (pending == n) {
                pending = i;
                previous_a = ca;
                previous_b = cb;
                continue;
            }
            // Each pair now means TWO output ordinals, possibly nonadjacent.
            const auto value = combine<Op>(load(previous_a, ca), load(previous_b, cb));
            store(pending, bc::native::part<0>(value));
            store(i, bc::native::part<1>(value));
            pending = n;
        }
    }
    if (pending != n)
        store(pending, combine_halves<Op>(load(previous_a, previous_b)));
}
template <boolean_op Op, output_kind Output> algebra_call grain(unsigned g) {
    assert(g == 1 || g == 2);
    return g == 1 ? &apply<Op, Output, 1> : &apply<Op, Output, 2>;
}
template <boolean_op Op> algebra_call output(output_kind o, unsigned g) {
    return o == output_kind::complete ? grain<Op, output_kind::complete>(g)
                                      : grain<Op, output_kind::selected_only>(g);
}
} // namespace
algebra_call bind_algebra(boolean_op op, output_kind o, unsigned g) {
    return op == boolean_op::intersection ? output<boolean_op::intersection>(o, g)
                                          : output<boolean_op::set_union>(o, g);
}
#endif
} // namespace bec_study
