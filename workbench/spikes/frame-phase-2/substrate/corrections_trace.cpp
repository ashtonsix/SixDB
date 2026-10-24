// PROGRAMS.md's second witness: corrections must describe the complete
// operation. Front A's original-row histories (instruments/histories) drive
// a candidate built from immutable shared images of A and B plus bounded
// group corrections (corrections.h). Edits go to A or B; every query is
// answered from the candidate and compared, outside any clock, with the
// independent expected answer, and the retained original reader (the
// uncorrected images) with the original answer. Covers the tiny fixture
// (repeated assignments, restoration, a threshold-straddling A edit, coarse
// masks with an excluded block) and real-column dispersed and repeated-hot
// histories. Functional only; no timings.
//
// usage: frame_substrate_corrections_trace COLS_DIR A B
#include "corrections.h"
#include "history.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace fi = frame_image;
namespace h = frame_histories;
using fi::u128;
using fi::u64;

namespace {
std::vector<u64> load(const std::string &path, std::size_t rows) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        std::abort();
    rows = std::min(rows, static_cast<std::size_t>(in.tellg()) / 8) / fi::block_rows * fi::block_rows;
    std::vector<u64> v(rows);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
}

// Fine masks for every row of the coarse-selected blocks (all blocks when
// the coarse list is empty).
std::vector<u64> coarse_rows(const std::vector<u64> &coarse, std::size_t blocks) {
    std::vector<u64> m(4 * blocks, 0);
    for (std::size_t b = 0; b < blocks; ++b)
        if (coarse.empty() || (coarse[b / 64] >> (b % 64) & 1))
            m[4 * b] = m[4 * b + 1] = m[4 * b + 2] = m[4 * b + 3] = ~u64{0};
    return m;
}

struct candidate {
    const fi::image &a_img, &b_img;
    fi::kind_index b_index;
    fi::corrected a, b;
    candidate(const fi::image &ai, const fi::image &bi) : a_img(ai), b_img(bi), b_index(fi::index_by_kind(bi)), a(ai), b(bi) {}

    h::answer query(const h::read &r) const {
        const std::size_t blocks = b_img.blocks;
        const auto coarse = coarse_rows(r.coarse, blocks);
        h::answer out;
        switch (r.op) {
        case h::operation::sum: {
            const fi::corrected &c = r.column == h::field::a ? a : b;
            if (r.coarse.empty()) {
                out.sum = r.column == h::field::a ? c.sum(fi::index_by_kind(a_img)) : c.sum(b_index);
                out.count = blocks * fi::block_rows;
            } else {
                out.sum = c.masked_sum(coarse.data());
                for (u64 w : coarse)
                    out.count += std::popcount(w);
            }
            break;
        }
        case h::operation::count_ge: {
            const fi::corrected &c = r.column == h::field::a ? a : b;
            std::vector<u64> m(4 * blocks);
            c.mask_ge(r.threshold, m.data());
            for (std::size_t i = 0; i < m.size(); ++i)
                out.count += std::popcount(m[i] & coarse[i]);
            break;
        }
        case h::operation::pair: {
            // The complete selected contribution: A's corrected truth and B's
            // corrected value for every selected row.
            std::vector<u64> m(4 * blocks);
            a.mask_ge(r.threshold, m.data());
            for (std::size_t i = 0; i < m.size(); ++i)
                m[i] &= coarse[i], out.count += std::popcount(m[i]);
            out.sum = b.masked_sum(m.data());
            break;
        }
        case h::operation::points:
            for (std::size_t row : r.point_rows) {
                const std::size_t blk = row / fi::block_rows;
                if (!r.coarse.empty() && !(r.coarse[blk / 64] >> (blk % 64) & 1))
                    continue;
                out.sum += b.point(row), ++out.count;
            }
            break;
        }
        return out;
    }
    void apply(const h::batch &batch) {
        for (const auto &e : batch.edits)
            (e.column == h::field::a ? a : b).write(e.row, e.value);
    }
};

bool run(const char *label, const h::columns &original, const h::trace &trace) {
    const auto expected = h::prepare_expected(original, trace);
    if (!expected) {
        std::printf("{\"case\":\"%s\",\"ok\":false,\"refusal\":%d}\n", label, int(expected.error()));
        return false;
    }
    const fi::image ai = fi::build(original.a, fi::placement::by_kind);
    const fi::image bi = fi::build(original.b, fi::placement::by_kind);
    candidate cur(ai, bi);
    const candidate old_reader(ai, bi);  // no corrections: the retained original images
    std::size_t checked = 0, failed = 0, next = 0;
    for (std::size_t e = 0; e < trace.events.size(); ++e) {
        const auto &ev = trace.events[e];
        if (ev.kind == h::event_kind::edits) {
            cur.apply(trace.batches[ev.index]);
            continue;
        }
        while (next < expected->size() && (*expected)[next].event_index < e)
            ++next;
        const auto &x = (*expected)[next];
        const auto &r = trace.reads[x.read_index];
        const h::answer got = cur.query(r), was = old_reader.query(r);
        ++checked;
        if (!(got == x.current) || !(was == x.original)) {
            ++failed;
            if (failed <= 3)
                std::fprintf(stderr, "%s event %zu op %d: got %llu/%llu want %llu/%llu\n", label, e, int(r.op),
                             (unsigned long long)got.sum, (unsigned long long)got.count,
                             (unsigned long long)x.current.sum, (unsigned long long)x.current.count);
        }
    }
    std::printf("{\"case\":\"%s\",\"ok\":%s,\"queries\":%zu,\"failed\":%zu,\"corrections_a\":%zu,\"corrections_b\":%zu}\n", label,
                failed ? "false" : "true", checked, failed, cur.a.entries, cur.b.entries);
    return !failed;
}
}  // namespace

int main(int argc, char **argv) {
    bool ok = true;
    {
        const auto f = h::tiny_witness();
        ok &= run("tiny_witness", f.original, f.commands);
    }
    if (argc >= 4) {
        const std::string dir = argv[1];
        h::columns original{load(dir + "/" + argv[2] + ".u64", 2999808), {}};
        original.b = load(dir + "/" + argv[3] + ".u64", original.a.size());
        for (auto values : {h::edit_values::local_replacement, h::edit_values::dictionary_novelty})
        for (auto pattern : {h::edit_pattern::repeated_hot, h::edit_pattern::dispersed})
            for (auto mix : {h::read_mix::reduction, h::read_mix::selection, h::read_mix::point_heavy}) {
                auto batches = h::make_batches(original, pattern, values);
                auto reads = h::make_reads(original, mix, h::thresholds::varying);
                if (!batches || !reads) {
                    std::printf("{\"case\":\"make\",\"ok\":false}\n");
                    return 1;
                }
                const auto trace = h::make_trace(std::move(*batches), std::move(*reads), h::chronology::interleaved);
                const std::string label = std::string(argv[2]) + ":" + argv[3] + (pattern == h::edit_pattern::dispersed ? ":dispersed" : ":hot") +
                                          (mix == h::read_mix::reduction ? ":reduction" : mix == h::read_mix::selection ? ":selection" : ":points") +
                                          (values == h::edit_values::dictionary_novelty ? ":novel" : ":local");
                ok &= run(label.c_str(), original, trace);
            }
    }
    return ok ? 0 : 1;
}
