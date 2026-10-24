#include "cells.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace frame_cells {
namespace {
template<class T> std::vector<T> residuals(std::span<const u64> input, u64 base) {
    std::vector<T> out(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) out[i] = static_cast<T>(input[i] - base);
    return out;
}

template<class T> answer native_sum(const native& payload, const block_mask& selection, const std::vector<T>& x) {
    answer out;
    // Narrow accumulators within a bounded chunk keep the byte/u16 controls
    // vectorizable. u16*65536 fits u32; u32*65536 fits u64. u64 values use
    // independent 32-bit halves, retaining the exact u128 result.
    const auto reduce = [&](std::size_t first, std::size_t limit) {
        for (std::size_t start = first; start < limit; start += 65536) {
            const auto end = std::min(limit, start + 65536);
            if constexpr (sizeof(T) <= 2) {
                u32 sum = 0;
                for (std::size_t i = start; i < end; ++i) sum += x[i];
                out.sum += sum;
            } else if constexpr (sizeof(T) == 4) {
                u64 sum = 0;
                for (std::size_t i = start; i < end; ++i) sum += x[i];
                out.sum += sum;
            } else {
                u64 lo = 0, hi = 0;
                for (std::size_t i = start; i < end; ++i) {
                    lo += x[i] & 0xffffffffu;
                    hi += x[i] >> 32;
                }
                out.sum += u128{lo} + (u128{hi} << 32);
            }
            out.count += end - start;
        }
    };
    if (selection.all) reduce(0, x.size());
    else for (std::size_t b = 0; b < selection.blocks; ++b)
        if (selection.active(b)) reduce(b * fi::block_rows, (b + 1) * fi::block_rows);
    out.sum += u128{payload.base} * out.count;
    return out;
}

template<class A, class B> answer native_pair(const native& predicate, const native& payload, u64 threshold, const block_mask& selection, const std::vector<A>& a, const std::vector<B>& b) {
    if (threshold <= predicate.base) return native_sum(payload, selection, b);
    if (threshold > predicate.maximum) return {};
    const auto t = static_cast<A>(threshold - predicate.base);
    answer out;
    for (std::size_t block = 0; block < selection.blocks; ++block) {
        if (!selection.active(block)) continue;
        const auto first = block * fi::block_rows;
        u16 count = 0;
        if constexpr (sizeof(B) <= 2) {
            // One block: u8*256 fits u16; u16*256 fits u32. Avoid
            // needlessly widening both byte inputs to four-byte lanes.
            std::conditional_t<sizeof(B) == 1, u16, u32> sum = 0;
#pragma clang loop unroll(disable)
            for (unsigned j = 0; j < fi::block_rows; ++j) {
                const auto i = first + j;
                const bool hit = a[i] >= t;
                sum += b[i] & static_cast<B>(B{0} - static_cast<B>(hit));
                count += hit;
            }
            out.sum += sum;
        } else if constexpr (sizeof(B) == 4) {
            u64 sum = 0;
#pragma clang loop unroll(disable)
            for (unsigned j = 0; j < fi::block_rows; ++j) {
                const auto i = first + j;
                const bool hit = a[i] >= t;
                sum += b[i] & static_cast<B>(B{0} - static_cast<B>(hit));
                count += hit;
            }
            out.sum += sum;
        } else {
            u64 lo = 0, hi = 0;
#pragma clang loop unroll(disable)
            for (unsigned j = 0; j < fi::block_rows; ++j) {
                const auto i = first + j;
                const bool hit = a[i] >= t;
                const auto v = b[i] & static_cast<B>(B{0} - static_cast<B>(hit));
                lo += v & 0xffffffffu;
                hi += v >> 32;
                count += hit;
            }
            out.sum += u128{lo} + (u128{hi} << 32);
        }
        out.count += count;
    }
    out.sum += u128{payload.base} * out.count;
    return out;
}

void append_image(std::vector<range>& out, const fi::image& f, const std::string& prefix) {
    const auto add = [&](const auto& v, const char* role) {
        using T = typename std::decay_t<decltype(v)>::value_type;
        if (!v.empty()) out.push_back({v.data(), v.size() * sizeof(T), v.capacity() * sizeof(T), prefix + role});
    };
    add(f.kind_of, "kind"); add(f.width, "width"); add(f.base, "base");
    add(f.offset, "offset"); add(f.n, "runs"); add(f.bytes, "body+suffix");
}
} // namespace

native::native(std::span<const u64> input) : rows(input.size()) {
    if (input.empty()) return;
    const auto [lo, hi] = std::minmax_element(input.begin(), input.end());
    base = *lo; maximum = *hi;
    const auto span = maximum - base;
    if (span <= std::numeric_limits<u8>::max()) values = residuals<u8>(input, base);
    else if (span <= std::numeric_limits<u16>::max()) values = residuals<u16>(input, base);
    else if (span <= std::numeric_limits<u32>::max()) values = residuals<u32>(input, base);
    else values = residuals<u64>(input, base);
}
unsigned native::lane() const { return 1u << values.index(); }

cell::cell(std::span<const u64> av, std::span<const u64> bv, std::span<const u64> mask,
           u64 t, fi::placement placement) : an(av), bn(bv), threshold(t), rows(bv.size()) {
    if (av.size() != bv.size() || rows % fi::block_rows || rows > (1u << 24))
        throw std::invalid_argument("cell needs equal, complete 256-row blocks, at most 2^24 rows");
    const auto blocks = rows / fi::block_rows;
    if (mask.size() != (blocks + 63) / 64) throw std::invalid_argument("coarse mask domain mismatch");
    coarse.assign(mask.begin(), mask.end());
    a = fi::build(av, placement); b = fi::build(bv, placement);
    index = fi::index_by_kind(b);
    scratch.resize(4 * blocks);
    selected = block_mask::bind(coarse, rows);
    validate_native_domains(bn, selected, &an);
}

block_mask block_mask::bind(std::span<const u64> words, std::size_t rows) {
    if (rows % fi::block_rows || words.size() != (rows / fi::block_rows + 63) / 64)
        throw std::invalid_argument("native block mask domain mismatch");
    block_mask out{words, rows / fi::block_rows, true, true};
    for (std::size_t b = 0; b < out.blocks; ++b) {
        const bool bit = (words[b / 64] >> (b % 64)) & 1;
        out.all &= bit; out.none &= !bit;
    }
    return out;
}
answer native_sum_once(const native& payload, const block_mask& selection) {
    if (selection.none) return {};
    return std::visit([&](const auto& b) { return native_sum(payload, selection, b); }, payload.values);
}
void validate_native_domains(const native& payload, const block_mask& selection, const native* predicate) {
    if (payload.rows != selection.blocks * fi::block_rows || (predicate && predicate->rows != payload.rows))
        throw std::invalid_argument("native operands and selection need equal complete-block row domains");
}
answer native_pair_once(const native& predicate, const native& payload, u64 threshold, const block_mask& selection) {
    if (selection.none) return {};
    return std::visit([&](const auto& a, const auto& b) {
        return native_pair(predicate, payload, threshold, selection, a, b);
    }, predicate.values, payload.values);
}

std::vector<range> cell::ranges(operation op, method arm) const {
    std::vector<range> out;
    if (arm == method::native) {
        const auto add = [&](const native& n, const char* name) {
            std::visit([&](const auto& v) {
                using T = typename std::decay_t<decltype(v)>::value_type;
                if (!v.empty()) out.push_back({v.data(), v.size() * sizeof(T), v.capacity() * sizeof(T), name});
            }, n.values);
        };
        add(bn, "B.native");
        if (op == operation::pair) add(an, "A.native");
    } else {
        append_image(out, b, "B.");
        if (op == operation::pair) append_image(out, a, "A.");
        if (op == operation::sum && arm == method::alternative && !index.ids.empty()) {
            out.push_back({index.ids.data(), index.ids.size() * sizeof(u32), index.ids.capacity() * sizeof(u32), "kind.ids"});
            out.push_back({index.runs.data(), index.runs.size() * sizeof(index.runs[0]),
                           index.runs.capacity() * sizeof(index.runs[0]), "kind.runs"});
        }
    }
    // Request masks and output scratch are not deliberately flushed;
    // their logical/capacity bytes are reported separately by the driver.
    return out;
}

answer execute(cell& c, operation op, method arm) {
    if (c.selected.none) return {};
    if (arm == method::native) {
        if (op == operation::sum) return native_sum_once(c.bn, c.selected);
        return native_pair_once(c.an, c.bn, c.threshold, c.selected);
    }
    answer out;
    if (op == operation::sum) {
        if (c.selected.all) return {arm == method::frame ? fi::sum(c.b) : fi::sum_indexed(c.b, c.index), c.rows};
        // Coarse selection preserves original IDs. The indexed arm filters
        // its index before any body address is formed.
        if (arm == method::alternative) {
            for (const auto b : c.index.ids) if (c.active(b)) {
                out.sum += fi::block_sum(c.b, b); out.count += fi::block_rows;
            }
        } else for (std::size_t b = 0; b < c.b.blocks; ++b) if (c.active(b)) {
            out.sum += fi::block_sum(c.b, b); out.count += fi::block_rows;
        }
        return out;
    }
    if (arm == method::alternative) {
        // Segment the admitted original block runs so all-selected can use
        // the substrate's hoisted descriptor/kind dispatch.
        for (std::size_t b = 0; b < c.b.blocks;) {
            if (!c.active(b)) { ++b; continue; }
            auto end = b + 1;
            while (end < c.b.blocks && c.active(end)) ++end;
            out.count += fi::mask_ge(c.a, c.threshold, b, end, c.scratch.data());
            b = end;
        }
        for (std::size_t b = 0; b < c.b.blocks; ++b) if (c.active(b)) {
            const auto* m = c.scratch.data() + 4 * b;
            if (m[0] | m[1] | m[2] | m[3]) out.sum += fi::block_masked_sum(c.b, b, m);
        }
    } else for (std::size_t b = 0; b < c.b.blocks; ++b) if (c.active(b)) {
        const auto count = fi::mask_ge(c.a, c.threshold, b, b + 1, c.scratch.data());
        out.count += count;
        if (count) out.sum += fi::block_masked_sum(c.b, b, c.scratch.data() + 4 * b);
    }
    return out;
}

const char* arm_name(operation op, method arm) {
    if (arm == method::native) return "native-for";
    if (op == operation::sum) return arm == method::frame ? "frame-segments" : "frame-indexed";
    return arm == method::frame ? "frame-fused" : "frame-staged";
}
} // namespace frame_cells
