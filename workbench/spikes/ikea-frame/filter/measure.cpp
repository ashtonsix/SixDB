// Whole-image COUNT, actual result consumption; warm local exploration only.
#include "evidence.h"
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
namespace ff = frame_filter;
namespace fe = frame_encoding;
using ff::u64;
using Clock = std::chrono::steady_clock;
void need(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
std::vector<u64> load(const char *path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    need(bool(input), "cannot open input");
    const auto bytes = static_cast<std::size_t>(input.tellg());
    need(bytes && bytes % (256 * sizeof(u64)) == 0, "expected complete u64 blocks");
    std::vector<u64> values(bytes / sizeof(u64));
    input.seekg(0);
    need(bool(input.read(reinterpret_cast<char *>(values.data()), bytes)), "short input");
    return values;
}
template <class T> u64 count_native(const std::vector<T> &values, const fe::block_mask &mask,
                                    u64 rhs) {
    if (rhs > std::numeric_limits<T>::max()) return 0;
    const T cutoff = static_cast<T>(rhs);
    u64 total = 0;
    if (mask.popcount() == mask.blocks) {
        // Strong unmasked control: don't impose Frame's block dispatch on a
        // contiguous native array when the whole population is selected.
        for (auto value : values) total += value >= cutoff;
        return total;
    }
    fe::for_each_block(mask, [&](std::size_t b) {
        u64 n = 0;
        const auto *p = values.data() + b * fe::block_rows;
        for (unsigned i = 0; i < fe::block_rows; ++i) n += p[i] >= cutoff;
        total += n;
    });
    return total;
}
struct native_for {
    u64 base, maximum;
    unsigned width;
    bool ordered;
    std::vector<std::uint8_t> v8;
    std::vector<std::uint16_t> v16;
    std::vector<std::uint32_t> v32;
    std::vector<u64> v64;
    template <class T> void fill(std::vector<T> &out, std::span<const u64> in) {
        out.reserve(in.size());
        for (auto x : in) out.push_back(T(x - base));
    }
    explicit native_for(std::span<const u64> values) {
        const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
        base = *lo; maximum = *hi; width = fe::width_of(*hi - *lo);
        ordered = std::is_sorted(values.begin(), values.end());
        if (width <= 8) fill(v8, values);
        else if (width <= 16) fill(v16, values);
        else if (width <= 32) fill(v32, values);
        else fill(v64, values);
    }
    u64 count(const fe::block_mask &mask, u64 rhs) const {
        if (rhs <= base) return mask.popcount() * fe::block_rows;
        if (rhs > maximum) return 0;
        rhs -= base;
        if (width < 64 && rhs >> width) return 0;
        if (!v8.empty()) return count_native(v8, mask, rhs);
        if (!v16.empty()) return count_native(v16, mask, rhs);
        if (!v32.empty()) return count_native(v32, mask, rhs);
        return count_native(v64, mask, rhs);
    }
    u64 count_ordered(const fe::block_mask &mask, u64 rhs) const {
        assert(ordered);
        if (rhs <= base) return mask.popcount() * fe::block_rows;
        if (rhs > maximum) return 0;
        rhs -= base;
        const auto cut = [&](const auto &values) {
            return std::size_t(std::lower_bound(values.begin(), values.end(), rhs) - values.begin());
        };
        const auto first = !v8.empty() ? cut(v8) : !v16.empty() ? cut(v16) :
                           !v32.empty() ? cut(v32) : cut(v64);
        u64 count = 0;
        if (first % fe::block_rows) {
            const auto block = first / fe::block_rows;
            if ((mask.words[block / 64] >> (block % 64)) & 1)
                count += fe::block_rows - first % fe::block_rows;
        }
        const auto full = (first + fe::block_rows - 1) / fe::block_rows;
        for (auto word = full / 64; word < mask.words.size(); ++word) {
            const auto selected = mask.words[word] &
                (word == full / 64 ? UINT64_MAX << (full % 64) : UINT64_MAX);
            count += std::popcount(selected) * fe::block_rows;
        }
        return count;
    }
    std::size_t bytes() const {
        return sizeof(*this) + v8.capacity() + v16.capacity() * 2 +
               v32.capacity() * 4 + v64.capacity() * 8;
    }
};
struct arm {
    std::string name;
    std::function<u64(const fe::block_mask &, u64)> count;
};
template <class T> std::size_t capacity_bytes(const std::vector<T> &values) {
    return values.capacity() * sizeof(T);
}
std::size_t retained_bytes(const fe::column &column) {
    return sizeof(column) + capacity_bytes(column.blocks) + capacity_bytes(column.payload) +
           capacity_bytes(column.parts) + capacity_bytes(column.coded) +
           capacity_bytes(column.decoders) + capacity_bytes(column.d8) +
           capacity_bytes(column.d16) + capacity_bytes(column.d32) +
           capacity_bytes(column.exceptions) + capacity_bytes(column.dictionaries);
}
// Independent descriptive trace of complete sixteen-run/value windows.
// This is predicate topology, not measured traffic or branch counters.
void topology(const fe::column &c, const fe::block_mask &mask, u64 threshold,
              unsigned heads, ff::split_stage stage) {
    std::size_t eligible = 0, windows = 0, ties0 = 0, ties1 = 0;
    std::array<u64, 256> values;
    fe::for_each_block(mask, [&](std::size_t b) {
        const auto &d = c.blocks[b];
        if (fe::is_coded(c, b)) return;
        if (d.type != fe::kind::packed && d.type != fe::kind::runs) return;
        const unsigned width = stage == ff::split_stage::after_for ? d.width : 64;
        if (width < heads || d.base >= threshold) return;
        auto rhs = threshold - d.base;
        if (d.width < 64 && rhs >> d.width) return;
        if (stage == ff::split_stage::working64) rhs = threshold;
        ++eligible;
        c.decoders[b].read_unchecked(0, d.count, values.data());
        for (unsigned first = 0; first + 16 <= d.count; first += 16) {
            bool tie0 = false, tie1 = false;
            for (unsigned j = first; j < first + 16; ++j) {
                const u64 value = values[j] + (stage == ff::split_stage::working64 ? d.base : 0);
                tie0 |= (value >> (width - 8)) == (rhs >> (width - 8));
                if (heads == 16)
                    tie1 |= (value >> (width - 16)) == (rhs >> (width - 16));
            }
            ++windows; ties0 += tie0; ties1 += tie1;
        }
    });
    std::cout << "{\"type\":\"topology\",\"heads\":" << heads
        << ",\"stage\":\"" << (stage == ff::split_stage::after_for ? "after-for" : "working64")
        << "\",\"eligible_blocks\":" << eligible << ",\"complete16_windows\":" << windows
        << ",\"head0_tie_windows\":" << ties0 << ",\"head01_tie_windows\":" << ties1 << "}\n";
}
void run(const std::vector<u64> &values, unsigned trials, bool reverse) {
    auto column = fe::encode(values);
    native_for native(values);
    std::vector<std::unique_ptr<ff::image>> images;
    std::vector<arm> arms;
    arms.push_back({"decode-control", [&](const auto &m, u64 q) { return fe::encoded_count_ge(column, m, q); }});
    arms.push_back({"native64", [&](const auto &m, u64 q) { return count_native(values, m, q); }});
    arms.push_back({"native-global-for", [&](const auto &m, u64 q) { return native.count(m, q); }});
    if (native.ordered)
        arms.push_back({"native-ordered-for", [&](const auto &m, u64 q) { return native.count_ordered(m, q); }});
    for (auto stage : {ff::split_stage::after_for, ff::split_stage::working64}) {
        for (auto heads : {0u, 8u, 16u}) {
            if (stage == ff::split_stage::working64 && !heads) continue;
            const auto start = Clock::now();
            images.push_back(std::make_unique<ff::image>(column, heads, stage));
            const auto conversion_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
            auto *image = images.back().get();
            const auto name = std::string(stage == ff::split_stage::after_for ? "for-" : "working64-") + std::to_string(heads);
            std::cout << "{\"type\":\"conversion\",\"arm\":\"" << name << "\",\"ns\":"
                      << conversion_ns << ",\"extra_split_bytes\":" << image->split_bytes()
                      << ",\"extra_split_metadata_bytes\":" << image->split_metadata_bytes()
                      << ",\"extra_retained_bytes\":" << image->retained_bytes() << "}\n";
            arms.push_back({name + "-ordinary", [image](const auto &m, u64 q) { return image->count_ge(m, q, false); }});
            if (heads) arms.push_back({name + "-guarded", [image](const auto &m, u64 q) { return image->count_ge(m, q, true); }});
        }
    }
    const auto part8_start = Clock::now();
    ff::logical_heads part8(column, 8);
    const auto part8_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - part8_start).count();
    const auto part16_start = Clock::now();
    ff::logical_heads part16(column, 16);
    const auto part16_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - part16_start).count();
    const auto bounds_start = Clock::now();
    ff::window_bounds bounds(column);
    const auto bounds_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - bounds_start).count();
    const auto block_start = Clock::now();
    ff::block_bounds coarse_bounds(column);
    const auto block_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - block_start).count();
    for (auto [name, ns, bytes] : {std::tuple{"part-head8", part8_ns, part8.bytes()},
                                  std::tuple{"part-head16", part16_ns, part16.bytes()},
                                  std::tuple{"window-q8-bounds", bounds_ns, bounds.bytes()},
                                  std::tuple{"block-bounds", block_ns, coarse_bounds.bytes()}})
        std::cout << "{\"type\":\"conversion\",\"arm\":\"" << name << "\",\"ns\":" << ns
                  << ",\"extra_evidence_retained_bytes\":" << bytes << "}\n";
    arms.push_back({"part-head8", [&](const auto &m, u64 q) { return part8.count_ge(m, q); }});
    arms.push_back({"part-head16", [&](const auto &m, u64 q) { return part16.count_ge(m, q); }});
    arms.push_back({"window-q8-bounds", [&](const auto &m, u64 q) { return bounds.count_ge(m, q); }});
    arms.push_back({"block-bounds", [&](const auto &m, u64 q) { return coarse_bounds.count_ge(m, q); }});
    if (reverse) std::reverse(arms.begin(), arms.end());
    std::array<std::size_t, 5> kinds{};
    for (const auto &d : column.blocks) {
        need(unsigned(d.type) < kinds.size(), "unqualified material kind");
        ++kinds[unsigned(d.type)];
    }
    std::cout << "{\"type\":\"material\",\"rows\":" << values.size()
              << ",\"original_body_bytes\":" << column.payload.size()
              << ",\"descriptor_bytes\":" << column.descriptor_bytes()
              << ",\"original_retained_bytes\":" << retained_bytes(column)
              << ",\"native_global_for_retained_bytes\":" << native.bytes()
              << ",\"native64_retained_bytes\":" << sizeof(values) + values.capacity() * sizeof(u64)
              << ",\"kind_blocks\":[";
    for (unsigned i = 0; i < kinds.size(); ++i) std::cout << (i ? "," : "") << kinds[i];
    std::cout << "]}\n";
    std::vector<u64> sorted(values);
    std::sort(sorted.begin(), sorted.end());
    const std::array queries{sorted.front(), sorted[sorted.size()/4], sorted[sorted.size()/2], UINT64_MAX};
    for (unsigned pattern = 0; pattern < 2; ++pattern) {
        fe::block_mask mask(column.blocks.size());
        for (std::size_t b = 0; b < mask.blocks; ++b) if (!pattern || b % 2) mask.set(b);
        for (unsigned q = 0; q < queries.size(); ++q) {
            const auto rhs = queries[q];
            const auto expected = count_native(values, mask, rhs);
            std::cout << "{\"type\":\"query\",\"mask\":" << pattern << ",\"query\":" << q
                      << ",\"threshold\":" << rhs << ",\"answer\":" << expected << "}\n";
            for (auto stage : {ff::split_stage::after_for, ff::split_stage::working64})
                for (unsigned heads : {8u,16u}) topology(column, mask, rhs, heads, stage);
            for (auto &a : arms) need(a.count(mask, rhs) == expected, "whole-image prewarm/oracle differs");
            for (unsigned trial = 0; trial < trials; ++trial)
                for (unsigned offset = 0; offset < arms.size(); ++offset) {
                    auto &a = arms[(trial + offset) % arms.size()];
                    const auto start = Clock::now();
                    auto result = a.count(mask, rhs);
                    fe::consume(result);
                    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
                    need(result == expected, "timed consumer differs");
                    std::cout << "{\"type\":\"sample\",\"arm\":\"" << a.name << "\",\"trial\":" << trial
                              << ",\"mask\":" << pattern << ",\"query\":" << q << ",\"ns\":" << ns
                              << ",\"selected_rows\":" << mask.popcount() * 256 << ",\"answer\":" << result << "}\n";
                }
        }
    }
}
} // namespace
int main(int argc, char **argv) {
    need(argc >= 2, "usage: frame_filter_measure input.u64 [trials] [reverse]");
    run(load(argv[1]), argc > 2 ? std::stoul(argv[2]) : 7, argc > 3 && std::string_view(argv[3]) == "reverse");
}
