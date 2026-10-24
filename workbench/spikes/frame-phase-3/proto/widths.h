// Width-specialised SeriesPack arrays for the pack stage. Include only from
// the widths_*.cpp TUs, which instantiate them once per preset and width.
#pragma once
#include "stages.h"

namespace frame3 {
template <unsigned K, sp::preset P> class width_array_at final : public width_array {
    using L = sp::preset_layout<K, P>;
    std::vector<ikea::byte> storage_;  // owned bytes, or empty when placed
    std::span<const ikea::byte> bytes_;  // the array's bytes either way
    std::optional<sp::const_view<L>> view_;
    std::optional<decltype(sp::bind_reader(std::declval<const sp::const_view<L> &>()))> reader_;
    // Writes go through a mutable view of the same bytes; it is the effect source.
    std::optional<sp::view<L>> mutable_;
    std::optional<std::remove_cvref_t<decltype(*sp::bind_writer(std::declval<const sp::view<L> &>()))>> writer_;
    // Rows a modulo-2^64 range sum can cover without wrapping.
    static constexpr std::size_t exact_rows = K <= 48 ? part_rows : K <= 55 ? (std::size_t{1} << (64 - K)) / block_rows * block_rows : 0;

    template <comparison Op> u64 count_as(const bitset256 &blocks, const u8 *slot, const u16 *rows, const block_args &rhs) const {
        u64 c = 0;
        each(blocks, [&](unsigned b) { c += reader_->template count_unchecked<Op>(std::size_t{slot[b]} * block_rows, rows[b], rhs[b]); });
        return c;
    }
    template <comparison Op> void filter_as(const bitset256 &blocks, const u8 *slot, const u16 *rows, const block_args &rhs, block_rows_set &out) const {
        each(blocks, [&](unsigned b) {
            if (rows[b] < block_rows)
                out[b] = {};
            reader_->template filter_words_unchecked<Op>(std::size_t{slot[b]} * block_rows, rows[b], rhs[b], out[b].words.data());
        });
    }
    u128 range_sum(std::size_t first, std::size_t n) const {
        u128 s = 0;
        if constexpr (exact_rows != 0) {
            for (std::size_t at = 0; at < n; at += exact_rows)
                s += reader_->sum_unchecked(first + at, std::min(exact_rows, n - at));
        } else {
            alignas(64) u64 x[block_rows];
            for (std::size_t at = 0; at < n; at += block_rows) {
                const std::size_t m = std::min<std::size_t>(block_rows, n - at);
                reader_->read_unchecked(first + at, m, x);
                for (std::size_t i = 0; i < m; ++i)
                    s += x[i];
            }
        }
        return s;
    }

  public:
    explicit width_array_at(const std::vector<u64> &values) : storage_((values.size() + L::tile_rows - 1) / L::tile_rows * L::tile_bytes) {
        const sp::dense_output target{K, L::storage, storage_, values.size()};
        std::vector<ikea::journal::entry> records(*sp::effect_capacity_initialize(target));
        ikea::journal effects{records};
        if (!sp::initialize(target, std::span<const u64>(values), effects))
            std::abort();
        place(values.size(), storage_, storage_);
    }
    /// Placed in an owner's image: reads borrow bytes; writes, when writable
    /// is not empty, store into it (the same bytes as a private draft).
    width_array_at(std::span<const ikea::byte> bytes, std::size_t rows, std::span<ikea::byte> writable) { place(rows, bytes, writable); }
    void place(std::size_t rows, std::span<const ikea::byte> bytes, std::span<ikea::byte> writable) {
        bytes_ = bytes;
        auto v = sp::const_view<L>::attach(rows, {{{bytes, L::tile_bytes}, {}, {}}});
        if (!v)
            std::abort();
        view_.emplace(*v);
        reader_.emplace(sp::bind_reader(*view_));
        if (!writable.empty()) {
            mutable_.emplace(*sp::view<L>::attach(rows, {{{writable, L::tile_bytes}, {}, {}}}));
            writer_.emplace(*sp::bind_writer(*mutable_));
        }
    }
    // The width's point kernel over its dense bytes, for fused point paths.
    static u64 point_at(const void *data, std::size_t row) { return sp::detail::dense_point<L, u64>(data, row); }
    point_kernel point() const override { return {&point_at, bytes_.data(), bytes_.data(), L::tile_rows, L::tile_bytes}; }
    static std::size_t bytes_for(std::size_t rows) { return (rows + L::tile_rows - 1) / L::tile_rows * L::tile_bytes; }
    std::span<const ikea::byte> image() const override { return bytes_; }
    std::size_t rows() const override { return view_->size(); }
    void declare(std::size_t row, envelope &e) const override { e.add(frame_change::point_envelope(*mutable_, static_cast<unsigned>(row))); }
    bool set(std::size_t row, u64 value, ikea::journal &effects) override { return writer_ && writer_->set(row, value, effects).has_value(); }
    u64 digest() const override { return hash_bytes(0xcbf29ce484222325, bytes_.data(), bytes_.size()); }
    const ikea::byte *resolve(const ikea::journal::entry &e) const override {
        return mutable_ && e.source == &*mutable_ ? mutable_->plane_storage(e.plane).data() + e.offset : nullptr;
    }
    std::size_t bytes() const override { return bytes_.size(); }
    u128 sum(const bitset256 &members, const bitset256 &blocks) const override {
        // Selected blocks with consecutive slots form one range.
        if (members == blocks)
            return range_sum(0, view_->size());
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
        if (run)  // reaches the array's end, which may be a partial block
            s += range_sum(std::size_t{run_first} * block_rows, view_->size() - std::size_t{run_first} * block_rows);
        return s;
    }
    // A 16-row word of a block's selection, for SeriesPack's prefiltered SUM.
    static u16 word16(const bitset256 &r, std::size_t at) { return static_cast<u16>(r.words[at / 64] >> (at % 64)); }
    u128 masked_range(std::size_t first, std::size_t n, const bitset256 &pattern, std::size_t origin) const {
        if constexpr (exact_rows != 0) {
            u128 s = 0;
            for (std::size_t at = 0; at < n; at += exact_rows)
                s += reader_->sum_unchecked(first + at, std::min(exact_rows, n - at), ~u64{0},
                                            [&](std::size_t row) __attribute__((always_inline)) { return word16(pattern, (row - origin) % block_rows); });
            return s;
        } else {
            alignas(64) u64 x[block_rows];
            u128 s = 0;
            for (std::size_t at = 0; at < n; at += block_rows) {
                const std::size_t m = std::min<std::size_t>(block_rows, n - at);
                reader_->read_unchecked(first + at, m, x);
                for (std::size_t i = 0; i < m; ++i)
                    s += pattern.contains(static_cast<unsigned>((first + at + i - origin) % block_rows)) ? x[i] : 0;
            }
            return s;
        }
    }
    u128 sum_selected(const bitset256 &blocks, const u8 *slot, const u16 *rows, const block_rows_set &selected) const override {
        u128 s = 0;
        each(blocks, [&](unsigned b) {
            const std::size_t first = std::size_t{slot[b]} * block_rows;
            s += masked_range(first, rows[b], selected[b], first);
        });
        return s;
    }
    u128 sum_pattern(const bitset256 &members, const bitset256 &blocks, const bitset256 &pattern) const override {
        // Consecutive slots form one range; the pattern repeats every 256 rows.
        u128 s = 0;
        unsigned slot = 0, run_first = 0, run = 0;
        each(members, [&](unsigned b) {
            if (blocks.contains(b)) {
                if (!run)
                    run_first = slot;
                ++run;
            } else if (run) {
                s += masked_range(std::size_t{run_first} * block_rows, std::size_t{run} * block_rows, pattern, 0), run = 0;
            }
            ++slot;
        });
        if (run)
            s += masked_range(std::size_t{run_first} * block_rows, view_->size() - std::size_t{run_first} * block_rows, pattern, 0);
        return s;
    }
    u64 count(comparison op, const bitset256 &blocks, const u8 *slot, const u16 *rows, const block_args &rhs) const override {
        switch (op) {
        case comparison::equal: return count_as<comparison::equal>(blocks, slot, rows, rhs);
        case comparison::less: return count_as<comparison::less>(blocks, slot, rows, rhs);
        case comparison::less_equal: return count_as<comparison::less_equal>(blocks, slot, rows, rhs);
        case comparison::greater: return count_as<comparison::greater>(blocks, slot, rows, rhs);
        case comparison::greater_equal: return count_as<comparison::greater_equal>(blocks, slot, rows, rhs);
        }
        return 0;
    }
    void filter(comparison op, const bitset256 &blocks, const u8 *slot, const u16 *rows, const block_args &rhs, block_rows_set &out) const override {
        switch (op) {
        case comparison::equal: return filter_as<comparison::equal>(blocks, slot, rows, rhs, out);
        case comparison::less: return filter_as<comparison::less>(blocks, slot, rows, rhs, out);
        case comparison::less_equal: return filter_as<comparison::less_equal>(blocks, slot, rows, rhs, out);
        case comparison::greater: return filter_as<comparison::greater>(blocks, slot, rows, rhs, out);
        case comparison::greater_equal: return filter_as<comparison::greater_equal>(blocks, slot, rows, rhs, out);
        }
    }
    u64 get(std::size_t row) const override { return reader_->get_unchecked(row); }
    void read(std::size_t first, unsigned n, u64 *out) const override { reader_->read_unchecked(first, n, out); }
};


// Widths [First, Last] of preset P, one TU each (widths_*.cpp).
template <sp::preset P, unsigned First, unsigned Last> std::unique_ptr<width_array> make_width_range(unsigned K, const std::vector<u64> &values) {
    std::unique_ptr<width_array> out;
    [&]<unsigned... I>(std::integer_sequence<unsigned, I...>) {
        ((K == First + I ? (out = std::make_unique<width_array_at<First + I, P>>(values), 0) : 0), ...);
    }(std::make_integer_sequence<unsigned, Last - First + 1>{});
    return out;
}
template <sp::preset P, unsigned First, unsigned Last>
std::unique_ptr<width_array> place_width_range(unsigned K, std::span<const ikea::byte> bytes, std::size_t rows, std::span<ikea::byte> writable) {
    std::unique_ptr<width_array> out;
    [&]<unsigned... I>(std::integer_sequence<unsigned, I...>) {
        ((K == First + I ? (out = std::make_unique<width_array_at<First + I, P>>(bytes, rows, writable), 0) : 0), ...);
    }(std::make_integer_sequence<unsigned, Last - First + 1>{});
    return out;
}
#define FRAME3_WIDTHS(P, FIRST, LAST)                                                                                 \
    template std::unique_ptr<width_array> make_width_range<P, FIRST, LAST>(unsigned, const std::vector<u64> &); \
    template std::unique_ptr<width_array> place_width_range<P, FIRST, LAST>(unsigned, std::span<const ikea::byte>, std::size_t, std::span<ikea::byte>);
}  // namespace frame3
