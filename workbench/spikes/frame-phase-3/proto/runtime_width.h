// The pack leaf over SeriesPack's compiled-once runtime-width operations
// (ikea 0d44d527: bind_range_reader, runtime set and footprint). One class
// serves every width, so Frame compiles no width-specialised kernels; it is
// the same width_array seam as widths.h, selected with FRAME3_LEAF=runtime.
#pragma once
#include "stages.h"

#include <numeric>

namespace frame3 {
class runtime_width_array final : public width_array {
    unsigned K_;
    sp::geometry geometry_;
    std::vector<ikea::byte> storage_;  // owned bytes, or empty when placed
    std::span<const ikea::byte> bytes_;
    std::span<ikea::byte> writable_;
    std::size_t rows_ = 0;
    std::optional<sp::erased_reader<u64>> reader_;
    std::optional<sp::dense_output> output_;  // the effect source for writes
    std::size_t exact_rows_;                  // rows a modulo-2^64 sum covers

    unsigned tile_rows() const { return geometry_ == sp::geometry::local ? 8 : 32 * (8 / std::gcd(K_ % 8, 8u)); }
    u128 range_sum(std::size_t first, std::size_t n) const {
        u128 s = 0;
        if (exact_rows_) {
            for (std::size_t at = 0; at < n; at += exact_rows_)
                s += reader_->sum_unchecked(first + at, std::min(exact_rows_, n - at));
            return s;
        }
        alignas(64) u64 x[block_rows];
        for (std::size_t at = 0; at < n; at += block_rows) {
            const std::size_t m = std::min<std::size_t>(block_rows, n - at);
            reader_->read_unchecked(first + at, m, x);
            for (std::size_t i = 0; i < m; ++i)
                s += x[i];
        }
        return s;
    }
    static void masks_of(const bitset256 &r, u16 *masks) {
        for (unsigned k = 0; k < 16; ++k)
            masks[k] = static_cast<u16>(r.words[k / 4] >> (k % 4 * 16));
    }
    u128 masked(std::size_t first, std::size_t n, const bitset256 &pattern) const {
        u16 masks[16];
        masks_of(pattern, masks);
        if (exact_rows_) {
            u128 s = 0;
            // Chunks start on block boundaries, so the 16-mask period lines up.
            for (std::size_t at = 0; at < n; at += exact_rows_)
                s += reader_->sum_masked_unchecked(first + at, std::min(exact_rows_, n - at), masks, 16);
            return s;
        }
        alignas(64) u64 x[block_rows];
        u128 s = 0;
        for (std::size_t at = 0; at < n; at += block_rows) {
            const std::size_t m = std::min<std::size_t>(block_rows, n - at);
            reader_->read_unchecked(first + at, m, x);
            for (std::size_t i = 0; i < m; ++i)
                s += pattern.contains(static_cast<unsigned>(i)) ? x[i] : 0;
        }
        return s;
    }

  public:
    runtime_width_array(unsigned K, sp::geometry g, const std::vector<u64> &values) : K_(K), geometry_(g) {
        storage_.resize(bytes_for(K, g, values.size()));
        sp::dense_output target{K, g, storage_, values.size()};
        std::vector<ikea::journal::entry> records(*sp::effect_capacity_initialize(target));
        ikea::journal effects{records};
        if (!sp::initialize(target, std::span<const u64>(values), effects))
            std::abort();
        place(values.size(), storage_, storage_);
    }
    runtime_width_array(unsigned K, sp::geometry g, std::span<const ikea::byte> bytes, std::size_t rows, std::span<ikea::byte> writable)
        : K_(K), geometry_(g) {
        place(rows, bytes, writable);
    }
    static std::size_t bytes_for(unsigned K, sp::geometry g, std::size_t rows) {
        const unsigned tr = g == sp::geometry::local ? 8 : 32 * (8 / std::gcd(K % 8, 8u));
        return (rows + tr - 1) / tr * (tr * K / 8);
    }
    void place(std::size_t rows, std::span<const ikea::byte> bytes, std::span<ikea::byte> writable) {
        rows_ = rows, bytes_ = bytes, writable_ = writable;
        exact_rows_ = K_ <= 48 ? part_rows : K_ <= 55 ? (std::size_t{1} << (64 - K_)) / block_rows * block_rows : 0;
        auto r = sp::bind_range_reader<u64>(sp::dense_input{K_, geometry_, bytes, rows});
        if (!r)
            std::abort();
        reader_.emplace(*r);
        if (!writable.empty())
            output_.emplace(sp::dense_output{K_, geometry_, writable, rows});
    }
    std::span<const ikea::byte> image() const override { return bytes_; }
    std::size_t rows() const override { return rows_; }
    std::size_t bytes() const override { return bytes_.size(); }
    u64 digest() const override { return hash_bytes(0xcbf29ce484222325, bytes_.data(), bytes_.size()); }
    point_kernel point() const override {
        return {reader_->point_kernel(), reader_->resource(), bytes_.data(), tile_rows(), tile_rows() * K_ / 8};
    }
    u128 sum(const bitset256 &members, const bitset256 &blocks) const override {
        if (members == blocks)
            return range_sum(0, rows_);
        u128 s = 0;
        unsigned slot = 0, run_first = 0, run = 0;
        each(members, [&](unsigned b) {
            if (blocks.contains(b)) {
                if (!run)
                    run_first = slot;
                ++run;
            } else if (run) {
                s += range_sum(std::size_t{run_first} * block_rows, std::size_t{run} * block_rows), run = 0;
            }
            ++slot;
        });
        if (run)
            s += range_sum(std::size_t{run_first} * block_rows, rows_ - std::size_t{run_first} * block_rows);
        return s;
    }
    u128 sum_selected(const bitset256 &blocks, const u8 *slot, const u16 *rows, const block_rows_set &selected) const override {
        u128 s = 0;
        each(blocks, [&](unsigned b) { s += masked(std::size_t{slot[b]} * block_rows, rows[b], selected[b]); });
        return s;
    }
    u128 sum_pattern(const bitset256 &members, const bitset256 &blocks, const bitset256 &pattern) const override {
        u128 s = 0;
        unsigned slot = 0, run_first = 0, run = 0;
        each(members, [&](unsigned b) {
            if (blocks.contains(b)) {
                if (!run)
                    run_first = slot;
                ++run;
            } else if (run) {
                s += masked(std::size_t{run_first} * block_rows, std::size_t{run} * block_rows, pattern), run = 0;
            }
            ++slot;
        });
        if (run)
            s += masked(std::size_t{run_first} * block_rows, rows_ - std::size_t{run_first} * block_rows, pattern);
        return s;
    }
    u64 count(comparison op, const bitset256 &blocks, const u8 *slot, const u16 *rows, const block_args &rhs) const override {
        u64 c = 0;
        each(blocks, [&](unsigned b) { c += reader_->count_unchecked(op, std::size_t{slot[b]} * block_rows, rows[b], rhs[b]); });
        return c;
    }
    void filter(comparison op, const bitset256 &blocks, const u8 *slot, const u16 *rows, const block_args &rhs, block_rows_set &out) const override {
        each(blocks, [&](unsigned b) {
            if (rows[b] < block_rows)
                out[b] = {};
            reader_->filter_words_unchecked(op, std::size_t{slot[b]} * block_rows, rows[b], rhs[b], out[b].words.data());
        });
    }
    u64 get(std::size_t row) const override { return reader_->get_unchecked(row); }
    void read(std::size_t first, unsigned n, u64 *out) const override { reader_->read_unchecked(first, n, out); }
    void declare(std::size_t row, envelope &e) const override {
        const auto f = sp::footprint(*output_, row);
        if (!f)
            std::abort();
        for (unsigned i = 0; i < f->store_count; ++i)
            e.stores.add(writable_.data() + f->stores[i].offset, f->stores[i].size), ++e.records;
        // A preserving store reads what it stores, and the old value is read.
        for (unsigned i = 0; i < f->store_count; ++i)
            e.reads.add(writable_.data() + f->stores[i].offset, f->stores[i].size);
        for (unsigned i = 0; i < f->read_count; ++i)
            e.reads.add(writable_.data() + f->reads[i].offset, f->reads[i].size);
    }
    bool set(std::size_t row, u64 value, ikea::journal &effects) override { return output_ && sp::set(*output_, row, value, effects).has_value(); }
    const ikea::byte *resolve(const ikea::journal::entry &e) const override {
        return output_ && e.source == &*output_ ? output_->plane_storage(e.plane).data() + e.offset : nullptr;
    }
};
}  // namespace frame3
