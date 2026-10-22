// Private Frame point consumer of Claude's actual encoding material.
// This is not a proposed Frame API. Retain the column and its placement.
#pragma once
#include "material.h"
#include <ikea/intervals.h>

namespace frame_points {
namespace fe = frame_encoding;
enum class point_domain { values, stored_codes };

template <class RunSource> struct point_reader_program {
    const fe::column *source;
    std::size_t rows;
    std::vector<std::optional<fe::sp::decoder<fe::u64>>> packed_checkpoints;

    explicit point_reader_program(const fe::column &c, point_domain domain = point_domain::values)
        : source(&c), rows(c.rows) {
        if (domain == point_domain::values && c.options.dictionary != fe::dictionary_order::none)
            std::abort(); // Dictionary composition must be explicit, once around ready work.
        if (c.options.delta_checkpoints != fe::checkpoints::packed) return;
        if (std::none_of(c.blocks.begin(), c.blocks.end(), [](const auto &d) {
                return (d.type == fe::kind::delta || d.type == fe::kind::delta_patched) &&
                       d.width + 8 <= 64;
            })) return;
        packed_checkpoints.resize(c.blocks.size());
        for (std::size_t b = 0; b < c.blocks.size(); ++b) {
            const auto &d = c.blocks[b];
            if ((d.type != fe::kind::delta && d.type != fe::kind::delta_patched) ||
                d.width + 8 > 64) continue;
            const auto *bytes = c.payload.data() + d.offset + fe::plane_bytes(d.width, 256) + 8;
            auto bound = fe::sp::bind_decoder<fe::u64>(fe::sp::dense_input{
                unsigned(d.width) + 8, fe::sp::geometry::local,
                std::span(bytes, fe::plane_bytes(d.width + 8, 15)), 15});
            if (!bound) std::abort();
            packed_checkpoints[b] = *bound;
        }
    }
    std::size_t prepared_bytes() const {
        return sizeof(*this) + packed_checkpoints.capacity() * sizeof(packed_checkpoints[0]);
    }

    fe::u64 get(std::size_t row) const {
        assert(row < rows);
        const auto &c = *source;
        const auto b = row / fe::block_rows;
        const unsigned local = row % fe::block_rows;
        const auto &d = c.blocks[b];
        switch (d.type) {
        case fe::kind::constant: return d.base;
        case fe::kind::packed: return d.base + c.decoders[b].get_unchecked(local);
        case fe::kind::patched: {
            const auto p = fe::patches(c, b);
            const auto *position = std::lower_bound(p.positions, p.positions + p.n, local);
            if (position != p.positions + p.n && *position == local)
                return p.lo + c.exceptions[b]->get_unchecked(position - p.positions);
            return d.base + c.decoders[b].get_unchecked(local);
        }
        case fe::kind::runs: {
            const unsigned rank = ikea::intervals::upper_bound(RunSource{
                    c.payload.data() + d.offset + fe::plane_bytes(d.width, d.count), d.count}, local);
            return d.base + c.decoders[b].get_unchecked(rank);
        }
        case fe::kind::delta:
        case fe::kind::delta_patched: {
            unsigned steps = local;
            if (d.type == fe::kind::delta_patched) {
                const auto p = fe::delta_patches(c, b);
                const auto *at = std::lower_bound(p.positions, p.positions + p.n, local);
                if (at != p.positions + p.n && *at == local)
                    return p.lo + c.exceptions[b]->get_unchecked(at - p.positions);
                steps -= static_cast<unsigned>(at - p.positions);
            }
            if (!steps) return d.base; // First chain row, possibly after leading exceptions.
            const auto mode = c.options.delta_checkpoints;
            const unsigned spacing = mode == fe::checkpoints::none ? 256 :
                                     mode == fe::checkpoints::every64 ? 64 : 16;
            const unsigned first = local / spacing * spacing;
            const auto *tail = c.payload.data() + d.offset + fe::plane_bytes(d.width, 256);
            fe::i64 rbase;
            std::memcpy(&rbase, tail, 8);
            fe::u64 sum = 0;
            if (first) {
                const unsigned mark = first / spacing - 1;
                if (mode == fe::checkpoints::packed && d.width + 8 <= 64)
                    sum = packed_checkpoints[b]->get_unchecked(mark);
                else if (d.width + 8 <= 32) {
                    std::uint32_t value;
                    std::memcpy(&value, tail + 8 + mark * 4, 4);
                    sum = value;
                } else
                    std::memcpy(&sum, tail + 8 + mark * 8, 8);
            }
            // Read only the local prefix. Row zero's residual is physically
            // present and zero; whole-block materialisation is not the control.
            alignas(64) fe::u64 residuals[fe::block_rows];
            c.decoders[b].read_unchecked(first, local - first + 1, residuals);
            for (unsigned i = 0; i <= local - first; ++i) sum += residuals[i];
            return d.base + static_cast<fe::u64>(rbase) * steps + sum;
        }
        default: std::abort(); // A new owner kind needs separate reader qualification.
        }
    }
};

// Curated programs over the same bytes; no per-getter policy flag/load.
using point_reader = point_reader_program<fe::run_ends>;
using unpredictable_point_reader = point_reader_program<ikea::intervals::run_end_source>;

// Restore a dictionary edition after any existing stored-value reader. The
// unencoded getter remains the same body; no dictionary branch enters its hot
// path. Each wrapper borrows its own coherent image, including mixed raw/coded
// blocks in a part. Shared width operations need no new dictionary decoder.
template <class Reader> struct dictionary_points {
    const fe::column *source;
    const Reader *stored;
    std::size_t rows;
    dictionary_points(const fe::column &column, const Reader &reader)
        : source(&column), stored(&reader), rows(reader.rows) {}
    fe::u64 get(std::size_t row) const {
        const auto value = stored->get(row);
        const auto block = row / fe::block_rows;
        if (!fe::is_coded(*source, block)) return value;
        const auto &p = fe::part_of(*source, block);
        assert(value < p.count);
        return p.min + fe::plane_kernels(p.width).point(source->payload.data() + p.offset, value);
    }
};

// Admitted original coordinates; coarse selection excludes complete blocks.
// None consults only the mask and cached rows, never column descriptors/body.
template <class Reader, class Sink>
void selected_points(const Reader &reader, const fe::block_mask &mask,
                     std::span<const std::size_t> rows, Sink &&sink) {
    for (const auto row : rows) {
        assert(row < reader.rows);
        const auto block = row / fe::block_rows;
        if ((mask.words[block / 64] >> (block % 64)) & 1)
            sink(row, reader.get(row));
    }
}
} // namespace frame_points
