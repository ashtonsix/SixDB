// A distinct point-consumer preparation experiment, not a replacement Frame API.
#pragma once
#include "kernels.h"
#include "point_read.h"

namespace frame_points {
// Construct before borrowing. Encoded bytes and geometry keep their placement;
// erase redundant read bindings. Exception planes carry their own width, so
// the point consumer needs no sparse per-block decoder/index side tables.
struct compact_image {
    fe::column body;

    explicit compact_image(fe::column &&column) : body(std::move(column)) {
        for (std::size_t b = 0; b < body.blocks.size(); ++b) {
            const auto &d = body.blocks[b];
            if (d.type != fe::kind::constant && (!d.width || d.width > 64)) std::abort();
            if (d.type == fe::kind::patched || d.type == fe::kind::delta_patched) {
                const auto p = d.type == fe::kind::patched ? fe::patches(body, b)
                                                         : fe::delta_patches(body, b);
                if (p.n && (!p.value_width || p.value_width > 64)) std::abort();
            }
        }
        const auto release = []<class T>(std::vector<T> &v) { std::vector<T>{}.swap(v); };
        release(body.decoders);
        release(body.d8);
        release(body.d16);
        release(body.d32);
        release(body.exceptions);
        release(body.dictionaries);
    }
    compact_image(const compact_image &) = delete;
    compact_image(compact_image &&) = delete;
};

template <class RunSource> struct compact_reader_program {
    const compact_image *source;
    std::size_t rows;
    bool native_prefix;
    explicit compact_reader_program(const compact_image &image, bool native = false,
                            point_domain domain = point_domain::values)
        : source(&image), rows(image.body.rows), native_prefix(native) {
        if (domain == point_domain::values && image.body.options.dictionary != fe::dictionary_order::none)
            std::abort();
    }
    std::size_t prepared_bytes() const {
        return sizeof(*this);
    }
    fe::u64 get(std::size_t row) const {
        assert(row < rows);
        const auto &c = source->body;
        const auto b = row / fe::block_rows;
        const unsigned local = row % fe::block_rows;
        const auto &d = c.blocks[b];
        if (d.type == fe::kind::constant) return d.base;
        const auto *data = c.payload.data() + d.offset;
        switch (d.type) {
        case fe::kind::constant: return d.base;
        case fe::kind::packed: return d.base + local_kernels(d.width).point(data, local);
        case fe::kind::patched: {
            const auto p = fe::patches(c, b);
            const auto *position = std::lower_bound(p.positions, p.positions + p.n, local);
            if (position != p.positions + p.n && *position == local)
                return p.lo + local_kernels(p.value_width).point(p.values, position - p.positions);
            return d.base + local_kernels(d.width).point(data, local);
        }
        case fe::kind::runs: {
            const unsigned rank = ikea::intervals::upper_bound(RunSource{
                    data + fe::plane_bytes(d.width, d.count), d.count}, local);
            return d.base + local_kernels(d.width).point(data, rank);
        }
        case fe::kind::delta:
        case fe::kind::delta_patched: {
            unsigned steps = local;
            if (d.type == fe::kind::delta_patched) {
                const auto p = fe::delta_patches(c, b);
                const auto *at = std::lower_bound(p.positions, p.positions + p.n, local);
                if (at != p.positions + p.n && *at == local)
                    return p.lo + local_kernels(p.value_width).point(p.values, at - p.positions);
                steps -= static_cast<unsigned>(at - p.positions);
            }
            if (!steps) return d.base;
            const auto mode = c.options.delta_checkpoints;
            const unsigned spacing = mode == fe::checkpoints::none ? 256 :
                                     mode == fe::checkpoints::every64 ? 64 : 16;
            const unsigned first = local / spacing * spacing;
            const auto *tail = data + fe::plane_bytes(d.width, 256);
            fe::i64 rbase;
            std::memcpy(&rbase, tail, 8);
            fe::u64 sum = 0;
            if (first) {
                const unsigned mark = first / spacing - 1;
                if (mode == fe::checkpoints::packed && d.width + 8 <= 64)
                    sum = local_kernels(d.width + 8).point(tail + 8, mark);
                else if (d.width + 8 <= 32) {
                    std::uint32_t value;
                    std::memcpy(&value, tail + 8 + mark * 4, 4);
                    sum = value;
                } else
                    std::memcpy(&sum, tail + 8 + mark * 8, 8);
            }
            // One getter body for both programs. Only this delta leaf changes;
            // non-predicted reads do not get different prologues or branches.
            sum += local_kernels(d.width).prefixes[native_prefix](data, first, local - first + 1);
            return d.base + static_cast<fe::u64>(rbase) * steps + sum;
        }
        }
        std::abort();
    }
};
using compact_reader = compact_reader_program<fe::run_ends>;
using unpredictable_compact_reader = compact_reader_program<ikea::intervals::run_end_source>;

} // namespace frame_points
