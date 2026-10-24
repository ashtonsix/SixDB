// Placeholder stages (prototype 2): shapes the first landing does not
// support, built through the same contract to find what the contract must
// admit. Each has fit, emit and decode, and only the laws its shape forces
// us to think about; everything else falls back to frame.h's reference
// definitions.
//
//   runs   1 -> 2 with fewer rows: run sizes minus one and run values. Its
//          children have their own row counts per part, so it addresses them
//          by row range (read), not by the parent's blocks.
//   split  1 -> 2 with the same rows: the high head (x >> s) and the low
//          suffix. Filters and counts combine the children's selections.
//   record a TuplePack leaf shared by several columns' trees: each row is one
//          unit, each column one byte-contained code (at most 8 bits).
#pragma once
#include "stages.h"

#include <ikea/tuplepack.h>

namespace frame3 {

class runs final : public stage {
    std::unique_ptr<stage> children_[2];  // sizes minus one, values
    struct part {
        std::vector<u32> first;  // block b's runs are child rows [first[b], first[b + 1])
    };
    std::vector<part> parts_;

    // The run sizes and values of block b.
    unsigned load(unsigned p, unsigned b, u64 *sizes, u64 *values) const {
        const part &s = parts_[p];
        const unsigned n = s.first[b + 1] - s.first[b];
        children_[0]->read(p, s.first[b], n, sizes);
        children_[1]->read(p, s.first[b], n, values);
        return n;
    }

  public:
    runs(std::unique_ptr<stage> sizes, std::unique_ptr<stage> values) : children_{std::move(sizes), std::move(values)} {}
    const char *name() const override { return "runs"; }
    std::span<const std::unique_ptr<stage>> children() const override { return children_; }
    void build(unsigned p, const build_input &in) override {
        sized(p, in.values.size());
        if (parts_.size() <= p)
            parts_.resize(p + 1);
        part &s = parts_[p];
        std::vector<u64> sizes, values;
        for (unsigned b = 0; b < blocks_in(p); ++b) {
            s.first.push_back(static_cast<u32>(sizes.size()));
            const u64 *x = in.values.data() + std::size_t{b} * block_rows;
            const unsigned m = rows_in(p, b);
            for (unsigned r = 0, start = 0; r < m; ++r)
                if (r + 1 == m || x[r + 1] != x[r])
                    sizes.push_back(r - start), values.push_back(x[r]), start = r + 1;
        }
        s.first.push_back(static_cast<u32>(sizes.size()));
        children_[0]->build(p, {sizes});
        children_[1]->build(p, {values});
    }
    std::size_t bytes(unsigned p) const override { return parts_[p].first.size() * 4; }
    void clear(unsigned p) override {
        parts_[p] = {};
        stage::clear(p);
    }
    void decode(unsigned p, unsigned b, u64 *out) const override {
        alignas(64) u64 sz[block_rows], v[block_rows];
        const unsigned n = load(p, b, sz, v);
        for (unsigned i = 0, r = 0; i < n; ++i)
            for (u64 k = 0; k <= sz[i]; ++k)
                out[r++] = v[i];
    }
    // The run law: each run's work covers all of its rows.
    u128 sum(unsigned p, const bitset256 &blocks) const override {
        alignas(64) u64 sz[block_rows], v[block_rows];
        u128 t = 0;
        each(blocks, [&](unsigned b) {
            const unsigned n = load(p, b, sz, v);
            for (unsigned i = 0; i < n; ++i)
                t += static_cast<u128>(sz[i] + 1) * v[i];
        });
        return t;
    }
    u64 count(comparison op, unsigned p, const bitset256 &blocks, const block_args &rhs) const override {
        alignas(64) u64 sz[block_rows], v[block_rows];
        u64 c = 0;
        each(blocks, [&](unsigned b) {
            const unsigned n = load(p, b, sz, v);
            for (unsigned i = 0; i < n; ++i)
                c += test(op, v[i], rhs[b]) ? sz[i] + 1 : 0;
        });
        return c;
    }
};

class split final : public stage {
    std::unique_ptr<stage> children_[2];  // head, suffix
    unsigned s_;

  public:
    split(std::unique_ptr<stage> head, std::unique_ptr<stage> suffix, unsigned s) : children_{std::move(head), std::move(suffix)}, s_(s) {}
    const char *name() const override { return "split"; }
    std::span<const std::unique_ptr<stage>> children() const override { return children_; }
    void build(unsigned p, const build_input &in) override {
        sized(p, in.values.size());
        std::vector<u64> head(in.values.size()), suffix(in.values.size());
        for (std::size_t i = 0; i < in.values.size(); ++i)
            head[i] = in.values[i] >> s_, suffix[i] = in.values[i] & mask(s_);
        children_[0]->build(p, {head});
        children_[1]->build(p, {suffix});
    }
    std::size_t bytes(unsigned) const override { return 0; }
    void decode(unsigned p, unsigned b, u64 *out) const override {
        alignas(64) u64 lo[block_rows];
        children_[0]->decode(p, b, out);
        children_[1]->decode(p, b, lo);
        for (unsigned r = 0; r < rows_in(p, b); ++r)
            out[r] = out[r] << s_ | lo[r];
    }
    // Linearity: SUM = 2^s SUM(head) + SUM(suffix).
    u128 sum(unsigned p, const bitset256 &blocks) const override {
        return (children_[0]->sum(p, blocks) << s_) + children_[1]->sum(p, blocks);
    }
    u64 get(unsigned p, unsigned row) const override { return children_[0]->get(p, row) << s_ | children_[1]->get(p, row); }
    // x >= t: head > t's head, or head equal and suffix >= t's suffix. The
    // suffix is read only where the head ties (Ashton's separated heads).
    void filter(comparison op, unsigned p, const bitset256 &blocks, const block_args &rhs, block_rows_set &out) const override {
        if (op != comparison::greater_equal)
            return stage::filter(op, p, blocks, rhs, out);
        block_args th, tl;
        for (unsigned b = 0; b < part_blocks; ++b)
            th[b] = rhs[b] >> s_, tl[b] = rhs[b] & mask(s_);
        block_rows_set above, tie, low;
        children_[0]->filter(comparison::greater, p, blocks, th, above);
        children_[0]->filter(comparison::equal, p, blocks, th, tie);
        bitset256 tied;
        each(blocks, [&](unsigned b) {
            if (!tie[b].empty())
                tied.set(b);
        });
        if (!tied.empty())
            children_[1]->filter(comparison::greater_equal, p, tied, tl, low);
        each(blocks, [&](unsigned b) {
            out[b] = above[b];
            if (tied.contains(b))
                for (unsigned w = 0; w < 4; ++w)
                    out[b].words[w] |= tie[b].words[w] & low[b].words[w];
        });
    }
    u64 count(comparison op, unsigned p, const bitset256 &blocks, const block_args &rhs) const override {
        if (op != comparison::greater_equal)
            return stage::count(op, p, blocks, rhs);
        block_rows_set out;
        filter(op, p, blocks, rhs, out);
        u64 c = 0;
        each(blocks, [&](unsigned b) { c += out[b].count(); });
        return c;
    }
};

namespace tp = ikea::tuplepack;

/// One TuplePack unit per row, one code per field. Columns deposit their
/// residuals as their trees build; the part seals when every field arrived.
class record {
    unsigned fields_;
    struct field {
        std::vector<u64> values;
        unsigned width = 1;
        std::optional<tp::read_plan<8>> plan;
        std::optional<tp::reader<8, const std::byte>> reader;
        std::optional<tp::predicate<const std::byte, 64>> predicate;
    };
    struct part {
        std::vector<field> fields;
        unsigned arrived = 0;
        std::optional<tp::layout> layout;
        std::vector<std::byte> bytes;
        std::optional<tp::const_view> view;
    };
    std::vector<part> parts_;

    void seal(part &s) {
        std::vector<tp::code> codes;
        for (unsigned f = 0; f < fields_; ++f)
            codes.push_back({static_cast<u8>(f), 0, static_cast<u8>(s.fields[f].width)});
        s.layout.emplace(*tp::layout::make(fields_, codes));
        const std::size_t rows = s.fields[0].values.size();
        s.bytes.assign(rows * fields_, std::byte{0});
        const auto writable = *tp::view::attach(*s.layout, std::span(s.bytes), rows, fields_);
        const auto construction = *tp::write_plan<128>::make(*s.layout);
        const auto writer = *tp::bind_writer(construction, writable);
        std::vector<tp::packet<128>> input(rows);
        for (std::size_t r = 0; r < rows; ++r)
            for (unsigned f = 0; f < fields_; ++f)
                input[r][f] = static_cast<u8>(s.fields[f].values[r]);
        std::vector<ikea::journal::entry> records(rows);
        ikea::journal effects{records};
        if (!writer.initialize(0, input, effects))
            std::abort();
        s.view.emplace(*tp::const_view::attach(*s.layout, std::span<const std::byte>(s.bytes), rows, fields_));
        for (unsigned f = 0; f < fields_; ++f) {
            field &q = s.fields[f];
            const std::array<u8, 1> map{static_cast<u8>(f)};
            q.plan.emplace(*tp::read_plan<8>::make(*s.layout, map));
            q.reader.emplace(*tp::bind_reader(*q.plan, *s.view));
            q.predicate.emplace(*tp::bind_predicate<64>(*s.layout, f, *s.view));
            q.values = {};
        }
    }

  public:
    explicit record(unsigned fields) : fields_(fields) {}
    void deposit(unsigned p, unsigned f, std::span<const u64> values, const u8 *widths, unsigned blocks) {
        if (parts_.size() <= p)
            parts_.resize(p + 1);
        part &s = parts_[p];
        s.fields.resize(fields_);
        field &q = s.fields[f];
        q.values.assign(values.begin(), values.end());
        for (unsigned b = 0; b < blocks; ++b)
            q.width = std::max(q.width, widths ? unsigned{widths[b]} : frame_bounds::bits(*std::max_element(values.begin() + b * block_rows, values.begin() + std::min<std::size_t>(values.size(), (b + 1) * block_rows))));
        if (q.width > 8)
            std::abort();  // a code is byte-contained
        if (++s.arrived == fields_)
            seal(s);
    }
    u64 get(unsigned p, unsigned f, unsigned row) const { return static_cast<u8>(parts_[p].fields[f].reader->get_unchecked(row)); }
    u64 filter(comparison op, unsigned p, unsigned f, unsigned first, u64 rhs, u64 active) const {
        const auto &q = *parts_[p].fields[f].predicate;
        switch (op) {
        case comparison::equal: return q.filter_unchecked<comparison::equal>(first, rhs, active);
        case comparison::less: return q.filter_unchecked<comparison::less>(first, rhs, active);
        case comparison::less_equal: return q.filter_unchecked<comparison::less_equal>(first, rhs, active);
        case comparison::greater: return q.filter_unchecked<comparison::greater>(first, rhs, active);
        case comparison::greater_equal: return q.filter_unchecked<comparison::greater_equal>(first, rhs, active);
        }
        return 0;
    }
    std::size_t bytes(unsigned p) const { return parts_[p].bytes.size(); }
    unsigned fields() const { return fields_; }
};

/// A column's leaf inside a shared record. The leaf admits widths up to 8
/// (bound prices wider ones out), and filters through TuplePack's predicate.
class record_field final : public stage {
    std::shared_ptr<record> record_;
    unsigned field_;

  public:
    record_field(std::shared_ptr<record> r, unsigned field) : record_(std::move(r)), field_(field) {}
    const char *name() const override { return "record"; }
    std::size_t body_bytes(unsigned K) const override { return K <= 8 ? block_rows : std::size_t{1} << 30; }
    void build(unsigned p, const build_input &in) override {
        sized(p, in.values.size());
        record_->deposit(p, field_, in.values, in.widths, blocks_in(p));
    }
    std::size_t bytes(unsigned p) const override { return field_ ? 0 : record_->bytes(p); }  // the record counts once
    void decode(unsigned p, unsigned b, u64 *out) const override {
        for (unsigned r = 0; r < rows_in(p, b); ++r)
            out[r] = record_->get(p, field_, b * block_rows + r);
    }
    u64 get(unsigned p, unsigned row) const override { return record_->get(p, field_, row); }
    void filter(comparison op, unsigned p, const bitset256 &blocks, const block_args &rhs, block_rows_set &out) const override {
        each(blocks, [&](unsigned b) {
            const unsigned m = rows_in(p, b);
            out[b] = {};
            for (unsigned w = 0; w * 64 < m; ++w) {
                const unsigned n = std::min(64u, m - w * 64);
                out[b].words[w] = record_->filter(op, p, field_, b * block_rows + w * 64, rhs[b], n == 64 ? ~u64{0} : (u64{1} << n) - 1);
            }
        });
    }
    u64 count(comparison op, unsigned p, const bitset256 &blocks, const block_args &rhs) const override {
        block_rows_set out;
        filter(op, p, blocks, rhs, out);
        u64 c = 0;
        each(blocks, [&](unsigned b) { c += out[b].count(); });
        return c;
    }
};
}  // namespace frame3
