// Private experiment: fitting replacements in an acquired writable delta body.
// No allocator, lock manager, publication, exception insertion or anchor repair.
#pragma once
#include "../point/point_read.h"

namespace frame_lifecycle {
namespace fe = frame_encoding;
namespace sp = ikea::seriespack;
using fe::u64;
using i128 = __int128;
struct damage { std::size_t offset, size; };
struct repair_result {
    enum status { skipped, unchanged, applied, refused } state{refused};
    unsigned residuals{}, checkpoints{};
    i128 logical_change{};
    std::array<damage, 32> writes{};
    unsigned count{};
};
inline void require(bool ok) { if (!ok) std::abort(); }
// Resolve named source identities while their views are alive. These are actual
// coalesced issued-store envelopes, including any preserved neighbours.
template <class View>
void resolve(repair_result &out, const sp::composition::source_write_journal &journal,
             const View &view, std::size_t origin) {
    for (const auto &entry : journal.entries()) {
        require(entry.source == &view && !entry.bytes.plane && out.count < out.writes.size());
        out.writes[out.count++] = {origin + entry.bytes.offset, entry.bytes.size};
    }
}
template <unsigned K>
void store_mark(std::uint8_t *body, std::size_t offset, unsigned n, unsigned row,
                u64 value, repair_result &out) {
    using F = sp::format<K>;
    auto view = sp::view<F, std::uint8_t>::attach(n,
        {{{std::span(body + offset, fe::plane_bytes(K, n)), F::tile_bytes}, {}, {}}});
    require(bool(view));
    auto prepared = sp::bind_mutation(*view);
    require(bool(prepared));
    sp::no_summary summary;
    std::array<sp::composition::owner_write, 16> records;
    sp::composition::source_write_journal journal{records};
    require(bool(prepared->set(row, value, summary, journal)));
    resolve(out, journal, *view, offset);
}

// A coarse skip precedes all column/reader access. The caller owes stable named
// bytes, a coherent reader, and isolation over the complete physical envelope.
// Eight bits of spare checkpoint width bound the sum of <=256 residuals.
template <unsigned K>
[[gnu::noinline]] repair_result repair_delta(fe::column &column,
        const frame_points::point_reader &reader, unsigned row, u64 value,
        bool selected, unsigned available_effects = 32) {
    repair_result out;
    if (!selected) { out.state = repair_result::skipped; return out; }
    if (!row || row >= fe::block_rows || available_effects < out.writes.size()) return out;
    if (column.blocks.size() != 1 || column.options.dictionary != fe::dictionary_order::none)
        return out;
    const auto &d = column.blocks[0];
    if (d.type != fe::kind::delta || d.width != K || d.count != fe::block_rows) return out;
    const auto old = reader.get(row);
    const i128 delta = i128(value) - i128(old);
    if (!delta) { out.state = repair_result::unchanged; return out; }
    auto *body = column.payload.data() + d.offset;
    const auto &ops = fe::plane_kernels(K);
    const i128 current = ops.point(body, row);
    const i128 next = row + 1 < fe::block_rows ? ops.point(body, row + 1) : 0;
    constexpr u64 maximum = K == 64 ? UINT64_MAX : (u64{1} << K) - 1;
    const i128 replacement = current + delta, following = next - delta;
    if (replacement < 0 || replacement > i128(maximum) ||
        (row + 1 < fe::block_rows && (following < 0 || following > i128(maximum))))
        return out; // All value/capacity refusals precede any store.

    using F = sp::format<K>;
    auto view = sp::view<F, std::uint8_t>::attach(fe::block_rows,
        {{{std::span(body, fe::plane_bytes(K, fe::block_rows)), F::tile_bytes}, {}, {}}});
    require(bool(view));
    auto prepared = sp::bind_mutation(*view);
    require(bool(prepared));
    const auto first_capacity = prepared->effect_capacity(row, 1, sp::row_selection::all());
    const auto second_capacity = prepared->effect_capacity(
        row + 1, row + 1 < fe::block_rows, sp::row_selection::all());
    require(first_capacity && second_capacity && *first_capacity + *second_capacity <= 16);
    std::array<sp::composition::owner_write, 16> records;
    sp::composition::source_write_journal journal{records};
    sp::no_summary summary;
    require(bool(prepared->set(row, u64(replacement), summary, journal)));
    if (row + 1 < fe::block_rows)
        require(bool(prepared->set(row + 1, u64(following), summary, journal)));
    resolve(out, journal, *view, d.offset);
    out.residuals = 1 + unsigned(row + 1 < fe::block_rows);

    const auto mode = column.options.delta_checkpoints;
    const unsigned spacing = mode == fe::checkpoints::every64 ? 64 : 16;
    if (mode != fe::checkpoints::none && row + 1 < fe::block_rows &&
        (row + 1) % spacing == 0) {
        const unsigned mark = row / spacing, n = fe::block_rows / spacing - 1;
        const auto offset = fe::plane_bytes(K, fe::block_rows) + 8;
        if constexpr (K + 8 <= 64) {
            if (mode == fe::checkpoints::packed) {
                const auto before = fe::plane_kernels(K + 8).point(body + offset, mark);
                const auto after = before + u64(delta);
                if constexpr (K + 8 < 64) require(after < (u64{1} << (K + 8)));
                store_mark<K + 8>(body, offset, n, mark, after, out);
                // store_mark's relative offsets become offsets in the column.
                for (unsigned i = unsigned(journal.used); i < out.count; ++i)
                    out.writes[i].offset += d.offset;
                out.checkpoints = 1;
            }
        }
        if (!out.checkpoints) {
            constexpr unsigned bytes = K + 8 <= 32 ? 4 : 8;
            auto *at = body + offset + mark * bytes;
            u64 before = 0;
            std::memcpy(&before, at, bytes);
            const auto after = before + u64(delta);
            if constexpr (bytes == 4) require(after <= UINT32_MAX);
            require(out.count < out.writes.size());
            out.writes[out.count++] = {d.offset + offset + mark * bytes, bytes};
            std::memcpy(at, &after, bytes);
            out.checkpoints = 1;
        }
    }
    out.logical_change = delta;
    out.state = repair_result::applied;
    return out;
}
} // namespace frame_lifecycle
