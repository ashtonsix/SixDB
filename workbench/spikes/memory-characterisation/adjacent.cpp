// Controlled one-trigger spatial-fetch diagnostic. This records conditioned
// histories; cache maintenance removes lines, not hidden prefetch history.
// Build standalone with the pinned Linux Clang and -O3 -std=c++23.
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__x86_64__)
#include <cpuid.h>
#include <x86intrin.h>
#endif

#if !defined(__x86_64__) && !defined(__aarch64__)
int main() {
    std::cerr << "adjacent=unsupported: no qualified architecture probe\n";
    return 77;
}
#else
namespace {
using Word = std::uintptr_t;
constexpr Word cookie = 0xd1b54a32d192ed03ULL;
struct Unsupported : std::runtime_error { using std::runtime_error::runtime_error; };

inline void flush(const void* address) {
#if defined(__x86_64__)
    _mm_clflush(address);
#else
    asm volatile("dc civac, %0" :: "r"(address) : "memory");
#endif
}
inline void flush_done() {
#if defined(__x86_64__)
    _mm_mfence();
    _mm_lfence();
#else
    asm volatile("dsb sy\n\tisb" ::: "memory");
#endif
}

void maintenance_check() {
#if defined(__aarch64__)
    // This diagnostic is standalone and single-threaded. Never transplant the
    // fork/possible fault into a threaded production hardware service.
    const auto child = fork();
    if (child == 0) {
        const rlimit limit{0, 0};
        setrlimit(RLIMIT_CORE, &limit);
        alignas(256) Word data = cookie;
        flush(&data);
        flush_done();
        std::uint64_t tick, hz;
        asm volatile("isb\n\tmrs %0, cntvct_el0\n\tmrs %1, cntfrq_el0\n\tisb"
                     : "=r"(tick), "=r"(hz) :: "memory");
        _exit(tick && hz ? 0 : 1);
    }
    if (child < 0) throw Unsupported("standalone maintenance test could not fork");
    int status{};
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) throw Unsupported("standalone maintenance test could not wait");
    }
    std::cerr << "maintenance_check=standalone-child status=" << status
              << " exited=" << WIFEXITED(status)
              << " signal=" << (WIFSIGNALED(status) ? WTERMSIG(status) : 0) << '\n';
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw Unsupported("EL0 DC CIVAC/CNTVCT/CNTFRQ access failed in standalone child");
#endif
}

struct Options {
    int cpu = sched_getcpu();
    unsigned trials = 128, reps = 3;
    std::size_t mib = 128, requested_line = 0;
    std::uint64_t seed = 13579;
    std::vector<unsigned> delays_ns{0, 100, 300};
};

std::uint64_t now_ns() {
    timespec value{};
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &value))
        throw std::runtime_error("CLOCK_MONOTONIC_RAW failed");
    return std::uint64_t(value.tv_sec) * 1000000000 + std::uint64_t(value.tv_nsec);
}

void pin(int cpu) {
    if (cpu < 0 || cpu >= CPU_SETSIZE) throw std::runtime_error("invalid CPU");
    cpu_set_t allowed;
    if (sched_getaffinity(0, sizeof(allowed), &allowed) || !CPU_ISSET(cpu, &allowed))
        throw std::runtime_error("requested CPU is not in allowed affinity mask");
    cpu_set_t selected;
    CPU_ZERO(&selected);
    CPU_SET(cpu, &selected);
    if (sched_setaffinity(0, sizeof(selected), &selected))
        throw std::runtime_error("affinity failed");
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string value;
    if (!input || !std::getline(input, value))
        throw std::runtime_error("cannot read " + path.string());
    return value;
}
std::string reported_text(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string value;
    return input && std::getline(input, value) ? value : "unreported";
}

std::size_t line_geometry(int cpu) {
    const auto root = std::filesystem::path("/sys/devices/system/cpu") /
                      ("cpu" + std::to_string(cpu)) / "cache";
    std::size_t line = 0;
    std::vector<std::filesystem::path> paths;
    for (const auto& item : std::filesystem::directory_iterator(root))
        if (item.path().filename().string().starts_with("index")) paths.push_back(item.path());
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths) {
        const auto type = read_text(path / "type");
        const auto bytes = std::stoull(read_text(path / "coherency_line_size"));
        std::cerr << "cache index=" << path.filename().string()
                  << " level=" << read_text(path / "level") << " type=" << type
                  << " size=" << read_text(path / "size") << " line_bytes=" << bytes
                  << " sets=" << reported_text(path / "number_of_sets")
                  << " ways=" << reported_text(path / "ways_of_associativity")
                  << " shared_cpu_list=" << reported_text(path / "shared_cpu_list") << '\n';
        if (type == "Data" || type == "Unified") {
            if (line && line != bytes) throw std::runtime_error("mixed data/unified line geometry unsupported");
            line = bytes;
        }
    }
    if (!line) throw std::runtime_error("no actual reported data-cache line geometry");
#if defined(__x86_64__)
    unsigned a{}, b{}, c{}, d{};
    if (!__get_cpuid(1, &a, &b, &c, &d) || !(d & (1u << 19)))
        throw std::runtime_error("CLFLUSH unavailable");
    const auto flush_line = ((b >> 8) & 0xffu) * 8u;
    if (flush_line != line) throw std::runtime_error("CPUID CLFLUSH and sysfs line geometry disagree");
    if (!__get_cpuid(0x80000001, &a, &b, &c, &d) || !(d & (1u << 27)))
        throw std::runtime_error("RDTSCP unavailable");
#endif
    return line;
}

struct Stamp { std::uint64_t tick; unsigned aux; };
[[gnu::always_inline]] inline Stamp stamp() {
#if defined(__x86_64__)
    _mm_lfence();
    unsigned aux;
    const auto tick = __rdtscp(&aux);
    _mm_lfence();
    return {tick, aux};
#else
    std::uint64_t tick;
    asm volatile("dsb sy\n\tisb\n\tmrs %0, cntvct_el0\n\tisb" : "=r"(tick) :: "memory");
    // CNTVCT provides no RDTSCP-like CPU stamp. Affinity is restricted to one
    // CPU and checked outside measurement; zero is an explicitly labeled sentinel.
    return {tick, 0};
#endif
}

struct Measured {
    Word value;
    std::uint64_t start, stop;
    unsigned start_aux, stop_aux;
};

// Deliberately distinct demand-load instruction PCs for trigger and probe.
// The compiler cannot lift or erase the load: a volatile asm block with a
// memory clobber sits between serialized timestamps; the value is checked.
#if defined(__x86_64__)
#define OBSERVER_LOAD "movq (%1), %0"
#else
#define OBSERVER_LOAD "ldr %0, [%1]"
#endif
#define MEASURED_LOAD(name) \
extern "C" [[gnu::noinline]] Measured name(const void* address) { \
    const auto begin = stamp(); \
    Word value; \
    asm volatile(OBSERVER_LOAD : "=r"(value) : "r"(address) : "memory"); \
    const auto end = stamp(); \
    return {value, begin.tick, end.tick, begin.aux, end.aux}; \
}
MEASURED_LOAD(adjacent_anchor_load)
MEASURED_LOAD(adjacent_target_load)
#undef MEASURED_LOAD
#undef OBSERVER_LOAD

extern "C" [[gnu::noinline]] void adjacent_hint_t0(const void* address) {
    // Each arm is selected by an unpredictable branch. Serialize inside the
    // selected helper before its memory instruction: a wrong-path hint must
    // not warm an otherwise no-trigger trial while the arm guard resolves.
#if defined(__x86_64__)
    _mm_lfence();
    _mm_prefetch(static_cast<const char*>(address), _MM_HINT_T0);
    asm volatile("" ::: "memory");
#else
    asm volatile("dsb sy\n\tisb\n\tprfm pldl1keep, [%0]" :: "r"(address) : "memory");
#endif
}
extern "C" [[gnu::noinline]] void adjacent_hint_t1(const void* address) {
#if defined(__x86_64__)
    _mm_lfence();
    _mm_prefetch(static_cast<const char*>(address), _MM_HINT_T1);
    asm volatile("" ::: "memory");
#else
    asm volatile("dsb sy\n\tisb\n\tprfm pldl2keep, [%0]" :: "r"(address) : "memory");
#endif
}
extern "C" [[gnu::noinline]] Word adjacent_hot_load(const void* address) {
    Word value;
    // A fence only after this load would allow a mispredicted hot-arm branch
    // to launch an untimed target fetch in cold, demand and hint trials.
#if defined(__x86_64__)
    asm volatile("lfence\n\tmovq (%1), %0\n\tlfence" : "=r"(value) : "r"(address) : "memory");
#else
    asm volatile("dsb sy\n\tisb\n\tldr %0, [%1]\n\tdsb sy\n\tisb"
                 : "=r"(value) : "r"(address) : "memory");
#endif
    return value;
}

void wait_until(std::uint64_t until) {
#if defined(__x86_64__)
    // RDTSC here reads only the timebase: no extra data accesses or PAUSE.
    // Final LFENCE prevents the target from entering ahead of this wait.
    while (__rdtsc() < until) asm volatile("" ::: "memory");
    _mm_lfence();
#else
    std::uint64_t tick;
    do {
        asm volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(tick) :: "memory");
    } while (tick < until);
    asm volatile("dsb sy\n\tisb" ::: "memory");
#endif
}

double calibrate() {
#if defined(__aarch64__)
    std::uint64_t hz;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(hz));
    if (!hz) throw Unsupported("zero generic-timer frequency");
    const auto factor = 1e9 / double(hz);
    std::cerr << "timer_frequency_hz=" << hz << " timer_quantum_ns=" << factor << '\n';
#endif
    std::array<double, 3> ratios;
    for (auto& ratio : ratios) {
        const auto n0 = now_ns();
        const auto t0 = stamp();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto t1 = stamp();
        const auto n1 = now_ns();
        if (t0.aux != t1.aux || t1.tick <= t0.tick)
            throw std::runtime_error("timer calibration migrated or failed");
        ratio = double(n1 - n0) / double(t1.tick - t0.tick);
    }
    std::sort(ratios.begin(), ratios.end());
    if (ratios.back() / ratios.front() > 1.01)
        throw std::runtime_error("unstable TSC calibration");
#if defined(__aarch64__)
    if (std::abs(ratios[1] / factor - 1) > .01)
        throw std::runtime_error("CNTFRQ and wall-clock timer calibration disagree");
    return factor;
#else
    return ratios[1];
#endif
}

class Mapping {
public:
    std::size_t bytes;
    char* data;
    explicit Mapping(std::size_t size, std::size_t line) : bytes(size) {
        data = static_cast<char*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (data == MAP_FAILED) throw std::runtime_error("mmap failed");
        if (madvise(data, bytes, MADV_NOHUGEPAGE)) {
            munmap(data, bytes);
            throw std::runtime_error("MADV_NOHUGEPAGE failed");
        }
        // Setup writes every line while pinned to the selected CPU. Per-trial
        // CLFLUSH removes the relevant setup data; history remains conditioned.
        for (std::size_t offset = 0; offset < bytes; offset += line)
            *reinterpret_cast<Word*>(data + offset) = offset ^ cookie;
    }
    ~Mapping() { munmap(data, bytes); }
    Mapping(const Mapping&) = delete;
    void* at(std::size_t offset) const {
        if (offset > bytes - sizeof(Word)) throw std::runtime_error("probe address outside mapping");
        return data + offset;
    }
    void receipt() const {
        std::cerr << "mapping bytes=" << bytes << " virtual_begin=" << Word(data)
                  << " physical_contiguity_known=false request=base-MADV_NOHUGEPAGE\n";
        std::ifstream smaps("/proc/self/smaps");
        std::string text;
        bool selected = false;
        while (std::getline(smaps, text)) {
            unsigned long begin{}, end{};
            if (std::sscanf(text.c_str(), "%lx-%lx", &begin, &end) == 2)
                selected = Word(data) >= begin && Word(data) < end;
            else if (selected && (text.starts_with("KernelPageSize:") ||
                     text.starts_with("MMUPageSize:") || text.starts_with("AnonHugePages:") ||
                     text.starts_with("VmFlags:"))) std::cerr << text << '\n';
        }
        std::ifstream numa("/proc/self/numa_maps");
        while (std::getline(numa, text)) {
            unsigned long begin{};
            if (std::sscanf(text.c_str(), "%lx", &begin) == 1 && begin == Word(data))
                std::cerr << "numa_mapping=" << text << '\n';
        }
    }
};

enum class Location : unsigned { interior, page_start, page_end };
enum class Arm : unsigned { none, demand, hint_t0, hint_t1, demand_reflush, hot };
constexpr std::array location_names{"interior", "page_start", "page_end"};
constexpr std::array arm_names{"none", "demand", "prefetcht0", "prefetcht1", "demand_reflush", "hot"};
constexpr std::array target_names{"neighbor", "far", "anchor"};
struct Case { Location location; unsigned parity; int delta; };
struct Trial { unsigned case_id, delay_id, arm, target, ordinal; };

std::vector<unsigned> parse_delays(const std::string& text) {
    std::vector<unsigned> result;
    std::size_t start = 0;
    do {
        const auto end = text.find(',', start);
        const auto part = text.substr(start, end - start);
        std::size_t consumed{};
        const auto value = std::stoul(part, &consumed);
        if (consumed != part.size() || value > 100000) throw std::runtime_error("invalid delay");
        result.push_back(unsigned(value));
        if (end == std::string::npos) break;
        start = end + 1;
    } while (start < text.size());
    if (result.empty() || result.size() > 16) throw std::runtime_error("invalid delay count");
    return result;
}

Options options(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            std::cout << "adjacent --cpu N [--trials 128] [--reps 3] [--seed 13579] [--mib 128] [--line asserted_bytes] [--delays 0,100,300]\n";
            std::exit(0);
        }
        if (++i == argc) throw std::runtime_error("missing argument for " + arg);
        const std::string value = argv[i];
        if (arg == "--cpu") result.cpu = std::stoi(value);
        else if (arg == "--trials" || arg == "--reps") {
            const auto parsed = std::stoull(value);
            if (parsed > 10000) throw std::runtime_error("trial/repetition count outside bounded limits");
            if (arg == "--trials") result.trials = unsigned(parsed);
            else result.reps = unsigned(parsed);
        }
        else if (arg == "--seed") result.seed = std::stoull(value);
        else if (arg == "--mib") result.mib = std::stoull(value);
        else if (arg == "--line") result.requested_line = std::stoull(value);
        else if (arg == "--delays") result.delays_ns = parse_delays(value);
        else throw std::runtime_error("unknown option " + arg);
    }
    if (!result.trials || result.trials > 10000 || !result.reps || result.reps > 16 ||
        result.mib < 1 || result.mib > 4096) throw std::runtime_error("options outside bounded limits");
    return result;
}

void run(const Options& opt) {
    pin(opt.cpu);
    maintenance_check();
    const auto line = line_geometry(opt.cpu);
    const auto page_raw = sysconf(_SC_PAGESIZE);
    if (page_raw <= 0) throw std::runtime_error("invalid base page size");
    const auto page = std::size_t(page_raw);
    if (opt.requested_line && opt.requested_line != line)
        throw std::runtime_error("--line does not match actual geometry");
    // At least 64 lines/page keeps the half-page far control outside the
    // +/-8-line neighborhood for every near-target distance and placement.
    if (line < sizeof(Word) || (line & (line - 1)) || (page & (page - 1)) ||
        page % line || page / line < 64)
        throw Unsupported("line/page geometry cannot separate +/-8-line and half-page far controls");
    const auto ns_per_tick = calibrate();
    const auto expected_aux = stamp().aux;
    Mapping memory(opt.mib * 1024 * 1024, line);
    memory.receipt();
    const auto region_bytes = 4 * page;
    const auto regions = memory.bytes / region_bytes;
    const auto lines_per_page = page / line;
    if (regions < 16) throw std::runtime_error("mapping has too few regions");
    std::mt19937_64 rng(opt.seed);
    std::vector<Case> cases;
    for (const auto location : {Location::interior, Location::page_start, Location::page_end})
        for (unsigned parity = 0; parity < 2; ++parity)
            for (const int delta : {-8, -4, -2, -1, 1, 2, 4, 8})
                cases.push_back({location, parity, delta});
    std::vector<Trial> trials;
    const auto total = std::uint64_t(cases.size()) * opt.delays_ns.size() * 6 * 3 * opt.trials;
    if (total > 50000000) throw std::runtime_error("trial plan outside bounded limit");
    trials.reserve(total);
    for (unsigned case_id = 0; case_id < cases.size(); ++case_id)
        for (unsigned delay = 0; delay < opt.delays_ns.size(); ++delay)
            for (unsigned arm = 0; arm < 6; ++arm)
                for (unsigned target = 0; target < target_names.size(); ++target)
                    for (unsigned ordinal = 0; ordinal < opt.trials; ++ordinal)
                        trials.push_back({case_id, delay, arm, target, ordinal});
    std::cerr.precision(17);
    std::cerr << "config schema=1 cpu=" << opt.cpu << " observed_cpu=" << sched_getcpu()
              << " aux=" << expected_aux << " line_bytes=" << line << " page_bytes=" << page
              << " ns_per_tick=" << ns_per_tick << " seed=" << opt.seed
              << " reps=" << opt.reps << " trials_per_arm=" << opt.trials
              << " trials_per_rep=" << trials.size() << " regions=" << regions
              << " load_bytes=" << sizeof(Word) << " physical_contiguity_known=false"
#if defined(__x86_64__)
              << " architecture=x86_64 timer=tsc aux_kind=rdtscp-cpu-stamp"
                 " maintenance=clflush hint_t0=prefetcht0 hint_t1=prefetcht1\n";
#else
              << " architecture=aarch64 timer=cntvct aux_kind=unavailable-pinned-sentinel"
                 " maintenance=dc-civac hint_t0=prfm-pldl1keep hint_t1=prfm-pldl2keep\n";
#endif
#if defined(__x86_64__)
    std::cerr << "method=balanced-shuffle-all-cases-arms-targets-per-repetition; random-with-replacement-regions; distinct-anchor-target-PCs; LFENCE-before-every-conditional-demand-or-hint; CLFLUSH-conditioned-not-history-reset; one-target-load-per-trial; delay-from-completed-demand-or-issued-hint; reflush-after-delay; TSC-timebase-not-core-cycles\n";
#else
    std::cerr << "method=balanced-shuffle-all-cases-arms-targets-per-repetition; random-with-replacement-regions; distinct-anchor-target-PCs; DSB-SY-ISB-before-every-conditional-demand-or-hint; DC-CIVAC-conditioned-not-history-reset; one-target-load-per-trial; delay-from-completed-demand-or-issued-hint; reflush-after-delay; CNTVCT-timebase-not-core-cycles; no-per-load-migration-stamp\n";
#endif
    std::cout << "rep,sequence,case_id,location,anchor_parity,delta_lines,target_kind,arm,delay_ns,region,anchor_offset,target_offset,anchor_page_line,target_page_line,page_cross,trigger_ticks,target_ticks,trigger_to_target_ticks,start_aux,end_aux,value_ok\n";
    Word sink = 0;
    const auto wall_start = now_ns();
    for (unsigned rep = 0; rep < opt.reps; ++rep) {
        std::shuffle(trials.begin(), trials.end(), rng);
        for (std::size_t sequence = 0; sequence < trials.size(); ++sequence) {
            const auto& trial = trials[sequence];
            const auto& c = cases[trial.case_id];
            const auto region = rng() % regions;
            const auto region_start = region * region_bytes;
            std::size_t anchor_line;
            if (c.location == Location::interior) {
                const auto choices = (lines_per_page - 16) / 2;
                anchor_line = lines_per_page + 8 + c.parity + 2 * (rng() % choices);
            } else if (c.location == Location::page_start) {
                anchor_line = 2 * lines_per_page + c.parity;
            } else {
                anchor_line = 2 * lines_per_page - 2 + c.parity;
            }
            const auto neighbor_line = std::size_t(std::int64_t(anchor_line) + c.delta);
            const auto target_page = neighbor_line / lines_per_page;
            const auto far_line = target_page * lines_per_page +
                                  (neighbor_line + lines_per_page / 2) % lines_per_page;
            // The separately timed anchor target is a positive control for
            // software hints: did the requested line arrive at this slack?
            // It never adds an observer load to a neighbor/far trial.
            const auto target_line = trial.target == 2 ? anchor_line :
                                     trial.target == 1 ? far_line : neighbor_line;
            const auto anchor_offset = region_start + anchor_line * line;
            const auto target_offset = region_start + target_line * line;
            const auto* anchor = memory.at(anchor_offset);
            const auto* target = memory.at(target_offset);
            // Flush a common symmetric +/-8-line neighborhood and the matched
            // far control on every arm. Randomized flush ordering is independent
            // of the trigger/distance; no demand accesses to the data precede it.
            std::array<std::size_t, 18> cold;
            for (int delta = -8; delta <= 8; ++delta)
                cold[std::size_t(delta + 8)] = region_start + std::size_t(std::int64_t(anchor_line) + delta) * line;
            cold[17] = region_start + far_line * line;
            std::shuffle(cold.begin(), cold.end(), rng);
            for (auto offset : cold) flush(memory.at(offset));
            flush_done();
            if (trial.arm == unsigned(Arm::hot)) sink ^= adjacent_hot_load(target);
            const auto trigger_start = stamp();
            std::uint64_t trigger_end, trigger_ticks;
            if (trial.arm == unsigned(Arm::demand) || trial.arm == unsigned(Arm::demand_reflush)) {
                const auto measured = adjacent_anchor_load(anchor);
                if (measured.start_aux != expected_aux || measured.stop_aux != expected_aux ||
                    measured.value != (anchor_offset ^ cookie))
                    throw std::runtime_error("trigger migrated or value validation failed");
                sink ^= measured.value;
                trigger_end = measured.stop;
                trigger_ticks = measured.stop - measured.start;
            } else {
                if (trial.arm == unsigned(Arm::hint_t0)) adjacent_hint_t0(anchor);
                if (trial.arm == unsigned(Arm::hint_t1)) adjacent_hint_t1(anchor);
                const auto stop = stamp();
                if (trigger_start.aux != expected_aux || stop.aux != expected_aux)
                    throw std::runtime_error("trigger migrated");
                trigger_end = stop.tick;
                trigger_ticks = stop.tick - trigger_start.tick;
            }
            const auto delay = opt.delays_ns[trial.delay_id];
            const auto wait_ticks = std::uint64_t(std::ceil(double(delay) / ns_per_tick));
            wait_until(trigger_end + wait_ticks);
            if (trial.arm == unsigned(Arm::demand_reflush)) {
                // Remove the answer after the selected slack, rather than
                // flushing early and allowing an outstanding fetch to refill it.
                flush(target);
                flush_done();
            }
            const auto measured = adjacent_target_load(target);
            if (measured.start_aux != expected_aux || measured.stop_aux != expected_aux ||
                measured.stop < measured.start || measured.start < trigger_end ||
                measured.value != (target_offset ^ cookie))
                throw std::runtime_error("target migrated, timer reversed or value validation failed");
            sink ^= measured.value;
            std::cout << rep << ',' << sequence << ',' << trial.case_id << ','
                      << location_names[unsigned(c.location)] << ',' << c.parity << ',' << c.delta << ','
                      << target_names[trial.target] << ',' << arm_names[trial.arm] << ',' << delay << ','
                      << region << ',' << anchor_offset << ',' << target_offset << ','
                      << anchor_line % lines_per_page << ',' << target_line % lines_per_page << ','
                      << (anchor_line / lines_per_page != target_line / lines_per_page) << ','
                      << trigger_ticks << ',' << measured.stop - measured.start << ','
                      << measured.start - trigger_end << ',' << measured.start_aux << ','
                      << measured.stop_aux << ",1\n";
        }
        std::cerr << "completed_rep=" << rep << " trials=" << trials.size()
                  << " elapsed_ns=" << now_ns() - wall_start << '\n';
    }
    asm volatile("" :: "r"(sink) : "memory");
    std::cerr << "complete rows=" << trials.size() * opt.reps << " sink=" << sink
              << " elapsed_ns=" << now_ns() - wall_start << '\n';
    memory.receipt();
    cpu_set_t selected;
    if (sched_getaffinity(0, sizeof(selected), &selected) || CPU_COUNT(&selected) != 1 ||
        !CPU_ISSET(opt.cpu, &selected) || sched_getcpu() != opt.cpu)
        throw std::runtime_error("final CPU/affinity differs from selected single-CPU mask");
    std::cerr << "affinity_final_cpu=" << sched_getcpu() << " affinity_single_cpu=true\n";
}
} // namespace

int main(int argc, char** argv) try {
    run(options(argc, argv));
    return 0;
} catch (const Unsupported& error) {
    std::cerr << "adjacent=unsupported: " << error.what() << '\n';
    return 77;
} catch (const std::exception& error) {
    std::cerr << "adjacent=failed: " << error.what() << '\n';
    return 1;
}
#endif
