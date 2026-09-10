#include "codec.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <linux/perf_event.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

namespace ip = ikea::integers;
namespace {
constexpr std::uint64_t seed_default = 0x1ceab17bacULL;
constexpr std::uint64_t step = 0x9e3779b97f4a7c15ULL;

std::uint64_t mix(std::uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
std::uint8_t value_at(std::uint64_t index, unsigned width, std::uint64_t seed) {
    return static_cast<std::uint8_t>(mix(index + seed) & ((1u << width) - 1));
}
std::uint64_t now_ns() {
    timespec t{};
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &t)) throw std::runtime_error("clock_gettime failed");
    return std::uint64_t(t.tv_sec) * 1000000000ULL + std::uint64_t(t.tv_nsec);
}
struct Buffer {
    std::uint8_t* data = nullptr;
    std::size_t bytes = 0, allocated = 0;
    explicit Buffer(std::size_t count) : bytes(count), allocated((count + 63) & ~std::size_t{63}) {
        void* p = nullptr;
        if (posix_memalign(&p, 64, allocated)) throw std::bad_alloc();
        data = static_cast<std::uint8_t*>(p);
        // Write every byte: commit and prefault all allocated pages before timing.
        std::memset(data, 0, allocated);
    }
    ~Buffer() { std::free(data); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

int pin_cpu(int requested) {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof allowed, &allowed)) throw std::runtime_error("getaffinity failed");
    if (requested < 0) {
        if (const char* env = std::getenv("SIXDB_CPU")) requested = std::stoi(env);
        else for (int i = 0; i < CPU_SETSIZE; ++i) if (CPU_ISSET(i, &allowed)) { requested = i; break; }
    }
    if (requested < 0 || requested >= CPU_SETSIZE || !CPU_ISSET(requested, &allowed))
        throw std::runtime_error("Requested CPU is outside the allowed affinity mask");
    cpu_set_t one;
    CPU_ZERO(&one); CPU_SET(requested, &one);
    if (sched_setaffinity(0, sizeof one, &one) || sched_getcpu() != requested)
        throw std::runtime_error("CPU pinning failed");
    return requested;
}

struct PmuResult {
    std::string status = "not_requested";
    std::uint64_t cycles = 0, instructions = 0, cache_misses = 0, enabled = 0, running = 0;
};
class Pmu {
    std::array<int, 3> fd{{-1, -1, -1}};
    std::string status = "not_requested";
public:
    explicit Pmu(bool requested) {
        if (!requested) return;
        constexpr std::array<std::uint64_t, 3> events{{PERF_COUNT_HW_CPU_CYCLES,
            PERF_COUNT_HW_INSTRUCTIONS, PERF_COUNT_HW_CACHE_MISSES}};
        status = "available";
        for (unsigned i = 0; i != fd.size(); ++i) {
            perf_event_attr a{};
            a.size = sizeof a; a.type = PERF_TYPE_HARDWARE; a.config = events[i];
            a.disabled = i == 0; a.exclude_kernel = 1; a.exclude_hv = 1;
            a.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
            fd[i] = static_cast<int>(syscall(SYS_perf_event_open, &a, 0, -1, i ? fd[0] : -1, 0));
            if (fd[i] < 0) { status = "unavailable_errno_" + std::to_string(errno); close_all(); break; }
        }
    }
    ~Pmu() { close_all(); }
    void close_all() { for (auto& f : fd) { if (f >= 0) close(f); f = -1; } }
    void start() {
        if (fd[0] < 0) return;
        if (ioctl(fd[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) ||
            ioctl(fd[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP)) {
            status = "enable_failed_errno_" + std::to_string(errno); close_all();
        }
    }
    PmuResult stop() {
        PmuResult result; result.status = status;
        if (fd[0] < 0) return result;
        struct Read { std::uint64_t count, enabled, running, values[3]; } r{};
        if (ioctl(fd[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) ||
            read(fd[0], &r, sizeof r) != sizeof r || r.count != 3) {
            result.status = "read_failed"; return result;
        }
        result.cycles = r.values[0]; result.instructions = r.values[1];
        result.cache_misses = r.values[2]; result.enabled = r.enabled; result.running = r.running;
        if (r.running == 0) result.status = "not_scheduled";
        return result;
    }
};

struct Options {
    unsigned min_power = 10, max_power = 27, power_step = 4, width = 0, repetitions = 3;
    std::uint64_t samples = 1ULL << 20, bulk_bytes = 512ULL << 10, bulk_passes = 0;
    std::uint64_t payload_bytes = 0;
    std::uint64_t seed = seed_default;
    int cpu = -1;
    bool pmu = false;
    std::string suite = "all", operation = "all", provider = "all", layout = "all";
};
Options arguments(int argc, char** argv) {
    Options o;
    bool explicit_powers = false, explicit_payload = false;
    for (int i = 1; i < argc; ++i) if (std::string_view(argv[i]) == "--quick") {
        o.min_power = 10; o.max_power = 16; o.power_step = 3; o.repetitions = 1;
        o.samples = 1ULL << 14; o.bulk_bytes = 64ULL << 10; o.bulk_passes = 8;
    }
    auto number = [](const char* p) { std::size_t n = 0; const auto v = std::stoull(p, &n, 0);
        if (p[n]) throw std::runtime_error("Invalid numeric option"); return v; };
    for (int i = 1; i < argc; ++i) {
        const std::string name = argv[i];
        if (name == "--quick") continue;
        if (name == "--pmu") { o.pmu = true; continue; }
        if (name == "--help") {
            std::cout << "Usage: ikea_integer_bench [--quick] [--width 1..7] [--suite all|bulk|capacity]\n"
                "  [--operation all|encode|decode|dependent|independent|get16]\n"
                "  [--provider all|selected|prior|plain|contiguous] [--layout all|local|scan]\n"
                "  [--min-power N] [--max-power N<=30] [--power N] [--power-step N]\n"
                "  [--payload-bytes N] (capacity only, 256..1073741824; excludes power options)\n"
                "  [--samples N] [--repetitions N] [--bulk-bytes N] [--bulk-passes N]\n"
                "  [--seed N] [--cpu N] [--pmu]\n"
                "CSV goes to stdout; progress/errors to stderr. Calls are matched opaque endpoints.\n";
            std::exit(0);
        }
        if (i + 1 == argc) throw std::runtime_error("Option needs a value: " + name);
        const char* value = argv[++i];
        if (name == "--min-power" || name == "--max-power" || name == "--power" || name == "--power-step")
            explicit_powers = true;
        if (name == "--min-power") o.min_power = static_cast<unsigned>(number(value));
        else if (name == "--max-power") o.max_power = static_cast<unsigned>(number(value));
        else if (name == "--power") o.min_power = o.max_power = static_cast<unsigned>(number(value));
        else if (name == "--power-step") o.power_step = static_cast<unsigned>(number(value));
        else if (name == "--width") o.width = static_cast<unsigned>(number(value));
        else if (name == "--repetitions") o.repetitions = static_cast<unsigned>(number(value));
        else if (name == "--samples") o.samples = number(value);
        else if (name == "--bulk-bytes") o.bulk_bytes = number(value);
        else if (name == "--bulk-passes") o.bulk_passes = number(value);
        else if (name == "--payload-bytes") { o.payload_bytes = number(value); explicit_payload = true; }
        else if (name == "--seed") o.seed = number(value);
        else if (name == "--cpu") o.cpu = static_cast<int>(number(value));
        else if (name == "--suite") o.suite = value;
        else if (name == "--operation") o.operation = value;
        else if (name == "--provider") o.provider = value;
        else if (name == "--layout") o.layout = value;
        else throw std::runtime_error("Unknown option: " + name);
    }
    if (explicit_payload && (explicit_powers || o.payload_bytes < 256 || o.payload_bytes > (1ULL << 30)))
        throw std::runtime_error("Payload budget must be 256..1073741824 bytes and cannot accompany power options");
    if (o.min_power < 8 || o.max_power > 30 || o.min_power > o.max_power || !o.power_step || o.power_step > 30 ||
        o.width > 7 || !o.repetitions || !o.samples || o.samples % 8 || o.bulk_bytes < 512 ||
        o.bulk_bytes > (1ULL << 30)) throw std::runtime_error("Invalid extent, width, repetition or sample options (samples must be divisible by8)");
    auto valid = [](const std::string& s, std::initializer_list<std::string_view> choices) {
        return std::find(choices.begin(), choices.end(), s) != choices.end(); };
    if (!valid(o.suite, {"all", "bulk", "capacity"}) ||
        !valid(o.operation, {"all", "encode", "decode", "dependent", "independent", "get16"}) ||
        !valid(o.provider, {"all", "selected", "prior", "plain", "contiguous"}) ||
        !valid(o.layout, {"all", "local", "scan"})) throw std::runtime_error("Unknown filter value");
    return o;
}

std::uint8_t plain_point(const std::uint8_t* p, unsigned i) { return p[i]; }
void plain_group(const std::uint8_t* p, unsigned i, std::uint8_t* out) { std::memcpy(out, p + i, 16); }
void plain_copy(const std::uint8_t* p, std::uint8_t* out) { std::memcpy(out, p, 256); }
template<unsigned K> std::uint8_t contiguous_point(const std::uint8_t* p, unsigned i) {
    const unsigned bit = i * K, shift = bit & 7;
    unsigned word = p[bit >> 3];
    if (shift + K > 8) word |= unsigned(p[(bit >> 3) + 1]) << 8;
    return static_cast<std::uint8_t>((word >> shift) & ((1u << K) - 1));
}
template<unsigned K> void contiguous_group(const std::uint8_t* p, unsigned i, std::uint8_t* out) {
    for (unsigned j = 0; j != 16; ++j) out[j] = contiguous_point<K>(p, i + j);
}
template<unsigned K> void contiguous_decode(const std::uint8_t* p, std::uint8_t* out) {
    for (unsigned i = 0; i != 256; ++i) out[i] = contiguous_point<K>(p, i);
}
template<unsigned K> void contiguous_encode(const std::uint8_t* in, std::uint8_t* out) {
    std::memset(out, 0, 32 * K);
    for (unsigned i = 0; i != 256; ++i) {
        const unsigned bit = i * K, shift = bit & 7;
        out[bit >> 3] |= std::uint8_t(unsigned(in[i]) << shift);
        if (shift + K > 8) out[(bit >> 3) + 1] |= std::uint8_t(unsigned(in[i]) >> (8 - shift));
    }
}
template<unsigned K> constexpr ip::Codec contiguous_codec() {
    return {contiguous_point<K>, contiguous_group<K>, contiguous_decode<K>, contiguous_encode<K>};
}
constexpr std::array<ip::Codec, 7> contiguous{{contiguous_codec<1>(), contiguous_codec<2>(),
    contiguous_codec<3>(), contiguous_codec<4>(), contiguous_codec<5>(), contiguous_codec<6>(), contiguous_codec<7>()}};
struct Arm {
    std::string provider, layout;
    ip::Codec codec;
    std::size_t cell_bytes;
    bool bulk;
};
// The fixed-payload comparison permits more logical values without growing
// the allocation. Every arm still consumes complete 256-value cells. With a
// budget <=1 GiB and cell_bytes >=32, the largest logical count is 2^33.
constexpr unsigned payload_power(std::uint64_t budget, std::size_t cell_bytes) {
    return unsigned(std::bit_width(budget / cell_bytes)) + 7;
}
static_assert(sizeof(std::size_t) >= 8);
static_assert(payload_power(512ULL << 20, 32) == 32);
static_assert(payload_power(512ULL << 20, 224) == 29);
static_assert(payload_power(512ULL << 20, 256) == 29);
static_assert(payload_power(1ULL << 30, 32) == 33);
std::vector<Arm> arms(const Options& o, unsigned width) {
    std::vector<Arm> result;
    for (const auto layout : {ip::Layout::local, ip::Layout::scan}) {
        const std::string name = layout == ip::Layout::local ? "local" : "scan";
        if (o.layout != "all" && o.layout != name) continue;
        if (o.provider == "all" || o.provider == "selected")
            result.push_back({"selected", name, ip::codecs(layout)[width - 1], 32 * width, true});
#if defined(IP_HAS_PRIOR)
        if (o.provider == "all" || o.provider == "prior")
            result.push_back({"prior", name, ip::prior_codecs(layout)[width - 1], 32 * width, true});
#endif
    }
    if (o.provider == "all" || o.provider == "plain")
        result.push_back({"plain", "u8", {plain_point, plain_group, plain_copy, plain_copy}, 256, true});
    if (o.provider == "all" || o.provider == "contiguous")
        result.push_back({"contiguous", "lsb_bitpack", contiguous[width - 1], 32 * width, false});
    return result;
}

// Learn the wire permutation through one-hot encodings. This is accounting of
// logically required bytes, NOT instrumentation of the native kernel's loads.
struct Location { std::uint16_t byte = 0; std::uint8_t bit = 0; };
using Mapping = std::array<std::array<Location, 7>, 256>;
Mapping mapping(const Arm& arm, unsigned width) {
    Mapping result{};
    alignas(64) std::array<std::uint8_t, 256> in{}, out{};
    arm.codec.encode(in.data(), out.data());
    if (std::any_of(out.begin(), out.begin() + arm.cell_bytes, [](auto v) { return v != 0; }))
        throw std::runtime_error("Zero values must encode to zero in these bit permutations");
    for (unsigned i = 0; i != 256; ++i) for (unsigned b = 0; b != width; ++b) {
        in[i] = std::uint8_t(1u << b);
        arm.codec.encode(in.data(), out.data());
        unsigned count = 0;
        for (unsigned byte = 0; byte < arm.cell_bytes; ++byte) if (out[byte]) {
            count += std::popcount(out[byte]);
            result[i][b] = {static_cast<std::uint16_t>(byte), static_cast<std::uint8_t>(std::countr_zero(out[byte]))};
        }
        if (count != 1 || arm.codec.get1(out.data(), i) != in[i])
            throw std::runtime_error("One-hot wire/accounting check failed");
        in[i] = 0;
    }
    return result;
}
void fill_values(std::uint8_t* out, std::uint64_t count, unsigned width, std::uint64_t seed) {
    for (std::uint64_t i = 0; i != count; ++i) out[i] = value_at(i, width, seed);
}
void fill_packed(const Arm& arm, Buffer& packed, std::uint64_t count, unsigned width, std::uint64_t seed) {
    alignas(64) std::array<std::uint8_t, 256> values{};
    for (std::uint64_t base = 0; base != count; base += 256) {
        for (unsigned i = 0; i != 256; ++i) values[i] = value_at(base + i, width, seed);
        arm.codec.encode(values.data(), packed.data + (base / 256) * arm.cell_bytes);
    }
}

struct Result {
    std::uint64_t sum0 = 0, sum1 = 0, state = 0, address_sum = 0;
    bool operator==(const Result&) const = default;
};
enum class Access { dependent, independent, group16 };
const char* access_name(Access a) {
    return a == Access::dependent ? "dependent" : a == Access::independent ? "independent" : "get16";
}
std::uint64_t trace_seed(const Options& o, unsigned power, Access access, unsigned repetition) {
    return mix(o.seed ^ (std::uint64_t(power) << 40) ^ (std::uint64_t(access) << 32) ^ (step * (repetition + 1)));
}
template<Access A> __attribute__((noinline)) Result access_loop(ip::Codec codec, const std::uint8_t* data,
    std::size_t cell_bytes, std::uint64_t values, std::uint64_t operations, std::uint64_t seed) {
    auto point = codec.get1;
    auto group = codec.get16;
    // Match opaque boundaries, including the controls; prevent TU-local
    // constant propagation from changing only one arm into an inline loop.
    asm volatile("" : "+r"(point), "+r"(group) : : "memory");
    Result result; result.state = seed;
    alignas(64) std::array<std::uint8_t, 16> out{};
    if constexpr (A == Access::dependent) {
        for (std::uint64_t j = 0; j != operations; ++j) {
            const auto index = result.state & (values - 1);
            const auto v = point(data + (index >> 8) * cell_bytes, unsigned(index & 255));
            result.sum0 += v; result.address_sum += index;
            // Returned u8 participates, but does not truncate the address state.
            result.state = mix(result.state + step + v);
        }
    } else {
        for (std::uint64_t j = 0; j != operations; j += 8) {
            std::array<std::uint64_t, 8> indices{};
            for (unsigned lane = 0; lane != 8; ++lane) {
                const auto hash = mix(seed + step * (j + lane + 1));
                indices[lane] = A == Access::group16 ? (hash & (values / 16 - 1)) * 16 : hash & (values - 1);
            }
            for (const auto index : indices) {
                if constexpr (A == Access::group16) {
                    group(data + (index >> 8) * cell_bytes, unsigned(index & 255), out.data());
                    std::uint64_t lo, hi;
                    std::memcpy(&lo, out.data(), 8); std::memcpy(&hi, out.data() + 8, 8);
                    result.sum0 += lo; result.sum1 += hi;
                } else result.sum0 += point(data + (index >> 8) * cell_bytes, unsigned(index & 255));
                result.address_sum += index;
            }
        }
        result.state = mix(seed + step * operations);
    }
    return result;
}
Result access_loop(Access a, const Arm& arm, const Buffer& data, std::uint64_t values,
                   std::uint64_t operations, std::uint64_t seed) {
    if (a == Access::dependent) return access_loop<Access::dependent>(arm.codec, data.data, arm.cell_bytes, values, operations, seed);
    if (a == Access::independent) return access_loop<Access::independent>(arm.codec, data.data, arm.cell_bytes, values, operations, seed);
    return access_loop<Access::group16>(arm.codec, data.data, arm.cell_bytes, values, operations, seed);
}
struct Coverage {
    std::uint64_t lines = 0, unique = 0, required_line_visits = 0, trace_hash = 0;
    std::size_t bitmap_bytes = 0;
};
struct Replay { Result result; Coverage coverage; };
struct RequiredLines { std::array<std::uint8_t, 5> offsets{}; unsigned count = 0; };
using LineMap = std::array<std::array<RequiredLines, 256>, 2>;
LineMap required_lines(const Mapping& map, unsigned width, unsigned count) {
    LineMap result{};
    for (unsigned phase = 0; phase != 2; ++phase) for (unsigned i = 0; i != 256; i += count) {
        auto& out = result[phase][i];
        for (unsigned lane = 0; lane != count; ++lane) for (unsigned bit = 0; bit != width; ++bit) {
            const auto line = static_cast<std::uint8_t>((32 * phase + map[i + lane][bit].byte) / 64);
            if (std::find(out.offsets.begin(), out.offsets.begin() + out.count, line) == out.offsets.begin() + out.count) {
                if (out.count == out.offsets.size()) throw std::runtime_error("Required-line map exceeds a256-byte cell");
                out.offsets[out.count++] = line;
            }
        }
    }
    return result;
}
Replay replay(Access a, const Arm& arm, const Mapping& map, const Buffer& packed, std::uint64_t values,
              unsigned width, std::uint64_t operations, std::uint64_t seed, std::uint64_t data_seed) {
    Replay r; r.result.state = seed;
    r.coverage.lines = (packed.bytes + 63) / 64;
    std::vector<std::uint64_t> seen((r.coverage.lines + 63) / 64);
    const auto line_map = required_lines(map, width, a == Access::group16 ? 16 : 1);
    r.coverage.bitmap_bytes = seen.size() * sizeof(std::uint64_t) + sizeof(LineMap);
    alignas(64) std::array<std::uint8_t, 16> actual{}, expected{};
    std::uint64_t expected_state = seed;
    for (std::uint64_t j = 0; j != operations; ++j) {
        const auto hash = mix(seed + step * (j + 1));
        const auto index = a == Access::dependent ? r.result.state & (values - 1) :
            a == Access::group16 ? (hash & (values / 16 - 1)) * 16 : hash & (values - 1);
        if (a == Access::dependent && index != (expected_state & (values - 1)))
            throw std::runtime_error("Decoded-value dependency changed the expected address trace");
        const unsigned count = a == Access::group16 ? 16 : 1;
        for (unsigned lane = 0; lane != count; ++lane) expected[lane] = value_at(index + lane, width, data_seed);
        const auto* cell = packed.data + (index >> 8) * arm.cell_bytes;
        if (a == Access::group16) arm.codec.get16(cell, unsigned(index & 255), actual.data());
        else actual[0] = arm.codec.get1(cell, unsigned(index & 255));
        if (!std::equal(actual.begin(), actual.begin() + count, expected.begin()))
            throw std::runtime_error("Read/replay differs from the logical value generator");
        if (a == Access::group16) {
            std::uint64_t lo, hi; std::memcpy(&lo, expected.data(), 8); std::memcpy(&hi, expected.data() + 8, 8);
            r.result.sum0 += lo; r.result.sum1 += hi;
        } else r.result.sum0 += expected[0];
        r.result.address_sum += index;
        if (a == Access::dependent) {
            r.result.state = mix(r.result.state + step + actual[0]);
            expected_state = mix(expected_state + step + expected[0]);
        }
        r.coverage.trace_hash = mix(r.coverage.trace_hash ^ index ^ (step * (j + 1)));
        // All cell starts are0 or32 modulo64. Deduplication within an
        // operation was precomputed; only the exact trace census remains here.
        const auto cell_offset = (index >> 8) * arm.cell_bytes;
        const auto& lines = line_map[(cell_offset & 63) / 32][unsigned(index & 255)];
        r.coverage.required_line_visits += lines.count;
        for (unsigned k = 0; k != lines.count; ++k) {
            const auto line = cell_offset / 64 + lines.offsets[k]; const auto mask = 1ULL << (line & 63);
            if (!(seen[line >> 6] & mask)) { seen[line >> 6] |= mask; ++r.coverage.unique; }
        }
    }
    if (a != Access::dependent) r.result.state = mix(seed + step * operations);
    return r;
}

__attribute__((noinline)) void bulk_loop(void (*function)(const std::uint8_t*, std::uint8_t*),
    const std::uint8_t* in, std::uint8_t* out, std::size_t input_stride, std::size_t output_stride,
    std::uint64_t cells, std::uint64_t passes) {
    asm volatile("" : "+r"(function) : : "memory");
    for (std::uint64_t pass = 0; pass != passes; ++pass)
        for (std::uint64_t cell = 0; cell != cells; ++cell)
            function(in + cell * input_stride, out + cell * output_stride);
}
std::uint64_t bulk_passes(const Options& o, std::uint64_t values) {
    if (o.bulk_passes) return o.bulk_passes;
    Buffer source(values), output(values);
    fill_values(source.data, values, 7, o.seed);
    std::uint64_t passes = 1;
    for (;;) {
        const auto start = now_ns();
        bulk_loop(plain_copy, source.data, output.data, 256, 256, values / 256, passes);
        const auto elapsed = now_ns() - start;
        if (elapsed >= 20000000 || passes >= (1ULL << 20)) break;
        passes *= 2;
    }
    if (std::memcmp(source.data, output.data, values)) throw std::runtime_error("Bulk calibration checksum failed");
    return passes;
}
std::uint64_t check_bulk(bool encode, const Arm& arm, const Mapping& map, const Buffer& packed,
    const Buffer& plain, std::uint64_t values, unsigned width, std::uint64_t seed) {
    std::uint64_t checksum = 0;
    for (std::uint64_t base = 0; base != values; base += 256) {
        const auto* cell = packed.data + (base / 256) * arm.cell_bytes;
        for (unsigned i = 0; i != 256; ++i) {
            const auto expected = value_at(base + i, width, seed);
            unsigned actual = plain.data[base + i];
            if (encode) {
                actual = 0;
                for (unsigned bit = 0; bit != width; ++bit)
                    actual |= ((cell[map[i][bit].byte] >> map[i][bit].bit) & 1u) << bit;
            }
            if (actual != expected) throw std::runtime_error("Bulk output disagrees with logical input");
            checksum += expected;
        }
    }
    return checksum;
}

struct Timing {
    std::uint64_t ns = 0;
    long minor_faults = 0, major_faults = 0;
    PmuResult pmu;
};
template<class F> Timing timed(const Options& o, int cpu, F&& function) {
    if (sched_getcpu() != cpu) throw std::runtime_error("CPU changed before timing");
    Pmu pmu(o.pmu);
    rusage before{}, after{};
    getrusage(RUSAGE_SELF, &before);
    pmu.start();
    asm volatile("" : : : "memory");
    const auto start = now_ns();
    function();
    const auto elapsed = now_ns() - start;
    asm volatile("" : : : "memory");
    const auto counters = pmu.stop();
    getrusage(RUSAGE_SELF, &after);
    if (sched_getcpu() != cpu) throw std::runtime_error("CPU changed during timing");
    return {elapsed, after.ru_minflt - before.ru_minflt, after.ru_majflt - before.ru_majflt, counters};
}
void header() {
    std::cout << "suite,operation,call,provider,layout,width,repetition,cpu,logical_values,operations,values_per_operation,"
        "passes,seed,data_seed,payload_bytes,input_bytes,output_bytes,allocation_bytes,hot_metadata_bytes,trace_bytes,"
        "audit_aux_bytes,payload_alignment,page_bytes,elapsed_ns,ns_per_operation,ns_per_value,checksum0,checksum1,"
        "final_state,address_sum,trace_hash,required_unique_payload_lines,payload_lines,required_line_visits,"
        "required_line_coverage,coverage_kind,residence,minor_faults,major_faults,pmu_status,cycles_raw,instructions_raw,"
        "cache_misses_raw,pmu_time_enabled_ns,pmu_time_running_ns,capacity_mode,requested_payload_bytes\n";
}
void row(const Options& o, int cpu, const Arm& arm, unsigned width, unsigned repetition, std::string_view suite,
    std::string_view operation, std::uint64_t values, std::uint64_t operations, unsigned grain, std::uint64_t passes,
    std::uint64_t seed, std::uint64_t payload, std::uint64_t input, std::uint64_t output, std::uint64_t allocated,
    const Timing& timing, const Result& result, const Coverage& coverage) {
    const double op_ns = double(timing.ns) / double(operations);
    std::cout << suite << ',' << operation << ",opaque," << arm.provider << ',' << arm.layout << ',' << width << ','
        << repetition << ',' << cpu << ',' << values << ',' << operations << ',' << grain << ',' << passes << ','
        << seed << ',' << o.seed << ',' << payload << ',' << input << ',' << output << ',' << allocated << ','
        << sizeof(ip::Codec) << ",0," << sizeof(Mapping) + coverage.bitmap_bytes << ",64," << sysconf(_SC_PAGESIZE) << ','
        << timing.ns << ',' << op_ns << ',' << op_ns / grain << ',' << result.sum0 << ',' << result.sum1 << ','
        << result.state << ',' << result.address_sum << ',' << coverage.trace_hash << ',' << coverage.unique << ','
        << coverage.lines << ',' << coverage.required_line_visits << ',';
    if (coverage.lines) std::cout << double(coverage.unique) / double(coverage.lines); else std::cout << "NA";
    std::cout << ',' << (suite == "capacity" ? "required_bytes_exact_trace" : "full_sequential_extent")
        << ",unestablished," << timing.minor_faults << ',' << timing.major_faults << ',' << timing.pmu.status << ',';
    if (timing.pmu.status == "available") std::cout << timing.pmu.cycles << ',' << timing.pmu.instructions << ','
        << timing.pmu.cache_misses << ',' << timing.pmu.enabled << ',' << timing.pmu.running;
    else std::cout << "NA,NA,NA,NA,NA";
    std::cout << ',' << (suite == "capacity" ? (o.payload_bytes ? "fixed_payload" : "fixed_logical") : "not_applicable")
        << ',' << (suite == "capacity" ? o.payload_bytes : 0) << '\n' << std::flush;
}
bool operation(const Options& o, std::string_view name) { return o.operation == "all" || o.operation == name; }
void bulk(const Options& o, int cpu, const Arm& arm, unsigned width, std::uint64_t values, std::uint64_t passes) {
    if (!arm.bulk) return; // The contiguous control is explicitly a scalar capacity comparator.
    Buffer plain(values), packed(values / 256 * arm.cell_bytes);
    const auto map = mapping(arm, width);
    fill_values(plain.data, values, width, o.seed);
    fill_packed(arm, packed, values, width, o.seed);
    for (const bool encode : {false, true}) {
        const auto name = encode ? "encode" : "decode";
        if (!operation(o, name)) continue;
        auto function = encode ? arm.codec.encode : arm.codec.decode;
        const auto* in = encode ? plain.data : packed.data;
        auto* out = encode ? packed.data : plain.data;
        const auto in_stride = encode ? 256 : arm.cell_bytes;
        const auto out_stride = encode ? arm.cell_bytes : 256;
        bulk_loop(function, in, out, in_stride, out_stride, values / 256, 1); // explicit full warm pass
        for (unsigned rep = 0; rep != o.repetitions; ++rep) {
            const auto timing = timed(o, cpu, [&] { bulk_loop(function, in, out, in_stride, out_stride, values / 256, passes); });
            Result result; result.sum0 = check_bulk(encode, arm, map, packed, plain, values, width, o.seed);
            Coverage coverage; coverage.lines = (packed.bytes + 63) / 64; coverage.unique = coverage.lines;
            row(o, cpu, arm, width, rep, "bulk", name, values, values / 256 * passes, 256, passes, o.seed,
                packed.bytes, encode ? plain.bytes : packed.bytes, encode ? packed.bytes : plain.bytes,
                packed.allocated + plain.allocated, timing, result, coverage);
        }
    }
}
void capacity(const Options& o, int cpu, const Arm& arm, unsigned width, unsigned power) {
    if (power < 8 || power > (o.payload_bytes ? 33u : 30u)) throw std::runtime_error("Capacity power exceeds its mode's limit");
    const std::uint64_t values = 1ULL << power;
    Buffer packed(values / 256 * arm.cell_bytes);
    if (o.payload_bytes && packed.allocated > o.payload_bytes) throw std::runtime_error("Capacity allocation exceeds payload budget");
    const auto map = mapping(arm, width);
    fill_packed(arm, packed, values, width, o.seed);
    for (const auto access : {Access::dependent, Access::independent, Access::group16}) {
        if (!operation(o, access_name(access))) continue;
        for (unsigned rep = 0; rep != o.repetitions; ++rep) {
            const auto seed = trace_seed(o, power, access, rep);
            Result result;
            const auto timing = timed(o, cpu, [&] { result = access_loop(access, arm, packed, values, o.samples, seed); });
            // Replay AFTER the measurement. It must not prewarm its own trace.
            const auto audit = replay(access, arm, map, packed, values, width, o.samples, seed, o.seed);
            if (!(result == audit.result)) throw std::runtime_error("Timed checksum/state/address trace differs from replay");
            row(o, cpu, arm, width, rep, "capacity", access_name(access), values, o.samples,
                access == Access::group16 ? 16 : 1, 1, seed, packed.bytes, packed.bytes, 0, packed.allocated,
                timing, result, audit.coverage);
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = arguments(argc, argv);
#if !defined(IP_HAS_PRIOR)
        if (options.provider == "prior") throw std::runtime_error("Build with IP_HAS_PRIOR and link ikea_integer_prior to request prior cases");
        std::cerr << "Prior adapter not linked: prior cases absent, no parity claim.\n";
#endif
        const int cpu = pin_cpu(options.cpu);
        const auto values = (options.bulk_bytes / 2 / 256) * 256;
        const bool do_bulk = options.suite != "capacity" && (operation(options, "encode") || operation(options, "decode"));
        const bool do_capacity = options.suite != "bulk" &&
            (operation(options, "dependent") || operation(options, "independent") || operation(options, "get16"));
        if (!do_bulk && !do_capacity) throw std::runtime_error("Suite/operation filters select no cases");
        if (options.payload_bytes && !do_capacity) throw std::runtime_error("Payload budget requires a capacity operation");
        if (do_bulk && !do_capacity && options.provider == "contiguous")
            throw std::runtime_error("The contiguous control has capacity operations only");
        const auto passes = do_bulk ? bulk_passes(options, values) : 0;
        std::cerr << "Pinned CPU " << cpu << "; matched opaque callbacks; sequential cases/repetitions; bulk passes " << passes << ".\n"
            "Capacity allocations do not establish cache residence. Coverage counts required payload bytes, not observed hardware accesses.\n";
        std::cout << std::setprecision(10);
        header();
        for (unsigned width = options.width ? options.width : 1; width <= (options.width ? options.width : 7); ++width) {
            auto candidates = arms(options, width);
            if (candidates.empty()) throw std::runtime_error("Provider/layout filters select no arms");
            if (do_bulk) for (const auto& arm : candidates) {
                std::cerr << "bulk k=" << width << ' ' << arm.provider << '/' << arm.layout << '\n';
                bulk(options, cpu, arm, width, values, passes);
            }
            if (do_capacity) {
                if (options.payload_bytes) {
                    for (const auto& arm : candidates) {
                        const auto power = payload_power(options.payload_bytes, arm.cell_bytes);
                        std::cerr << "capacity fixed_payload budget=" << options.payload_bytes << " values=2^" << power
                            << " k=" << width << ' ' << arm.provider << '/' << arm.layout << '\n';
                        capacity(options, cpu, arm, width, power);
                    }
                    continue;
                }
                std::vector<unsigned> powers;
                for (unsigned p = options.min_power; p <= options.max_power; p += options.power_step) powers.push_back(p);
                if (powers.back() != options.max_power) powers.push_back(options.max_power);
                for (const auto power : powers) for (const auto& arm : candidates) {
                    std::cerr << "capacity 2^" << power << " k=" << width << ' ' << arm.provider << '/' << arm.layout << '\n';
                    capacity(options, cpu, arm, width, power);
                }
            }
        }
        std::cerr << "All emitted measurements passed output and trace replay checks.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "benchmark failed: " << e.what() << '\n';
        return 1;
    }
}
