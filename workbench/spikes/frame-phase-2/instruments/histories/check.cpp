#include "history.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sys/mman.h>
#include <unistd.h>

namespace h = frame_histories;
using h::u64;
using h::u128;
namespace {
std::size_t checks = 0;
void require(bool condition, const char* what) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "history check: %s\n", what); std::abort(); }
}
template<class T> void refused_as(const h::result<T>& result, h::refusal why, const char* what) {
    require(!result && result.error() == why, what);
}
void tiny() {
    const auto f = h::tiny_witness();
    const auto expected = h::prepare_expected(f.original, f.commands);
    require(bool(expected) && expected->size() == 15, "tiny admission");
    const std::array<h::answer, 5> original{{{360, 256}, {0, 4}, {170, 4}, {110, 5}, {0, 0}}};
    const std::array<h::answer, 5> changed{{{514, 256}, {0, 5}, {354, 5}, {355, 5}, {0, 0}}};
    for (std::size_t i = 0; i < expected->size(); ++i) {
        require((*expected)[i].original == original[i % 5], "retained reader hand answer");
        require((*expected)[i].current == (i / 5 == 1 ? changed[i % 5] : original[i % 5]),
                "ordered repeated/novel/restored hand answer");
    }
    auto swapped = f.commands;
    std::swap(swapped.batches[0].edits[1], swapped.batches[0].edits[2]);
    const auto different = h::prepare_expected(f.original, swapped);
    require(different && (*different)[5].current == h::answer{513, 256}, "assignment order changes SUM");
    require((*different)[7].current == h::answer{353, 5}, "assignment order changes selected SUM");
    require((*different)[8].current == h::answer{353, 5}, "duplicate points consume changed row twice");
}
void extrema() {
    h::columns v;
    const u64 hi = u64{1} << 63, max = std::numeric_limits<u64>::max();
    v.a.resize(256); v.b.resize(256);
    v.a[0] = max; v.a[1] = hi; v.a[2] = hi - 1;
    v.b[0] = max; v.b[1] = max; v.b[2] = hi;
    require(h::evaluate(v.a, v.b, {h::operation::sum, h::field::b, 0, {}, {}}) ==
                h::result<h::answer>(h::answer{u128(max) * 2 + hi, 256}), "SUM exceeds u64 exactly");
    require(h::evaluate(v.a, v.b, {h::operation::count_ge, h::field::a, hi, {}, {}}) ==
                h::result<h::answer>(h::answer{0, 2}), "high-bit unsigned threshold");
    require(h::evaluate(v.a, v.b, {h::operation::pair, h::field::b, hi, {}, {}}) ==
                h::result<h::answer>(h::answer{u128(max) * 2, 2}), "selected high-bit exact SUM");
    require(h::evaluate(v.a, v.b, {h::operation::points, h::field::a, 0, {}, {0, 0, 1, 2}}) ==
                h::result<h::answer>(h::answer{u128(max) * 2 + hi + (hi - 1), 4}), "point order/duplicates/extrema");
}
void refused() {
    h::columns v{{}, {}};
    require(h::evaluate(v.a, v.b, {}) == h::result<h::answer>(h::answer{}), "empty SUM");
    v.a.resize(256); v.b.resize(512);
    refused_as(h::evaluate(v.a, v.b, {}), h::refusal::unequal_domains, "unequal domains");
    v.b.resize(256); v.a.resize(255);
    refused_as(h::evaluate(v.a, v.b, {}), h::refusal::unequal_domains, "unequal short domains");
    v.b.resize(255);
    refused_as(h::evaluate(v.a, v.b, {}), h::refusal::incomplete_block, "partial block refused");
    v.a.resize(256); v.b.resize(256);
    refused_as(h::evaluate(v.a, v.b, {h::operation::sum, h::field::b, 0, {2}, {}}),
                h::refusal::bad_mask, "unused mask bit refused");
    refused_as(h::evaluate(v.a, v.b, {h::operation::sum, h::field::b, 0, {1, 0}, {}}),
                h::refusal::bad_mask, "oversized mask refused");
    refused_as(h::evaluate(v.a, v.b, {h::operation::points, h::field::b, 0, {0}, {256}}),
                h::refusal::bad_row, "masked-out invalid index still refused");
    h::batch malformed{{{h::field::a, 0, 99}, {h::field::b, 256, 1}}};
    require(!h::apply(v, malformed) && v.a[0] == 0, "all batch validation before any store");
    h::trace t;
    t.events.push_back({h::event_kind::query, 0});
    refused_as(h::prepare_expected(v, t), h::refusal::bad_event, "bad scheduled reference");
}
void generated() {
    h::columns original;
    original.a.resize(65536); original.b.resize(65536);
    for (std::size_t i = 0; i < original.a.size(); ++i) {
        original.a[i] = i % 97;
        original.b[i] = (u64{1} << 63) + i % 17;
    }
    for (auto pattern : {h::edit_pattern::repeated_hot, h::edit_pattern::dispersed}) {
        for (auto values : {h::edit_values::local_replacement, h::edit_values::dictionary_novelty}) {
            const auto batches = h::make_batches(original, pattern, values);
            require(batches && batches->size() == 8, "eight generated batches");
            for (const auto& b : *batches) require(b.edits.size() == 64, "64 ordered edits per batch");
            for (auto mix : {h::read_mix::reduction, h::read_mix::selection, h::read_mix::point_heavy}) {
                const auto repeated = h::make_reads(original, mix, h::thresholds::repeated);
                const auto varying = h::make_reads(original, mix, h::thresholds::varying);
                require(repeated && varying && repeated->size() == 16, "16 prepared read commands");
                bool different = false;
                for (std::size_t i = 0; i < 16; ++i) {
                    require((*repeated)[i].threshold == (*repeated)[0].threshold, "repeated threshold frozen");
                    different |= (*varying)[i].threshold != (*repeated)[i].threshold;
                    require((*repeated)[i].point_rows == (*varying)[i].point_rows, "threshold mode preserves point trace");
                    if ((*repeated)[i].op == h::operation::points) {
                        require((*repeated)[i].point_rows.size() == 256, "256 independent requests");
                        require((*repeated)[i].point_rows[16] == (*repeated)[i].point_rows[15], "intentional request duplicate");
                    }
                }
                require(different, "varying query list differs");
                auto burst = h::make_trace(*batches, *varying, h::chronology::burst_then_reads);
                auto interleaved = h::make_trace(*batches, *varying, h::chronology::interleaved);
                std::array<std::size_t, 16> br{}, ir{};
                std::vector<std::size_t> be, ie;
                for (auto e : burst.events) {
                    if (e.kind == h::event_kind::query) ++br[e.index]; else be.push_back(e.index);
                }
                for (auto e : interleaved.events) {
                    if (e.kind == h::event_kind::query) ++ir[e.index]; else ie.push_back(e.index);
                }
                require(br == ir && br[0] == 8 && be == ie, "same read multiset and ordered edits");
                const auto bo = h::prepare_expected(original, burst), io = h::prepare_expected(original, interleaved);
                require(bo && io && bo->size() == 128 && io->size() == 128, "all generated current/old results");
                for (std::size_t i = 0; i < 16; ++i) {
                    require((*bo)[112 + i].current == (*io)[112 + i].current, "same terminal logical state");
                    require((*bo)[i].original == (*io)[i].original, "same retained-original answers");
                }
                require(burst.fold_after_batches == std::vector<std::size_t>{4, 8}, "two fold opportunities");
            }
            if (values == h::edit_values::dictionary_novelty) {
                for (const auto& b : *batches) for (const auto& e : b.edits) {
                    const auto& old = e.column == h::field::a ? original.a : original.b;
                    require(std::find(old.begin(), old.end(), e.value) == old.end(), "dictionary-novel assignment absent initially");
                }
            }
        }
    }
}
void excluded_pages() {
    const long page_query = ::sysconf(_SC_PAGESIZE);
    require(page_query > 0 && std::size_t(page_query) % (256 * sizeof(u64)) == 0, "whole blocks per guard page");
    const std::size_t page = std::size_t(page_query), rows = 2 * page / sizeof(u64), blocks = rows / 256;
    auto* p = static_cast<u64*>(::mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    require(p != MAP_FAILED, "guard allocation");
    std::fill(p, p + rows, u64{7});
    require(::mprotect(reinterpret_cast<char*>(p) + page, page, PROT_NONE) == 0, "protected excluded blocks");
    std::vector<u64> coarse((blocks + 63) / 64);
    for (std::size_t b = 0; b < page / (256 * sizeof(u64)); ++b) coarse[b / 64] |= u64{1} << (b % 64);
    const std::span<const u64> input(p, rows);
    for (auto op : {h::operation::sum, h::operation::count_ge, h::operation::pair, h::operation::points}) {
        h::read r{op, h::field::b, 7, coarse, {}};
        if (op == h::operation::points) r.point_rows = {0, rows / 2, 0, rows - 1};
        const auto out = h::evaluate(input, input, r);
        const u64 count = op == h::operation::points ? 2 : rows / 2;
        require(out && out->count == count && out->sum == (op == h::operation::count_ge ? 0 : u128{7} * count),
                "excluded inaccessible blocks never loaded");
        // An invalid request is refused without touching even the active page.
        r.point_rows = {rows};
        refused_as(h::evaluate(input, input, r), h::refusal::bad_row, "structural refusal precedes values");
    }
    require(::mprotect(p, page, PROT_NONE) == 0, "all value pages inaccessible");
    for (auto op : {h::operation::sum, h::operation::count_ge, h::operation::pair, h::operation::points}) {
        h::read empty{op, h::field::a, 0, std::vector<u64>(coarse.size()), {}};
        if (op == h::operation::points) empty.point_rows = {0, rows - 1, 0};
        require(h::evaluate(input, input, empty) == h::result<h::answer>(h::answer{}),
                "empty mask across wholly inaccessible values");
    }
    const auto bad = h::evaluate(input, input, {h::operation::points, h::field::b, 0, coarse, {rows}});
    require(!bad && bad.error() == h::refusal::bad_row, "bad index refused with all values inaccessible");
    const auto unequal = h::evaluate(input, input.first(rows - 1), {});
    require(!unequal && unequal.error() == h::refusal::unequal_domains, "domain refusal with all values inaccessible");
    require(::munmap(p, 2 * page) == 0, "guard release");
}
} // namespace
int main() {
    tiny(); extrema(); refused(); generated(); excluded_pages();
    std::printf("{\"checks\":%zu,\"status\":\"pass\"}\n", checks);
}
