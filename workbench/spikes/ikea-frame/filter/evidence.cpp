#include "evidence.h"
#include "implementation.h"
#include <array>

namespace frame_filter {
namespace {
namespace fe = frame_encoding;
namespace sp = fe::sp;
namespace cp = sp::composition;
using C = ikea::comparison;
constexpr unsigned part_rows = 65536;
constexpr unsigned part_blocks = part_rows / fe::block_rows;
using unresolved_rows = std::array<std::uint16_t, 16>;

// Keep complete reconstruction and its scratch frame out of a resolved block's
// path. Decode a tied block once, then inspect only the unresolved original rows.
// This is deliberately a strong simple fallback, not a new prediction decoder.
[[gnu::noinline]] u64 refine(const fe::column &column, std::size_t block, u64 rhs,
                           const unresolved_rows &unresolved) {
    std::array<u64, fe::block_rows> values;
    fe::decode_block(column, block, values.data());
    u64 count = 0;
    for (unsigned window = 0; window < 16; ++window) {
        auto remaining = unresolved[window];
        while (remaining) {
            count += values[window * 16 + std::countr_zero(remaining)] >= rhs;
            remaining &= remaining - 1;
        }
    }
    return count;
}

template <unsigned H>
u64 count_heads(const fe::column &column, std::size_t block, u64 rhs, u64 head_rhs,
                bool needs_refinement, const std::uint8_t *head0, const std::uint8_t *head1) {
    const auto source = detail::view<H, H>(fe::block_rows,
        static_cast<const std::uint8_t *>(nullptr), head0, head1);
    const auto expression = cp::describe(source);
    unresolved_rows unresolved{};
    unsigned any = 0;
    u64 count = 0;
    for (unsigned first = 0; first < fe::block_rows; first += 16) {
        unsigned above, equal;
#if defined(__aarch64__) || defined(__AVX2__)
        cp::native_ops ops;
        const auto high = ops.read(expression.head0, first, 0xffff);
        constexpr unsigned low_bits = H - 8;
        above = sp::filter_values<C::greater>(high, head_rhs >> low_bits);
        equal = sp::filter_values<C::equal>(high, head_rhs >> low_bits);
        if constexpr (H == 16) {
            if (equal) {
                const auto low = ops.read(expression.head1, first, 0xffff);
                above |= equal & sp::filter_values<C::greater>(low, head_rhs & 255);
                equal &= sp::filter_values<C::equal>(low, head_rhs & 255);
            }
        }
#else
        above = cp::filter16_unchecked<C::greater>(expression, first, head_rhs);
        equal = cp::filter16_unchecked<C::equal>(expression, first, head_rhs);
#endif
        if (!needs_refinement) count += std::popcount(above | equal);
        else {
            count += std::popcount(above);
            unresolved[first / 16] = equal;
            any |= equal;
        }
    }
    return count + (any ? refine(column, block, rhs, unresolved) : 0);
}
} // namespace

logical_heads::logical_heads(const fe::column &column, unsigned heads)
    : original_(column), head0_(column.rows), head1_(heads == 16 ? column.rows : 0) {
    assert(heads == 8 || heads == 16);
    std::vector<u64> values;
    for (std::size_t begin = 0; begin < column.blocks.size(); begin += part_blocks) {
        const unsigned blocks = std::min<std::size_t>(part_blocks, column.blocks.size() - begin);
        values.resize(blocks * fe::block_rows);
        for (unsigned b = 0; b < blocks; ++b)
            fe::decode_block(column, begin + b, values.data() + b * fe::block_rows);
        const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
        const auto base = *lo, maximum = *hi;
        const unsigned width = fe::width_of(maximum - base);
        const unsigned h = heads == 16 && width > 8 ? 16 : 8;
        const unsigned shift = width > h ? width - h : 0;
        parts_.push_back({base, maximum, std::uint8_t(shift), std::uint8_t(h)});
        for (auto &value : values) value = (value - base) >> shift;
        const auto offset = begin * fe::block_rows;
        kernels(h, h).pack(values, nullptr, head0_.data() + offset,
                          h == 16 ? head1_.data() + offset : nullptr);
    }
}
u64 logical_heads::count_ge(const fe::block_mask &mask, u64 rhs) const {
    u64 count = 0;
    fe::for_each_block(mask, [&](std::size_t b) __attribute__((always_inline)) {
        const auto &p = parts_[b / part_blocks];
        if (rhs <= p.base) { count += fe::block_rows; return; }
        if (rhs > p.maximum) return;
        const u64 head_rhs = (rhs - p.base) >> p.shift;
        // Exact whole-value heads, or a threshold at the first value in a
        // head bin, settle equal heads without fetching the encoded body.
        const bool needs_refinement = p.shift && ((rhs - p.base) & ((u64{1} << p.shift) - 1));
        const auto offset = b * fe::block_rows;
        if (p.heads == 8)
            count += count_heads<8>(original_, b, rhs, head_rhs, needs_refinement,
                                    head0_.data() + offset, nullptr);
        else
            count += count_heads<16>(original_, b, rhs, head_rhs, needs_refinement,
                                     head0_.data() + offset, head1_.data() + offset);
    });
    return count;
}

window_bounds::window_bounds(const fe::column &column)
    : original_(column), blocks_(column.blocks.size()) {
    std::array<u64, fe::block_rows> values;
    for (std::size_t b = 0; b < blocks_.size(); ++b) {
        fe::decode_block(column, b, values.data());
        const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
        auto &d = blocks_[b];
        d.base = *lo; d.maximum = *hi;
        const unsigned width = fe::width_of(d.maximum - d.base);
        d.shift = width > 8 ? width - 8 : 0;
        for (unsigned first = 0; first < fe::block_rows; first += 16) {
            const auto [lower, upper] = std::minmax_element(values.begin() + first,
                                                          values.begin() + first + 16);
            d.lower[first / 16] = (*lower - d.base) >> d.shift;
            d.upper[first / 16] = (*upper - d.base) >> d.shift;
        }
    }
}
u64 window_bounds::count_ge(const fe::block_mask &mask, u64 rhs) const {
    u64 count = 0;
    fe::for_each_block(mask, [&](std::size_t b) __attribute__((always_inline)) {
        const auto &d = blocks_[b];
        if (rhs <= d.base) { count += fe::block_rows; return; }
        if (rhs > d.maximum) return;
        const u64 relative = rhs - d.base;
        const u64 low_mask = d.shift ? (u64{1} << d.shift) - 1 : 0;
        unresolved_rows unresolved{};
        unsigned any = 0;
        for (unsigned window = 0; window < 16; ++window) {
            const u64 lower = u64{d.lower[window]} << d.shift;
            const u64 upper = (u64{d.upper[window]} << d.shift) | low_mask;
            if (lower >= relative) count += 16;
            else if (upper >= relative) any |= unresolved[window] = 0xffff;
        }
        if (any) count += refine(original_, b, rhs, unresolved);
    });
    return count;
}
} // namespace frame_filter
