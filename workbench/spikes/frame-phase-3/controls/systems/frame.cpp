// Matched six-cell Frame/native driver. Shared stages and native once-controls
// supply all representation and execution algorithms; this TU only composes them.
// Frame queries built stage arrays, not the separately emitted/discarded images.
#include "../../proto/trees.h"

#include <cells.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace fc = frame_cells;
namespace {
using u64 = frame3::u64;
using u128 = frame3::u128;
using answer = fc::answer;
using clock_type = std::chrono::steady_clock;
enum class op { sum, count, pair };
void need(bool ok, const char *what) { if (!ok) throw std::runtime_error(what); }
const char *name(op operation) { return operation == op::sum ? "sum" : operation == op::count ? "count" : "pair"; }
std::string decimal(u128 value) {
    std::string out;
    do { out.push_back(static_cast<char>('0' + value % 10)); value /= 10; } while (value);
    std::reverse(out.begin(), out.end());
    return out;
}
answer expected(std::span<const u64> a, std::span<const u64> b, op operation, u64 threshold) {
    need(a.size() == b.size(), "unequal oracle columns");
    answer out;
    for (std::size_t r = 0; r < a.size(); ++r)
        if (operation == op::sum || a[r] >= threshold) {
            ++out.count;
            if (operation != op::count) out.sum += b[r];
        }
    return out;
}

struct frame_query {
    op operation;
    std::size_t rows;
    u64 threshold;
    std::unique_ptr<frame3::column> a, b;
    frame3::block_args rhs{};
    frame3::block_rows_set selected{}; // Private scratch, prepared once, overwritten by every filter.
    static std::unique_ptr<frame3::stage> recipe() {
        // The owner image contract places packed arrays in 16-block groups.
        // bound_pack() defaults to part-wide placement, which cannot emit
        // these group images. Construct the shared stages at the image grain.
        return std::make_unique<frame3::bound>(
            std::make_unique<frame3::pack<frame3::sp::preset::compact>>(frame3::group_blocks));
    }
    frame_query(std::span<const u64> av, std::span<const u64> bv, op operation, u64 threshold)
        : operation(operation), rows(av.size()), threshold(threshold) {
        need(av.size() == bv.size(), "unequal Frame columns");
        if (operation != op::sum)
            a = std::make_unique<frame3::column>(recipe(), av);
        if (operation != op::count)
            b = std::make_unique<frame3::column>(recipe(), bv);
        rhs.fill(threshold);
    }
    answer execute() {
        if (operation == op::sum) return {b->sum(), rows}; // Known cardinality, no extra COUNT.
        if (operation == op::count) return {0, a->count(frame3::comparison::greater_equal, threshold)};
        answer out;
        for (unsigned p = 0; p < a->parts(); ++p) {
            const auto all = a->blocks(p);
            a->root().filter(frame3::comparison::greater_equal, p, all, rhs, selected);
            frame3::bitset256 hit;
            frame3::each(all, [&](unsigned block) {
                if (!selected[block].empty()) {
                    hit.set(block);
                    out.count += selected[block].count();
                }
            });
            if (!hit.empty()) out.sum += b->root().sum_selected(p, hit, selected);
        }
        return out;
    }
    std::size_t serialized_bytes() const {
        std::size_t bytes = 0;
        // Emit actual complete group images outside all clocks; bytes() is not
        // substituted for serialization, nor called a retained-capacity proof.
        for (const auto *c : {a.get(), b.get()}) if (c)
            for (const auto &[path, stream] : c->streams()) {
                (void)path;
                for (unsigned p = 0; p < c->parts(); ++p)
                    for (unsigned g = 0; g < stream->groups_in(p); ++g) bytes += c->image(*stream, p, g).size();
            }
        return bytes;
    }
};

struct native_query {
    op operation;
    u64 threshold;
    std::unique_ptr<fc::native> a, b;
    std::vector<u64> coarse;
    fc::block_mask all;
    native_query(std::span<const u64> av, std::span<const u64> bv, op operation, u64 threshold)
        : operation(operation), threshold(threshold), coarse((av.size() / 256 + 63) / 64, ~u64{0}) {
        need(av.size() == bv.size() && av.size() % 256 == 0, "native needs equal complete-block columns");
        if (operation != op::sum) a = std::make_unique<fc::native>(av);
        if (operation != op::count) b = std::make_unique<fc::native>(bv);
        all = fc::block_mask::bind(coarse, av.size());
        if (operation == op::pair) fc::validate_native_domains(*b, all, a.get());
        else fc::validate_native_domains(operation == op::sum ? *b : *a, all);
    }
    answer execute() const {
        if (operation == op::sum) return fc::native_sum_once(*b, all);
        if (operation == op::count) return {0, fc::native_count_once(*a, threshold, all)};
        return fc::native_pair_once(*a, *b, threshold, all);
    }
    std::size_t payload_bytes(bool capacity) const {
        std::size_t bytes = 0;
        for (const auto *c : {a.get(), b.get()}) if (c)
            bytes += std::visit([&](const auto &v) {
                using value_type = typename std::decay_t<decltype(v)>::value_type;
                return (capacity ? v.capacity() : v.size()) * sizeof(value_type);
            }, c->values);
        return bytes;
    }
};

// The source oracle is never derived from fitted descriptors, emitted bytes or
// another arm. Complete blocks include zero, wide extrema and distinct A/Bs.
std::size_t qualify() {
    struct fixture { std::vector<u64> a, b; u64 interior; unsigned a_lane{}, b_lane{}; };
    std::vector<fixture> fixtures{{{}, {}, 1}, {std::vector<u64>(512), std::vector<u64>(512), 1}};
    fixture wide{{}, {}, u64{1} << 63, 8, 8}, narrow{{}, {}, (u64{1} << 63) + 66536, 2, 4},
        swapped{{}, {}, (u64{1} << 63) + 135536, 4, 2};
    constexpr u64 a_base = (u64{1} << 63) + 65536, b_base = u64{1} << 62;
    const std::array<u64, 8> extrema{0, 1, ~u64{0}, ~u64{0} - 1, u64{1} << 63, (u64{1} << 63) + 7, 17, UINT32_MAX};
    const std::array<u64, 5> a16{0, 300, 65535, 1000, 40000}, b32{UINT32_MAX, (u64{1} << 31) + 17, 0, 37, UINT32_MAX - 13};
    const std::array<u64, 5> a32{UINT32_MAX, 0, 70000, 2100000000, 17}, b16{0, 65535, 700, 1, 40000};
    for (unsigned r = 0; r < 512; ++r) {
        wide.a.push_back(extrema[r % 8]); wide.b.push_back(extrema[(3 * r + 2) % 8]);
        narrow.a.push_back(a_base + a16[r % 5]); narrow.b.push_back(b_base + b32[r % 5]);
        swapped.a.push_back(a_base + a32[r % 5]); swapped.b.push_back(b_base + b16[r % 5]);
    }
    fixtures.push_back(std::move(wide)); fixtures.push_back(std::move(narrow)); fixtures.push_back(std::move(swapped));
    std::size_t checks = 0;
    for (const auto &f : fixtures) for (auto operation : {op::sum, op::count, op::pair})
        for (auto t : {u64{0}, f.interior, ~u64{0}}) {
            const auto want = expected(f.a, f.b, operation, t);
            frame_query frame(f.a, f.b, operation, t);
            native_query native(f.a, f.b, operation, t);
            if (operation != op::sum && f.a_lane) need(native.a->lane() == f.a_lane, "synthetic native predicate lane");
            if (operation != op::count && f.b_lane) need(native.b->lane() == f.b_lane, "synthetic native payload lane");
            need(frame.execute() == want, "synthetic Frame answer"); ++checks;
            need(native.execute() == want, "synthetic native answer"); ++checks;
            (void)frame.serialized_bytes(); // Qualify the separately reported image-emission route too.
        }
    return checks;
}

std::vector<u64> load(const std::string &directory, const std::string &column, std::size_t rows) {
    need(std::endian::native == std::endian::little, "little-endian corpus required");
    std::ifstream file(directory + "/" + column + ".u64", std::ios::binary | std::ios::ate);
    need(file && static_cast<std::size_t>(file.tellg()) >= rows * 8, "missing/short corpus input");
    std::vector<u64> out(rows); file.seekg(0);
    file.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(rows * 8));
    need(bool(file), "read corpus prefix");
    return out;
}
u64 quantile(std::span<const u64> values, unsigned percent) {
    std::vector<u64> copy(values.begin(), values.end());
    const auto k = copy.size() * percent / 100;
    std::nth_element(copy.begin(), copy.begin() + k, copy.end());
    return copy[k];
}
double elapsed(clock_type::time_point start) {
    return std::chrono::duration<double, std::nano>(clock_type::now() - start).count();
}
void measure(const char *label, const char *an, const char *bn, const std::map<std::string, std::vector<u64>> &columns,
    op operation, unsigned percent, unsigned rounds, unsigned rotation) {
    const auto &a = columns.at(an), &b = columns.at(bn);
    const auto threshold = operation == op::sum ? 0 : quantile(a, percent);
    const auto want = expected(a, b, operation, threshold);
    std::size_t touched = 0;
    for (std::size_t first = 0; first < a.size(); first += 256) {
        bool hit = false;
        for (std::size_t r = first; r < std::min(first + 256, a.size()); ++r) hit |= operation == op::sum || a[r] >= threshold;
        touched += hit;
    }
    std::unique_ptr<frame_query> frame;
    std::unique_ptr<native_query> native;
    std::array<double, 2> construction{};
    for (unsigned j = 0; j < 2; ++j) {
        const unsigned arm = (j + rotation) % 2;
        const auto start = clock_type::now();
        if (arm == 0) frame = std::make_unique<frame_query>(a, b, operation, threshold);
        else native = std::make_unique<native_query>(a, b, operation, threshold);
        construction[arm] = elapsed(start);
    }
    const auto serialized = frame->serialized_bytes(); // Serialization scratch dies before warm queries.
    const auto native_size = native->payload_bytes(false), native_capacity = native->payload_bytes(true);
    const auto source_bytes = (operation == op::sum ? b.capacity() : a.capacity()) * sizeof(u64) +
        (operation == op::pair && &a != &b ? b.capacity() * sizeof(u64) : 0);
    need(frame->execute() == want, "warm Frame answer");
    need(native->execute() == want, "warm native answer");
    std::array<std::vector<double>, 2> samples;
    for (auto &sample : samples) sample.reserve(rounds);
    for (unsigned round = 0; round < rounds; ++round) for (unsigned j = 0; j < 2; ++j) {
        const unsigned arm = (j + round + rotation) % 2;
        const auto start = clock_type::now();
        const answer actual = arm == 0 ? frame->execute() : native->execute();
        const double ns = elapsed(start);
        samples[arm].push_back(ns);
        need(actual == want, "timed exact Frame/native answer");
    }
    for (unsigned arm = 0; arm < 2; ++arm) {
        auto sorted = samples[arm]; std::sort(sorted.begin(), sorted.end());
        const auto frame_bytes = arm == 0 ? std::to_string(serialized) : "null";
        const auto native_bytes = arm == 1 ? std::to_string(native_size) : "null";
        const auto native_cap = arm == 1 ? std::to_string(native_capacity) : "null";
        const auto a_lane = arm == 1 && native->a ? std::to_string(native->a->lane()) : "null";
        const auto b_lane = arm == 1 && native->b ? std::to_string(native->b->lane()) : "null";
        std::printf("{\"engine\":\"%s\",\"case\":\"%s\",\"arm\":\"%s\",\"op\":\"%s\",\"a\":\"%s\",\"b\":\"%s\",\"rows\":%zu,\"threshold\":\"%llu\",\"selected_rows\":%llu,\"touched_blocks\":%zu,\"sum\":\"%s\",\"construction_ns\":%.0f,\"construction_boundary\":\"fit_build_and_domain_admission\",\"preparation_ns\":null,\"known_source_capacity_bytes\":%zu,\"original_corpus_columns_in_process\":5,\"frame_serialized_image_bytes\":%s,\"native_typed_payload_size_bytes\":%s,\"native_typed_payload_capacity_bytes\":%s,\"engine_retained_capacity_bytes\":null,\"a_lane_bytes\":%s,\"b_lane_bytes\":%s,\"condition\":\"repeated_after_untimed_warm_query_no_flush\",\"query_boundary\":\"%s\",\"execution_storage\":\"%s\",\"co_resident_query_treatments\":2,\"threads\":1,\"round_rotation\":%u,\"median_ns\":%.0f,\"samples_ns\":[",
            arm == 0 ? "frame" : "native", label, arm == 0 ? "bound_pack_compact_groups16" : "native_typed_for", name(operation), an, bn,
            a.size(), static_cast<unsigned long long>(threshold), static_cast<unsigned long long>(want.count), touched,
            decimal(want.sum).c_str(), construction[arm], source_bytes, frame_bytes.c_str(), native_bytes.c_str(), native_cap.c_str(),
            a_lane.c_str(), b_lane.c_str(), arm == 0 ? "shared_stage_query_consume" : "native_once_consume",
            arm == 0 ? "built_stage_arrays" : "typed_FOR_arrays", rotation, sorted[sorted.size() / 2]);
        for (std::size_t i = 0; i < samples[arm].size(); ++i) std::printf("%s%.0f", i ? "," : "", samples[arm][i]);
        std::printf("]}\n");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--check-only") {
            std::printf("{\"checks\":%zu,\"pass\":true}\n", qualify());
            return 0;
        }
        need(argc == 4, "usage: frame3_frame_corpus COLS_DIR ROWS ROUNDS");
        const auto rows = std::stoull(argv[2]);
        const auto rounds = static_cast<unsigned>(std::stoul(argv[3]));
        need(rows && rows <= 65536 && rows % 256 == 0 && rounds && rounds <= 101 && rounds % 2 == 1, "bounded rows and odd rounds");
        std::fprintf(stderr, "Frame/native synthetic qualification: %zu exact answers passed\n", qualify());
        std::map<std::string, std::vector<u64>> columns;
        for (const char *n : {"OS", "URLHash", "EventTime", "WatchID", "ClientIP"}) columns[n] = load(argv[1], n, rows);
        unsigned rotation = 0;
        for (const char *n : {"OS", "URLHash"}) {
            const auto sum_name = std::string("sum_") + n, count_name = std::string("count_") + n;
            measure(sum_name.c_str(), n, n, columns, op::sum, 0, rounds, rotation++);
            measure(count_name.c_str(), n, n, columns, op::count, 50, rounds, rotation++);
        }
        measure("pair_EventTime_ClientIP", "EventTime", "ClientIP", columns, op::pair, 90, rounds, rotation++);
        measure("pair_WatchID_ClientIP", "WatchID", "ClientIP", columns, op::pair, 90, rounds, rotation++);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
