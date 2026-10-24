// Seam cases (CHECKLIST.md B1): three discriminating cases over the same
// images and leaf work, rather than a matrix of composition labels.
//
//   count   COUNT(x >= t) where most blocks settle from descriptors: the
//           per-block law (classify, partial set) against the one-pass
//           descriptor law (count_uniform), the hand glue's per-block loop,
//           and front A's narrow native control (its pair counts rows).
//   pair    predicate on A, then SUM / SUM+MAX / projection of B, at the same
//           selected count with a clustered and a dispersed predicate column:
//           a retained column-wide mask against per-part fusion (A's filter
//           rows feed B's selected SUM part by part, skipping empty blocks),
//           against front A's fused native pair (frame_cells_controls).
//   points  independent points over a column replicated past the LLC: the
//           ready fused plan, a batch-staged plan (prefetch entries, then
//           bodies, then compute) and a Loom Ring of three-stage chains,
//           warm and after flushing.
//
// Every timed answer is checked against plain arrays after its timer stops.
//
// usage: frame3_seams COLS_DIR ROWS ROUNDS
#include "trees.h"

#include <cells.h>
#include <loom/hardware.h>
#include <loom/ring.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <thread>

using namespace frame3;

namespace {
template <class F> double time_ns(F &&f) {
    const auto a = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count();
}
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : v[v.size() / 2];
}
std::vector<u64> load(const std::string &dir, const std::string &name, std::size_t rows) {
    std::ifstream f(dir + "/" + name + ".u64", std::ios::binary | std::ios::ate);
    if (!f)
        std::abort();
    rows = std::min(rows, static_cast<std::size_t>(f.tellg()) / 8) / block_rows * block_rows;
    std::vector<u64> v(rows);
    f.seekg(0);
    f.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
}
u64 quantile(std::vector<u64> v, double q) {
    const std::size_t k = static_cast<std::size_t>(q * static_cast<double>(v.size()));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}
std::vector<u64> flush_buffer(256u << 20);
void flush() {
    volatile u64 s = 0;
    for (std::size_t i = 0; i < flush_buffer.size(); i += 8)
        s += flush_buffer[i]++;
}
void fail(const char *what) {
    std::fprintf(stderr, "wrong answer: %s\n", what);
    std::exit(1);
}

// ---- case 1 -----------------------------------------------------------
void count_case(const std::string &name, const std::vector<u64> &v, int rounds) {
    const u64 t = quantile(v, 0.5);
    u64 want = 0;
    for (u64 x : v)
        want += x >= t;
    column c(bound_pack<sp::preset::compact>(), v);
    frame_cells::native native(v);
    std::vector<u64> all_blocks((v.size() / block_rows + 63) / 64, ~u64{0});
    const auto mask = frame_cells::block_mask::bind(all_blocks, v.size());
    std::vector<std::pair<std::string, std::function<u64()>>> arms{
        {"frame_per_block_law",
         [&] {
             block_args rhs;
             rhs.fill(t);
             u64 n = 0;
             for (unsigned p = 0; p < c.parts(); ++p)
                 n += c.root().count(comparison::greater_equal, p, c.blocks(p), rhs);
             return n;
         }},
        {"frame_descriptor_pass", [&] { return c.count(comparison::greater_equal, t); }},
        {"native_narrow_count", [&] { return frame_cells::native_count_once(native, t, mask); }},
    };
    std::vector<std::vector<double>> ts(arms.size());
    for (int r = 0; r < rounds; ++r)
        for (std::size_t j = 0; j < arms.size(); ++j) {
            const std::size_t i = (j + static_cast<std::size_t>(r)) % arms.size();
            u64 got = 0;
            ts[i].push_back(time_ns([&] { got = arms[i].second(); }));
            if (got != want)
                fail(arms[i].first.c_str());
        }
    for (std::size_t i = 0; i < arms.size(); ++i)
        std::printf("{\"case\":\"count\",\"column\":\"%s\",\"arm\":\"%s\",\"ns_per_block\":%.2f}\n", name.c_str(), arms[i].first.c_str(),
                    median(ts[i]) / static_cast<double>(v.size() / block_rows));
}

// ---- case 2 -----------------------------------------------------------
void pair_case(const std::string &an, const std::vector<u64> &a, const std::string &bn, const std::vector<u64> &b, int rounds) {
    const std::size_t n = std::min(a.size(), b.size()), blocks = n / block_rows;
    const u64 t = quantile(std::vector<u64>(a.begin(), a.begin() + n), 0.9);
    std::vector<u64> av(a.begin(), a.begin() + n), bv(b.begin(), b.begin() + n);
    u128 want_sum = 0;
    u64 want_max = 0, want_count = 0;
    std::size_t touched = 0;
    for (std::size_t blk = 0; blk < blocks; ++blk) {
        bool any = false;
        for (std::size_t i = blk * block_rows; i < (blk + 1) * block_rows; ++i)
            if (av[i] >= t)
                want_sum += bv[i], want_max = std::max(want_max, bv[i]), ++want_count, any = true;
        touched += any;
    }
    column ca(bound_pack<sp::preset::compact>(), av), cb(bound_pack<sp::preset::compact>(), bv);
    frame_cells::native na(av), nb(bv);
    std::vector<u64> all_blocks((blocks + 63) / 64, ~u64{0});
    const auto mask = frame_cells::block_mask::bind(all_blocks, n);
    std::vector<u64> words((n + 63) / 64), out(n);
    struct result {
        u128 sum = 0;
        u64 max = 0, count = 0;
    };
    // Per-part fusion: A's filter rows for one part feed B's selected SUM for
    // the same part, so no column-wide mask exists and empty blocks are skipped.
    auto fused = [&](bool want_projection) {
        result r;
        block_args rhs;
        rhs.fill(t);
        block_rows_set rows;
        std::size_t k = 0;
        for (unsigned p = 0; p < ca.parts(); ++p) {
            const bitset256 all = ca.blocks(p);
            ca.root().filter(comparison::greater_equal, p, all, rhs, rows);
            bitset256 hit;
            each(all, [&](unsigned blk) {
                if (!rows[blk].empty())
                    hit.set(blk);
            });
            if (hit.empty())
                continue;
            if (want_projection) {
                const std::size_t m = cb.root().select(p, hit, rows, out.data() + k);
                for (std::size_t i = k; i < k + m; ++i)
                    r.max = std::max(r.max, out[i]), r.sum += out[i];
                k += m;
            } else
                r.sum += cb.root().sum_selected(p, hit, rows);
        }
        r.count = k;
        return r;
    };
    std::vector<std::pair<std::string, std::function<result()>>> arms{
        {"retained_mask_sum",
         [&] {
             ca.filter(comparison::greater_equal, t, words.data());
             return result{cb.sum(words.data()), 0, 0};
         }},
        {"fused_per_part_sum", [&] { return fused(false); }},
        {"retained_mask_sum_max",
         [&] {
             ca.filter(comparison::greater_equal, t, words.data());
             const std::size_t k = cb.select(words.data(), out.data());
             result r{0, 0, k};
             for (std::size_t i = 0; i < k; ++i)
                 r.sum += out[i], r.max = std::max(r.max, out[i]);
             return r;
         }},
        {"fused_per_part_sum_max", [&] { return fused(true); }},
        {"native_fused_pair_sum",
         [&] {
             const auto ans = frame_cells::native_pair_once(na, nb, t, mask);
             return result{ans.sum, 0, 0};
         }},
        {"native_scalar_sum_max",
         [&] {
             result r;
             for (std::size_t i = 0; i < n; ++i)
                 if (av[i] >= t)
                     r.sum += bv[i], r.max = std::max(r.max, bv[i]), ++r.count;
             return r;
         }},
    };
    std::vector<std::vector<double>> ts(arms.size());
    for (int r = 0; r < rounds; ++r)
        for (std::size_t j = 0; j < arms.size(); ++j) {
            const std::size_t i = (j + static_cast<std::size_t>(r)) % arms.size();
            result got;
            ts[i].push_back(time_ns([&] { got = arms[i].second(); }));
            const bool with_max = arms[i].first.find("max") != std::string::npos;
            if (got.sum != want_sum || (with_max && (got.max != want_max || got.count != want_count)))
                fail(arms[i].first.c_str());
        }
    for (std::size_t i = 0; i < arms.size(); ++i)
        std::printf("{\"case\":\"pair\",\"predicate\":\"%s\",\"payload\":\"%s\",\"selected\":%.3f,\"touched_blocks\":%.3f,\"arm\":\"%s\",\"ns_per_block\":%.2f}\n",
                    an.c_str(), bn.c_str(), static_cast<double>(want_count) / static_cast<double>(n),
                    static_cast<double>(touched) / static_cast<double>(blocks), arms[i].first.c_str(), median(ts[i]) / static_cast<double>(blocks));
}

// ---- case 3 -----------------------------------------------------------
struct point_frame {
    const u32 *rows;
    u64 *out;
};
using point_ring = loom::Ring<point_frame *, u64, u64>::WithShared<const point_plan *>;
// Stage 1 has the entry in cache: prefetch the leaf tile and exceptions,
// yield. Stage 2 computes. arguments: index into rows, stage.
LOOM_NATIVE_CC void point_visit(point_ring::Cursor ring, point_ring::Slot *slot, const point_plan *plan, point_frame *frame, u64 i, u64 stage) {
    const u32 row = frame->rows[i];
    if (stage == 0) {
        plan->prefetch_body(row);
        [[clang::musttail]] return point_ring::yield(ring, slot, plan, frame, i, u64{1});
    }
    frame->out[i] = plan->get(row);
    [[clang::musttail]] return point_ring::finish(ring, slot, plan, frame, i, stage);
}

void points_case(const std::string &name, const std::vector<u64> &base, int rounds) {
    // Replicate past the LLC (with distinct values per copy).
    std::vector<u64> v;
    for (u64 copy = 0; copy < 8; ++copy)
        for (u64 x : base)
            v.push_back(x + copy);
    column c(bound_pack<sp::preset::compact>(), v);
    const auto plan = *point_plan::make(c);
    std::mt19937_64 rng(3);
    std::vector<u32> rows(65536);
    for (auto &r : rows)
        r = static_cast<u32>(rng() % v.size());
    u64 want = 0;
    for (u32 r : rows)
        want += v[r];
    std::vector<u64> out(rows.size());
    frame_cells::native narrow(v);
    std::vector<u64> all_words((v.size() / block_rows + 63) / 64, ~u64{0});
    const auto all_mask = frame_cells::block_mask::bind(all_words, v.size());
    std::vector<std::size_t> request_rows(rows.begin(), rows.end());
    const auto requests = frame_cells::point_requests::bind(request_rows, v.size());
    frame_cells::validate_native_points(narrow, all_mask, requests);

    loom::HardwareOptions options;
    options.limits = {2 * 1024 * 1024, 3 * 1024 * 1024, std::chrono::milliseconds(100)};
    loom::HardwareService hardware(options);
    auto context = loom::discover_hardware();
    auto profile = hardware.request(*context);
    loom::Worker worker;
    if (!context || !profile || !loom::adopt(worker, context->cpu))
        std::abort();
    auto arena = orbital::allocation::open_arena(8 * 1024 * 1024);
    if (!arena || !loom::configure(worker, *arena, 0, *profile))
        std::abort();
    auto demand = loom::bind_demand({loom::MemoryPattern::irregular, 64}, 64);
    point_ring ring;
    if (!demand || !ring.configure(worker, {32, 0}, {&plan}))
        std::abort();
    point_frame frame{rows.data(), out.data()};

    std::vector<std::pair<std::string, std::function<void()>>> arms{
        {"native_u64", [&] {
             for (std::size_t i = 0; i < rows.size(); ++i)
                 out[i] = v[rows[i]];
         }},
        {"plan_ready", [&] {
             for (std::size_t i = 0; i < rows.size(); ++i)
                 out[i] = plan.get(rows[i]);
         }},
        {"plan_batch_staged", [&] {
             constexpr std::size_t B = 32;
             for (std::size_t i0 = 0; i0 < rows.size(); i0 += B) {
                 const std::size_t i1 = std::min(rows.size(), i0 + B);
                 for (std::size_t i = i0; i < i1; ++i)
                     plan.prefetch_entry(rows[i]);
                 for (std::size_t i = i0; i < i1; ++i)
                     plan.prefetch_body(rows[i]);
                 for (std::size_t i = i0; i < i1; ++i)
                     out[i] = plan.get(rows[i]);
             }
         }},
        {"plan_pipelined16", [&] { plan.get_many<16>(rows.data(), rows.size(), out.data()); }},
        {"plan_pipelined32", [&] { plan.get_many<32>(rows.data(), rows.size(), out.data()); }},
        {"native_narrow_points", [&] {
             // Front A's narrow native gather; its answer is the sum, so out
             // carries it in slot 0.
             const auto ans = frame_cells::native_points_once(narrow, all_mask, requests);
             std::fill(out.begin(), out.end(), 0);
             out[0] = static_cast<u64>(ans.sum);
         }},
        {"plan_loom_ring32", [&] {
             for (std::size_t i = 0; i < rows.size(); ++i)
                 while (!ring.push(point_visit, {&frame, u64{i}, u64{0}}, loom::MemoryDependency{plan.entry_address(rows[i]), *demand}))
                     ring.spin({64});
             while (ring.pending())
                 ring.spin({256});
         }},
    };
    for (const bool cold : {false, true}) {
        std::vector<std::vector<double>> ts(arms.size());
        for (int r = 0; r < rounds; ++r)
            for (std::size_t j = 0; j < arms.size(); ++j) {
                const std::size_t i = (j + static_cast<std::size_t>(r)) % arms.size();
                if (cold)
                    flush();
                std::fill(out.begin(), out.end(), 0);
                ts[i].push_back(time_ns(arms[i].second));
                u64 got = 0;
                for (u64 x : out)
                    got += x;
                if (got != want)
                    fail(arms[i].first.c_str());
            }
        for (std::size_t i = 0; i < arms.size(); ++i)
            std::printf("{\"case\":\"points\",\"column\":\"%s\",\"rows\":%zu,\"bits_per_value\":%.2f,\"condition\":\"%s\",\"arm\":\"%s\",\"ns_per_point\":%.2f}\n",
                        name.c_str(), v.size(), 8.0 * static_cast<double>(c.bytes()) / static_cast<double>(v.size()), cold ? "flushed" : "warm",
                        arms[i].first.c_str(), median(ts[i]) / static_cast<double>(rows.size()));
    }
    hardware.cancel();
    if (!ring.close())
        std::abort();
    for (;;) {
        const auto progress = loom::shutdown(worker);
        if (progress.kind() == orbital::Progress::Kind::failed)
            std::abort();
        if (progress.kind() == orbital::Progress::Kind::done)
            break;
        loom::poll(worker, {64, 0});
        std::this_thread::yield();
    }
    loom::relinquish(worker);
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s COLS_DIR ROWS ROUNDS\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::size_t rows = std::strtoull(argv[2], nullptr, 10);
    const int rounds = std::atoi(argv[3]);
    std::map<std::string, std::vector<u64>> cols;
    for (const char *name : {"OS", "UserID", "CounterID", "WatchID", "EventTime", "ClientIP", "RegionID"})
        cols[name] = load(dir, name, rows);
    for (const char *name : {"UserID", "CounterID", "OS", "EventTime"})
        count_case(name, cols[name], rounds);
    // Clustered predicates (CounterID, EventTime follow the sort order) and a
    // dispersed one (WatchID is a hash), all selecting their top 10%.
    for (const char *pred : {"CounterID", "EventTime", "WatchID"})
        for (const char *payload : {"UserID", "ClientIP", "RegionID"})
            pair_case(pred, cols[pred], payload, cols[payload], rounds);
    for (const char *name : {"ClientIP", "UserID", "OS"})
        points_case(name, cols[name], rounds);
}
