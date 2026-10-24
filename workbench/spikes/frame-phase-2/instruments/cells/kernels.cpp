#include "cells.h"

#include <algorithm>
#include <bit>
#include <cstdint>
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

template<class T> u64 native_count(const native& predicate, u64 threshold, const block_mask& selection, const std::vector<T>& values) {
    if (threshold > predicate.maximum) return 0;
    const bool all_values = threshold <= predicate.base;
    const auto rhs = static_cast<T>(threshold - predicate.base);
    const auto count = [&](std::size_t first, std::size_t end) {
        if (all_values) return static_cast<u64>(end - first);
        u64 out = 0;
        for (std::size_t i = first; i < end; ++i) out += values[i] >= rhs;
        return out;
    };
    if (selection.all) return count(0, values.size());
    u64 out = 0;
    for (std::size_t b = 0; b < selection.blocks; ++b)
        if (selection.active(b)) out += count(b * fi::block_rows, (b + 1) * fi::block_rows);
    return out;
}

template<class T, bool Dense> answer native_selected_sum(const native& payload, const block_mask& selection,
                                                        const row_mask& fine, const std::vector<T>& values) {
    answer out;
    for (std::size_t b = 0; b < selection.blocks; ++b) {
        if (!selection.active(b)) continue;
        for (unsigned w = 0; w < 4; ++w) {
            const auto word = b * 4 + w;
            auto bits = fine.words[word];
            if (!bits) continue;
            const auto first = word * 64;
            u64 lo = 0, hi = 0;
            if (bits == ~u64{0}) {
                for (unsigned j = 0; j < 64; ++j) {
                    if constexpr (sizeof(T) < 8) lo += values[first + j];
                    else {
                        lo += static_cast<u32>(values[first + j]);
                        hi += values[first + j] >> 32;
                    }
                }
                out.count += 64;
            } else if constexpr (Dense) {
                for (unsigned j = 0; j < 64; ++j) {
                    const auto keep = static_cast<T>(T{0} - static_cast<T>((bits >> j) & 1));
                    const auto value = static_cast<T>(values[first + j] & keep);
                    if constexpr (sizeof(T) < 8) lo += value;
                    else { lo += static_cast<u32>(value); hi += value >> 32; }
                }
                out.count += std::popcount(bits);
            } else {
                out.count += std::popcount(bits);
                for (; bits; bits &= bits - 1) {
                    const auto value = values[first + std::countr_zero(bits)];
                    if constexpr (sizeof(T) < 8) lo += value;
                    else { lo += static_cast<u32>(value); hi += value >> 32; }
                }
            }
            out.sum += u128{lo} + (u128{hi} << 32);
        }
    }
    out.sum += u128{payload.base} * out.count;
    return out;
}

template<class T, bool All> answer native_points(const native& payload, const block_mask& selection,
                                                const point_requests& requests, const std::vector<T>& values) {
    answer out;
    // Bounded half sums avoid per-request carry chains, with four independent
    // accumulators. Chunking also bounds full-u64 request lists, not just rows.
    for (std::size_t first = 0; first < requests.rows.size();) {
        const auto end = first + std::min<std::size_t>(65536, requests.rows.size() - first);
        u64 lo[4]{}, hi[4]{};
        const auto one = [&](std::size_t q, unsigned lane) {
            const auto row = requests.rows[q];
            if constexpr (!All) {
                if (!selection.active(row / fi::block_rows)) return;
                ++out.count;
            }
            const auto value = values[row];
            if constexpr (sizeof(T) < 8) lo[lane] += value;
            else { lo[lane] += static_cast<u32>(value); hi[lane] += value >> 32; }
        };
        std::size_t q = first;
        for (; end - q >= 4; q += 4) {
            one(q, 0); one(q + 1, 1); one(q + 2, 2); one(q + 3, 3);
        }
        for (; q < end; ++q) one(q, 0);
        if constexpr (All) out.count += end - first;
        for (unsigned i = 0; i < 4; ++i) out.sum += u128{lo[i]} + (u128{hi[i]} << 32);
        first = end;
    }
    out.sum += u128{payload.base} * out.count;
    return out;
}

template<class T, bool All> void native_gather(const native& payload, const block_mask& selection,
                                              const point_requests& requests, const std::vector<T>& values,
                                              std::span<u64> output) {
    // Disjoint output is proved once by validate_native_gather, outside clocks.
    const T* __restrict source = values.data();
    const std::size_t* __restrict rows = requests.rows.data();
    u64* __restrict target = output.data();
    for (std::size_t i = 0; i < requests.rows.size(); ++i) {
        const auto row = rows[i];
        if constexpr (!All) {
            if (!selection.active(row / fi::block_rows)) { target[i] = 0; continue; }
        }
        target[i] = payload.base + static_cast<u64>(source[row]);
    }
}

bool overlaps(std::span<u64> output, const void* data, std::size_t bytes) {
    if (output.empty() || !bytes) return false;
    const auto a = reinterpret_cast<std::uintptr_t>(output.data()), b = reinterpret_cast<std::uintptr_t>(data);
    return a <= b ? b - a < output.size_bytes() : a - b < bytes;
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
row_mask row_mask::bind(std::span<const u64> words, std::size_t rows) {
    if (words.size() != rows / 64 + (rows % 64 != 0))
        throw std::invalid_argument("native fine mask domain mismatch");
    row_mask out{words, rows, true, true};
    for (std::size_t w = 0; w < words.size(); ++w) {
        const auto expected = w + 1 == words.size() && rows % 64 ? (u64{1} << (rows % 64)) - 1 : ~u64{0};
        if (words[w] & ~expected) throw std::invalid_argument("native fine mask has out-of-domain bits");
        out.all &= words[w] == expected;
        out.none &= words[w] == 0;
    }
    return out;
}
point_requests point_requests::bind(std::span<const std::size_t> rows, std::size_t domain_rows) {
    for (const auto row : rows)
        if (row >= domain_rows) throw std::invalid_argument("native point outside row domain");
    return {rows, domain_rows};
}
answer native_sum_once(const native& payload, const block_mask& selection) {
    if (selection.none) return {};
    return std::visit([&](const auto& b) { return native_sum(payload, selection, b); }, payload.values);
}
void validate_native_domains(const native& payload, const block_mask& selection, const native* predicate) {
    if (payload.rows != selection.blocks * fi::block_rows || (predicate && predicate->rows != payload.rows))
        throw std::invalid_argument("native operands and selection need equal complete-block row domains");
}
void validate_native_selection(const native& payload, const block_mask& selection, const row_mask& fine) {
    validate_native_domains(payload, selection);
    if (fine.rows != payload.rows) throw std::invalid_argument("native fine selection needs the payload row domain");
}
void validate_native_points(const native& payload, const block_mask& selection, const point_requests& requests) {
    validate_native_domains(payload, selection);
    if (requests.domain_rows != payload.rows) throw std::invalid_argument("native points need the payload row domain");
}
void validate_native_gather(const native& payload, const block_mask& selection, const point_requests& requests,
                            std::span<u64> output) {
    validate_native_points(payload, selection, requests);
    if (output.size() != requests.rows.size()) throw std::invalid_argument("native gather output needs one value per request");
    if (overlaps(output, &payload, sizeof(payload)) || overlaps(output, &selection, sizeof(selection)) ||
        overlaps(output, &requests, sizeof(requests)) ||
        overlaps(output, selection.words.data(), selection.words.size_bytes()) ||
        overlaps(output, requests.rows.data(), requests.rows.size_bytes()))
        throw std::invalid_argument("native gather output overlaps input owner or borrowed storage");
    std::visit([&](const auto& values) {
        using T = typename std::decay_t<decltype(values)>::value_type;
        if (overlaps(output, values.data(), values.size() * sizeof(T)))
            throw std::invalid_argument("native gather output overlaps payload storage");
    }, payload.values);
}
answer native_pair_once(const native& predicate, const native& payload, u64 threshold, const block_mask& selection) {
    if (selection.none) return {};
    return std::visit([&](const auto& a, const auto& b) {
        return native_pair(predicate, payload, threshold, selection, a, b);
    }, predicate.values, payload.values);
}
u64 native_count_once(const native& predicate, u64 threshold, const block_mask& selection) {
    if (selection.none) return 0;
    return std::visit([&](const auto& values) { return native_count(predicate, threshold, selection, values); }, predicate.values);
}
answer native_selected_sum_once(const native& payload, const block_mask& selection, const row_mask& fine) {
    if (selection.none || fine.none) return {};
    if (fine.all) return native_sum_once(payload, selection);
    return std::visit([&](const auto& values) { return native_selected_sum<typename std::decay_t<decltype(values)>::value_type, false>(payload, selection, fine, values); }, payload.values);
}
answer native_selected_sum_dense_once(const native& payload, const block_mask& selection, const row_mask& fine) {
    if (selection.none || fine.none) return {};
    if (fine.all) return native_sum_once(payload, selection);
    return std::visit([&](const auto& values) { return native_selected_sum<typename std::decay_t<decltype(values)>::value_type, true>(payload, selection, fine, values); }, payload.values);
}
answer native_points_once(const native& payload, const block_mask& selection, const point_requests& requests) {
    if (selection.none || requests.rows.empty()) return {};
    return std::visit([&](const auto& values) {
        using T = typename std::decay_t<decltype(values)>::value_type;
        if (selection.all) return native_points<T, true>(payload, selection, requests, values);
        return native_points<T, false>(payload, selection, requests, values);
    }, payload.values);
}
void native_gather_once(const native& payload, const block_mask& selection, const point_requests& requests,
                        std::span<u64> output) {
    if (selection.none) { std::fill(output.begin(), output.end(), u64{0}); return; }
    if (requests.rows.empty()) return;
    std::visit([&](const auto& values) {
        using T = typename std::decay_t<decltype(values)>::value_type;
        if (selection.all) native_gather<T, true>(payload, selection, requests, values, output);
        else native_gather<T, false>(payload, selection, requests, values, output);
    }, payload.values);
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
