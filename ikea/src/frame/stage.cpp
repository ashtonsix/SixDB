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

bitset256 request::lower(unsigned b) const noexcept {
    const unsigned at = b * block_rows, n = rows_in(b);
    const bool inside = first_ <= at && at + n <= first_ + count_;
    const unsigned lo = inside ? 0 : std::max(first_, at) - at, hi = inside ? n : std::min(first_ + count_, at + n) - at;
    if (lo >= hi)
        return {};
    bitset256 r = inside ? bitset256::first_n(n) : bitset256::first_n(hi) & ~bitset256::first_n(lo);
    if (fine_.is_all())
        return r;
    if (const bitset256 *borrowed = fine_.block_at(at))
        return r & *borrowed;
    // Read exactly the span's rows of each word: the fine selection need
    // cover no other row.
    bitset256 f;
    for (unsigned w = lo / 64; w * 64 < hi; ++w) {
        const unsigned from = std::max(lo, w * 64), to = std::min(hi, w * 64 + 64);
        f.words[w] = fine_.mask(at + from, to - from) << (from - w * 64);
    }
    return f;
}

std::uint64_t request::count_rows() const noexcept {
    std::uint64_t n = std::uint64_t{full_.count()} * block_rows;
    const unsigned last = (rows_ + block_rows - 1) / block_rows - 1;
    if (rows_ && full_.contains(last))
        n -= block_rows - rows_in(last);
    partial().for_each([&](unsigned b) { n += lower(b).count(); });
    return n;
}

request request::whole(unsigned rows, const bitset256 &coarse) noexcept {
    request q;
    q.rows_ = rows, q.count_ = rows;
    const unsigned nb = (rows + block_rows - 1) / block_rows;
    q.blocks_ = q.full_ = coarse & bitset256::first_n(nb);
    q.selected_ = std::uint64_t{q.blocks_.count()} * block_rows;
    if (nb && q.blocks_.contains(nb - 1))
        q.selected_ -= block_rows - q.rows_in(nb - 1);
    return q;
}

request request::make(unsigned rows, unsigned first, unsigned count, const bitset256 &coarse,
                      const seriespack::selection &fine) noexcept {
    request q;
    q.rows_ = rows, q.first_ = first, q.count_ = count, q.fine_ = fine;
    if (!count)
        return q;
    const unsigned b0 = first / block_rows, b1 = (first + count - 1) / block_rows + 1;
    if (fine.is_all()) {
        // Without a fine selection only the span's two edge blocks can be
        // partial: the rest is bit arithmetic.
        q.blocks_ = q.full_ = coarse & bitset256::first_n(b1) & ~bitset256::first_n(b0);
        q.selected_ = std::uint64_t{q.blocks_.count()} * block_rows;
        if (q.blocks_.contains(b1 - 1))
            q.selected_ -= block_rows - q.rows_in(b1 - 1);
        for (const unsigned b : {b0, b1 - 1})
            if (q.full_.contains(b)) {
                const unsigned n = q.lower(b).count();
                q.full_.set(b, n == q.rows_in(b));
                q.selected_ -= q.rows_in(b) - n;
            }
        return q;
    }
    // A fine selection borrowing consecutive blocks over the whole span is
    // read in place by rows_of.
    if (const bitset256 *head = fine.block_at(std::size_t{b0} * block_rows),
                       *tail = fine.block_at(std::size_t{b1 - 1} * block_rows);
        head && tail && tail == head + (b1 - 1 - b0))
        q.borrowed_ = head;
    (coarse & bitset256::first_n(b1) & ~bitset256::first_n(b0)).for_each([&](unsigned b) {
        const bitset256 r = q.borrowed_ && q.inside(b) ? q.borrowed_[b - b0] & bitset256::first_n(q.rows_in(b)) : q.lower(b);
        if (r.empty())
            return;
        q.blocks_.set(b);
        const unsigned n = r.count();
        q.full_.set(b, n == q.rows_in(b));
        q.selected_ += n;
    });
    return q;
}

namespace {
// The selected rows of block b: a dense loop over a wholly selected block,
// else its bits.
template <class F> void each_row(const request &q, unsigned b, F &&f) {
    if (q.full().contains(b))
        for (unsigned r = 0, n = q.rows_in(b); r < n; ++r)
            f(r);
    else
        q.rows_of(b).for_each(f);
}
// Exact sum of n <= 256 values: their low and high 32-bit halves each sum
// within 64 bits, so the loop vectorises.
u128 block_sum(const std::uint64_t *x, unsigned n) noexcept {
    std::uint64_t lo = 0, hi = 0;
    for (unsigned r = 0; r < n; ++r)
        lo += x[r] & 0xffffffff, hi += x[r] >> 32;
    return (static_cast<u128>(hi) << 32) + lo;
}

// Partial blocks choose a sparse walk or an identity-element reduction.
// These conservative crossovers come from the native Zen 5 and Graviton 4
// comparisons in workbench/spikes/frame-seam/landing. Full blocks bypass this.
#if defined(__AVX512F__)
constexpr unsigned dense_sum_rows = 85, dense_summary_rows = 85;
#elif defined(__AVX2__)
constexpr unsigned dense_sum_rows = 103, dense_summary_rows = 160;
#else
constexpr unsigned dense_sum_rows = 115, dense_summary_rows = 192;
#endif
bool dense(unsigned selected, unsigned n, unsigned crossover) noexcept { return selected * block_rows >= crossover * n; }

// Exact SUM of the rows of block x selected by r (clear past its n rows).
u128 masked_sum(const std::uint64_t *x, const bitset256 &r, unsigned n) noexcept {
    if (!dense(r.count(), n, dense_sum_rows)) {
        u128 s = 0;
        r.for_each([&](unsigned i) { s += x[i]; });
        return s;
    }
    std::uint64_t lo = 0, hi = 0;
    const auto row = [&](std::uint64_t v, std::uint64_t keep) {
        v &= keep;
        lo += v & 0xffffffff, hi += v >> 32;
    };
    unsigned w = 0;
    for (; w * 64 + 64 <= n; ++w)
        for (unsigned i = 0; i < 64; ++i)
            row(x[w * 64 + i], 0 - (r.words[w] >> i & 1));
    for (unsigned i = w * 64; i < n; ++i)
        row(x[i], 0 - (r.words[w] >> (i % 64) & 1));
    return (static_cast<u128>(hi) << 32) + lo;
}

// The same choice for SUM, COUNT, MIN and MAX together, folded into out.
void masked_summary(const std::uint64_t *x, const bitset256 &r, unsigned n, summary &out) noexcept {
    const unsigned selected = r.count();
    if (!dense(selected, n, dense_summary_rows)) {
        r.for_each([&](unsigned i) {
            out.sum += x[i], ++out.count;
            out.min = std::min(out.min, x[i]), out.max = std::max(out.max, x[i]);
        });
        return;
    }
    std::uint64_t lo = 0, hi = 0, mn = out.min, mx = out.max;
    const auto row = [&](std::uint64_t v, std::uint64_t keep) {
        const std::uint64_t s = v & keep;
        lo += s & 0xffffffff, hi += s >> 32;
        mn = std::min(mn, v | ~keep), mx = std::max(mx, s);
    };
    unsigned w = 0;
    for (; w * 64 + 64 <= n; ++w)
        for (unsigned i = 0; i < 64; ++i)
            row(x[w * 64 + i], 0 - (r.words[w] >> i & 1));
    for (unsigned i = w * 64; i < n; ++i)
        row(x[i], 0 - (r.words[w] >> (i % 64) & 1));
    out.sum += (static_cast<u128>(hi) << 32) + lo, out.count += selected, out.min = mn, out.max = mx;
}
} // namespace

u128 stage::sum(unsigned unit, const request &q) const {
    alignas(64) std::uint64_t x[block_rows];
    u128 s = 0;
    q.blocks().for_each([&](unsigned b) {
        decode(unit, b, x);
        s += q.full().contains(b) ? block_sum(x, rows_in(unit, b)) : masked_sum(x, q.rows_of(b), rows_in(unit, b));
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

void stage::summarise(unsigned unit, const request &q, summary &out) const {
    alignas(64) std::uint64_t x[block_rows];
    q.blocks().for_each([&](unsigned b) {
        decode(unit, b, x);
        if (q.full().contains(b)) {
            const unsigned n = rows_in(unit, b);
            std::uint64_t lo = out.min, hi = out.max;
            for (unsigned r = 0; r < n; ++r)
                lo = std::min(lo, x[r]), hi = std::max(hi, x[r]);
            out.sum += block_sum(x, n), out.count += n, out.min = lo, out.max = hi;
            return;
        }
        masked_summary(x, q.rows_of(b), rows_in(unit, b), out);
    });
}

bool stage::max(unsigned unit, const request &q, std::uint64_t &m) const {
    alignas(64) std::uint64_t x[block_rows];
    q.blocks().for_each([&](unsigned b) {
        decode(unit, b, x);
        each_row(q, b, [&](unsigned i) { m = std::max(m, x[i]); });
    });
    return !q.blocks().empty();
}

std::uint64_t stage::count(unsigned unit, const request &q, const predicate &p) const {
    alignas(64) std::uint64_t x[block_rows];
    return with_op(p.op, [&]<comparison Op>() {
        std::uint64_t c = 0;
        q.blocks().for_each([&](unsigned b) {
            decode(unit, b, x);
            const std::uint64_t t = p.at(b);
            each_row(q, b, [&](unsigned r) { c += ikea::compare<Op>(x[r], t); });
        });
        return c;
    });
}

void stage::filter(unsigned unit, const request &q, const predicate &p, std::span<bitset256> out) const {
    alignas(64) std::uint64_t x[block_rows];
    with_op(p.op, [&]<comparison Op>() {
        q.blocks().for_each([&](unsigned b) {
            decode(unit, b, x);
            const std::uint64_t t = p.at(b);
            // One word at a time, in a register: whole words have a fixed
            // trip count the compiler can vectorise.
            bitset256 m;
            const unsigned n = rows_in(unit, b);
            for (unsigned w = 0; w * 64 < n; ++w) {
                const std::uint64_t *y = x + w * 64;
                std::uint64_t bits = 0;
                if (n - w * 64 >= 64)
                    for (unsigned i = 0; i < 64; ++i)
                        bits |= std::uint64_t{ikea::compare<Op>(y[i], t)} << i;
                else
                    for (unsigned i = 0; i < n - w * 64; ++i)
                        bits |= std::uint64_t{ikea::compare<Op>(y[i], t)} << i;
                m.words[w] = bits;
            }
            out[b] = m & q.rows_of(b);
        });
    });
}

std::size_t stage::select(unsigned unit, const request &q, std::uint64_t *out) const {
    alignas(64) std::uint64_t x[block_rows];
    std::size_t k = 0;
    q.blocks().for_each([&](unsigned b) {
        decode(unit, b, x);
        each_row(q, b, [&](unsigned i) { out[k++] = x[i]; });
    });
    return k;
}

namespace {
request selected(const stage &s, unsigned unit, const bitset256 &blocks, const block_rows_set &rows) {
    return request::make(s.rows(unit), 0, s.rows(unit), blocks,
                         seriespack::selection::bits(0, std::span<const bitset256>(rows)));
}
} // namespace

u128 stage::sum_selected(unsigned unit, const bitset256 &blocks, const block_rows_set &rows) const {
    return sum(unit, selected(*this, unit, blocks, rows));
}
void stage::summarise_selected(unsigned unit, const bitset256 &blocks, const block_rows_set &rows, summary &out) const {
    summarise(unit, selected(*this, unit, blocks, rows), out);
}
bool stage::max_selected(unsigned unit, const bitset256 &blocks, const block_rows_set &rows, std::uint64_t &m) const {
    return max(unit, selected(*this, unit, blocks, rows), m);
}
std::size_t stage::select(unsigned unit, const bitset256 &blocks, const block_rows_set &rows, std::uint64_t *out) const {
    return select(unit, selected(*this, unit, blocks, rows), out);
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

} // namespace ikea::frame::author
