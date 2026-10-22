// A distinct point-consumer preparation experiment, not a replacement Frame API.
#pragma once
#include "kernels.h"
#include "point_read.h"

namespace frame_points {
// Construct before borrowing. Encoded bytes and geometry keep their placement;
// erase only redundant read bindings. Exception bindings remain sparse because
// the current experimental body stores their width only in that binding.
struct compact_image {
    fe::column body;
    std::vector<std::uint32_t> exception_index;
    std::vector<fe::sp::decoder<fe::u64>> exceptions;

    explicit compact_image(fe::column &&column) : body(std::move(column)) {
        std::size_t patches = 0;
        for (const auto &d : body.blocks) {
            if (d.type != fe::kind::constant && (!d.width || d.width > 64)) std::abort();
            patches += d.type == fe::kind::patched;
        }
        if (patches) {
            exception_index.resize(body.blocks.size());
            exceptions.reserve(patches);
            for (std::size_t b = 0; b < body.blocks.size(); ++b)
                if (body.blocks[b].type == fe::kind::patched) {
                    if (exceptions.size() >= UINT32_MAX) std::abort();
                    exception_index[b] = static_cast<std::uint32_t>(exceptions.size());
                    exceptions.push_back(*body.exceptions[b]);
                }
        }
        const auto release = []<class T>(std::vector<T> &v) { std::vector<T>{}.swap(v); };
        release(body.decoders);
        release(body.d8);
        release(body.d16);
        release(body.d32);
        release(body.exceptions);
        if (!body.options.compact_runs) release(body.runs);
    }
    compact_image(const compact_image &) = delete;
    compact_image(compact_image &&) = delete;
};

struct compact_reader {
    const compact_image *source;
    std::size_t rows;
    explicit compact_reader(const compact_image &image) : source(&image), rows(image.body.rows) {}
    std::size_t prepared_bytes() const {
        return sizeof(*this) + source->exception_index.capacity() * sizeof(std::uint32_t) +
               source->exceptions.capacity() * sizeof(source->exceptions[0]);
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
                return p.lo + source->exceptions[source->exception_index[b]]
                    .get_unchecked(position - p.positions);
            return d.base + local_kernels(d.width).point(data, local);
        }
        case fe::kind::runs: {
            const unsigned rank = c.options.compact_runs
                ? fe::bl::reverse(*c.runs[b]->view, local)
                : fe::bl::upper_bound(fe::run_ends{
                    data + fe::plane_bytes(d.width, d.count), d.count}, local);
            return d.base + local_kernels(d.width).point(data, rank);
        }
        case fe::kind::delta: {
            if (!local) return d.base;
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
            alignas(64) fe::u64 residuals[fe::block_rows];
            local_kernels(d.width).range(data, first, local - first + 1, residuals);
            for (unsigned i = 0; i <= local - first; ++i) sum += residuals[i];
            return d.base + static_cast<fe::u64>(rbase) * local + sum;
        }
        }
        std::abort();
    }
};
} // namespace frame_points
