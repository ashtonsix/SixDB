#include "cells.h"

#include <algorithm>
#include <array>
#include <barrier>
#include <charconv>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <sched.h>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <time.h>
#include <unistd.h>

namespace fc = frame_cells;
using fc::u64;
using fc::u128;
namespace {
u64 now(clockid_t clock = CLOCK_MONOTONIC_RAW) {
    timespec t{};
    if (clock_gettime(clock, &t)) throw std::runtime_error("clock_gettime");
    return u64(t.tv_sec) * 1'000'000'000 + u64(t.tv_nsec);
}
u64 number(std::string_view text) {
    u64 n;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), n);
    if (error != std::errc{} || end != text.data() + text.size()) throw std::invalid_argument("bad integer");
    return n;
}
std::string quote(std::string_view text) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += c;
    }
    return out + '"';
}
std::string decimal(u128 n) {
    std::string out;
    do { out += char('0' + n % 10); n /= 10; } while (n);
    std::reverse(out.begin(), out.end());
    return out;
}
u64 mix(u64 x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
struct random {
    u64 state = 1729;
    u64 below(u64 n) {
        const auto reject = (u64{0} - n) % n;
        for (;;) { state += 0x9e3779b97f4a7c15ULL; auto v = mix(state); if (v >= reject) return v % n; }
    }
};
void pin(int cpu) {
    cpu_set_t allowed, mask;
    if (cpu < 0 || cpu >= CPU_SETSIZE || sched_getaffinity(0, sizeof(allowed), &allowed) || !CPU_ISSET(cpu, &allowed))
        throw std::invalid_argument("CPU outside allowed set");
    CPU_ZERO(&mask); CPU_SET(cpu, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask)) throw std::runtime_error("pin failed");
}
void flush(const std::vector<fc::range>& ranges, std::size_t line) {
    for (const auto& r : ranges) {
        if (!r.bytes) continue;
        const auto end = reinterpret_cast<std::uintptr_t>(r.data) + r.bytes;
        for (auto p = reinterpret_cast<std::uintptr_t>(r.data) & ~(line - 1); p < end; p += line) {
#if defined(__x86_64__)
            // CLFLUSH (not optional CLFLUSHOPT); a child tests availability.
            asm volatile("clflush (%0)" :: "r"(p) : "memory");
#elif defined(__aarch64__)
            asm volatile("dc civac, %0" :: "r"(p) : "memory");
#else
            throw std::runtime_error("no cache-conditioning implementation");
#endif
        }
    }
#if defined(__x86_64__)
    asm volatile("mfence" ::: "memory");
#elif defined(__aarch64__)
    asm volatile("dsb ish" ::: "memory");
#endif
}
void qualify_flush(std::size_t line) {
    // Test before creating threads. Unsupported user-space maintenance refuses
    // this condition; it must not silently become a warm-repeat observation.
    const auto pid = fork();
    if (pid < 0) throw std::runtime_error("fork for cache-maintenance test failed");
    if (!pid) {
        alignas(4096) std::array<unsigned char, 4096> probe{};
        probe[0] = 7;
        flush({{probe.data(), probe.size(), probe.size(), "probe"}}, line);
        _exit(probe[0] == 7 ? 0 : 1);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status))
        throw std::runtime_error("cache maintenance unsupported; refusing flushed-first-pass");
}

fc::answer oracle(std::span<const u64> a, std::span<const u64> b, std::span<const u64> coarse,
                  u64 threshold, fc::operation op) {
    fc::answer out;
    for (std::size_t i = 0; i < b.size(); ++i) {
        const auto block = i / fc::fi::block_rows;
        if (!((coarse[block / 64] >> (block % 64)) & 1)) continue;
        if (op == fc::operation::sum || a[i] >= threshold) { out.sum += b[i]; ++out.count; }
    }
    return out;
}
std::vector<u64> coarse_for(std::size_t blocks, const std::string& kind) {
    std::vector<u64> out((blocks + 63) / 64);
    for (std::size_t b = 0; b < blocks; ++b) {
        const bool hit = kind == "all" || (kind == "even" && b % 2 == 0) ||
                         (kind == "cluster" && b < (blocks + 1) / 2);
        if (hit) out[b / 64] |= u64{1} << (b % 64);
    }
    return out;
}

void check() {
    std::size_t cases = 0, points = 0;
    const std::array<u64, 4> spans{255, 65535, 0xffffffffULL, ~u64{0}};
    // Distinct logical sources force every native A/B lane pair, then mixed
    // constant/run/plane/wide blocks and overflowing-u64 aggregate answers.
    for (unsigned ai = 0; ai < 5; ++ai) for (unsigned bi = 0; bi < 5; ++bi) {
        std::vector<u64> a(8 * fc::fi::block_rows), b(a.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            const auto value = [&](unsigned kind, u64 salt) {
                const auto block = i / fc::fi::block_rows, row = i % fc::fi::block_rows;
                if (kind == 4) {
                    if (block % 4 == 0) return u64{17 + block};
                    if (block % 4 == 1) return u64{100 + row / 32};
                    if (block % 4 == 2) return u64{1000 + (mix(row + salt) & 1023)};
                    return mix(i + salt);
                }
                const auto span = spans[kind];
                // Endpoints force the advertised width; nonzero FOR bases in
                // all narrower lanes test bias and mixed-domain thresholds.
                const auto r = row == 0 ? u64{0} : row == 1 ? span : mix(i + salt) & span;
                return r + (kind == 3 ? 0 : 123456789);
            };
            a[i] = value(ai, 19); b[i] = value(bi, 1741);
        }
        for (const auto placement : {fc::fi::placement::address, fc::fi::placement::by_kind})
            for (const std::string coarse : {"all", "none", "even", "cluster"}) {
                const auto mask = coarse_for(a.size() / fc::fi::block_rows, coarse);
                for (const auto threshold : {u64{0}, u64{123456789}, u64{123456800}, u64{0x100000000}, ~u64{0}}) {
                    fc::cell c(a, b, mask, threshold, placement);
                    for (const auto op : {fc::operation::sum, fc::operation::pair})
                        for (const auto arm : {fc::method::frame, fc::method::native, fc::method::alternative}) {
                            const auto expected = oracle(a, b, mask, threshold, op);
                            // Dirty scratch catches accidental use of stale
                            // excluded blocks or a previously selected mask.
                            std::fill(c.scratch.begin(), c.scratch.end(), ~u64{0});
                            if (fc::execute(c, op, arm) != expected) throw std::runtime_error("independent answer mismatch");
                            ++cases;
                        }
                    if (threshold == 0 && coarse == "all") {
                        for (std::size_t i = 0; i < a.size(); ++i) {
                            if (fc::fi::point(c.a, i) != a[i] || fc::fi::point(c.b, i) != b[i])
                                throw std::runtime_error("shared-image point mismatch");
                            points += 2;
                        }
                    }
                }
            }
    }
    // Empty and physical noninterference: a none-selected query may hold no
    // payload at all, independent of otherwise populated source dimensions.
    fc::cell empty({}, {}, {}, 0, fc::fi::placement::address);
    for (const auto op : {fc::operation::sum, fc::operation::pair})
        for (const auto arm : {fc::method::frame, fc::method::native, fc::method::alternative})
            if (fc::execute(empty, op, arm) != fc::answer{}) throw std::runtime_error("empty mismatch");
    std::vector<u64> values(256, 7), none(1);
    fc::cell excluded(values, values, none, 0, fc::fi::placement::address);
    excluded.a = {}; excluded.b = {};
    for (const auto op : {fc::operation::sum, fc::operation::pair})
        for (const auto arm : {fc::method::frame, fc::method::native, fc::method::alternative})
            if (fc::execute(excluded, op, arm) != fc::answer{}) throw std::runtime_error("excluded mismatch");
    unsigned refusals = 0;
    try { fc::cell wrong(values, std::span(values).first(255), none, 0, fc::fi::placement::address); }
    catch (const std::invalid_argument&) { ++refusals; }
    try { fc::cell wrong(values, values, {}, 0, fc::fi::placement::address); }
    catch (const std::invalid_argument&) { ++refusals; }
    std::vector<u64> large(512, 7), one{1}, two{2};
    fc::native small_native(values), large_native(large);
    const auto mask256 = fc::block_mask::bind(one, 256), mask512 = fc::block_mask::bind(two, 512);
    try { fc::validate_native_domains(large_native, mask256); }
    catch (const std::invalid_argument&) { ++refusals; }
    try { fc::validate_native_domains(small_native, mask512); }
    catch (const std::invalid_argument&) { ++refusals; }
    try { fc::validate_native_domains(small_native, mask256, &large_native); }
    catch (const std::invalid_argument&) { ++refusals; }
    if (refusals != 5) throw std::runtime_error("admission mismatch");
    std::cout << "{\"event\":\"check\",\"cases\":" << cases + 12 << ",\"point_values\":" << points
              << ",\"refusals\":" << refusals << ",\"verified\":true}\n";
}

struct config {
    std::string a_path, b_path, coarse = "all", condition = "warm-repeat", sample = "stratified";
    std::vector<int> cpus;
    std::size_t blocks = 8, pool = 1, line = 0, actual_line = 0, max_bytes = 512 * 1024 * 1024;
    u64 duration = 20'000'000, seed = 1729, threshold = 0;
    unsigned trials = 6, quantum = 64;
    bool explicit_threshold = false, shuffled = false;
    fc::operation op = fc::operation::sum;
    fc::fi::placement placement = fc::fi::placement::address;
};
std::vector<u64> load(const std::string& path, const std::vector<std::size_t>& block_ids) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("input open: " + path);
    std::vector<u64> out(block_ids.size() * fc::fi::block_rows);
    for (std::size_t i = 0; i < block_ids.size(); ++i) {
        in.seekg(static_cast<std::streamoff>(block_ids[i] * fc::fi::block_rows * sizeof(u64)));
        in.read(reinterpret_cast<char*>(out.data() + i * fc::fi::block_rows), fc::fi::block_rows * sizeof(u64));
        if (!in) throw std::runtime_error("input shorter than declared block IDs");
    }
    return out;
}
std::size_t file_blocks(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() < 0 || std::size_t(in.tellg()) % sizeof(u64)) throw std::invalid_argument("input needs u64 records");
    return std::size_t(in.tellg()) / sizeof(u64) / fc::fi::block_rows;
}
std::string read_text(const char* path) {
    std::ifstream in(path);
    std::ostringstream out; out << in.rdbuf(); return out.str();
}
struct alignas(128) result {
    u64 start{}, end{}, cpu_ns{}, queries{}, sum_lo{}, sum_hi{}, count{}, mismatch{};
    int actual_cpu = -1;
};
struct worker {
    std::vector<std::unique_ptr<fc::cell>> cells;
    std::vector<std::size_t> order;
    std::array<std::vector<fc::range>, 3> ranges;
    std::array<std::size_t, 3> logical{}, capacity{};
    std::size_t preparation{}, scratch{}, owners{};
    std::string error;
    result measured;
};
fc::answer query(worker& w, const config& c, fc::method arm, u64 at) {
    return fc::execute(*w.cells[w.order[at]], c.op, arm);
}
void verify_result(const result& r, fc::answer expected) {
    const u128 sum = expected.sum * r.queries;
    if (!r.queries || r.mismatch || r.sum_lo != u64(sum) || r.sum_hi != u64(sum >> 64) || r.count != expected.count * r.queries)
        throw std::runtime_error("timed answer differs from independent oracle");
}

void measure(const config& c, const std::vector<u64>& a, const std::vector<u64>& b, const std::vector<u64>& mask) {
    const auto expected = oracle(a, b, mask, c.threshold, c.op);
    const std::size_t threads = c.cpus.size();
    std::vector<worker> workers(threads);
    std::barrier sync(static_cast<std::ptrdiff_t>(threads + 1));
    u64 deadline = 0;
    unsigned treatment = 0;
    bool stop = false;
    std::vector<std::thread> pool;
    for (std::size_t id = 0; id < threads; ++id) pool.emplace_back([&, id] {
        auto& w = workers[id];
        try {
            pin(c.cpus[id]);
            w.cells.reserve(c.pool);
            for (std::size_t i = 0; i < c.pool; ++i)
                w.cells.push_back(std::make_unique<fc::cell>(a, b, mask, c.threshold, c.placement));
            w.order.resize(c.pool); std::iota(w.order.begin(), w.order.end(), 0);
            if (c.shuffled) {
                random rng{c.seed};
                for (std::size_t n = w.order.size(); n > 1; --n) std::swap(w.order[n - 1], w.order[rng.below(n)]);
            }
            auto ordered = w.order;
            std::sort(ordered.begin(), ordered.end());
            for (std::size_t i = 0; i < ordered.size(); ++i)
                if (ordered[i] != i) throw std::runtime_error("pool trace is not a permutation");
            for (unsigned arm = 0; arm < 3; ++arm) for (const auto& cell : w.cells) {
                const auto ranges = cell->ranges(c.op, static_cast<fc::method>(arm));
                for (const auto& r : ranges) { w.logical[arm] += r.bytes; w.capacity[arm] += r.capacity_bytes; }
                w.ranges[arm].insert(w.ranges[arm].end(), ranges.begin(), ranges.end());
            }
            w.preparation = c.pool * mask.size() * sizeof(u64) + w.order.size() * sizeof(std::size_t);
            w.scratch = c.pool * 4 * c.blocks * sizeof(u64);
            w.owners = c.pool * sizeof(fc::cell);
            // Every arm and every physical replica is functionally checked,
            // before the timed/conditioning loop begins.
            for (std::size_t at = 0; at < c.pool; ++at) for (unsigned arm = 0; arm < 3; ++arm)
                if (query(w, c, static_cast<fc::method>(arm), at) != expected)
                    throw std::runtime_error("replica answer differs from independent oracle");
        } catch (const std::exception& e) { w.error = e.what(); }
        sync.arrive_and_wait();
        for (;;) {
            sync.arrive_and_wait();
            if (stop) break;
            const auto arm = static_cast<fc::method>(treatment % 3);
            try {
                if (c.condition == "flushed-first-pass") flush(w.ranges[treatment % 3], c.line);
                else for (std::size_t at = 0; at < c.pool; ++at)
                    if (query(w, c, arm, at) != expected) throw std::runtime_error("conditioning answer mismatch");
            } catch (const std::exception& e) { w.error = e.what(); }
            sync.arrive_and_wait();
            sync.arrive_and_wait();
            result observed;
            fc::answer accumulated;
            const auto cpu_start = now(CLOCK_THREAD_CPUTIME_ID);
            observed.start = now();
            if (c.condition == "flushed-first-pass") {
                for (std::size_t at = 0; at < c.pool; ++at) {
                    const auto r = query(w, c, arm, at);
                    observed.mismatch |= r != expected;
                    accumulated.sum += r.sum; accumulated.count += r.count;
                }
                observed.queries = c.pool;
            } else {
                // A bounded number of complete cells is the clock quantum.
                // No worker's oracle
                // or receipt collection runs until every clock has stopped.
                std::size_t at = 0;
                do {
                    for (unsigned q = 0; q < c.quantum; ++q) {
                        const auto r = query(w, c, arm, at);
                        observed.mismatch |= r != expected;
                        accumulated.sum += r.sum; accumulated.count += r.count;
                        if (++at == w.order.size()) at = 0;
                    }
                    observed.queries += c.quantum;
                } while (now() < deadline);
            }
            observed.end = now(); observed.cpu_ns = now(CLOCK_THREAD_CPUTIME_ID) - cpu_start;
            observed.actual_cpu = sched_getcpu();
            observed.sum_lo = u64(accumulated.sum); observed.sum_hi = u64(accumulated.sum >> 64);
            observed.count = accumulated.count;
            w.measured = observed;
            sync.arrive_and_wait();
        }
    });
    sync.arrive_and_wait();
    const auto close = [&] { stop = true; sync.arrive_and_wait(); for (auto& t : pool) t.join(); };
    try {
        for (const auto& w : workers) if (!w.error.empty()) throw std::runtime_error(w.error);
        std::cout << "{\"event\":\"allocation\",\"workers\":[";
        for (std::size_t id = 0; id < threads; ++id) {
            const auto& w = workers[id];
            if (id) std::cout << ',';
            std::cout << "{\"cpu\":" << c.cpus[id] << ",\"owner_bytes\":" << w.owners
                      << ",\"unflushed_preparation_bytes\":" << w.preparation << ",\"scratch_bytes\":" << w.scratch << ",\"pool_order\":[";
            for (std::size_t i = 0; i < w.order.size(); ++i) { if (i) std::cout << ','; std::cout << w.order[i]; }
            std::cout << "],\"arms\":[";
            for (unsigned arm = 0; arm < 3; ++arm) {
                if (arm) std::cout << ',';
                std::cout << "{\"name\":" << quote(fc::arm_name(c.op, static_cast<fc::method>(arm)))
                          << ",\"logical_range_bytes\":" << w.logical[arm] << ",\"capacity_range_bytes\":" << w.capacity[arm]
                          << ",\"ranges\":[";
                for (std::size_t r = 0; r < w.ranges[arm].size(); ++r) {
                    if (r) std::cout << ',';
                    const auto& at = w.ranges[arm][r];
                    std::cout << "[" << reinterpret_cast<std::uintptr_t>(at.data) << ',' << at.bytes << ',' << at.capacity_bytes
                              << ',' << quote(at.role) << ']';
                }
                std::cout << "]}";
            }
            std::cout << "]}";
        }
        std::cout << "]}\n";
        // Once per cohort, outside all timed work. These are full process
        // maps, not a claim that anonymous malloc arenas are dedicated VMAs.
        std::cout << "{\"event\":\"maps\",\"smaps\":" << quote(read_text("/proc/self/smaps"))
                  << ",\"numa_maps_available\":" << (std::filesystem::exists("/proc/self/numa_maps") ? "true" : "false")
                  << ",\"numa_maps\":" << quote(read_text("/proc/self/numa_maps")) << "}\n";
        constexpr std::array<unsigned, 6> williams{0, 1, 5, 2, 4, 3};
        for (unsigned trial = 0; trial < c.trials; ++trial) for (unsigned position = 0; position < 6; ++position) {
            treatment = (williams[position] + trial) % 6;
            sync.arrive_and_wait(); // treatment publication / begin conditioning
            sync.arrive_and_wait(); // everyone conditioned
            // Same process clock and barrier publication. Start skew and the
            // full-query deadline overshoot are retained in raw intervals.
            deadline = now() + c.duration;
            sync.arrive_and_wait();
            sync.arrive_and_wait(); // every worker has stopped timing
            u64 first = ~u64{0}, last = 0, latest_start = 0, earliest_end = ~u64{0}, total = 0;
            for (const auto& w : workers) {
                if (!w.error.empty()) throw std::runtime_error(w.error);
                verify_result(w.measured, expected);
                first = std::min(first, w.measured.start); last = std::max(last, w.measured.end);
                latest_start = std::max(latest_start, w.measured.start); earliest_end = std::min(earliest_end, w.measured.end);
                total += w.measured.queries;
            }
            const auto overlap = earliest_end > latest_start ? earliest_end - latest_start : 0;
            std::cout << "{\"event\":\"sample\",\"trial\":" << trial << ",\"position\":" << position
                      << ",\"treatment\":" << treatment << ",\"arm\":" << quote(fc::arm_name(c.op, static_cast<fc::method>(treatment % 3)))
                      << ",\"duplicate\":" << treatment / 3 << ",\"deadline_ns\":" << (c.condition == "warm-repeat" ? std::to_string(deadline) : "null")
                      << ",\"wall_ns\":" << last - first << ",\"overlap_ns\":" << overlap
                      << ",\"queries\":" << total << ",\"verified\":true,\"workers\":[";
            for (std::size_t id = 0; id < threads; ++id) {
                if (id) std::cout << ',';
                const auto& r = workers[id].measured;
                std::cout << "{\"cpu\":" << c.cpus[id] << ",\"actual_cpu\":" << r.actual_cpu
                          << ",\"start_ns\":" << r.start << ",\"end_ns\":" << r.end << ",\"cpu_ns\":" << r.cpu_ns
                          << ",\"queries\":" << r.queries << ",\"distinct_replicas\":" << std::min<u64>(c.pool, r.queries)
                          << ",\"sum_lo\":" << r.sum_lo << ",\"sum_hi\":" << r.sum_hi << ",\"count\":" << r.count
                          << ",\"mismatch\":" << r.mismatch << '}';
            }
            std::cout << "]}\n";
        }
        close();
    } catch (...) { close(); throw; }
}
} // namespace

int main(int argc, char** argv) {
    try {
        config c;
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--check") { check(); return 0; }
            if (key == "--help") {
                std::cout << "frame_cells --a A.u64 --b B.u64 --cpus 0[,1] [--operation sum|pair] [--cell-blocks 8] "
                             "[--pool 1] [--ms 20] [--trials 6] [--quantum 64] [--condition warm-repeat|flushed-first-pass] "
                             "[--sample prefix|stratified] [--coarse all|none|even|cluster] [--visit sequential|shuffled] "
                             "[--placement address|kind] [--threshold U64] [--line AUTO] [--max-bytes 536870912]\n";
                return 0;
            }
            if (++i == argc) throw std::invalid_argument("option needs value");
            const std::string value = argv[i];
            if (key == "--a") c.a_path = value;
            else if (key == "--b") c.b_path = value;
            else if (key == "--cell-blocks") c.blocks = number(value);
            else if (key == "--pool") c.pool = number(value);
            else if (key == "--line") c.line = number(value);
            else if (key == "--max-bytes") c.max_bytes = number(value);
            else if (key == "--ms") { const auto ms = number(value); if (ms > 60'000) throw std::invalid_argument("ms too large"); c.duration = ms * 1'000'000; }
            else if (key == "--trials") c.trials = static_cast<unsigned>(number(value));
            else if (key == "--quantum") c.quantum = static_cast<unsigned>(number(value));
            else if (key == "--threshold") { c.threshold = number(value); c.explicit_threshold = true; }
            else if (key == "--seed") c.seed = number(value);
            else if (key == "--coarse") c.coarse = value;
            else if (key == "--condition") c.condition = value;
            else if (key == "--sample") c.sample = value;
            else if (key == "--visit") { if (value != "sequential" && value != "shuffled") throw std::invalid_argument("unknown visit"); c.shuffled = value == "shuffled"; }
            else if (key == "--placement") { if (value != "address" && value != "kind") throw std::invalid_argument("unknown placement"); c.placement = value == "address" ? fc::fi::placement::address : fc::fi::placement::by_kind; }
            else if (key == "--operation") { if (value != "sum" && value != "pair") throw std::invalid_argument("unknown operation"); c.op = value == "sum" ? fc::operation::sum : fc::operation::pair; }
            else if (key == "--cpus") {
                std::istringstream in(value); std::string cpu;
                while (std::getline(in, cpu, ',')) {
                    const auto n = number(cpu); if (n >= CPU_SETSIZE) throw std::invalid_argument("CPU too large");
                    c.cpus.push_back(static_cast<int>(n));
                }
            } else throw std::invalid_argument("unknown option: " + key);
        }
        if (c.a_path.empty() || c.b_path.empty() || c.cpus.empty() || !c.blocks || c.blocks > (1u << 16) ||
            !c.pool || c.pool > (1u << 20) || !c.duration || !c.trials || c.trials > 120 || c.trials % 6 ||
            !c.quantum || c.quantum > 4096) throw std::invalid_argument("invalid bounds/required arguments");
        auto cpus = c.cpus; std::sort(cpus.begin(), cpus.end());
        if (std::adjacent_find(cpus.begin(), cpus.end()) != cpus.end()) throw std::invalid_argument("duplicate CPU");
        for (const auto cpu : c.cpus) { cpu_set_t allowed; if (sched_getaffinity(0, sizeof(allowed), &allowed) || !CPU_ISSET(cpu, &allowed)) throw std::invalid_argument("unavailable CPU"); }
        // A successful maintenance instruction does not qualify its stride.
        // Read real L1 data geometry; a stride no greater than its line size
        // covers every intersected line (smaller strides merely repeat work).
        c.actual_line = std::numeric_limits<std::size_t>::max();
        for (const auto cpu : c.cpus) {
            const auto cache = std::filesystem::path("/sys/devices/system/cpu") / ("cpu" + std::to_string(cpu)) / "cache";
            std::size_t found = 0;
            for (const auto& entry : std::filesystem::directory_iterator(cache)) {
                std::ifstream type_file(entry.path() / "type"), level_file(entry.path() / "level"), line_file(entry.path() / "coherency_line_size");
                std::string type; unsigned level = 0; std::size_t line = 0;
                type_file >> type; level_file >> level; line_file >> line;
                if (type == "Data" && level == 1 && line) found = line;
            }
            if (!found) throw std::invalid_argument("missing L1 data line geometry");
            c.actual_line = std::min(c.actual_line, found);
        }
        if (!c.line) c.line = c.actual_line;
        if (c.line < 32 || c.line > c.actual_line || (c.line & (c.line - 1))) throw std::invalid_argument("flush stride must be a power of two no greater than actual data line size");
        if (c.condition != "warm-repeat" && c.condition != "flushed-first-pass") throw std::invalid_argument("unknown condition");
        if (c.coarse != "all" && c.coarse != "none" && c.coarse != "even" && c.coarse != "cluster") throw std::invalid_argument("unknown coarse mask");
        if (c.sample != "prefix" && c.sample != "stratified") throw std::invalid_argument("unknown sample");
        // Conservative pre-allocation bound includes both native/Frame pairs,
        // image suffixes, index, scratch, objects and range receipts. It is a
        // driver refusal, not a production image sizing policy.
        const u128 estimate = u128{c.cpus.size()} * c.pool * (u128{c.blocks} * 10'000 + 2048);
        if (estimate > c.max_bytes) throw std::invalid_argument("pool exceeds conservative max-bytes bound");
        const auto available = std::min(file_blocks(c.a_path), file_blocks(c.b_path));
        if (c.blocks > available) throw std::invalid_argument("not enough complete source blocks");
        std::vector<std::size_t> ids(c.blocks);
        for (std::size_t i = 0; i < ids.size(); ++i) ids[i] = c.sample == "prefix" ? i : i * available / c.blocks;
        const auto a = load(c.a_path, ids), b = load(c.b_path, ids);
        if (!c.explicit_threshold) { auto ordered = a; std::nth_element(ordered.begin(), ordered.begin() + ordered.size() / 2, ordered.end()); c.threshold = ordered[ordered.size() / 2]; }
        const auto mask = coarse_for(c.blocks, c.coarse);
        if (c.condition == "flushed-first-pass") qualify_flush(c.line);
        const auto expected = oracle(a, b, mask, c.threshold, c.op);
        std::cout << "{\"event\":\"config\",\"protocol\":1,\"operation\":" << quote(c.op == fc::operation::sum ? "sum" : "pair")
                  << ",\"condition\":" << quote(c.condition) << ",\"execution\":" << quote(c.condition == "warm-repeat" ? "common-window" : "completed-batch")
                  << ",\"a\":" << quote(c.a_path) << ",\"b\":" << quote(c.b_path) << ",\"sample\":" << quote(c.sample)
                  << ",\"rows_per_query\":" << b.size() << ",\"pool\":" << c.pool << ",\"duration_ns\":" << c.duration
                  << ",\"trials\":" << c.trials << ",\"quantum\":" << c.quantum << ",\"threshold\":" << c.threshold << ",\"coarse\":" << quote(c.coarse)
                  << ",\"visit\":" << quote(c.shuffled ? "shuffled" : "sequential") << ",\"seed\":" << c.seed
                  << ",\"placement\":" << quote(c.placement == fc::fi::placement::address ? "address" : "kind")
                  << ",\"line_bytes\":" << c.actual_line << ",\"flush_stride_bytes\":" << c.line << ",\"expected_sum\":" << quote(decimal(expected.sum))
                  << ",\"expected_count\":" << expected.count << ",\"source_block_ids\":[";
        for (std::size_t i = 0; i < ids.size(); ++i) { if (i) std::cout << ','; std::cout << ids[i]; }
        std::cout << "]}\n";
        measure(c, a, b, mask);
        return 0;
    } catch (const std::exception& e) { std::cerr << "frame_cells: " << e.what() << '\n'; return 2; }
}
