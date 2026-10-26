#include <ikea/frame/author/stage.h>

namespace ikea::frame::author {

void stage::sized(unsigned unit, std::size_t rows) {
    if (rows_.size() <= unit)
        rows_.resize(unit + 1);
    rows_[unit] = static_cast<std::uint32_t>(rows);
}

void stage::streams(const std::string &prefix, std::vector<std::pair<std::string, const stage *>> &out) const {
    const auto ch = children();
    for (unsigned i = 0; i < ch.size(); ++i) {
        const std::string path = prefix + role(i);
        if (!inline_child(i))
            out.emplace_back(path, ch[i].get());
        ch[i]->streams(path + "/", out);
    }
}

void stage::emit(unsigned unit, unsigned group, std::vector<std::byte> &out) const {
    const auto ch = children();
    for (unsigned i = 0; i < ch.size(); ++i)
        if (inline_child(i))
            ch[i]->emit(unit, group, out);
}

std::expected<std::size_t, error> stage::attach(unsigned unit, std::size_t rows, unsigned group,
                                                std::span<const std::byte> image) {
    sized(unit, rows);
    std::size_t used = 0;
    const auto ch = children();
    for (unsigned i = 0; i < ch.size(); ++i)
        if (inline_child(i)) {
            const auto n = ch[i]->attach(unit, rows, group, image.subspan(used));
            if (!n)
                return n;
            used += *n;
        }
    return used;
}

std::expected<void, error> stage::validate_unit(unsigned unit) const {
    for (const auto &c : children())
        if (auto v = c->validate_unit(unit); !v)
            return v;
    return {};
}

void stage::describe(unsigned unit, census &c) const {
    for (const auto &ch : children())
        ch->describe(unit, c);
}

u128 stage::sum(unsigned unit, const bitset256 &blocks) const {
    alignas(64) std::uint64_t x[block_rows];
    u128 s = 0;
    blocks.for_each([&](unsigned b) {
        decode(unit, b, x);
        for (unsigned r = 0; r < rows_in(unit, b); ++r)
            s += x[r];
    });
    return s;
}

u128 stage::sum_selected(unsigned unit, const bitset256 &blocks, const block_rows_set &rows) const {
    alignas(64) std::uint64_t x[block_rows];
    u128 s = 0;
    blocks.for_each([&](unsigned b) {
        decode(unit, b, x);
        (rows[b] & bitset256::first_n(rows_in(unit, b))).for_each([&](unsigned r) { s += x[r]; });
    });
    return s;
}

u128 stage::weighted_sum(unsigned unit, const bitset256 &blocks, const std::uint8_t *weights, unsigned period) const {
    alignas(64) std::uint64_t x[block_rows];
    u128 s = 0;
    blocks.for_each([&](unsigned b) {
        decode(unit, b, x);
        for (unsigned r = 0; r < rows_in(unit, b); ++r)
            s += static_cast<u128>(x[r]) * weights[r % period];
    });
    return s;
}

void stage::summarise_selected(unsigned unit, const bitset256 &blocks, const block_rows_set &rows, summary &out) const {
    alignas(64) std::uint64_t x[block_rows];
    blocks.for_each([&](unsigned b) {
        decode(unit, b, x);
        (rows[b] & bitset256::first_n(rows_in(unit, b))).for_each([&](unsigned r) {
            out.sum += x[r], ++out.count;
            out.min = std::min(out.min, x[r]), out.max = std::max(out.max, x[r]);
        });
    });
}

bool stage::max_selected(unsigned unit, const bitset256 &blocks, const block_rows_set &rows, std::uint64_t &m) const {
    alignas(64) std::uint64_t x[block_rows];
    bool any = false;
    blocks.for_each([&](unsigned b) {
        const bitset256 r = rows[b] & bitset256::first_n(rows_in(unit, b));
        if (r.empty())
            return;
        decode(unit, b, x);
        r.for_each([&](unsigned i) { m = std::max(m, x[i]); });
        any = true;
    });
    return any;
}

std::uint64_t stage::count(comparison op, unsigned unit, const bitset256 &blocks, const block_args &rhs) const {
    alignas(64) std::uint64_t x[block_rows];
    std::uint64_t c = 0;
    blocks.for_each([&](unsigned b) {
        decode(unit, b, x);
        for (unsigned r = 0; r < rows_in(unit, b); ++r)
            c += test(op, x[r], rhs[b]);
    });
    return c;
}

std::uint64_t stage::count_uniform(comparison op, unsigned unit, const bitset256 &blocks, std::uint64_t t) const {
    block_args rhs;
    rhs.fill(t);
    return count(op, unit, blocks, rhs);
}

void stage::filter(comparison op, unsigned unit, const bitset256 &blocks, const block_args &rhs, std::span<bitset256> out) const {
    alignas(64) std::uint64_t x[block_rows];
    blocks.for_each([&](unsigned b) {
        decode(unit, b, x);
        bitset256 m;
        for (unsigned r = 0; r < rows_in(unit, b); ++r)
            m.words[r / 64] |= std::uint64_t{test(op, x[r], rhs[b])} << (r % 64);
        out[b] = m;
    });
}

std::uint64_t stage::get(unsigned unit, unsigned row) const {
    alignas(64) std::uint64_t x[block_rows];
    decode(unit, row / block_rows, x);
    return x[row % block_rows];
}

void stage::read(unsigned unit, unsigned first, unsigned n, std::uint64_t *out) const {
    alignas(64) std::uint64_t x[block_rows];
    while (n) {
        const unsigned b = first / block_rows, r = first % block_rows, m = std::min(n, block_rows - r);
        decode(unit, b, x);
        std::copy_n(x + r, m, out);
        out += m, first += m, n -= m;
    }
}

std::size_t stage::select(unsigned unit, const bitset256 &blocks, const block_rows_set &rows, std::uint64_t *out) const {
    alignas(64) std::uint64_t x[block_rows];
    std::size_t k = 0;
    blocks.for_each([&](unsigned b) {
        const bitset256 r = rows[b] & bitset256::first_n(rows_in(unit, b));
        if (r.empty())
            return;
        decode(unit, b, x);
        r.for_each([&](unsigned i) { out[k++] = x[i]; });
    });
    return k;
}

} // namespace ikea::frame::author
