// The pack leaf over SeriesPack's compiled-once runtime-width readers: one
// class serves every width, so Frame compiles no width-specialised kernels.
#include "../sections.h"

#include <ikea/frame/detail/stages.h>
#include <ikea/seriespack/layout_value.h>
#include <ikea/seriespack/reader.h>
#include <ikea/seriespack/writer.h>

#include <bit>
#include <optional>

namespace ikea::frame::detail {
namespace {
namespace sp = ikea::seriespack;
using author::stage;

std::uint64_t mask(unsigned K) noexcept { return K >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << K) - 1; }

/// One SeriesPack array of a group's width-K blocks, in slot order.
struct width_array {
    std::vector<std::byte> owned; // built bytes; empty when placed
    std::span<const std::byte> bytes;
    std::size_t rows = 0;
    std::optional<sp::erased_reader<std::uint64_t>> reader;
};

class pack_stage final : public stage {
    struct group {
        std::array<bitset256, 65> members{};
        std::array<width_array, 65> arrays;
        std::vector<std::uint8_t> present; // widths with blocks, ascending
    };
    struct unit_state {
        std::array<std::uint8_t, unit_blocks> width{}, slot{};
        std::array<std::uint16_t, unit_blocks> rows{};
        std::vector<group> groups;
    };
    std::vector<unit_state> units_;
    pack_preset preset_;

    sp::geometry geometry(unsigned K) const noexcept {
        return packing_geometry(preset_, K) == pack_geometry::striped ? sp::geometry::striped : sp::geometry::local;
    }
    std::optional<sp::storage_requirements> storage(unsigned K, std::size_t rows) const noexcept {
        const auto r = sp::layout_value{K, 0, geometry(K)}.required_storage(rows);
        return r ? std::optional(*r) : std::nullopt;
    }
    std::expected<void, error> bind(width_array &a, unsigned K) const {
        auto r = sp::bind_range_reader<std::uint64_t>(sp::dense_input{K, geometry(K), a.bytes, a.rows});
        if (!r)
            return std::unexpected(error::contents);
        a.reader.emplace(*r);
        return {};
    }
    unit_state &unit(unsigned u) {
        if (units_.size() <= u)
            units_.resize(u + 1);
        return units_[u];
    }
    const width_array *array_of(const unit_state &s, unsigned b) const {
        return s.width[b] ? &s.groups[b / group_blocks].arrays[s.width[b]] : nullptr;
    }
    std::size_t first_row(const unit_state &s, unsigned b) const { return std::size_t{s.slot[b]} * block_rows; }
    template <class F> void each_width(const unit_state &s, const bitset256 &blocks, F &&f) const {
        for (const group &q : s.groups)
            for (unsigned K : q.present)
                if (const bitset256 m = q.members[K] & blocks; !m.empty())
                    f(q, K, m);
    }
    // Consecutive slots of the selected members of one width are contiguous
    // rows of its array: visit each run as (first row, rows).
    template <class F> void each_run(const group &q, unsigned K, const bitset256 &selected, F &&f) const {
        const width_array &a = q.arrays[K];
        unsigned slot = 0, run_first = 0, run = 0;
        auto flush = [&] {
            if (run) {
                const std::size_t first = std::size_t{run_first} * block_rows;
                f(first, std::min<std::size_t>(std::size_t{run} * block_rows, a.rows - first));
                run = 0;
            }
        };
        q.members[K].for_each([&](unsigned b) {
            if (selected.contains(b)) {
                if (!run)
                    run_first = slot;
                ++run;
            } else
                flush();
            ++slot;
        });
        flush();
    }
    static void masks_of(const bitset256 &r, std::uint16_t *masks) noexcept {
        for (unsigned k = 0; k < 16; ++k)
            masks[k] = static_cast<std::uint16_t>(r.words[k / 4] >> (k % 4 * 16));
    }

  public:
    explicit pack_stage(pack_preset preset) : preset_(preset) {}

    std::size_t body_bytes(unsigned K) const override {
        const auto r = K ? storage(K, block_rows) : std::nullopt;
        return r ? r->plane_bytes[0] : 0;
    }

    std::expected<void, error> fit(unsigned u, const author::fit_input &in) override {
        if (in.values.size() > unit_rows)
            return std::unexpected(error::range);
        unit_state &s = unit(u) = {};
        sized(u, in.values.size());
        s.groups.resize(groups_in(u));
        for (unsigned g = 0; g < s.groups.size(); ++g) {
            group &q = s.groups[g];
            std::array<std::vector<std::uint64_t>, 65> staged;
            const unsigned first = g * group_blocks, n = std::min(group_blocks, blocks_in(u) - first);
            for (unsigned b = first; b < first + n; ++b) {
                const unsigned m = rows_in(u, b);
                const std::uint64_t *x = in.values.data() + std::size_t{b} * block_rows;
                std::uint64_t top = 0;
                for (unsigned r = 0; r < m; ++r)
                    top |= x[r];
                const unsigned K = in.widths ? in.widths[b] : static_cast<unsigned>(std::bit_width(top));
                if (K > 64 || (top & ~mask(K)))
                    return std::unexpected(error::range); // a parent fixed a width its values exceed
                s.rows[b] = static_cast<std::uint16_t>(m);
                s.width[b] = static_cast<std::uint8_t>(K);
                s.slot[b] = static_cast<std::uint8_t>(K ? staged[K].size() / block_rows : 0);
                q.members[K].set(b);
                if (K)
                    staged[K].insert(staged[K].end(), x, x + m);
            }
            for (unsigned K = 0; K <= 64; ++K) {
                if (q.members[K].empty())
                    continue;
                q.present.push_back(static_cast<std::uint8_t>(K));
                if (!K)
                    continue;
                width_array &a = q.arrays[K];
                const auto need = storage(K, staged[K].size());
                if (!need)
                    return std::unexpected(error::capacity);
                a.owned.resize(need->plane_bytes[0]);
                a.rows = staged[K].size();
                const sp::dense_output target{K, geometry(K), a.owned, a.rows};
                const auto records = sp::effect_capacity_initialize(target);
                if (!records)
                    return std::unexpected(error::capacity);
                std::vector<journal::entry> entries(*records);
                journal effects{entries};
                if (!sp::initialize(target, std::span<const std::uint64_t>(staged[K]), effects))
                    return std::unexpected(error::capacity);
                a.bytes = a.owned;
                if (auto bound = bind(a, K); !bound)
                    return bound;
            }
        }
        return {};
    }

    // Section: per block width u8, slot u8, rows u16; the number of widths
    // present u8; then each present width ascending as K u8, rows u32,
    // bytes u32 and its SeriesPack array.
    void emit(unsigned u, unsigned g, std::vector<std::byte> &out) const override {
        const unit_state &s = units_[u];
        const group &q = s.groups[g];
        const unsigned first = g * group_blocks, n = std::min(group_blocks, blocks_in(u) - first);
        for (unsigned b = first; b < first + n; ++b)
            put<std::uint8_t>(out, s.width[b]), put<std::uint8_t>(out, s.slot[b]), put<std::uint16_t>(out, s.rows[b]);
        put<std::uint8_t>(out, static_cast<std::uint8_t>(q.present.size()));
        for (unsigned K : q.present) {
            const width_array &a = q.arrays[K];
            put<std::uint8_t>(out, static_cast<std::uint8_t>(K));
            put<std::uint32_t>(out, static_cast<std::uint32_t>(a.rows));
            put<std::uint32_t>(out, static_cast<std::uint32_t>(a.bytes.size()));
            put_bytes(out, a.bytes);
        }
    }

    std::expected<std::size_t, error> attach(unsigned u, std::size_t rows, unsigned g,
                                             std::span<const std::byte> in) override {
        if (rows > unit_rows)
            return std::unexpected(error::range);
        sized(u, rows);
        unit_state &s = unit(u);
        s.groups.resize(groups_in(u));
        if (g >= s.groups.size())
            return std::unexpected(error::range);
        group &q = s.groups[g] = group{};
        cursor c{in};
        const unsigned first = g * group_blocks, n = std::min(group_blocks, blocks_in(u) - first);
        std::array<std::size_t, 65> expected_rows{};
        std::array<unsigned, 65> next_slot{};
        for (unsigned b = first; b < first + n; ++b) {
            const unsigned K = c.take<std::uint8_t>(), slot = c.take<std::uint8_t>(), m = c.take<std::uint16_t>();
            // Width zero owns no array, so every constant block has slot 0.
            if (!c.ok || K > 64 || m != rows_in(u, b) || slot != (K ? next_slot[K]++ : 0))
                return std::unexpected(error::contents);
            s.width[b] = static_cast<std::uint8_t>(K), s.slot[b] = static_cast<std::uint8_t>(slot);
            s.rows[b] = static_cast<std::uint16_t>(m);
            q.members[K].set(b);
            expected_rows[K] += m;
        }
        const unsigned present = c.take<std::uint8_t>();
        int previous = -1;
        for (unsigned i = 0; i < present; ++i) {
            const unsigned K = c.take<std::uint8_t>();
            const std::size_t n_rows = c.take<std::uint32_t>(), n_bytes = c.take<std::uint32_t>();
            if (!c.ok || K > 64 || static_cast<int>(K) <= previous || q.members[K].empty())
                return std::unexpected(error::contents);
            previous = static_cast<int>(K);
            q.present.push_back(static_cast<std::uint8_t>(K));
            const auto need = K ? storage(K, n_rows) : std::nullopt;
            if ((K ? n_rows != expected_rows[K] : n_rows != 0) || (K ? !need || need->plane_bytes[0] != n_bytes : n_bytes))
                return std::unexpected(error::contents);
            const auto bytes = c.span(n_bytes);
            if (!c.ok)
                return std::unexpected(error::contents);
            if (K) {
                width_array &a = q.arrays[K];
                a.bytes = bytes, a.rows = n_rows;
                if (auto bound = bind(a, K); !bound)
                    return std::unexpected(bound.error());
            }
        }
        unsigned widths = 0;
        for (const auto &m : q.members)
            widths += !m.empty();
        if (!c.ok || widths != present)
            return std::unexpected(error::contents);
        return c.at;
    }

    void describe(unsigned u, author::census &c) const override {
        const unit_state &s = units_[u];
        for (unsigned b = 0; b < blocks_in(u); ++b)
            ++c.blocks[s.width[b]][0];
    }
    bool leaf_entry(unsigned u, unsigned b, author::point_leaf &e) const override {
        const unit_state &s = units_[u];
        e = {};
        e.base = first_row(s, b);
        if (const auto *a = array_of(s, b)) {
            const auto need = storage(s.width[b], a->rows);
            e.get = a->reader->point_kernel(), e.resource = a->reader->resource(), e.data = a->bytes.data();
            e.tile_rows = static_cast<unsigned>(need->tile_rows), e.tile_bytes = static_cast<unsigned>(need->tile_bytes[0]);
        }
        return true;
    }

    void decode(unsigned u, unsigned b, std::uint64_t *out) const override {
        const unit_state &s = units_[u];
        if (const auto *a = array_of(s, b))
            a->reader->read_unchecked(first_row(s, b), s.rows[b], out);
        else
            std::fill_n(out, s.rows[b], std::uint64_t{0});
    }
    u128 sum(unsigned u, const bitset256 &blocks) const override {
        u128 t = 0;
        each_width(units_[u], blocks, [&](const group &q, unsigned K, const bitset256 &m) {
            if (K)
                each_run(q, K, m, [&](std::size_t first, std::size_t n) { t += q.arrays[K].reader->sum_exact_unchecked(first, n); });
        });
        return t;
    }
    u128 sum_selected(unsigned u, const bitset256 &blocks, const block_rows_set &rows) const override {
        const unit_state &s = units_[u];
        u128 t = 0;
        each_width(s, blocks, [&](const group &q, unsigned K, const bitset256 &m) {
            if (K)
                m.for_each([&](unsigned b) {
                    std::uint16_t masks[16];
                    masks_of(rows[b], masks);
                    t += q.arrays[K].reader->sum_masked_exact_unchecked(first_row(s, b), s.rows[b], masks);
                });
        });
        return t;
    }
    u128 weighted_sum(unsigned u, const bitset256 &blocks, const std::uint8_t *weights, unsigned period) const override {
        u128 t = 0;
        // Arrays start on block boundaries and 256 % period == 0, so a row's
        // weight is the same in array and block coordinates.
        each_width(units_[u], blocks, [&](const group &q, unsigned K, const bitset256 &m) {
            if (K)
                each_run(q, K, m, [&](std::size_t first, std::size_t n) {
                    t += q.arrays[K].reader->weighted_sum_unchecked(first, n, weights, period);
                });
        });
        return t;
    }
    std::uint64_t count(comparison op, unsigned u, const bitset256 &blocks, const block_args &rhs) const override {
        const unit_state &s = units_[u];
        std::uint64_t c = 0;
        each_width(s, blocks, [&](const group &q, unsigned K, const bitset256 &m) {
            m.for_each([&](unsigned b) {
                c += K ? q.arrays[K].reader->count_unchecked(op, first_row(s, b), s.rows[b], rhs[b])
                       : author::test(op, 0, rhs[b]) ? s.rows[b] : 0;
            });
        });
        return c;
    }
    void filter(comparison op, unsigned u, const bitset256 &blocks, const block_args &rhs, std::span<bitset256> out) const override {
        const unit_state &s = units_[u];
        each_width(s, blocks, [&](const group &q, unsigned K, const bitset256 &m) {
            m.for_each([&](unsigned b) {
                if (!K) {
                    out[b] = author::test(op, 0, rhs[b]) ? bitset256::first_n(s.rows[b]) : bitset256{};
                    return;
                }
                if (s.rows[b] < block_rows)
                    out[b] = {};
                q.arrays[K].reader->filter_words_unchecked(op, first_row(s, b), s.rows[b], rhs[b], out[b].words.data());
            });
        });
    }
    std::uint64_t get(unsigned u, unsigned row) const override {
        const unit_state &s = units_[u];
        const unsigned b = row / block_rows;
        const auto *a = array_of(s, b);
        return a ? a->reader->get_unchecked(first_row(s, b) + row % block_rows) : 0;
    }
    void read(unsigned u, unsigned first, unsigned n, std::uint64_t *out) const override {
        const unit_state &s = units_[u];
        while (n) {
            const unsigned b = first / block_rows, r = first % block_rows, m = std::min(n, s.rows[b] - r);
            if (const auto *a = array_of(s, b))
                a->reader->read_unchecked(first_row(s, b) + r, m, out);
            else
                std::fill_n(out, m, std::uint64_t{0});
            out += m, first += m, n -= m;
        }
    }
};
} // namespace

std::unique_ptr<author::stage> make_pack(pack_preset preset) { return std::make_unique<pack_stage>(preset); }

} // namespace ikea::frame::detail
