#include "bandwidth_kernels.h"
#include <algorithm>
#include <array>
#include <barrier>
#include <bit>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <sched.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <thread>
#include <unordered_map>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace fi = frame_instruments;
namespace {
using u64 = std::uint64_t;

u64 clock_ns(clockid_t clock = CLOCK_MONOTONIC_RAW) {
    timespec at{};
    if (clock_gettime(clock, &at)) throw std::runtime_error("clock_gettime failed");
    return u64(at.tv_sec) * 1'000'000'000 + at.tv_nsec;
}

u64 number(std::string_view text) {
    u64 result{};
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument("invalid integer: " + std::string(text));
    return result;
}

std::string json_quote(std::string_view text) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += c;
    }
    return out + '"';
}

void pin(int cpu) {
    cpu_set_t allowed;
    if (cpu < 0 || cpu >= CPU_SETSIZE || sched_getaffinity(0, sizeof(allowed), &allowed) ||
        !CPU_ISSET(cpu, &allowed)) throw std::invalid_argument("CPU outside allowed affinity");
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set)) throw std::runtime_error("pin failed");
}

// A fully specified PRNG and shuffle keep the same line permutation across ISAs.
u64 mix(u64 x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
struct Random {
    u64 state;
    u64 next() { state += 0x9e3779b97f4a7c15ULL; return mix(state); }
    u64 below(u64 n) {
        const auto threshold = (u64(0) - n) % n;
        for (;;) { auto x = next(); if (x >= threshold) return x % n; }
    }
};

struct Config {
    std::vector<int> cpus;
    std::size_t bytes = 16 * 1024;
    std::size_t line = 64;
    unsigned chains = 1;
    unsigned trials = 4;
    u64 target_ns = 20'000'000;
    u64 seed = 1729;
    bool random = false;
    bool check = false;
    bool steady = false;
};

// Guard VMAs keep mapping receipts distinct even when the kernel would merge
// adjacent anonymous allocations. The worker first touches its own body.
struct Image {
    void* reserve = MAP_FAILED;
    std::size_t reserve_bytes{};
    std::uint64_t* values{};
    std::size_t bytes{}, line{}, cycle{};
    unsigned chains{};
    u64 sum{}, trace_hash = 0xcbf29ce484222325ULL;
    std::vector<std::uint32_t> permutation;
    std::array<fi::word, 64> starts{};
    int advice_errno{};

    explicit Image(const Config& c) : bytes(c.bytes), line(c.line), chains(c.chains) {
        const std::size_t page = std::size_t(sysconf(_SC_PAGESIZE));
        reserve_bytes = bytes + 2 * page;
        reserve = mmap(nullptr, reserve_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (reserve == MAP_FAILED) throw std::runtime_error("mmap: " + std::string(strerror(errno)));
        values = reinterpret_cast<std::uint64_t*>(static_cast<char*>(reserve) + page);
        if (mprotect(values, bytes, PROT_READ | PROT_WRITE)) {
            munmap(reserve, reserve_bytes); reserve = MAP_FAILED;
            throw std::runtime_error("mprotect failed");
        }
        if (madvise(values, bytes, MADV_NOHUGEPAGE)) advice_errno = errno;
        for (std::size_t i = 0; i < bytes / sizeof(u64); ++i) {
            const u64 value = mix(c.seed + i);
            values[i] = value; sum += value;
        }
        if (!c.random) return;
        const auto lines = bytes / line;
        cycle = lines / chains;
        permutation.resize(lines);
        std::iota(permutation.begin(), permutation.end(), 0);
        Random rng{c.seed};
        for (std::size_t n = lines; n > 1; --n)
            std::swap(permutation[n - 1], permutation[rng.below(n)]);
        for (auto id : permutation) {
            // Identity of the logical line order, not ASLR-dependent pointers.
            for (unsigned b = 0; b < 4; ++b) {
                trace_hash ^= (id >> (b * 8)) & 255;
                trace_hash *= 0x100000001b3ULL;
            }
        }
        for (unsigned k = 0; k < chains; ++k) {
            starts[k] = address(k * cycle);
            for (std::size_t p = 0; p < cycle; ++p)
                *reinterpret_cast<fi::word*>(address(k * cycle + p)) =
                    address(k * cycle + (p + 1) % cycle);
        }
        // Check every edge independently of the timed chase; the permutation
        // also proves that cycles partition the complete addressed line set.
        std::vector<bool> seen(lines);
        for (auto i : permutation) {
            if (i >= lines || seen[i]) throw std::runtime_error("permutation is not bijective");
            seen[i] = true;
        }
        for (unsigned k = 0; k < chains; ++k)
            for (std::size_t p = 0; p < cycle; ++p)
                if (*reinterpret_cast<const fi::word*>(address(k * cycle + p)) !=
                    address(k * cycle + (p + 1) % cycle))
                    throw std::runtime_error("cycle edge mismatch");
    }
    ~Image() { if (reserve != MAP_FAILED) munmap(reserve, reserve_bytes); }
    Image(const Image&) = delete;
    fi::word address(std::size_t p) const {
        return reinterpret_cast<fi::word>(values) + std::size_t(permutation[p]) * line;
    }
    u64 run(const Config& c, u64 passes, std::array<fi::word, 64>& at) const {
        if (!c.random) {
            u64 answer = 0;
            // Opaque separate-TU calls prevent lifting one read over repetitions.
            for (u64 n = 0; n < passes; ++n) answer += fi::scan_once(values, bytes / sizeof(u64));
            return answer;
        }
        // The extra step makes omitting whole-cycle work observably wrong for
        // a zero-work implementation, even when the pass count is a power of 2.
        const u64 steps = passes * cycle + 1;
        fi::chase_for(chains)(at.data(), steps);
        return 0;
    }
    u64 units_for(const Config& c, u64 passes) const {
        return c.random ? chains * (passes * cycle + 1) : passes * (bytes / sizeof(u64));
    }
    u64 stream(const Config& c, u64 deadline, std::array<fi::word, 64>& at, u64& units) const {
        u64 answer = 0;
        units = 0;
        if (c.random) {
            const auto body = fi::chase_for(chains);
            constexpr u64 loads = 4096;
            do {
                body(at.data(), loads / chains);
                units += loads;
            } while (clock_ns() < deadline);
        } else {
            const auto words = bytes / sizeof(u64);
            std::size_t position = 0;
            do {
                // Bound the last chunk rather than giving a fast worker an
                // entire long scan's head start during the cohort's drain.
                std::size_t moved = 0;
                while (moved < 8192) {
                    const auto take = std::min({std::size_t(8192) - moved, words - position});
                    answer += fi::scan_once(values + position, take);
                    position = (position + take) % words;
                    moved += take;
                    units += take;
                }
            } while (clock_ns() < deadline);
        }
        return answer;
    }
    void verify(const Config& c, u64 units, u64 answer, const std::array<fi::word, 64>& at) const {
        if (!c.random) {
            const auto words = bytes / sizeof(u64);
            u64 expected = sum * (units / words);
            for (std::size_t i = 0; i < units % words; ++i) expected += mix(c.seed + i);
            if (answer != expected) throw std::runtime_error("sequential answer mismatch");
            return;
        }
        if (units % chains) throw std::runtime_error("unequal chain step count");
        const u64 steps = units / chains;
        for (unsigned k = 0; k < chains; ++k) {
            if (at[k] != address(k * cycle + steps % cycle))
                throw std::runtime_error("random final position mismatch");
        }
    }
    void check_steps() const {
        for (u64 steps : {u64(0), u64(1), u64(cycle - 1), u64(cycle + 7), u64(3 * cycle + 1)}) {
            auto at = starts;
            fi::chase_for(chains)(at.data(), steps);
            for (unsigned k = 0; k < chains; ++k)
                if (at[k] != address(k * cycle + steps % cycle))
                    throw std::runtime_error("short/wrapping chase law failed");
        }
    }

};

struct alignas(128) Observation {
    u64 start{}, end{}, cpu_ns{}, answer{}, units{};
    int actual_cpu{};
    std::exception_ptr error;
    std::unique_ptr<Image> image;
    std::array<fi::word, 64> cursors{};
};

class Cohort {
    const Config& config;
    std::barrier<> gate;
    u64 passes = 0;
    u64 deadline = 0;
    std::vector<Observation> observations;
    std::vector<std::jthread> workers;
public:
    explicit Cohort(const Config& c) : config(c), gate(std::ptrdiff_t(c.cpus.size() + 1)),
                                      observations(c.cpus.size()) {
        workers.reserve(c.cpus.size());
        try {
        for (std::size_t i = 0; i < c.cpus.size(); ++i) workers.emplace_back([&, i] {
            auto& at = observations[i];
            try {
                pin(c.cpus[i]);
                at.image = std::make_unique<Image>(c);
                if (c.random) at.image->check_steps();
            } catch (...) { at.error = std::current_exception(); }
            gate.arrive_and_wait();
            for (;;) {
                gate.arrive_and_wait();
                if (!passes) break;
                if (!at.error) try {
                    // Declared warm-repeat condition, outside the timed interval.
                    auto warm = at.image->starts;
                    auto answer = at.image->run(c, 1, warm);
                    at.image->verify(c, at.image->units_for(c, 1), answer, warm);
                    at.cursors = at.image->starts;
                } catch (...) { at.error = std::current_exception(); }
                gate.arrive_and_wait();
                gate.arrive_and_wait();  // Publish the common read deadline.
                if (!at.error) try {
                    const auto before = clock_ns(CLOCK_THREAD_CPUTIME_ID);
                    at.start = clock_ns();
                    if (c.steady) at.answer = at.image->stream(c, deadline, at.cursors, at.units);
                    else {
                        at.answer = at.image->run(c, passes, at.cursors);
                        at.units = at.image->units_for(c, passes);
                    }
                    at.end = clock_ns();
                    at.cpu_ns = clock_ns(CLOCK_THREAD_CPUTIME_ID) - before;
                    at.actual_cpu = sched_getcpu();
                } catch (...) { at.error = std::current_exception(); }
                // Keep potentially large partial-scan oracle work out of every
                // worker's timed interval, including the last worker to finish.
                gate.arrive_and_wait();
                if (!at.error) try {
                    at.image->verify(c, at.units, at.answer, at.cursors);
                    if (c.random) {
                        for (unsigned k = 0; k < c.chains; ++k) at.answer ^= mix(at.cursors[k] + k);
                    }
                } catch (...) { at.error = std::current_exception(); }
                gate.arrive_and_wait();
            }
        });
        } catch (...) {
            // A partial thread launch must not strand already-created workers.
            for (std::size_t i = workers.size(); i < c.cpus.size(); ++i) gate.arrive_and_drop();
            gate.arrive_and_wait();
            passes = 0; gate.arrive_and_wait();
            workers.clear();
            throw;
        }
        gate.arrive_and_wait();
    }
    ~Cohort() { passes = 0; gate.arrive_and_wait(); }
    void require_valid() const {
        for (const auto& at : observations) if (at.error) std::rethrow_exception(at.error);
    }
    void memory() const {
        // /proc/self/smaps walks the process's page tables. Read it once after
        // every mapping is ready, rather than once per worker over the whole MM.
        std::unordered_map<fi::word, std::size_t> owners;
        for (std::size_t i = 0; i < observations.size(); ++i)
            owners.emplace(reinterpret_cast<fi::word>(observations[i].image->values), i);
        std::vector<std::string> smaps(observations.size()), nodes(observations.size());
        std::ifstream pages("/proc/self/smaps");
        std::size_t selected = observations.size();
        for (std::string row; std::getline(pages, row);) {
            fi::word first{};
            auto [end, error] = std::from_chars(row.data(), row.data() + row.size(), first, 16);
            if (error == std::errc{} && end != row.data() + row.size() && *end == '-') {
                auto found = owners.find(first);
                selected = found == owners.end() ? observations.size() : found->second;
            }
            if (selected < observations.size()) smaps[selected] += row + '\n';
        }
        std::ifstream placement("/proc/self/numa_maps");
        for (std::string row; std::getline(placement, row);) {
            fi::word first{};
            auto [end, error] = std::from_chars(row.data(), row.data() + row.size(), first, 16);
            if (error != std::errc{}) continue;
            auto found = owners.find(first);
            if (found != owners.end()) nodes[found->second] += row + '\n';
        }
        for (std::size_t i = 0; i < observations.size(); ++i) {
            const auto& at = observations[i];
            std::cout << "{\"type\":\"mapping\",\"cpu\":" << config.cpus[i]
                      << ",\"bytes\":" << config.bytes << ",\"line\":" << config.line
                      << ",\"trace_hash_fnv64\":" << json_quote(std::to_string(at.image->trace_hash))
                      << ",\"retained_untimed_permutation_bytes\":"
                      << at.image->permutation.capacity() * sizeof(std::uint32_t)
                      << ",\"madv_nohugepage_errno\":" << at.image->advice_errno
                      << ",\"smaps\":" << json_quote(smaps[i]) << ",\"numa_maps\":" << json_quote(nodes[i]) << "}\n";
        }
    }
    u64 sample(u64 repeat, int trial, std::string_view arm) {
        passes = repeat;
        gate.arrive_and_wait();  // Let each worker condition its own image.
        gate.arrive_and_wait();  // No timed work starts until conditioning ends.
        deadline = clock_ns() + config.target_ns;
        gate.arrive_and_wait();
        gate.arrive_and_wait();
        gate.arrive_and_wait();
        require_valid();
        u64 first = std::numeric_limits<u64>::max(), last = 0, late = 0;
        u64 early = std::numeric_limits<u64>::max();
        for (std::size_t i = 0; i < observations.size(); ++i) {
            const auto& at = observations[i];
            if (at.actual_cpu != config.cpus[i] || at.end <= at.start)
                throw std::runtime_error("bad timed CPU or interval");
            first = std::min(first, at.start); last = std::max(last, at.end);
            late = std::max(late, at.start); early = std::min(early, at.end);
        }
        const u64 units = observations[0].units;
        u64 total_units = 0;
        for (const auto& at : observations) total_units += at.units;
        if (trial >= 0) {
            std::cout << "{\"type\":\"sample\",\"protocol\":2,\"mode\":"
                      << json_quote(config.random ? "chase" : "scan") << ",\"arm\":" << json_quote(arm)
                      << ",\"execution\":" << json_quote(config.steady ? "common-window" : "fixed-batch")
                      << ",\"duration_ns\":" << (config.steady ? config.target_ns : 0)
                      << ",\"common_deadline_ns\":" << (config.steady ? deadline : 0)
                      << ",\"condition\":\"warm-repeat\",\"bytes_per_worker\":" << config.bytes
                      << ",\"line\":" << config.line << ",\"chains\":" << config.chains
                      << ",\"seed\":" << config.seed << ",\"passes\":" << repeat
                      << ",\"trial\":" << trial << ",\"units_per_worker\":" << (config.steady ? "null" : std::to_string(units))
                      << ",\"useful_bytes_per_worker\":" << (config.steady ? "null" : std::to_string(units * sizeof(u64)))
                      << ",\"nominal_line_bytes_per_worker\":" << (config.steady ? "null" : std::to_string(config.random ? units * config.line : units * sizeof(u64)))
                      << ",\"total_units\":" << total_units
                      << ",\"total_useful_bytes\":" << total_units * sizeof(u64)
                      << ",\"total_nominal_line_bytes\":" << total_units * (config.random ? config.line : sizeof(u64))
                      << ",\"wall_ns\":" << last - first << ",\"overlap_ns\":"
                      << (early > late ? early - late : 0) << ",\"workers\":[";
            for (std::size_t i = 0; i < observations.size(); ++i) {
                if (i) std::cout << ',';
                const auto& at = observations[i];
                const auto distinct = config.random ? std::min<u64>(config.bytes / config.line, at.units) * config.line
                    : ((std::min<u64>(config.bytes / sizeof(u64), at.units) * sizeof(u64) + config.line - 1) / config.line) * config.line;
                std::cout << "{\"cpu\":" << config.cpus[i] << ",\"actual_cpu\":" << at.actual_cpu
                          << ",\"start_ns\":" << at.start << ",\"end_ns\":" << at.end
                          << ",\"units\":" << at.units
                          << ",\"distinct_read_line_bytes\":" << distinct
                          << ",\"thread_cpu_ns\":" << at.cpu_ns << ",\"answer\":" << json_quote(std::to_string(at.answer)) << '}';
            }
            std::cout << "],\"verified\":true}\n" << std::flush;
        }
        return last - first;
    }
};

Config parse(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        std::string_view key = argv[i];
        if (key == "--check") { c.check = true; continue; }
        if (key == "--steady") { c.steady = true; continue; }
        if (++i == argc) throw std::invalid_argument("missing option value");
        std::string_view value = argv[i];
        if (key == "--cpus") {
            while (!value.empty()) {
                auto n = value.find(','); c.cpus.push_back(int(number(value.substr(0, n))));
                if (n == std::string_view::npos) break;
                value.remove_prefix(n + 1);
            }
        } else if (key == "--bytes") c.bytes = number(value);
        else if (key == "--line") c.line = number(value);
        else if (key == "--chains") c.chains = unsigned(number(value));
        else if (key == "--trials") c.trials = unsigned(number(value));
        else if (key == "--ms") c.target_ns = number(value) * 1'000'000;
        else if (key == "--seed") c.seed = number(value);
        else if (key == "--mode") {
            if (value != "scan" && value != "chase") throw std::invalid_argument("unknown mode");
            c.random = value == "chase";
        } else throw std::invalid_argument("unknown option");
    }
    auto sorted = c.cpus; std::sort(sorted.begin(), sorted.end());
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    if (c.cpus.empty() || std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end() ||
        c.bytes < page || c.bytes % page || !std::has_single_bit(c.line) || c.line < sizeof(u64) ||
        c.line > page || c.bytes / c.line > std::numeric_limits<std::uint32_t>::max() ||
        !fi::chase_for(c.chains) || c.bytes / c.line % c.chains ||
        !c.trials || c.trials % 2 || !c.target_ns || c.target_ns > 1'000'000'000)
        throw std::invalid_argument("invalid CPUs/geometry/even trials/time budget");
    return c;
}
} // namespace

int main(int argc, char** argv) try {
    auto c = parse(argc, argv);
    Cohort cohort(c);
    cohort.require_valid(); cohort.memory();
    if (c.check) {
        cohort.sample(1, 0, "check"); cohort.sample(3, 1, "check");
        return 0;
    }
    u64 passes = 1;
    while (!c.steady && cohort.sample(passes, -1, "calibrate") < c.target_ns) {
        if (passes >= (u64(1) << 32)) throw std::runtime_error("calibration failed to progress");
        passes *= 2;
    }
    for (unsigned trial = 0; trial < c.trials; ++trial) {
        // Two identical treatments, position/predecessor balanced. Conditioning
        // is repeated independently immediately before every interval.
        for (unsigned position = 0; position < 2; ++position)
            cohort.sample(passes, int(trial), (trial % 2 == position) ? "primary" : "duplicate");
    }
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n'; return 2;
}
