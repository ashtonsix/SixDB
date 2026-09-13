// Same logical rows and requests across three physical placements.
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sched.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

using U = std::uint64_t;
constexpr U cookie = 0x9e3779b97f4a7c15ULL;
enum class Layout { split, dense, padded };
const char* name(Layout x) { return x == Layout::split ? "split64+32" : x == Layout::dense ? "dense96" : "padded128"; }

U mix(U x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
U selection_tag(U row, U seed) {
    // One chosen row per group of eight, with its position randomized by seed.
    // Fixed row%8==0 would confound sparse extension with dense96's line phase.
    return (row & 7) ^ (mix(seed ^ 0x8f517926b184cda3ULL ^ (row / 8)) & 7);
}
U now() {
    timespec t{};
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &t)) throw std::runtime_error("clock_gettime");
    return U(t.tv_sec) * 1000000000 + t.tv_nsec;
}
void pin(int cpu) {
    cpu_set_t available;
    if (sched_getaffinity(0, sizeof available, &available)) throw std::runtime_error("getaffinity");
    if (cpu < 0 || cpu >= CPU_SETSIZE || !CPU_ISSET(cpu, &available)) throw std::runtime_error("CPU outside allowed affinity");
    cpu_set_t selected; CPU_ZERO(&selected); CPU_SET(cpu, &selected);
    if (sched_setaffinity(0, sizeof selected, &selected)) throw std::runtime_error("setaffinity");
}

struct Mapping {
    std::size_t requested, phase, page, committed, reserved;
    void* allocation;
    std::byte* base;
    Mapping(std::size_t bytes, std::size_t offset) : requested(bytes), phase(offset), page(sysconf(_SC_PAGESIZE)) {
        committed = ((bytes + offset + page - 1) / page) * page;
        reserved = committed + 2 * page;
        allocation = mmap(nullptr, reserved, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (allocation == MAP_FAILED) throw std::runtime_error("mmap failed");
        base = static_cast<std::byte*>(allocation) + page;
        if (mprotect(base, committed, PROT_READ | PROT_WRITE)) throw std::runtime_error("mprotect failed");
        // Ask for base mappings; actual receipts, not this return code, own placement claims.
        if (madvise(base, committed, MADV_NOHUGEPAGE)) throw std::runtime_error("MADV_NOHUGEPAGE failed");
        std::memset(base, 0, committed); // Touch padding too; allocation is first-touched on the pinned CPU.
    }
    ~Mapping() { munmap(allocation, reserved); }
    Mapping(const Mapping&) = delete;
    U* data() const { return reinterpret_cast<U*>(base + phase); }
    void receipt(const std::string& tag) const {
        std::cerr << "mapping " << tag << " address=" << static_cast<void*>(base)
                  << " data=" << static_cast<void*>(base + phase) << " requested_bytes=" << requested
                  << " committed_bytes=" << committed << " reserved_bytes=" << reserved << '\n';
        std::ifstream maps("/proc/self/smaps");
        std::string line; bool active = false;
        while (std::getline(maps, line)) {
            std::uintptr_t lo{}, hi{}; char dash{};
            std::istringstream in(line);
            if ((in >> std::hex >> lo >> dash >> hi) && dash == '-') {
                active = lo <= reinterpret_cast<std::uintptr_t>(base) && reinterpret_cast<std::uintptr_t>(base) < hi;
                if (active) std::cerr << line << '\n';
            } else if (active && (line.starts_with("Size:") || line.starts_with("Rss:") ||
                       line.starts_with("KernelPageSize:") || line.starts_with("MMUPageSize:") ||
                       line.starts_with("AnonHugePages:") || line.starts_with("VmFlags:"))) std::cerr << line << '\n';
        }
        std::ifstream numa("/proc/self/numa_maps");
        bool found = false;
        while (std::getline(numa, line)) {
            std::uintptr_t start{}; std::istringstream in(line); in >> std::hex >> start;
            if (start == reinterpret_cast<std::uintptr_t>(base)) { std::cerr << "numa " << line << '\n'; found = true; }
        }
        if (!found) std::cerr << "numa unavailable: " << (numa.is_open() ? "no matching mapping receipt" : "/proc/self/numa_maps unreadable") << '\n';
    }
};

U core_value(const U* p) {
    return p[0] + std::rotl(p[1], 7) + std::rotl(p[2], 13) + std::rotl(p[3], 19) +
           std::rotl(p[4], 29) + std::rotl(p[5], 37) + std::rotl(p[6], 43) + std::rotl(p[7], 53);
}
U useful_compute(U value, unsigned rounds) {
    for (unsigned i = 0; i < rounds; ++i) value = std::rotl(value * 0xd6e8feb86659fd93ULL + i, 17);
    return value;
}
U extension_value(const U* p) { return p[0] + std::rotl(p[1], 11) + std::rotl(p[2], 31) + std::rotl(p[3], 47); }

struct Data {
    Layout layout;
    std::size_t rows;
    Mapping primary;
    // One unused word mapping in unsplit layouts is avoided with optional ownership.
    std::unique_ptr<Mapping> secondary;
    std::array<U, 32> starts{};
    std::array<U, 3> expected{};
    U order_fingerprint = 0;
    Data(Layout which, std::size_t n, std::size_t phase, std::size_t ext_phase, U seed, bool ordered, unsigned compute)
        : layout(which), rows(n), primary(n * (which == Layout::split ? 64 : which == Layout::dense ? 96 : 128), phase) {
        if (which == Layout::split) secondary = std::make_unique<Mapping>(n * 32, ext_phase);
        std::vector<U> order(n);
        std::iota(order.begin(), order.end(), U(0));
        std::mt19937_64 rng(seed);
        if (!ordered) std::shuffle(order.begin(), order.end(), rng);
        for (U id : order) order_fingerprint = mix(order_fingerprint ^ id);
        for (unsigned i = 0; i < 32; ++i) starts[i] = order[i * n / 32];
        for (std::size_t i = 0; i < n; ++i) {
            U id = order[i];
            std::array<U, 12> words{};
            words[0] = order[(i + 1) % n] ^ cookie;
            for (unsigned f = 1; f < 12; ++f) words[f] = mix(seed ^ (id * 13 + f));
            words[1] = (words[1] & ~U(7)) | selection_tag(id, seed);
            std::memcpy(core(id), words.data(), 64);
            std::memcpy(extension(id), words.data() + 8, 32);
            // Oracle consumes generated logical words before physical placement is used.
            U v = useful_compute(core_value(words.data()), compute);
            U e = extension_value(words.data() + 8);
            expected[0] += v;
            expected[1] += v + ((words[1] & 7) == 0 ? e : 0);
            expected[2] += v + e;
        }
        for (std::size_t i = 0; i < n; ++i) {
            U id = order[i];
            if ((core(id)[0] ^ cookie) != order[(i + 1) % n]) throw std::runtime_error("link validation");
            for (unsigned f = 1; f < 12; ++f) {
                U want = mix(seed ^ (id * 13 + f));
                if (f == 1) want = (want & ~U(7)) | selection_tag(id, seed);
                U got = f < 8 ? core(id)[f] : extension(id)[f - 8];
                if (got != want) throw std::runtime_error("logical word validation");
            }
        }
    }
    U* core(U row) const {
        return primary.data() + row * (layout == Layout::split ? 8 : layout == Layout::dense ? 12 : 16);
    }
    U* extension(U row) const { return secondary ? secondary->data() + row * 4 : core(row) + 8; }
    std::size_t allocated() const { return primary.committed + (secondary ? secondary->committed : 0); }
    void receipt(const std::string& tag) const {
        primary.receipt(tag + " core");
        if (secondary) secondary->receipt(tag + " extension");
    }
};

// Count union of demanded address lines within one operation. This is a model,
// with no inference about transfers, hits, prefetches or reuse across operations.
unsigned demands(const Data& data, U id, bool ext, std::size_t granule) {
    auto a = reinterpret_cast<std::uintptr_t>(data.core(id));
    auto b = reinterpret_cast<std::uintptr_t>(data.extension(id));
    auto lo = a / granule, hi = (a + 63) / granule;
    unsigned count = hi - lo + 1;
    if (ext) {
        auto elo = b / granule, ehi = (b + 31) / granule;
        count += ehi - elo + 1;
        if (std::max(lo, elo) <= std::min(hi, ehi)) count -= std::min(hi, ehi) - std::max(lo, elo) + 1;
    }
    return count;
}

template<Layout L, unsigned Mode>
__attribute__((always_inline)) inline void step(const Data& d, U& id, U& sum, unsigned compute, bool prefetch) {
    constexpr unsigned stride = L == Layout::split ? 8 : L == Layout::dense ? 12 : 16;
    const U* p = d.primary.data() + id * stride;
    const U* e;
    if constexpr (L == Layout::split) e = d.secondary->data() + id * 4;
    else e = p + 8;
    bool use_extension = Mode == 2 || (Mode == 1 && (p[1] & 7) == 0);
    if (prefetch && use_extension) __builtin_prefetch(e, 0, 3);
    U v = useful_compute(core_value(p), compute);
    // The computed result is consumed even on core-only rows.
    if (use_extension) v += extension_value(e);
    id = p[0] ^ cookie;
    sum += v;
}
template<Layout L, unsigned K, unsigned Mode, std::size_t... I>
__attribute__((noinline)) U walk(const Data& d, U rounds, unsigned compute, bool prefetch, std::index_sequence<I...>) {
    std::array<U, K> ids{d.starts[I * 32 / K]...};
    std::array<U, K> sums{};
    for (U r = 0; r < rounds; ++r) (step<L, Mode>(d, ids[I], sums[I], compute, prefetch), ...);
    return std::accumulate(sums.begin(), sums.end(), U(0));
}
template<Layout L, unsigned K>
U modes(const Data& d, unsigned mode, U rounds, unsigned compute, bool prefetch) {
    if (mode == 0) return walk<L, K, 0>(d, rounds, compute, prefetch, std::make_index_sequence<K>{});
    if (mode == 1) return walk<L, K, 1>(d, rounds, compute, prefetch, std::make_index_sequence<K>{});
    return walk<L, K, 2>(d, rounds, compute, prefetch, std::make_index_sequence<K>{});
}
template<Layout L>
U concurrency(const Data& d, unsigned k, unsigned mode, U rounds, unsigned compute, bool prefetch) {
    if (k == 1) return modes<L, 1>(d, mode, rounds, compute, prefetch);
    if (k == 8) return modes<L, 8>(d, mode, rounds, compute, prefetch);
    return modes<L, 32>(d, mode, rounds, compute, prefetch);
}
U execute(const Data& d, unsigned k, unsigned mode, U passes, unsigned compute, bool prefetch) {
    U rounds = (d.rows / k) * passes;
    if (d.layout == Layout::split) return concurrency<Layout::split>(d, k, mode, rounds, compute, prefetch);
    if (d.layout == Layout::dense) return concurrency<Layout::dense>(d, k, mode, rounds, compute, prefetch);
    return concurrency<Layout::padded>(d, k, mode, rounds, compute, prefetch);
}
void verify(U actual, U expected) { if (actual != expected) throw std::runtime_error("consumer checksum mismatch"); }

void check() {
    unsigned cases = 0;
    for (auto rows : {32U, 96U, 256U}) for (auto phase : {0U, 8U, 32U, 64U, 96U})
        for (auto layout : {Layout::split, Layout::dense, Layout::padded})
        for (bool ordered : {false, true}) for (unsigned compute : {0U, 3U}) {
            Data d(layout, rows, phase, (phase + 32) % 128, 457 + phase, ordered, compute);
            for (auto mode : {0U, 1U, 2U}) for (auto k : {1U, 8U, 32U}) for (bool prefetch : {false, true}) {
                verify(execute(d, k, mode, 3, compute, prefetch), d.expected[mode] * 3);
                ++cases;
            }
            for (auto line : {64U, 128U}) for (U row = 0; row < rows; ++row) for (bool ext : {false, true}) {
                std::set<std::uintptr_t> touched;
                for (unsigned b = 0; b < 64; ++b) touched.insert((reinterpret_cast<std::uintptr_t>(d.core(row)) + b) / line);
                if (ext) for (unsigned b = 0; b < 32; ++b) touched.insert((reinterpret_cast<std::uintptr_t>(d.extension(row)) + b) / line);
                verify(demands(d, row, ext, line), touched.size());
            }
        }
    Data dense(Layout::dense, 32, 0, 0, 1, false, 0);
    U core64 = 0, full64 = 0;
    for (unsigned i = 0; i < 32; ++i) { core64 += demands(dense, i, false, 64); full64 += demands(dense, i, true, 64); }
    verify(core64, 48); verify(full64, 64);
    std::cout << "Validated " << cases << " consumer cases, generated words/cycles, and byte-enumerated 64/128B geometry.\n";
}

int main(int argc, char** argv) try {
    std::size_t rows = 256, phase = 0, ext_phase = 0, line = 64;
    U seed = 12971; unsigned reps = 3, compute = 0; int cpu = sched_getcpu();
    double ms = 5; bool ordered = false, checking = false, prefetch = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--check") checking = true;
        else if (a == "--ordered") ordered = true;
        else if (a == "--prefetch-extension") prefetch = true;
        else {
            if (++i == argc) throw std::runtime_error("missing value: " + a);
            std::string v = argv[i];
            if (a == "--rows") rows = std::stoull(v);
            else if (a == "--phase") phase = std::stoull(v);
            else if (a == "--extension-phase") ext_phase = std::stoull(v);
            else if (a == "--line") line = std::stoull(v);
            else if (a == "--seed") seed = std::stoull(v);
            else if (a == "--reps") reps = std::stoul(v);
            else if (a == "--compute") compute = std::stoul(v);
            else if (a == "--cpu") cpu = std::stoi(v);
            else if (a == "--ms") ms = std::stod(v);
            else throw std::runtime_error("unknown option: " + a);
        }
    }
    if (rows < 32 || rows % 32 || rows > (U(1) << 26) || !reps || reps > 20 ||
        phase % 8 || ext_phase % 8 || phase >= U(sysconf(_SC_PAGESIZE)) || ext_phase >= U(sysconf(_SC_PAGESIZE)) ||
        !std::has_single_bit(line) || line < 32 || line > 512 || compute > 64 || !std::isfinite(ms) || ms <= 0 || ms > 1000)
        throw std::runtime_error("invalid geometry/range (rows: positive multiple of 32; phases: aligned u64 within page)");
    pin(cpu);
    if (checking) { check(); return 0; }
    std::cout << "layout,rows,phase,extension_phase,line_bytes,seed,rep,pattern,k,extension_denominator,compute_rounds,prefetch_extension,allocated_bytes,reserved_bytes,operations,extension_operations,useful_bytes,model_lines,model_pages,passes,elapsed_ns,ns_per_operation,checksum,logical_checksum,target_ns,calibration_batches,order_fingerprint\n";
    std::mt19937_64 order_rng(seed ^ 17831);
    for (unsigned rep = 0; rep < reps; ++rep) {
        std::array layouts{Layout::split, Layout::dense, Layout::padded};
        std::shuffle(layouts.begin(), layouts.end(), order_rng);
        for (auto layout : layouts) {
            Data d(layout, rows, phase, ext_phase, seed, ordered, compute);
            std::string tag = "rep=" + std::to_string(rep) + " layout=" + name(layout);
            d.receipt(tag + " before");
            std::array<U, 3> line_totals{}, page_totals{};
            for (U row = 0; row < rows; ++row) {
                U core_lines = demands(d, row, false, line), full_lines = demands(d, row, true, line);
                U core_pages = demands(d, row, false, d.primary.page), full_pages = demands(d, row, true, d.primary.page);
                bool selected = selection_tag(row, seed) == 0;
                line_totals[0] += core_lines; line_totals[1] += selected ? full_lines : core_lines; line_totals[2] += full_lines;
                page_totals[0] += core_pages; page_totals[1] += selected ? full_pages : core_pages; page_totals[2] += full_pages;
            }
            std::array<std::pair<unsigned, unsigned>, 9> cases{}; unsigned c = 0;
            for (auto mode : {0U, 1U, 2U}) for (auto k : {1U, 8U, 32U}) cases[c++] = {mode, k};
            std::shuffle(cases.begin(), cases.end(), order_rng);
            for (auto [mode, k] : cases) {
                U warm_start = now();
                U warm = execute(d, k, mode, 1, compute, prefetch);
                U warm_ns = now() - warm_start;
                verify(warm, d.expected[mode]);
                U target = std::ceil(ms * 1e6);
                U passes = std::clamp<U>(std::ceil(double(target) / std::max<U>(1, warm_ns)), 1, 100000);
                U result = 0, elapsed = 0;
                unsigned calibration = 0;
                for (;;) {
                    U start = now();
                    result = execute(d, k, mode, passes, compute, prefetch);
                    elapsed = now() - start;
                    verify(result, d.expected[mode] * passes);
                    if (elapsed >= target || passes == 100000 || calibration == 8) break;
                    ++calibration;
                    passes = std::clamp<U>(std::ceil(passes * (double(target) / std::max<U>(1, elapsed)) * 1.1), passes + 1, 100000);
                }
                U extensions = mode == 0 ? 0 : mode == 1 ? rows / 8 : rows;
                U operations = rows * passes;
                std::cout << name(layout) << ',' << rows << ',' << phase << ',' << ext_phase << ',' << line << ',' << seed << ',' << rep
                          << ',' << (ordered ? "ordered" : "random") << ',' << k << ',' << (mode == 0 ? 0 : mode == 1 ? 8 : 1)
                          << ',' << compute << ',' << prefetch << ',' << d.allocated() << ','
                          << d.primary.reserved + (d.secondary ? d.secondary->reserved : 0) << ',' << operations << ',' << extensions * passes
                          << ',' << (rows * 64 + extensions * 32) * passes << ',' << line_totals[mode] * passes << ',' << page_totals[mode] * passes
                          << ',' << passes << ',' << elapsed << ',' << std::setprecision(12) << double(elapsed) / operations << ',' << result
                          << ',' << d.expected[mode] << ',' << target << ',' << calibration << ',' << d.order_fingerprint << '\n';
            }
            d.receipt(tag + " after");
        }
    }
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
