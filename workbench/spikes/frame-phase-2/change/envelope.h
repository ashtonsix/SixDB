// Experiment-owned physical admission, not a lock manager or a public Frame API.
#pragma once
#include <ikea/seriespack.h>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#include <vector>

namespace frame_change {
namespace sp = ikea::seriespack;
using word = std::uint64_t;
using wide = __int128;

struct range {
    std::uintptr_t first, limit;
};

// Physical addresses deliberately unify aliases from different named sources.
// The owner keeps mappings/interpretation stable between declaration and use.
struct ranges {
    std::vector<range> spans;
    void add(const void* at, std::size_t bytes) {
        if (!bytes) return;
        const auto first = reinterpret_cast<std::uintptr_t>(at);
        spans.push_back({first, first + bytes});
        std::sort(spans.begin(), spans.end(), [](auto a, auto b) { return a.first < b.first; });
        std::size_t n = 0;
        for (auto span : spans) {
            if (n && span.first <= spans[n - 1].limit)
                spans[n - 1].limit = std::max(spans[n - 1].limit, span.limit);
            else spans[n++] = span;
        }
        spans.resize(n);
    }
    void add(const ranges& other) {
        for (auto s : other.spans) add(reinterpret_cast<const void*>(s.first), s.limit - s.first);
    }
    bool contains(const ranges& other) const {
        return std::all_of(other.spans.begin(), other.spans.end(), [&](auto s) {
            return std::any_of(spans.begin(), spans.end(), [&](auto p) {
                return p.first <= s.first && s.limit <= p.limit;
            });
        });
    }
    std::size_t bytes() const {
        std::size_t n = 0;
        for (auto r : spans) n += r.limit - r.first;
        return n;
    }
};

struct envelope {
    ranges reads, stores;
    std::size_t records = 0;
    ranges serialization() const { auto r = reads; r.add(stores); return r; }
    void add(const envelope& other) {
        reads.add(other.reads); stores.add(other.stores); records += other.records;
    }
};
struct grant {
    ranges readable, writable, serialized;
};
enum class refusal { range, value, effects, reads, stores, serialization, kind };
inline std::expected<void, refusal> admit(const envelope& p, const grant& g, std::size_t records) {
    if (!g.readable.contains(p.reads)) return std::unexpected(refusal::reads);
    if (!g.writable.contains(p.stores)) return std::unexpected(refusal::stores);
    if (!g.serialized.contains(p.serialization())) return std::unexpected(refusal::serialization);
    if (p.records > records) return std::unexpected(refusal::effects);
    return {};
}
inline grant full_grant(const envelope& p) { return {p.reads, p.stores, p.serialization()}; }

template <class View> ranges point_reads(const View& view, unsigned row) {
    ranges r;
    sp::visit_point_reads(view, row, [&](const auto* at, std::size_t n) { r.add(at, n); });
    return r;
}
template <class View> envelope point_envelope(const View& view, unsigned row, bool old_value = true) {
    envelope p;
    sp::author::visit_writes_point(view, row, [&](auto s) {
        const auto bytes = view.plane_storage(s.plane);
        p.stores.add(bytes.data() + s.offset, s.size);
        ++p.records;
    });
    // Conservative initialized-read authority for a preserving point setter.
    // Full overwrite fields need not all be read; no minimal-read claim is made.
    p.reads = p.stores;
    if (old_value) p.reads.add(point_reads(view, row));
    return p;
}

struct replacement { unsigned row; word logical; };
template <class Block> struct packed_plan {
    envelope access;
    std::vector<std::pair<Block*, replacement>> changes;
};

// One possible replacement per original coarse block. An excluded block is
// skipped before reading either its command or its view/payload metadata.
template <class Block, std::size_t Extent>
std::expected<packed_plan<Block>, refusal> declare_packed(std::span<Block, Extent> blocks,
        std::span<const replacement> commands, std::uint64_t coarse) {
    packed_plan<Block> p;
    if (blocks.size() > 64) return std::unexpected(refusal::range);
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        if (!(coarse & (word{1} << b))) continue;
        if (b >= commands.size()) return std::unexpected(refusal::range);
        const auto c = commands[b];
        auto& block = blocks[b];
        if (c.row >= block.rows) return std::unexpected(refusal::range);
        if (c.logical < block.base || c.logical - block.base > block.maximum)
            return std::unexpected(refusal::value);
        p.access.add(point_envelope(*block.view, c.row));
        p.changes.push_back({&block, c});
    }
    return p;
}
template <class Block>
std::expected<wide, refusal> apply_packed(const packed_plan<Block>& p, const grant& permission,
                                         ikea::journal& effects) {
    if (effects.used > effects.storage.size()) return std::unexpected(refusal::effects);
    auto accepted = admit(p.access, permission, effects.remaining());
    if (!accepted) return std::unexpected(accepted.error());
    wide contribution = 0;
    for (auto [block, c] : p.changes) {
        const auto old = sp::get_unchecked(*block->view, c.row);
        const auto next = c.logical - block->base;
        contribution += wide(next) - wide(old);
        auto writer = sp::bind_writer(*block->view);
        if (!writer || !writer->set(c.row, next, effects)) std::abort(); // fully admitted above
    }
    return contribution;
}
} // namespace frame_change
