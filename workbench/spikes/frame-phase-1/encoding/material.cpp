// Compiled once: block encoding (per block, values or part dictionary codes)
// and whole-block decode for the private Frame material.
#include "material.h"

#include <array>
#include <atomic>
#include <cassert>

namespace frame_encoding {
namespace {
// One block's body under the existing kinds, appended to payload (the
// descriptor's offset is into payload). Kind-agnostic about what the values
// mean: a dictionary code block passes its codes here unchanged.
descriptor emit_block(std::span<const u64> block, const encode_options &options,
                      std::vector<std::uint8_t> &payload) {
    const checkpoints delta_checkpoints = options.delta_checkpoints;
    std::vector<u64> scratch(block_rows);
    const auto [lo, hi] = std::minmax_element(block.begin(), block.end());
    descriptor d{*lo, static_cast<std::uint32_t>(payload.size()), 0, kind::constant, 0};
    if (*lo == *hi)
        return d;
    const unsigned k = width_of(*hi - *lo);
    std::vector<u64> run_values;
    std::vector<unsigned> run_sizes;
    for (std::size_t i = 0; i < block_rows;) {
        std::size_t j = i + 1;
        while (j < block_rows && block[j] == block[i])
            ++j;
        run_values.push_back(block[i] - *lo);
        run_sizes.push_back(static_cast<unsigned>(j - i));
        i = j;
    }
    // Physical widths: exact unless an experimental byte-lane option rounds them.
    const unsigned packed_width = options.packed_byte_lanes ? byte_lane_width(k) : k;
    const unsigned runs_width = options.run_values == encode_options::run_width::byte_lanes ? byte_lane_width(k) : k;
    const std::size_t packed_cost = plane_bytes(packed_width, block_rows);
    // Delta candidate: every true step must fit i64 so wrapping u64
    // reconstruction is exact.
    bool delta_ok = true;
    i128 dmin = 0, dmax = 0;
    for (std::size_t i = 1; i < block_rows; ++i) {
        const i128 step = static_cast<i128>(block[i]) - static_cast<i128>(block[i - 1]);
        if (step < -(static_cast<i128>(1) << 63) || step >= (static_cast<i128>(1) << 63))
            delta_ok = false;
        dmin = i == 1 ? step : std::min(dmin, step);
        dmax = i == 1 ? step : std::max(dmax, step);
    }
    const unsigned dk = delta_ok ? std::max(1u, width_of(static_cast<u64>(dmax - dmin))) : 64;
    const std::size_t checkpoint_bytes = dk + 8 <= 32 ? 4 : 8;
    const std::size_t delta_cost = plane_bytes(dk, block_rows) + 8 + checkpoint_cost(delta_checkpoints, dk);
    // Patched candidate: the narrowest window of 256 - e sorted values for
    // e in {2, 8, 32}; values outside base + [0, 2^K) become exceptions.
    std::vector<u64> sorted(block.begin(), block.end());
    std::sort(sorted.begin(), sorted.end());
    const unsigned full_width = k;
    std::size_t patched_cost = SIZE_MAX;
    u64 patched_base = 0;
    unsigned patched_k = 0;
    for (unsigned e : {2u, 8u, 32u}) {
        const std::size_t keep = block_rows - e;
        u64 best_span = UINT64_MAX, best_base = 0;
        for (std::size_t s = 0; s + keep <= block_rows; ++s)
            if (sorted[s + keep - 1] - sorted[s] < best_span) {
                best_span = sorted[s + keep - 1] - sorted[s];
                best_base = sorted[s];
            }
        const unsigned pk = std::max(1u, width_of(best_span));
        if (pk > 56 || pk >= k)
            continue;
        const u64 limit = pk == 64 ? UINT64_MAX : (u64{1} << pk) - 1;
        std::size_t n = 0;
        for (u64 v : block)
            n += v < best_base || v - best_base > limit;
        const std::size_t cost = plane_bytes(pk, block_rows) + 8 + 2 + n +
                                 plane_bytes(full_width, n);
        if (n && cost < patched_cost) {
            patched_cost = cost;
            patched_base = best_base;
            patched_k = pk;
        }
    }
    const std::size_t runs_cost = plane_bytes(runs_width, run_values.size()) + run_sizes.size();
    // Delta-with-exceptions candidate: for e in {2, 8, 32}, two exception
    // sets, each priced exactly under the delta_patched law (material.h):
    //   values  rows outside the narrowest window of 256 - e sorted values
    //           (sentinels, level outliers), as for patched;
    //   steps   a left-to-right pass keeping steps inside the narrowest
    //           window of 255 - e sorted steps: an out-of-window row
    //           becomes an exception when, within the remaining budget,
    //           a later row steps back into the window from the last kept
    //           value (spikes and sentinel runs inside a smooth series).
    std::size_t dp_cost = SIZE_MAX;
    std::array<bool, block_rows> dp_flags{};
    i128 dp_dmin = 0;
    unsigned dp_k = 0, dp_vw = 0;
    auto price = [&](const std::array<bool, block_rows> &flags) {
        bool kept = false, first = true;
        u64 prev = 0, emax = 0;
        std::size_t n = 0;
        i128 lo_step = 0, hi_step = 0;
        for (std::size_t i = 0; i < block_rows; ++i) {
            if (flags[i]) {
                ++n;
                emax = std::max(emax, block[i] - *lo);
                continue;
            }
            if (kept) {
                const i128 step = static_cast<i128>(block[i]) - static_cast<i128>(prev);
                if (step < -(static_cast<i128>(1) << 63) || step >= (static_cast<i128>(1) << 63))
                    return;
                lo_step = first ? step : std::min(lo_step, step);
                hi_step = first ? step : std::max(hi_step, step);
                first = false;
            }
            prev = block[i];
            kept = true;
        }
        if (!n || n > 32 || first)
            return;
        const unsigned pk = std::max(1u, width_of(static_cast<u64>(hi_step - lo_step)));
        const unsigned vw = std::max(1u, width_of(emax));
        const std::size_t cost = plane_bytes(pk, block_rows) + 8 +
                                 checkpoint_cost(delta_checkpoints, pk) + 8 + 2 + n +
                                 plane_bytes(vw, n);
        if (cost < dp_cost) {
            dp_cost = cost;
            dp_flags = flags;
            dp_dmin = lo_step;
            dp_k = pk;
            dp_vw = vw;
        }
    };
    if (options.delta_exceptions) {
        std::vector<i128> steps;
        for (std::size_t i = 1; i < block_rows; ++i)
            steps.push_back(static_cast<i128>(block[i]) - static_cast<i128>(block[i - 1]));
        std::sort(steps.begin(), steps.end());
        for (unsigned e : {2u, 8u, 32u}) {
            std::array<bool, block_rows> flags{};
            const std::size_t keep = block_rows - e;
            u64 best_span = UINT64_MAX, best_base = 0;
            for (std::size_t s = 0; s + keep <= block_rows; ++s)
                if (sorted[s + keep - 1] - sorted[s] < best_span) {
                    best_span = sorted[s + keep - 1] - sorted[s];
                    best_base = sorted[s];
                }
            for (std::size_t i = 0; i < block_rows; ++i)
                flags[i] = block[i] < best_base || block[i] - best_base > best_span;
            price(flags);
            const std::size_t keep_steps = block_rows - 1 - e;
            i128 ws = 0, we = 0;
            for (std::size_t s = 0; s + keep_steps <= steps.size(); ++s)
                if (s == 0 || steps[s + keep_steps - 1] - steps[s] < we - ws) {
                    ws = steps[s];
                    we = steps[s + keep_steps - 1];
                }
            auto inside = [&](u64 to, u64 from) {
                const i128 step = static_cast<i128>(to) - static_cast<i128>(from);
                return step >= ws && step <= we;
            };
            flags.fill(false);
            unsigned budget = e;
            // A leading outlier: row 0 is out of step with rows 1 and 2.
            if (!inside(block[1], block[0]) && inside(block[2], block[1])) {
                flags[0] = true;
                --budget;
            }
            u64 prev = block[flags[0] ? 1 : 0];
            for (std::size_t i = flags[0] ? 2 : 1; i < block_rows; ++i) {
                if (inside(block[i], prev)) {
                    prev = block[i];
                    continue;
                }
                std::size_t j = i + 1;
                while (j < block_rows && j - i <= budget && !inside(block[j], prev))
                    ++j;
                if (j - i > budget) {  // no return within budget: keep the step
                    prev = block[i];
                    continue;
                }
                for (std::size_t r = i; r < j; ++r)
                    flags[r] = true;  // a trailing outlier run returns at j = 256
                budget -= static_cast<unsigned>(j - i);
                if (j == block_rows)
                    break;
                prev = block[j];
                i = j;
            }
            price(flags);
        }
    }
    if (dp_cost < std::min({patched_cost, packed_cost, runs_cost,
                            delta_ok ? delta_cost : SIZE_MAX})) {
        d.type = kind::delta_patched;
        d.count = block_rows;
        d.width = static_cast<std::uint8_t>(dp_k);
        std::vector<std::uint8_t> positions;
        std::vector<u64> exceptional;
        std::vector<u64> marks;
        const std::size_t every = delta_checkpoints == checkpoints::every64 ? 64 : 16;
        bool kept = false;
        u64 prev = 0, cumulative = 0;
        for (std::size_t i = 0; i < block_rows; ++i) {
            scratch[i] = 0;
            if (dp_flags[i]) {
                positions.push_back(static_cast<std::uint8_t>(i));
                exceptional.push_back(block[i] - *lo);
            } else if (!kept) {
                d.base = block[i];  // the anchor: first non-exception row
                kept = true;
                prev = block[i];
            } else {
                scratch[i] = static_cast<u64>(static_cast<i128>(block[i]) -
                                              static_cast<i128>(prev) - dp_dmin);
                prev = block[i];
            }
            cumulative += scratch[i];
            if (delta_checkpoints != checkpoints::none && i % every == every - 1 &&
                i + 1 < block_rows)
                marks.push_back(cumulative);  // S(i): stored residuals through row i
        }
        const i64 rbase = static_cast<i64>(dp_dmin);
        const std::size_t plane = plane_bytes(dp_k, block_rows);
        const std::size_t marks_bytes = checkpoint_cost(delta_checkpoints, dp_k);
        const std::size_t native = dp_k + 8 <= 32 ? 4 : 8;
        payload.resize(payload.size() + dp_cost);
        std::uint8_t *at = payload.data() + d.offset;
        pack(dp_k, scratch, at);
        std::memcpy(at + plane, &rbase, 8);
        std::uint8_t *stored = at + plane + 8;
        if (delta_checkpoints == checkpoints::packed && dp_k + 8 <= 64)
            pack(dp_k + 8, marks, stored);
        else for (std::size_t j = 0; j < marks.size(); ++j) {
            if (native == 4) {
                const auto v = static_cast<std::uint32_t>(marks[j]);
                std::memcpy(stored + j * 4, &v, 4);
            } else {
                std::memcpy(stored + j * 8, &marks[j], 8);
            }
        }
        std::uint8_t *tail = stored + marks_bytes;
        std::memcpy(tail, &*lo, 8);
        tail[8] = static_cast<std::uint8_t>(positions.size());
        tail[9] = static_cast<std::uint8_t>(dp_vw);
        std::memcpy(tail + 10, positions.data(), positions.size());
        pack(dp_vw, exceptional, tail + 10 + positions.size());
    } else if (patched_cost < std::min({packed_cost, runs_cost, delta_ok ? delta_cost : SIZE_MAX})) {
        d.type = kind::patched;
        d.base = patched_base;
        d.count = block_rows;
        d.width = static_cast<std::uint8_t>(patched_k);
        const u64 limit = (u64{1} << patched_k) - 1;
        std::vector<std::uint8_t> positions;
        std::vector<u64> exceptional;
        for (std::size_t i = 0; i < block_rows; ++i) {
            const u64 v = block[i];
            if (v < patched_base || v - patched_base > limit) {
                positions.push_back(static_cast<std::uint8_t>(i));
                exceptional.push_back(v - *lo);
                scratch[i] = 0;
            } else {
                scratch[i] = v - patched_base;
            }
        }
        const std::size_t plane = plane_bytes(patched_k, block_rows);
        payload.resize(payload.size() + patched_cost);
        std::uint8_t *at = payload.data() + d.offset;
        pack(patched_k, scratch, at);
        std::memcpy(at + plane, &*lo, 8);
        at[plane + 8] = static_cast<std::uint8_t>(positions.size());
        at[plane + 9] = static_cast<std::uint8_t>(full_width);
        std::memcpy(at + plane + 10, positions.data(), positions.size());
        pack(full_width, exceptional, at + plane + 10 + positions.size());
    } else if (delta_ok && delta_cost < std::min(packed_cost, runs_cost)) {
        d.type = kind::delta;
        d.base = block[0];
        d.count = block_rows;
        d.width = static_cast<std::uint8_t>(dk);
        const i64 rbase = static_cast<i64>(dmin);
        scratch[0] = 0;
        u64 cumulative = 0;
        std::vector<u64> marks;
        for (std::size_t i = 1; i < block_rows; ++i) {
            scratch[i] = static_cast<u64>(
                static_cast<i128>(block[i]) - static_cast<i128>(block[i - 1]) - dmin);
            cumulative += scratch[i];
            const std::size_t every =
                delta_checkpoints == checkpoints::every64 ? 64 : 16;
            if (delta_checkpoints != checkpoints::none && i % every == every - 1 &&
                i + 1 < block_rows)
                marks.push_back(cumulative);  // sum through row i
        }
        const std::size_t plane = plane_bytes(dk, block_rows);
        payload.resize(payload.size() + delta_cost);
        pack(dk, scratch, payload.data() + d.offset);
        std::memcpy(payload.data() + d.offset + plane, &rbase, 8);
        std::uint8_t *stored = payload.data() + d.offset + plane + 8;
        if (delta_checkpoints == checkpoints::packed && dk + 8 <= 64)
            pack(dk + 8, marks, stored);
        else for (std::size_t j = 0; j < marks.size(); ++j) {
            std::uint8_t *at = stored + j * checkpoint_bytes;
            if (checkpoint_bytes == 4) {
                const auto v = static_cast<std::uint32_t>(marks[j]);
                std::memcpy(at, &v, 4);
            } else {
                std::memcpy(at, &marks[j], 8);
            }
        }
    } else if (runs_cost < packed_cost) {
        d.type = kind::runs;
        d.count = static_cast<std::uint16_t>(run_values.size());
        d.width = static_cast<std::uint8_t>(runs_width > k ? k | byte_lane_flag : k);
        const std::size_t values_bytes = plane_bytes(runs_width, run_values.size());
        payload.resize(payload.size() + values_bytes + run_sizes.size());
        pack(runs_width, run_values, payload.data() + d.offset);
        unsigned end = 0;
        for (std::size_t r = 0; r < run_sizes.size(); ++r) {
            end += run_sizes[r];
            payload[d.offset + values_bytes + r] = static_cast<std::uint8_t>(end - 1);
        }
    } else {
        for (std::size_t i = 0; i < block_rows; ++i)
            scratch[i] = block[i] - *lo;
        d.type = kind::packed;
        d.count = block_rows;
        d.width = static_cast<std::uint8_t>(packed_width > k ? k | byte_lane_flag : k);
        payload.resize(payload.size() + packed_cost);
        pack(packed_width, scratch, payload.data() + d.offset);
    }
    return d;
}

// One block's candidate body, built into its own payload at offset 0.
struct candidate {
    descriptor d;
    std::vector<std::uint8_t> payload;
    std::size_t bytes() const { return payload.size(); }
};

candidate emit_candidate(std::span<const u64> block, const encode_options &options) {
    candidate k;
    k.d = emit_block(block, options, k.payload);
    return k;
}

// The arrival body (material.h) of one block of arrival codes whose frontier
// before the block is frontier.
candidate emit_arrival(std::span<const u64> codes, u64 frontier) {
    std::array<u64, 4> flags{};
    std::vector<u64> back;
    u64 at = frontier;
    for (std::size_t i = 0; i < block_rows; ++i) {
        if (codes[i] == at) {
            flags[i / 64] |= u64{1} << (i % 64);
            ++at;
        } else {
            back.push_back(at - 1 - codes[i]);  // codes[i] < at
        }
    }
    const unsigned w = std::max(1u, width_of(back.empty() ? 0 : *std::max_element(back.begin(), back.end())));
    candidate k;
    k.d = {frontier, 0, static_cast<std::uint16_t>(back.size()), kind::arrival, static_cast<std::uint8_t>(w)};
    k.payload.resize(arrival_flag_bytes + plane_bytes(w, back.size()));
    std::memcpy(k.payload.data(), flags.data(), arrival_flag_bytes);
    if (!back.empty())
        pack(w, back, k.payload.data() + arrival_flag_bytes);
    return k;
}

// Predictor-consistent fills of one block (material.h, null_fill). valid has
// the block's four validity words; values are read at valid rows only.
void fill_block(const u64 *values, const u64 *valid, const encode_options &options, null_fill mode, u64 *out) {
    auto is_valid = [&](unsigned i) { return valid[i / 64] >> (i % 64) & 1; };
    unsigned n = 0, first = block_rows, last = 0;
    u64 lo = UINT64_MAX, hi = 0;
    for (unsigned i = 0; i < block_rows; ++i)
        if (is_valid(i)) {
            out[i] = values[i];
            lo = std::min(lo, values[i]), hi = std::max(hi, values[i]);
            first = std::min(first, i), last = i, ++n;
        }
    if (n == block_rows)
        return;
    if (mode == null_fill::zero || !n) {
        for (unsigned i = 0; i < block_rows; ++i)
            if (!is_valid(i))
                out[i] = 0;
        return;
    }
    std::vector<std::array<u64, block_rows>> candidates;
    candidates.emplace_back();  // base: the valid minimum
    candidates.emplace_back();  // previous valid value
    for (unsigned i = 0; i < block_rows; ++i) {
        candidates[0][i] = is_valid(i) ? out[i] : lo;
        candidates[1][i] = is_valid(i) ? out[i] : i < first ? out[first] : candidates[1][i - 1];
    }
    // Interpolation across interior gaps; edges copy the nearest valid value.
    candidates.emplace_back(candidates[1]);
    for (unsigned p = first; p < last;) {
        unsigned q = p + 1;
        while (!is_valid(q))
            ++q;
        const i128 a = out[p], span = static_cast<i128>(out[q]) - a;
        for (unsigned i = p + 1; i < q; ++i) {
            // floor division towards -inf keeps a + step within [x_p, x_q]
            const i128 num = span * (i - p), den = q - p;
            const i128 quot = num >= 0 ? num / den : -((-num + den - 1) / den);
            candidates.back()[i] = static_cast<u64>(a + quot);
        }
        p = q;
    }
    // previous + r (delta s = 0), r the smallest step between adjacent valid
    // rows; refused when any filler would leave [lo, hi].
    bool have_r = false;
    i128 r = 0;
    for (unsigned i = first; i < last; ++i)
        if (is_valid(i) && is_valid(i + 1)) {
            const i128 step = static_cast<i128>(out[i + 1]) - static_cast<i128>(out[i]);
            r = have_r ? std::min(r, step) : step;
            have_r = true;
        }
    if (have_r) {
        std::array<u64, block_rows> c{};
        bool inside = true;
        for (unsigned i = 0; i < block_rows && inside; ++i) {
            if (is_valid(i)) {
                c[i] = out[i];
                continue;
            }
            const i128 v = i < first ? static_cast<i128>(out[first]) - r * (first - i) : static_cast<i128>(c[i - 1]) + r;
            inside = v >= static_cast<i128>(lo) && v <= static_cast<i128>(hi);
            c[i] = inside ? static_cast<u64>(v) : 0;
        }
        if (inside)
            candidates.push_back(c);
    }
    std::size_t best = SIZE_MAX, pick = 0;
    for (std::size_t k = 0; k < candidates.size(); ++k) {
        std::vector<std::uint8_t> payload;
        emit_block(candidates[k], options, payload);
        if (payload.size() < best)
            best = payload.size(), pick = k;
    }
    std::copy(candidates[pick].begin(), candidates[pick].end(), out);
}
}  // namespace

std::vector<u64> fill_nulls(std::span<const u64> values, std::span<const u64> validity, encode_options options,
                            null_fill fill) {
    const std::size_t nblocks = values.size() / block_rows;
    if (validity.size() < 4 * nblocks)
        std::abort();
    std::vector<u64> out(nblocks * block_rows);
    for (std::size_t b = 0; b < nblocks; ++b)
        fill_block(values.data() + b * block_rows, validity.data() + 4 * b, options, fill, out.data() + b * block_rows);
    return out;
}

nullable_column encode_nullable(std::span<const u64> values, std::span<const u64> validity, encode_options options,
                                null_fill fill) {
    const auto filled = fill_nulls(values, validity, options, fill);
    nullable_column c{encode(filled, options), {}};
    c.validity.assign(validity.begin(), validity.begin() + 4 * (filled.size() / block_rows));
    return c;
}

column encode(std::span<const u64> values, encode_options options) {
    static std::atomic<u64> images{0};
    column c;
    c.options = options;
    c.image = ++images;
    c.rows = values.size() / block_rows * block_rows;
    const std::size_t nblocks = c.rows / block_rows;
    const bool dictionary = options.dictionary != dictionary_order::none;
    if (dictionary)
        c.coded.assign((nblocks + 63) / 64, 0);
    std::vector<candidate> plain_bodies, code_bodies;
    std::vector<u64> codes(block_rows);
    for (std::size_t first = 0; first < nblocks; first += part_blocks) {
        const std::size_t limit = std::min(first + part_blocks, nblocks);
        const auto part = values.subspan(first * block_rows, (limit - first) * block_rows);
        plain_bodies.clear();
        code_bodies.clear();
        for (std::size_t b = first; b < limit; ++b)
            plain_bodies.push_back(emit_candidate(values.subspan(b * block_rows, block_rows), options));
        if (!dictionary) {
            for (auto &k : plain_bodies) {
                k.d.offset += static_cast<std::uint32_t>(c.payload.size());
                c.payload.insert(c.payload.end(), k.payload.begin(), k.payload.end());
                c.blocks.push_back(k.d);
            }
            continue;
        }
        // The part's dictionary: distinct values ascending, and the code of
        // each (its rank in that order, or by descending frequency with ties
        // by value). rank_of[i] is the code of distinct[i].
        std::vector<u64> distinct(part.begin(), part.end());
        std::sort(distinct.begin(), distinct.end());
        std::vector<std::uint32_t> frequency;
        {
            std::size_t n = 0;
            for (std::size_t i = 0; i < distinct.size(); ++n) {
                std::size_t j = i + 1;
                while (j < distinct.size() && distinct[j] == distinct[i])
                    ++j;
                distinct[n] = distinct[i];
                frequency.push_back(static_cast<std::uint32_t>(j - i));
                i = j;
            }
            distinct.resize(n);
        }
        std::vector<std::uint32_t> rank_of(distinct.size());
        for (std::size_t i = 0; i < distinct.size(); ++i)
            rank_of[i] = static_cast<std::uint32_t>(i);
        if (options.dictionary == dictionary_order::frequency) {
            std::vector<std::uint32_t> order(rank_of);
            std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
                return frequency[a] > frequency[b];  // stable: ties stay by value
            });
            for (std::size_t r = 0; r < order.size(); ++r)
                rank_of[order[r]] = static_cast<std::uint32_t>(r);
        } else if (options.dictionary == dictionary_order::arrival) {
            std::fill(rank_of.begin(), rank_of.end(), UINT32_MAX);
            std::uint32_t next = 0;
            for (u64 v : part) {
                auto &r = rank_of[static_cast<std::size_t>(
                    std::lower_bound(distinct.begin(), distinct.end(), v) - distinct.begin())];
                if (r == UINT32_MAX)
                    r = next++;
            }
        }
        std::vector<u64> stored(distinct.size());  // dictionary in code order
        for (std::size_t i = 0; i < distinct.size(); ++i)
            stored[rank_of[i]] = distinct[i] - distinct.front();
        const unsigned dw = std::max(1u, width_of(distinct.back() - distinct.front()));
        const std::size_t dictionary_cost = plane_bytes(dw, stored.size());
        std::size_t without = 0, with = dictionary_cost;
        bool any = false;
        u64 frontier = 0;  // arrival: entries introduced before block b
        for (std::size_t b = first; b < limit; ++b) {
            for (std::size_t i = 0; i < block_rows; ++i) {
                const u64 v = values[b * block_rows + i];
                codes[i] = rank_of[static_cast<std::size_t>(
                    std::lower_bound(distinct.begin(), distinct.end(), v) - distinct.begin())];
            }
            code_bodies.push_back(emit_candidate(codes, options));
            if (options.dictionary == dictionary_order::arrival) {
                // The frontier-relative body competes with the ordinary kinds
                // over the same codes and wins only when strictly smaller.
                auto arrival = emit_arrival(codes, frontier);
                if (arrival.bytes() < code_bodies.back().bytes())
                    code_bodies.back() = std::move(arrival);
                for (u64 code : codes)
                    frontier += code == frontier;
            }
            const std::size_t v = plain_bodies[b - first].bytes(), k = code_bodies.back().bytes();
            without += v;
            with += std::min(v, k);
            any |= k < v;
        }
        // Per-block opt-out: a block keeps its values unless its codes are
        // strictly cheaper. Per-part opt-out: the dictionary is dropped (count
        // 0, no plane, no coded blocks) unless it pays for itself overall.
        const bool keep = any && (options.dictionary_every_part || with < without);
        dictionary_part record{distinct.front(), static_cast<std::uint32_t>(c.payload.size()), 0, 0};
        if (keep) {
            record.count = static_cast<std::uint32_t>(stored.size());
            record.width = static_cast<std::uint8_t>(dw);
            c.payload.resize(c.payload.size() + dictionary_cost);
            pack(dw, stored, c.payload.data() + record.offset);
        }
        c.parts.push_back(record);
        for (std::size_t b = first; b < limit; ++b) {
            auto &v = plain_bodies[b - first], &k = code_bodies[b - first];
            const bool use_codes = keep && k.bytes() < v.bytes();
            auto &w = use_codes ? k : v;
            if (use_codes)
                c.coded[b / 64] |= u64{1} << (b % 64);
            w.d.offset += static_cast<std::uint32_t>(c.payload.size());
            c.payload.insert(c.payload.end(), w.payload.begin(), w.payload.end());
            c.blocks.push_back(w.d);
        }
    }
    if (!options.bind_decoders)
        return c;  // owner operations read planes through the catalogue
    for (const auto &p : c.parts) {
        c.dictionaries.emplace_back();
        if (!p.count)
            continue;
        auto decoder = sp::bind_reader<u64>(sp::dense_input{
            p.width, sp::geometry::local,
            std::as_bytes(std::span<const std::uint8_t>(c.payload).subspan(p.offset, plane_bytes(p.width, p.count))),
            p.count});
        if (!decoder)
            std::abort();
        c.dictionaries.back() = *decoder;
    }
    // Bind decoders once, after the payload stops growing.
    c.decoders.reserve(c.blocks.size());
    for (const auto &d : c.blocks) {
        c.d8.emplace_back();
        c.d16.emplace_back();
        c.d32.emplace_back();
        c.exceptions.emplace_back();
        if (d.type == kind::constant || (d.type == kind::arrival && !d.count)) {
            c.decoders.emplace_back(nullptr, 0, nullptr, nullptr, nullptr);
            continue;
        }
        // An arrival block's plane (its references) follows the flags.
        const std::size_t at = d.offset + (d.type == kind::arrival ? arrival_flag_bytes : 0);
        const unsigned width = plane_width(d);
        const sp::dense_input input{
            width, sp::geometry::local,
            std::as_bytes(std::span<const std::uint8_t>(c.payload).subspan(at, plane_bytes(width, d.count))),
            d.count};
        auto decoder = sp::bind_reader<u64>(input);
        if (!decoder)
            std::abort();
        c.decoders.push_back(*decoder);
        if (d.type == kind::patched || d.type == kind::delta_patched) {
            // Exception values sit after the positions at the width stored in
            // the body.
            const std::size_t b = c.decoders.size() - 1;
            const auto p = d.type == kind::patched ? patches(c, b) : delta_patches(c, b);
            const unsigned ew = p.value_width;  // stored in the body
            auto exc = sp::bind_reader<u64>(sp::dense_input{
                ew, sp::geometry::local,
                std::as_bytes(std::span<const std::uint8_t>(p.values, plane_bytes(ew, p.n))), p.n});
            if (!exc)
                std::abort();
            c.exceptions.back() = *exc;
            continue;  // patched and delta_patched sums use the u64 decoder
        }
        if (d.type == kind::arrival)
            continue;
        if (width <= 8)
            c.d8.back() = *sp::bind_reader<std::uint8_t>(input);
        else if (width <= 16)
            c.d16.back() = *sp::bind_reader<std::uint16_t>(input);
        else if (width <= 32)
            c.d32.back() = *sp::bind_reader<std::uint32_t>(input);
    }
    return c;
}

bool admits(const borrowed_block &k) {
    const auto &d = k.d;
    if (k.rows != block_rows || static_cast<unsigned>(d.type) > static_cast<unsigned>(kind::arrival))
        return false;
    if (d.type != kind::constant && !k.body)
        return false;
    // A byte-lane view's exact width lies below its plane's byte lane.
    if (k.exact_width && ((d.type != kind::runs && d.type != kind::packed) || k.exact_width >= d.width ||
                          byte_lane_width(k.exact_width) != d.width))
        return false;
    const auto extent = extent_of(d, k.delta_checkpoints, k.body, k.grant);
    if (!extent || extent->read_grant_bytes > k.grant)
        return false;
    if (d.type == kind::patched || d.type == kind::delta_patched) {
        const auto p = patches(k);
        for (unsigned e = 1; e < p.n; ++e)
            if (p.positions[e] <= p.positions[e - 1])
                return false;
    }
    if (d.type == kind::arrival &&
        (!k.coded || k.dictionary.order != dictionary_order::arrival ||
         arrival_literals(arrival_flags(k)) + d.count != block_rows ||
         d.base + arrival_literals(arrival_flags(k)) > k.dictionary.count))
        return false;
    if (!k.coded)
        return true;
    const auto &v = k.dictionary;
    return v.count && v.width >= 1 && v.width <= 64 && v.order != dictionary_order::none &&
           v.plane && v.bytes() <= v.grant;
}

u64 stored_at(const borrowed_block &k, unsigned row) {
    const auto &d = k.d;
    switch (d.type) {
    case kind::constant: return d.base;
    case kind::packed: return d.base + plane_kernels(d.width).point(k.body, row);
    case kind::patched: {
        const auto p = patches(k);
        const auto *at = std::lower_bound(p.positions, p.positions + p.n, row);
        if (at != p.positions + p.n && *at == row)
            return p.lo + plane_kernels(p.value_width).point(p.values, at - p.positions);
        return d.base + plane_kernels(d.width).point(k.body, row);
    }
    case kind::runs:
        return d.base + plane_kernels(d.width).point(k.body, iv::upper_bound(runs_of(k), row));
    case kind::delta:
    case kind::delta_patched: {
        // value = anchor + rbase * m(row) + S(row), with S from the nearest
        // checkpoint at or before row plus the residuals since it.
        unsigned steps = row;
        if (d.type == kind::delta_patched) {
            const auto p = patches(k);
            const auto *at = std::lower_bound(p.positions, p.positions + p.n, row);
            if (at != p.positions + p.n && *at == row)
                return p.lo + plane_kernels(p.value_width).point(p.values, at - p.positions);
            steps -= static_cast<unsigned>(at - p.positions);
        }
        if (!steps)
            return d.base;  // S is 0 through the anchor
        const auto mode = k.delta_checkpoints;
        const unsigned spacing = mode == checkpoints::none ? 256 : mode == checkpoints::every64 ? 64 : 16;
        const unsigned first = row / spacing * spacing;
        const std::uint8_t *marks = k.body + plane_bytes(d.width, block_rows) + 8;
        u64 sum = 0;
        if (first) {
            const unsigned mark = first / spacing - 1;
            if (mode == checkpoints::packed && d.width + 8 <= 64)
                sum = plane_kernels(d.width + 8).point(marks, mark);
            else if (d.width + 8 <= 32) {
                std::uint32_t v;
                std::memcpy(&v, marks + mark * 4, 4);
                sum = v;
            } else {
                std::memcpy(&sum, marks + mark * 8, 8);
            }
        }
        // first is 16-aligned and first + ceil(take / 16) * 16 <= 256, so the
        // catalogue's native-packet SUM (full 16-row windows of the residual
        // plane, modulo 2^64) reads the plane advanced to row first: the same
        // modular prefix, without materialising the residuals (Ikea LEAD's
        // proposal; 0.48-0.73x point time on real delta columns).
        sum += plane_kernels(d.width).sum(k.body + plane_bytes(d.width, first), row - first + 1);
        return d.base + static_cast<u64>(delta_rbase(k)) * steps + sum;
    }
    case kind::arrival: {
        // The frontier at row is F plus the flagged rows before it.
        const auto w = arrival_flags(k);
        const unsigned word = row / 64, bit = row % 64;
        unsigned before = static_cast<unsigned>(std::popcount(w[word] & ((u64{1} << bit) - 1)));
        for (unsigned i = 0; i < word; ++i)
            before += static_cast<unsigned>(std::popcount(w[i]));
        const u64 frontier = d.base + before;
        if (w[word] >> bit & 1)
            return frontier;
        return frontier - 1 - plane_kernels(d.width).point(k.body + arrival_flag_bytes, row - before);
    }
    }
    std::abort();
}

u64 stored_count_eq(const borrowed_block &k, u64 s) {
    const auto &d = k.d;
    alignas(64) u64 buffer[block_rows];
    u64 total = 0;
    switch (d.type) {
    case kind::constant:
        return d.base == s ? block_rows : 0;
    case kind::packed:
    case kind::runs: {
        if (s < d.base)
            return 0;
        const u64 rhs = s - d.base;
        if (above_bound(k, rhs))
            return 0;
        if (d.type == kind::packed) {
            read_plane(k, 0, block_rows, buffer);
            for (std::size_t i = 0; i < block_rows; ++i)
                total += buffer[i] == rhs;
            return total;
        }
        if (run_values_program != run_program::wide)
            return runs_count(k, run_values_program, [rhs](u64 v) { return v == rhs; });
        read_plane(k, 0, d.count, buffer);
        each_run(k, [&](unsigned rank, unsigned size) {
            if (buffer[rank] == rhs)
                total += size;
        });
        return total;
    }
    case kind::arrival: {
        // Every code of the block is below its final frontier; a code in
        // [F, F + L) is introduced by exactly one literal row here.
        const auto w = arrival_flags(k);
        const u64 end = d.base + arrival_literals(w);
        if (s >= end)
            return 0;
        total = s >= d.base;
        each_arrival_reference(k, w, [&](unsigned, u64 code) { total += code == s; });
        return total;
    }
    case kind::delta:
    case kind::patched:
    case kind::delta_patched:
        break;
    }
    decode_stored(k, buffer);
    for (std::size_t i = 0; i < block_rows; ++i)
        total += buffer[i] == s;
    return total;
}

void decode_stored(const borrowed_block &k, u64 *out) {
    const auto &d = k.d;
    switch (d.type) {
    case kind::constant:
        std::fill(out, out + block_rows, d.base);
        return;
    case kind::packed:
        read_plane(k, 0, block_rows, out);
        for (std::size_t i = 0; i < block_rows; ++i)
            out[i] += d.base;
        return;
    case kind::patched: {
        read_plane(k, 0, block_rows, out);
        for (std::size_t i = 0; i < block_rows; ++i)
            out[i] += d.base;
        const auto p = patches(k);
        alignas(64) u64 exc[block_rows];
        read_exceptions(p, exc);
        for (unsigned e = 0; e < p.n; ++e)
            out[p.positions[e]] = p.lo + exc[e];
        return;
    }
    case kind::delta: {
        read_plane(k, 0, block_rows, out);
        const i64 rbase = delta_rbase(k);
        u64 x = d.base;
        out[0] = x;
        for (std::size_t i = 1; i < block_rows; ++i) {
            x += static_cast<u64>(rbase) + out[i];
            out[i] = x;
        }
        return;
    }
    case kind::delta_patched:
        decode_delta_patched(k, out);
        return;
    case kind::arrival:
        decode_arrival(k, out);
        return;
    case kind::runs: {
        alignas(64) u64 values[block_rows];
        read_plane(k, 0, d.count, values);
        std::size_t row = 0;
        each_run(k, [&](unsigned rank, unsigned size) {
            std::fill(out + row, out + row + size, values[rank] + d.base);
            row += size;
        });
        return;
    }
    }
    std::abort();
}
void decode_stored(const column &c, std::size_t b, u64 *out) {
    decode_stored(borrow(c, b), out);
}

// A coded block's stored values are codes; each row's value is
// min + dictionary[code].
void decode_block(const borrowed_block &k, u64 *out) {
    decode_stored(k, out);
    if (!k.coded)
        return;
    for (std::size_t i = 0; i < block_rows; ++i)
        out[i] = k.dictionary.min + dictionary_at(k.dictionary, out[i]);
}
void decode_block(const column &c, std::size_t b, u64 *out) {
    decode_block(borrow(c, b), out);
}

void dictionary_cache::clear() {
    entries.clear();
    last_values = nullptr;
    charged = 0;
}

dictionary_cache::material &dictionary_cache::room(const dictionary_view &p, std::size_t adding) {
    auto it = entries.find(p.edition);
    const std::size_t fresh = it == entries.end() ? identity_bytes : 0;
    if (charged + fresh + adding > capacity_bytes && charged) {
        clear();  // holders keep their own tables alive
        it = entries.end();
    }
    if (it == entries.end()) {
        it = entries.try_emplace(p.edition).first;
        charged += identity_bytes;
    }
    charged += adding;
    return it->second;
}

dictionary_cache::table dictionary_cache::values(const dictionary_view &p) {
    if (auto it = entries.find(p.edition); it != entries.end() && it->second.values)
        return it->second.values;
    auto decoded = std::make_shared<std::vector<u64>>(p.count);
    read_dictionary(p, 0, p.count, decoded->data());
    auto &m = room(p, p.count * sizeof(u64));
    m.values = std::move(decoded);
    last_id = p.edition;
    last_values = m.values->data();
    return m.values;
}

u64 dictionary_cache::entry_slow(const dictionary_view &p, u64 code) {
    const auto held = values(p);
    last_id = p.edition;
    last_values = held->data();
    return last_values[code];
}

const dictionary_cache::slot &dictionary_cache::comparison_slot(const dictionary_view &p, comparison kind, u64 rhs) {
    if (auto it = entries.find(p.edition); it != entries.end()) {
        const auto &m = it->second;
        for (unsigned i = 0; i < m.used; ++i)
            if (m.thresholds[i].kind == kind && m.thresholds[i].rhs == rhs)
                return m.thresholds[i];
    }
    slot made{kind, rhs, 0, 0, nullptr, 0};
    if (p.order == dictionary_order::sorted) {
        // A lower bound over the plane; no decode needed.
        made.first = dictionary_lower_bound(p, rhs);
        if (kind == comparison::eq)
            made.limit = rhs == ~u64{0} ? p.count : dictionary_lower_bound(p, rhs + 1);
    } else {
        const auto held = values(p);
        const u64 *v = held->data();
        auto truth = std::make_shared<std::vector<u64>>((p.count + 63) / 64, 0);
        for (u64 first = 0; first < p.count; first += 64) {
            u64 bits = 0;
            const u64 n = std::min<u64>(64, p.count - first);
            for (u64 i = 0; i < n; ++i) {  // no overflow: at most the part maximum
                const u64 x = p.min + v[first + i];
                bits |= static_cast<u64>(kind == comparison::ge ? x >= rhs : x == rhs) << i;
            }
            (*truth)[first / 64] = bits;
        }
        made.bytes = truth->size() * sizeof(u64);
        made.truth = std::move(truth);
    }
    auto &m = room(p, made.bytes);
    slot &s = m.used < thresholds_per_identity ? m.thresholds[m.used++] : m.thresholds[m.next++ % thresholds_per_identity];
    charged -= s.truth ? s.bytes : 0;  // a replaced slot's table
    s = std::move(made);
    return s;
}

dictionary_cache::threshold_material dictionary_cache::threshold(const dictionary_view &p, u64 t) {
    const slot &s = comparison_slot(p, comparison::ge, t);
    return {s.first, s.truth};
}

dictionary_cache::equality_material dictionary_cache::equality(const dictionary_view &p, u64 v) {
    const slot &s = comparison_slot(p, comparison::eq, v);
    return {s.first, s.limit, s.truth};
}

std::optional<u64> dictionary_cache::code_of(const dictionary_view &p, u64 v) {
    assert(p.injective);
    if (v < p.min)
        return std::nullopt;
    const u64 rhs = v - p.min;
    if (p.width < 64 && rhs >> p.width)
        return std::nullopt;
    std::shared_ptr<const index_table> index;
    if (auto it = entries.find(p.edition); it != entries.end())
        index = it->second.index;
    if (!index) {
        auto made = std::make_shared<index_table>();
        made->values = values(p);
        const u64 *e = made->values->data();
        const std::size_t slots = std::bit_ceil(std::max<std::size_t>(4, p.count + p.count / 2));
        made->slots.assign(slots, UINT32_MAX);
        made->shift = static_cast<unsigned>(64 - std::countr_zero(slots));
        for (std::uint32_t code = 0; code < p.count; ++code)
            for (u64 i = e[code] * 0x9e3779b97f4a7c15u >> made->shift;; i = (i + 1) & (slots - 1))
                if (made->slots[i] == UINT32_MAX) {
                    made->slots[i] = code;
                    break;
                }
        auto &m = room(p, slots * sizeof(std::uint32_t));
        if (!m.values) {  // cleared while building: keep the entries with their index
            m.values = made->values;
            charged += p.count * sizeof(u64);
        }
        m.index = made;
        index = std::move(made);
    }
    const u64 *e = index->values->data();
    const std::size_t mask = index->slots.size() - 1;
    for (u64 i = rhs * 0x9e3779b97f4a7c15u >> index->shift;; i = (i + 1) & mask) {
        const std::uint32_t code = index->slots[i];
        if (code == UINT32_MAX)
            return std::nullopt;
        if (e[code] == rhs)
            return code;
    }
}
}  // namespace frame_encoding
