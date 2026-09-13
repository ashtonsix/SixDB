#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
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
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__x86_64__)
#include <cpuid.h>
#include <x86intrin.h>
#endif

namespace {
using U = std::uintptr_t;
constexpr U cookie = 0xd1b54a32d192ed03ULL;
std::mt19937_64 rng(0x5349584442);
U sink;
double tick_ns = 1;
int memory_node = -1;
int first_touch_cpu = -1;

std::uint64_t now_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return std::uint64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}
inline void fence() {
#if defined(__x86_64__)
    _mm_lfence();
#elif defined(__aarch64__)
    asm volatile("dsb ish\n\tisb" ::: "memory");
#endif
}
inline std::uint64_t ticks() {
#if defined(__x86_64__)
    _mm_lfence();
    auto value = __rdtsc();
    _mm_lfence();
    return value;
#elif defined(__aarch64__)
    std::uint64_t value;
    asm volatile("isb\n\tmrs %0, cntvct_el0\n\tisb" : "=r"(value) :: "memory");
    return value;
#else
    return now_ns();
#endif
}
void calibrate() {
#if defined(__aarch64__)
    std::uint64_t hz;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(hz));
    tick_ns = 1e9 / hz;
#elif defined(__x86_64__)
    const auto n0 = now_ns(), t0 = ticks();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto t1 = ticks(), n1 = now_ns();
    tick_ns = double(n1 - n0) / double(t1 - t0);
#endif
    std::cerr << "timer_ns_per_tick=" << tick_ns << '\n';
}
inline U load(U p) { return *reinterpret_cast<volatile U*>(p); }
// Keep one demand-load instruction per call site, including the shared-PC arm.
template<int Id> [[gnu::noinline]] void touch(U p) {
    const auto value = load(p);
    asm volatile("" :: "r"(value), "i"(Id) : "memory");
    fence();
}
template<std::size_t... I> auto touch_table(std::index_sequence<I...>) {
    return std::array{&touch<int(I)>...};
}
auto touches = touch_table(std::make_index_sequence<65>{});
void flush(U p) {
#if defined(__x86_64__)
    _mm_clflush(reinterpret_cast<void*>(p));
#elif defined(__aarch64__)
    asm volatile("dc civac, %0" :: "r"(p) : "memory");
#endif
}
void flush_done() {
#if defined(__x86_64__)
    _mm_mfence();
#else
    fence();
#endif
}
bool flush_supported() {
#if defined(__x86_64__)
    unsigned a, b, c, d;
    return __get_cpuid(1, &a, &b, &c, &d) && (d & (1u << 19));
#elif defined(__aarch64__)
    const auto child = fork();
    if (child == 0) {
        rlimit limit{0, 0};
        setrlimit(RLIMIT_CORE, &limit);
        alignas(256) U data{}; flush(U(&data)); flush_done(); _exit(0);
    }
    if (child < 0) return false;
    int status{};
    if (waitpid(child, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#else
    return false;
#endif
}
void pin(int cpu) {
    if (cpu < 0 || cpu >= CPU_SETSIZE) throw std::runtime_error("invalid CPU");
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set)) throw std::runtime_error("affinity failed");
}
struct Mapping {
    std::size_t bytes;
    char* data;
    explicit Mapping(std::size_t n, bool huge = false) : bytes(n) {
        data = static_cast<char*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (data == MAP_FAILED) throw std::runtime_error("mmap failed");
        if (madvise(data, bytes, huge ? MADV_HUGEPAGE : MADV_NOHUGEPAGE))
            throw std::runtime_error("madvise failed");
        if (memory_node >= 0) {
            const unsigned long mask = 1UL << memory_node;
            // Linux get_nodes() subtracts one from maxnode before copying bits.
            if (syscall(SYS_mbind, data, bytes, 2 /* MPOL_BIND */, &mask, sizeof(mask) * 8 + 1, 0))
                throw std::runtime_error(std::string("mbind unavailable: ") + std::strerror(errno));
        }
        // First-touch belongs to setup, on the measurement CPU.
        const int original_cpu = sched_getcpu();
        if (first_touch_cpu >= 0) pin(first_touch_cpu);
        std::memset(data, 0, bytes);
        if (first_touch_cpu >= 0) pin(original_cpu);
    }
    ~Mapping() { munmap(data, bytes); }
    U at(std::size_t offset) { assert(offset + sizeof(U) <= bytes); return U(data + offset); }
    void receipt(const std::string& name) {
        std::ifstream in("/proc/self/smaps");
        std::string line;
        bool selected = false;
        std::cerr << "mapping=" << name << " bytes=" << bytes << '\n';
        while (std::getline(in, line)) {
            unsigned long begin, end;
            if (std::sscanf(line.c_str(), "%lx-%lx", &begin, &end) == 2)
                selected = U(data) >= begin && U(data) < end;
            else if (selected && (line.starts_with("KernelPageSize:") ||
                line.starts_with("MMUPageSize:") || line.starts_with("AnonHugePages:") ||
                line.starts_with("VmFlags:"))) std::cerr << line << '\n';
        }
        std::ifstream nodes("/proc/self/numa_maps");
        while (std::getline(nodes, line)) {
            unsigned long begin;
            if (std::sscanf(line.c_str(), "%lx", &begin) == 1 && begin == U(data))
                std::cerr << "numa_mapping=" << line << '\n';
        }
    }
};
void emit(const std::string& family, const std::string& variant, std::size_t bytes,
          int k, int stride, int train, int ahead, int rep, std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto q = [&](double p) { return values[std::size_t(p * (values.size() - 1))]; };
    std::cout << family << ',' << variant << ',' << bytes << ',' << k << ',' << stride << ','
              << train << ',' << ahead << ',' << rep << ',' << values.size() << ','
              << q(.1) << ',' << q(.5) << ',' << q(.9) << '\n';
}

template<std::size_t N> [[gnu::noinline]] U chase(std::array<U, N>& p, std::size_t steps) {
    for (std::size_t i = 0; i < steps; ++i) {
#pragma clang loop unroll(full)
        for (std::size_t j = 0; j < N; ++j) p[j] = load(p[j]) ^ cookie;
    }
    U sum = 0;
    for (auto value : p) sum ^= value;
    return sum;
}
template<std::size_t N> void measure_chains(const std::vector<U>& order,
    const std::string& family, const std::string& variant, std::size_t bytes,
    int repetitions, double min_ms, int reported_k = N) {
    if (order.size() < N) return;
    std::array<U, N> p{};
    for (std::size_t i = 0; i < order.size(); ++i)
        *reinterpret_cast<U*>(order[i]) = order[i + N < order.size() ? i + N : i % N] ^ cookie;
    std::copy_n(order.begin(), N, p.begin());
    // Validate every link and all disjoint cycle lengths outside measurement.
    for (std::size_t j = 0; j < N; ++j) {
        auto pos = p[j];
        for (std::size_t i = j; i < order.size(); i += N) {
            if (pos != order[i]) throw std::runtime_error("chain validation failed");
            pos = load(pos) ^ cookie;
        }
        if (pos != p[j]) throw std::runtime_error("chain closure failed");
    }
    sink ^= chase(p, 1024);
    std::size_t steps = 1024;
    auto start = now_ns();
    sink ^= chase(p, steps);
    const auto elapsed = std::max<std::uint64_t>(1, now_ns() - start);
    steps = std::clamp<std::size_t>(std::size_t(steps * min_ms * 1e6 / elapsed), 1024, 16000000);
    for (int rep = 0; rep < repetitions; ++rep) {
        std::vector<double> values;
        for (int sample = 0; sample < 3; ++sample) {
            start = now_ns();
            sink ^= chase(p, steps);
            values.push_back(double(now_ns() - start) / (steps * N));
        }
        emit(family, variant, bytes, reported_k, 0, 0, 0, rep, values);
    }
}
void mlp(std::size_t max_bytes, std::size_t line, bool huge, int reps, double ms) {
    for (auto bytes : {std::size_t(16 * 1024), std::size_t(512 * 1024),
                       std::size_t(8 * 1024 * 1024), max_bytes}) {
        Mapping mapping(bytes, huge);
        std::vector<U> order(bytes / line);
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = mapping.at(i * line);
        std::shuffle(order.begin(), order.end(), rng);
        mapping.receipt("mlp-" + std::to_string(bytes));
        const std::string variant = huge ? "thp-requested" : "base";
#define MEASURE(N) measure_chains<N>(order, "mlp", variant, bytes, reps, ms)
        MEASURE(1); MEASURE(2); MEASURE(4); MEASURE(8); MEASURE(12); MEASURE(16);
        MEASURE(24); MEASURE(32); MEASURE(48); MEASURE(64);
#undef MEASURE
        if (memory_node >= 0 || first_touch_cpu >= 0) mapping.receipt("mlp-final-" + std::to_string(bytes));
    }
}
void tlb(std::size_t page, std::size_t line, bool huge, int reps, double ms) {
    for (std::size_t count : {16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192}) {
        for (bool sparse : {false, true}) {
            const auto bytes = std::max(page, count * (sparse ? page : line));
            Mapping mapping(bytes, huge);
            std::vector<U> order(count);
            for (std::size_t i = 0; i < count; ++i)
                order[i] = mapping.at(sparse ? i * page + ((i * 131) % (page / line)) * line : i * line);
            std::shuffle(order.begin(), order.end(), rng);
            measure_chains<1>(order, "tlb", std::string(sparse ? "sparse-" : "packed-") +
                (huge ? "thp-requested" : "base"), bytes, reps, ms);
            mapping.receipt("tlb-" + std::to_string(count) + (sparse ? "-sparse" : "-packed"));
        }
    }
}
void stream_walk(std::size_t bytes, std::size_t page, std::size_t line, int reps, double ms) {
    Mapping memory(bytes);
    std::vector<std::size_t> pages(bytes / page);
    std::iota(pages.begin(), pages.end(), 0);
    std::shuffle(pages.begin(), pages.end(), rng);
    const std::size_t positions = page / (2 * line);
    for (int streams : {1, 2, 4, 8, 12, 16, 24, 32, 48, 64}) {
        for (bool random : {false, true}) {
            std::vector<U> order;
            order.reserve(pages.size() * positions);
            for (std::size_t group = 0; group < pages.size(); group += streams) {
                const auto count = std::min<std::size_t>(streams, pages.size() - group);
                std::vector<std::vector<std::size_t>> offsets(count, std::vector<std::size_t>(positions));
                for (auto& permutation : offsets) {
                    std::iota(permutation.begin(), permutation.end(), 0);
                    if (random) std::shuffle(permutation.begin(), permutation.end(), rng);
                }
                for (std::size_t pos = 0; pos < positions; ++pos)
                    for (std::size_t stream = 0; stream < count; ++stream)
                        order.push_back(memory.at(pages[group + stream] * page + offsets[stream][pos] * 2 * line));
            }
            // One global dependency removes demand MLP as a competing explanation.
            measure_chains<1>(order, "streams", random ? "shuffled-within-page" : "stride2",
                              bytes, reps, ms, streams);
        }
    }
}
// Each trial has a fresh randomly selected region, one timed target and controls.
// Flushing does not reset prefetch history. We record conditioning, not cold state.
void probes(std::size_t line, std::size_t page, int reps, int trials, bool quick) {
    if (!flush_supported()) { std::cerr << "prefetch=unsupported-cache-maintenance\n"; return; }
    const std::size_t region = std::max<std::size_t>(65536, page * 4);
    Mapping memory(region * 512);
    struct Case { std::string family, variant; int k, stride, train, ahead, parity; bool boundary, pcs; };
    std::vector<Case> cases;
    for (int parity : {0, 1}) {
        for (int offset : {8, 16, 32, 64, 128, 256})
            cases.push_back({"spatial", "offset-parity" + std::to_string(parity), 0, 1, 1, offset, parity, false, false});
        for (int delta : {-2, -1, 1, 2, 4})
            cases.push_back({"adjacent", "parity" + std::to_string(parity), 0, 1, 1, delta, parity, false, false});
    }
    for (int stride : {1, 2, 4, -1, -2})
        for (int training : {1, 2, 3, 4, 6, 8, 12, 16})
            cases.push_back({"learning", "within-page", 0, stride, training, 1, 0, false, false});
    for (int ahead : {1, 2, 4, 8, 16})
        for (bool boundary : {false, true})
            cases.push_back({"lookahead", boundary ? "cross-page" : "within-page", 0, 1, 8, ahead, 0, boundary, false});
    if (!quick) for (int k : {0, 1, 2, 4, 8, 12, 16, 24, 32, 48, 64})
        for (bool pcs : {false, true})
            cases.push_back({"retention", pcs ? "distinct-pc" : "shared-pc", k, 2, 8, 1, 0, false, pcs});
    for (const auto& c : cases) for (int rep = 0; rep < reps; ++rep) {
        if (c.family == "learning" && (c.train * std::abs(c.stride) + 8) * line >= page) continue;
        std::array<std::vector<double>, 3> samples;
        // Balanced shuffled arm order reduces time/order bias inside each case.
        std::vector<int> arms(trials * 3);
        for (std::size_t i = 0; i < arms.size(); ++i) arms[i] = i % 3;
        std::shuffle(arms.begin(), arms.end(), rng);
        for (int arm : arms) {
            const auto block = (rng() % (512 - 65)) * region;
            U base = memory.at(block + page * 2 + (4 + c.parity) * line);
            if (c.stride < 0) base = memory.at(block + page * 3 - 4 * line);
            if (c.boundary) base = memory.at(block + page * 3 - c.train * line);
            U target = base + (c.family == "spatial" ? c.ahead :
                ((c.train - 1) * c.stride + c.ahead * c.stride) * std::int64_t(line));
            U resume = base + c.train * c.stride * std::int64_t(line);
            if (c.family == "retention") target = resume + c.stride * line;
            std::vector<U> cold{target};
            for (int i = 0; i < c.train; ++i) cold.push_back(base + i * c.stride * std::int64_t(line));
            for (int s = 1; s <= c.k; ++s) for (int i = 0; i < c.train; ++i)
                cold.push_back(base + s * region + i * c.stride * std::int64_t(line));
            std::shuffle(cold.begin(), cold.end(), rng);
            for (auto address : cold) flush(address);
            flush_done();
            if (arm == 1) for (int i = 0; i < c.train; ++i)
                touches[0](base + i * c.stride * std::int64_t(line));
            for (int s = 1; s <= c.k; ++s) for (int i = 0; i < c.train; ++i)
                touches[c.pcs ? s : 0](base + s * region + i * c.stride * std::int64_t(line));
            if (c.family == "retention") {
                // Remove an already-prefetched answer. Test prediction after
                // resuming the victim, not how long its cached line survives.
                flush(resume); flush(target); flush_done();
                touches[0](resume);
            }
            if (arm == 0) { flush(target); flush_done(); }
            if (arm == 2) touches[0](target);
            // Fixed processing slack; no additional memory accesses intentionally issued.
            U delay = target;
            for (int i = 0; i < 128; ++i) asm volatile("" : "+r"(delay));
            fence();
            const auto start = ticks();
            const U value = load(target);
            fence();
            const auto stop = ticks();
            sink ^= value;
            samples[arm].push_back(double(stop - start) * tick_ns);
        }
        for (int arm = 0; arm < 3; ++arm)
            emit(c.family, c.variant + (arm == 0 ? ":cold" : arm == 1 ? ":trained" : ":hot"),
                 memory.bytes, c.k, c.stride, c.train, c.ahead, rep, samples[arm]);
    }
}
void stress(int cpu, const std::string& mode, std::atomic<bool>& ready, std::atomic<bool>& stop) {
    pin(cpu);
    Mapping memory(64 * 1024 * 1024);
    auto* values = reinterpret_cast<volatile U*>(memory.data);
    U value = 1;
    ready.store(true, std::memory_order_release);
    while (!stop.load(std::memory_order_relaxed)) {
        if (mode == "compute") {
            for (int i = 0; i < 4096; ++i) { value = value * 6364136223846793005ULL + 1; asm volatile("" : "+r"(value)); }
        } else {
            for (std::size_t i = 0; i < memory.bytes / sizeof(U); i += 8) value += values[i];
        }
    }
    asm volatile("" :: "r"(value) : "memory");
}
void coherence(int peer, std::size_t line, int reps, int trials) {
    if (peer < 0) throw std::runtime_error("coherence requires --peer");
    if (!flush_supported()) { std::cerr << "coherence=unsupported-cache-maintenance\n"; return; }
    Mapping memory(32 * 1024 * 1024);
    struct alignas(256) Flag { std::atomic<int> value{0}; } request, response;
    U address = 0;
    int mode = 0;
    // The release/acquire handshake protects address and mode, and prevents a
    // payload write racing the receiver. Control flags occupy separate lines.
    std::jthread sender([&] {
        pin(peer);
        for (;;) {
            int signal;
            while (!(signal = request.value.load(std::memory_order_acquire))) {
#if defined(__x86_64__)
                _mm_pause();
#elif defined(__aarch64__)
                asm volatile("yield");
#endif
            }
            if (signal < 0) break;
            if (mode == 1) touches[1](address);
            if (mode == 2) { *reinterpret_cast<volatile U*>(address) = 7; flush_done(); }
            request.value.store(0, std::memory_order_relaxed);
            response.value.store(1, std::memory_order_release);
        }
    });
    for (int rep = 0; rep < reps; ++rep) {
        std::array<std::vector<double>, 4> samples;
        std::vector<int> arms(trials * 4);
        for (std::size_t i = 0; i < arms.size(); ++i) arms[i] = i % 4;
        std::shuffle(arms.begin(), arms.end(), rng);
        for (int arm : arms) {
            address = memory.at((rng() % (memory.bytes / (line * 4))) * line * 4);
            mode = arm;
            flush(address); flush_done();
            request.value.store(1, std::memory_order_release);
            while (!response.value.load(std::memory_order_acquire)) {
#if defined(__x86_64__)
                _mm_pause();
#elif defined(__aarch64__)
                asm volatile("yield");
#endif
            }
            response.value.store(0, std::memory_order_relaxed);
            if (arm == 3) touches[0](address);
            const auto start = ticks();
            auto value = load(address);
            fence();
            const auto stop = ticks();
            sink ^= value;
            samples[arm].push_back(double(stop - start) * tick_ns);
        }
        const std::array names{"cold", "peer-clean", "peer-dirty", "local-hot"};
        for (int arm = 0; arm < 4; ++arm)
            emit("handoff", names[arm], memory.bytes, 2, 0, 0, 0, rep, samples[arm]);
    }
    request.value.store(-1, std::memory_order_release);
    sender.join();
    // Same instruction, same counts: only the two atomic addresses change.
    alignas(256) std::array<std::atomic<U>, 128> counters{};
    constexpr std::size_t iterations = 200000;
    for (int offset : {0, 1, 2, 4, 8, 16, 32, 64}) for (int rep = 0; rep < reps; ++rep) {
        std::atomic<bool> ready{false}, start{false};
        std::uint64_t peer_end = 0;
        std::jthread writer([&] {
            pin(peer);
            ready.store(true, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {}
            for (std::size_t i = 0; i < iterations; ++i) counters[offset].fetch_add(1, std::memory_order_relaxed);
            peer_end = now_ns();
        });
        while (!ready.load(std::memory_order_acquire)) {}
        const auto t0 = now_ns();
        start.store(true, std::memory_order_release);
        for (std::size_t i = 0; i < iterations; ++i) counters[0].fetch_add(1, std::memory_order_relaxed);
        const auto local_end = now_ns();
        writer.join();
        emit("sharing", offset == 0 ? "same-word" : offset * sizeof(U) < line ? "same-line" : "separate-lines",
             sizeof(counters), 2, offset * sizeof(U), 0, 0, rep,
             {double(std::max(peer_end, local_end) - t0) / (iterations * 2)});
    }
}
}
int main(int argc, char** argv) try {
    std::size_t mib = 256, line = 64, page = std::size_t(sysconf(_SC_PAGESIZE));
    int reps = 3, trials = 120, cpu = sched_getcpu(), peer = -1;
    double ms = 5;
    bool huge = false, quick = false;
    std::string suite = "all", interference = "stream";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { if (++i == argc) throw std::runtime_error("missing argument"); return argv[i]; };
        if (arg == "--mib") mib = std::stoul(next());
        else if (arg == "--seed") rng.seed(std::stoull(next()));
        else if (arg == "--memory-node") memory_node = std::stoi(next());
        else if (arg == "--first-touch-cpu") first_touch_cpu = std::stoi(next());
        else if (arg == "--line") line = std::stoul(next());
        else if (arg == "--reps") reps = std::stoi(next());
        else if (arg == "--trials") trials = std::stoi(next());
        else if (arg == "--ms") ms = std::stod(next());
        else if (arg == "--cpu") cpu = std::stoi(next());
        else if (arg == "--peer") peer = std::stoi(next());
        else if (arg == "--interference") interference = next();
        else if (arg == "--suite") suite = next();
        else if (arg == "--huge") huge = true;
        else if (arg == "--quick") quick = true;
        else throw std::runtime_error("unknown option: " + arg);
    }
    if (line < sizeof(U) || line > 512 || (line & (line - 1)) || page < 4096 ||
        mib < 16 || mib > 1024 || reps < 1 || reps > 20 || trials < 5 || trials > 10000 ||
        !std::isfinite(ms) || ms <= 0 || ms > 1000 || peer == cpu || memory_node < -1 || memory_node > 63 ||
        first_touch_cpu < -1 || (first_touch_cpu >= 0 && memory_node >= 0) ||
        (interference != "compute" && interference != "stream") ||
        (suite != "all" && suite != "mlp" && suite != "tlb" && suite != "prefetch" && suite != "coherence" && suite != "streams"))
        throw std::runtime_error("invalid probe options");
    pin(cpu); calibrate();
    std::atomic<bool> ready = false, stop = false;
    // jthread ensures exceptions cannot leave an interference thread running.
    std::jthread sibling;
    if (peer >= 0 && suite != "coherence") {
        sibling = std::jthread([&](std::stop_token token) {
            std::stop_callback callback(token, [&] { stop.store(true); });
            stress(peer, interference, ready, stop);
        });
        while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
    }
    std::cout << "family,variant,bytes,k,stride,train,ahead,rep,n,p10,median,p90\n";
    if (suite == "all" || suite == "mlp") mlp(mib * 1024 * 1024, line, huge, reps, ms);
    if (suite == "all" || suite == "tlb") tlb(page, line, huge, reps, ms);
    if (suite == "all" || suite == "prefetch") probes(line, page, reps, trials, quick);
    if (suite == "coherence") coherence(peer, line, reps, trials);
    if (suite == "streams" || (suite == "all" && !quick)) stream_walk(mib * 1024 * 1024, page, line, reps, ms);
    stop.store(true);
    std::cerr << "checksum=" << sink << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
