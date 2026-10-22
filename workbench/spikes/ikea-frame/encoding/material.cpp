// Compiled once: SeriesPack packing at runtime width (64 instantiations),
// block encoding and whole-block decode for the private Frame material.
#include "material.h"

namespace frame_encoding {
void pack(unsigned k, std::span<const u64> values, std::uint8_t *out) {
    bool done = false;
    sp::detail::each<64>([&](auto index) {
        constexpr unsigned K = decltype(index)::value + 1;
        if (k != K)
            return;
        using F = sp::format<K>;
        const std::size_t bytes = plane_bytes(K, values.size());
        auto v = sp::view<F, std::uint8_t>::attach(
            values.size(), {{{std::span(out, bytes), F::tile_bytes}, {}, {}}});
        if (!v)
            std::abort();
        auto prepared = sp::bind_mutation(*v);
        if (!prepared)
            std::abort();
        auto write = prepared->template erase<u64, sp::no_summary>();
        auto capacity = prepared->construction_effect_capacity();
        if (!capacity)
            std::abort();
        std::vector<sp::composition::owner_write> records(*capacity);
        sp::composition::write_journal effects{records};
        if (!write.initialize(values, effects))
            std::abort();
        done = true;
    });
    if (!done)
        std::abort();
}

const bl::layout &run_layout() {
    static const auto layout = bl::layout::prepare(
        // A runs block never holds one 256-row run (that block is constant), so
        // every run is at most 255 rows and sizes fit u8.
        {.family = bl::geometry::sizes, .maximum = 255, .extent_limit = 256, .immortal = true});
    if (!layout)
        std::abort();
    return *layout;
}

column encode(std::span<const u64> values) {
    column c;
    c.rows = values.size() / block_rows * block_rows;
    const auto &layout = run_layout();
    std::vector<u64> scratch(block_rows);
    std::vector<std::pair<std::size_t, std::size_t>> planes;  // (offset, block)
    for (std::size_t b = 0; b * block_rows < c.rows; ++b) {
        const auto block = values.subspan(b * block_rows, block_rows);
        const auto [lo, hi] = std::minmax_element(block.begin(), block.end());
        descriptor d{*lo, static_cast<std::uint32_t>(c.payload.size()), 0, kind::constant, 0};
        c.runs.emplace_back();
        if (*lo == *hi) {
            c.blocks.push_back(d);
            continue;
        }
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
        const std::size_t packed_cost = plane_bytes(k, block_rows);
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
        const std::size_t delta_cost = plane_bytes(dk, block_rows) + 8 + checkpoint_cost(dk);
        const std::size_t runs_cost =
            plane_bytes(k, run_values.size()) +
            (compact_runs ? layout.directory_bytes() + layout.control_bytes() +
                                layout.lengths_bytes(static_cast<unsigned>(run_sizes.size()))
                          : run_sizes.size());
        if (delta_ok && delta_cost < std::min(packed_cost, runs_cost)) {
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
            c.payload.resize(c.payload.size() + delta_cost);
            pack(dk, scratch, c.payload.data() + d.offset);
            std::memcpy(c.payload.data() + d.offset + plane, &rbase, 8);
            std::uint8_t *stored = c.payload.data() + d.offset + plane + 8;
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
        } else if (runs_cost < packed_cost && !compact_runs) {
            d.type = kind::runs;
            d.count = static_cast<std::uint16_t>(run_values.size());
            d.width = static_cast<std::uint8_t>(k);
            const std::size_t values_bytes = plane_bytes(k, run_values.size());
            c.payload.resize(c.payload.size() + values_bytes + run_sizes.size());
            pack(k, run_values, c.payload.data() + d.offset);
            unsigned end = 0;
            for (std::size_t r = 0; r < run_sizes.size(); ++r) {
                end += run_sizes[r];
                c.payload[d.offset + values_bytes + r] = static_cast<std::uint8_t>(end - 1);
            }
        } else if (runs_cost < packed_cost) {
            auto g = std::make_unique<runs_geometry>();
            g->directory.resize(layout.directory_bytes());
            g->lengths.resize(layout.lengths_bytes(static_cast<unsigned>(run_sizes.size())));
            g->control.resize(layout.control_bytes());
            auto view = bl::view::bind(layout, {g->directory, g->lengths, g->control});
            if (!view)
                std::abort();
            g->view.emplace(*view);
            std::vector<ikea::owner_write> records(8 * (run_sizes.size() + 2));
            ikea::source_write_journal effects{records};
            if (!bl::initialize(*g->view, effects))
                std::abort();
            for (unsigned size : run_sizes) {
                effects.used = 0;
                if (!bl::append(*g->view, size, effects))
                    std::abort();
            }
            d.type = kind::runs;
            d.count = static_cast<std::uint16_t>(run_values.size());
            d.width = static_cast<std::uint8_t>(k);
            c.payload.resize(c.payload.size() + plane_bytes(k, run_values.size()));
            pack(k, run_values, c.payload.data() + d.offset);
            c.runs.back() = std::move(g);
        } else {
            for (std::size_t i = 0; i < block_rows; ++i)
                scratch[i] = block[i] - *lo;
            d.type = kind::packed;
            d.count = block_rows;
            d.width = static_cast<std::uint8_t>(k);
            c.payload.resize(c.payload.size() + packed_cost);
            pack(k, scratch, c.payload.data() + d.offset);
        }
        c.blocks.push_back(d);
    }
    // Bind decoders once, after the payload stops growing.
    c.decoders.reserve(c.blocks.size());
    for (const auto &d : c.blocks) {
        c.d8.emplace_back();
        c.d16.emplace_back();
        c.d32.emplace_back();
        if (d.type == kind::constant) {
            c.decoders.emplace_back(nullptr, 0, nullptr, nullptr, nullptr);
            continue;
        }
        const sp::dense_input input{
            d.width, sp::geometry::local,
            std::span<const std::uint8_t>(c.payload).subspan(d.offset, plane_bytes(d.width, d.count)),
            d.count};
        auto decoder = sp::bind_decoder<u64>(input);
        if (!decoder)
            std::abort();
        c.decoders.push_back(*decoder);
        if (d.width <= 8)
            c.d8.back() = *sp::bind_decoder<std::uint8_t>(input);
        else if (d.width <= 16)
            c.d16.back() = *sp::bind_decoder<std::uint16_t>(input);
        else if (d.width <= 32)
            c.d32.back() = *sp::bind_decoder<std::uint32_t>(input);
    }
    return c;
}

void decode_block(const column &c, std::size_t b, u64 *out) {
    const auto &d = c.blocks[b];
    switch (d.type) {
    case kind::constant:
        std::fill(out, out + block_rows, d.base);
        return;
    case kind::packed:
        c.decoders[b].read_unchecked(0, block_rows, out);
        for (std::size_t i = 0; i < block_rows; ++i)
            out[i] += d.base;
        return;
    case kind::delta: {
        c.decoders[b].read_unchecked(0, block_rows, out);
        i64 rbase;
        std::memcpy(&rbase, c.payload.data() + d.offset + plane_bytes(d.width, block_rows), 8);
        u64 x = d.base;
        out[0] = x;
        for (std::size_t i = 1; i < block_rows; ++i) {
            x += static_cast<u64>(rbase) + out[i];
            out[i] = x;
        }
        return;
    }
    case kind::runs: {
        alignas(64) u64 values[block_rows];
        c.decoders[b].read_unchecked(0, d.count, values);
        std::size_t row = 0;
        each_run(c, b, [&](unsigned rank, unsigned size) {
            std::fill(out + row, out + row + size, values[rank] + d.base);
            row += size;
        });
        return;
    }
    }
    std::abort();
}
}  // namespace frame_encoding
