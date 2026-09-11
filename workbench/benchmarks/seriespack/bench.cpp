// Matched whole-array codec and random materialization workloads. Workbench's
// pinned Google Benchmark supplies timing/repetitions; worker.py owns capture.
#include <ikea/seriespack.h>
#include <benchmark/benchmark.h>
#include "points.h"
#include "probe_controls.h"
#include "dependent_walk.h"
#ifdef SERIESPACK_HAS_PRIOR
#include "calico.h"
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <sched.h>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace sp = ikea::seriespack;
namespace seriespack_measurement {
void register_composition_benchmarks();
void register_casing_benchmarks();
void register_checked_point_benchmarks();
void register_head_placement_benchmarks();
void register_boundary_probe();
void register_ordinary_runtime_benchmarks();
}
namespace {
using U = std::uint64_t;
constexpr U seed = 0x64e9'8762'7a0b'18cfULL;
U mix(U x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
U value_at(std::size_t i, unsigned width) {
    return mix(seed + i) & (~U{0} >> (64 - width));
}
void require(bool good, const char* message) {
    if (!good) throw std::runtime_error(message);
}
std::size_t option_size(const char* name, std::size_t fallback) {
    const auto* text = std::getenv(name);
    if (!text) return fallback;
    std::size_t read = 0;
    const auto result = std::stoull(text, &read);
    require(text[read] == '\0' && result != 0, "invalid size environment variable");
    return result;
}
int pin() {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    require(sched_getaffinity(0, sizeof allowed, &allowed) == 0, "get affinity");
    int cpu = -1;
    if (const auto* requested = std::getenv("SIXDB_CPU")) cpu = std::stoi(requested);
    else for (int i = 0; i < CPU_SETSIZE; ++i)
        if (CPU_ISSET(i, &allowed)) { cpu = i; break; }
    require(cpu >= 0 && cpu < CPU_SETSIZE && CPU_ISSET(cpu, &allowed), "CPU outside allowed affinity");
    cpu_set_t one;
    CPU_ZERO(&one); CPU_SET(cpu, &one);
    require(sched_setaffinity(0, sizeof one, &one) == 0 && sched_getcpu() == cpu, "pin CPU");
    return cpu;
}
struct buffer {
    std::byte* data = nullptr;
    std::size_t size = 0;
    explicit buffer(std::size_t bytes = 0) : size(bytes) {
        if (!bytes) return;
        void* pointer = nullptr;
        require(posix_memalign(&pointer, 64, (bytes + 63) & ~std::size_t{63}) == 0, "allocation");
        data = static_cast<std::byte*>(pointer);
        std::memset(data, 0, bytes); // Prefault outside timing.
    }
    ~buffer() { std::free(data); }
    buffer(const buffer&) = delete;
    buffer& operator=(const buffer&) = delete;
    std::span<std::byte> span() { return {data, size}; }
};
unsigned element_size(unsigned width) {
    return width <= 8 ? 1 : width <= 16 ? 2 : width <= 32 ? 4 : 8;
}
sp::input_values input_values(const std::byte* data, std::size_t count, unsigned bytes) {
    if (bytes == 1) return sp::input_values{std::span(reinterpret_cast<const std::uint8_t*>(data), count)};
    if (bytes == 2) return sp::input_values{std::span(reinterpret_cast<const std::uint16_t*>(data), count)};
    if (bytes == 4) return sp::input_values{std::span(reinterpret_cast<const std::uint32_t*>(data), count)};
    return sp::input_values{std::span(reinterpret_cast<const U*>(data), count)};
}
sp::output_values output_values(std::byte* data, std::size_t count, unsigned bytes) {
    if (bytes == 1) return sp::output_values{std::span(reinterpret_cast<std::uint8_t*>(data), count)};
    if (bytes == 2) return sp::output_values{std::span(reinterpret_cast<std::uint16_t*>(data), count)};
    if (bytes == 4) return sp::output_values{std::span(reinterpret_cast<std::uint32_t*>(data), count)};
    return sp::output_values{std::span(reinterpret_cast<U*>(data), count)};
}
void store_value(std::byte* data, std::size_t i, unsigned bytes, U value) {
    if (bytes == 1) { const auto v = std::uint8_t(value); std::memcpy(data + i, &v, 1); }
    else if (bytes == 2) { const auto v = std::uint16_t(value); std::memcpy(data + i * 2, &v, 2); }
    else if (bytes == 4) { const auto v = std::uint32_t(value); std::memcpy(data + i * 4, &v, 4); }
    else std::memcpy(data + i * 8, &value, 8);
}
U load_value(const std::byte* data, std::size_t i, unsigned bytes) {
    if (bytes == 1) return std::to_integer<std::uint8_t>(data[i]);
    if (bytes == 2) { std::uint16_t v; std::memcpy(&v, data + i * 2, 2); return v; }
    if (bytes == 4) { std::uint32_t v; std::memcpy(&v, data + i * 4, 4); return v; }
    U v; std::memcpy(&v, data + i * 8, 8); return v;
}
const char* target_name(sp::execution_target t) {
    switch (t) {
        case sp::execution_target::automatic: return "automatic";
        case sp::execution_target::scalar: return "scalar";
        case sp::execution_target::avx2: return "avx2";
        case sp::execution_target::avx512: return "avx512";
        case sp::execution_target::neon: return "neon";
    }
    return "invalid";
}
struct configuration {
    unsigned width = 1, head = 0, io_bytes = 8;
    sp::geometry geometry = sp::geometry::local8;
    sp::execution_target target = sp::execution_target::automatic;
    bool prior = false, random = false;
    std::size_t count = 8192;
    unsigned point_path = 0; // 0: bound calls; 1/2: static arithmetic/offset regions.
    bool predecessor = false; // Immediate LocalPack/ScanPack or wider-body control.
    bool predecessor_region32 = false;
    bool operator==(const configuration&) const = default;
};
struct fixture {
    configuration c;
    sp::description layout;
    std::size_t tiles, tile_values, tile_bytes, encoded_bytes;
    buffer payload, head0, head1, input, output;
    std::optional<sp::mutable_view> view;
    std::optional<sp::bound_reader> reader;
    std::optional<sp::bound_encoder> encoder;
    seriespack_measurement::point_region point_region;
    seriespack_measurement::predecessor_codec predecessor{};
    seriespack_measurement::predecessor_u64_codec predecessor64{};
    std::vector<std::size_t> indices;
    std::size_t cursor = 0;
#ifdef SERIESPACK_HAS_PRIOR
    seriespack_measurement::prior_codec prior{};
    seriespack_measurement::prior_u8_codec prior8{};
#endif
    seriespack_measurement::dependent_walk dependent;
    explicit fixture(configuration conf)
        : c(conf), layout{c.width, c.head, c.geometry},
          tiles(c.count / (c.prior ? 256 : sp::tile_values(layout))),
          tile_values(c.prior ? 256 : sp::tile_values(layout)),
          tile_bytes(c.prior ? 32 * c.width : sp::tile_bytes(layout)),
          encoded_bytes(c.count * c.width / 8),
          payload(tiles * tile_bytes), head0(c.head >= 8 ? c.count : 0),
          head1(c.head == 16 ? c.count : 0),
          input(c.random ? 0 : c.count * c.io_bytes),
          output(c.random ? 0 : c.count * c.io_bytes) {
        require(c.count % 256 == 0, "whole-cell comparison extent");
        if (c.predecessor) {
            require(c.prior && ((c.width <= 7 && (c.io_bytes == 1 || c.random)) ||
                    (c.width == 56 && c.io_bytes == 8 && c.geometry == sp::geometry::local8)),
                    "predecessor control carrier/geometry");
            if (c.width <= 7) predecessor = seriespack_measurement::predecessor(c.width, c.geometry == sp::geometry::striped);
            else predecessor64 = seriespack_measurement::predecessor56(c.predecessor_region32);
        } else if (!c.prior) {
            auto attached = sp::mutable_view::attach(layout, c.count,
                {{payload.span(), tile_bytes}, {{{head0.span(), tile_values}, {head1.span(), tile_values}}}});
            require(attached.has_value(), "SeriesPack attach");
            view = *attached;
            auto r = sp::bind_reader(view->as_const(), c.target);
            auto e = sp::bind_encoder(*view, c.target);
            require(r.has_value() && e.has_value(), "SeriesPack bind");
            reader = *r; encoder = *e;
            require(reader->target() == c.target && encoder->target() == c.target, "target changed");
            if (c.point_path) point_region = seriespack_measurement::static_points(layout,
                c.point_path == 1 ? sp::point_reader::arithmetic : sp::point_reader::constant_offsets);
        }
#ifdef SERIESPACK_HAS_PRIOR
        else {
            prior = seriespack_measurement::prior(c.width, c.geometry == sp::geometry::striped);
            if (c.io_bytes == 1) prior8 = seriespack_measurement::prior_u8(c.width, c.geometry == sp::geometry::striped);
        }
#endif
        if (c.random) {
            // Bounded staging keeps the k=1 random-access fixture from needing
            // an eight-times-wider, potentially multi-GiB materialized array.
            constexpr std::size_t batch = 8192;
            std::array<U, batch> values;
            for (std::size_t begin = 0; begin < c.count; begin += batch) {
                const auto count = std::min(batch, c.count - begin);
                for (std::size_t i = 0; i < count; ++i) values[i] = value_at(begin + i, c.width);
                if (c.predecessor) {
                    auto* out = reinterpret_cast<std::uint8_t*>(payload.data) + begin * c.width / 8;
                    if (c.width <= 7) {
                        std::array<std::uint8_t, batch> narrow;
                        for (std::size_t i = 0; i < count; ++i) narrow[i] = static_cast<std::uint8_t>(values[i]);
                        predecessor.encode(narrow.data(), out, count);
                    } else predecessor64.encode(values.data(), out, count);
                } else if (!c.prior) {
                    auto p = view->placement();
                    const auto first = begin / tile_values;
                    p.payload.bytes = p.payload.bytes.subspan(first * p.payload.stride);
                    if (c.head >= 8) p.heads[0].bytes = p.heads[0].bytes.subspan(first * p.heads[0].stride);
                    if (c.head == 16) p.heads[1].bytes = p.heads[1].bytes.subspan(first * p.heads[1].stride);
                    const auto part = sp::mutable_view::assume_valid(layout, count, p);
                    const auto e = sp::bind_encoder(part, c.target);
                    e->encode(sp::input_values{std::span(values).first(count)});
                }
#ifdef SERIESPACK_HAS_PRIOR
                else prior.encode(values.data(), reinterpret_cast<std::uint8_t*>(payload.data) + begin * c.width / 8, count);
#endif
            }
            // The sample stream spans the whole encoded resident set; it is
            // carried between timed batches, never replayed from a tiny prefix.
            const auto queries = std::max<std::size_t>(8192, encoded_bytes / 16);
            indices.resize(queries);
            for (std::size_t i = 0; i < queries; ++i) indices[i] = mix(seed + i * 0x9e3779b97f4a7c15ULL) % c.count;
            for (std::size_t i = 0; i < std::min<std::size_t>(queries, 1024); ++i)
                require(get(indices[i]) == value_at(indices[i], c.width), "point comparator correctness");
            if (c.point_path) {
                U expected = 0;
                for (std::size_t i = 0; i < 1024; ++i) expected += value_at(indices[i], c.width);
                require(point_region.sum && point_region.sum(view->as_const(), indices.data(), 1024) == expected,
                        "static point region correctness");
            }
            alignas(64) std::array<U, 16> block;
            for (auto index : {std::size_t{0}, (c.count / 2) & ~std::size_t{15}, c.count - 16}) {
                get16(index, block.data());
                for (unsigned lane = 0; lane < 16; ++lane)
                    require(block[lane] == value_at(index + lane, c.width), "get16 comparator correctness");
            }
        } else {
            for (std::size_t i = 0; i < c.count; ++i)
                store_value(input.data, i, c.io_bytes, value_at(i, c.width));
            encode();
            if (c.predecessor) {
                buffer canonical(encoded_bytes);
                const auto attached = sp::mutable_view::attach(layout, c.count,
                    {{canonical.span(), sp::tile_bytes(layout)}, {}});
                require(attached.has_value(), "predecessor canonical placement");
                require(sp::encode(*attached, input_values(input.data, c.count, c.io_bytes),
                    nullptr, sp::execution_target::scalar).has_value(), "predecessor canonical construction");
                require(std::memcmp(payload.data, canonical.data, encoded_bytes) == 0,
                        "predecessor same-wire bytes");
            }
            decode();
            for (std::size_t i = 0; i < c.count; ++i)
                require(load_value(output.data, i, c.io_bytes) == value_at(i, c.width), "bulk comparator correctness");
        }
    }
    void encode() {
        if (c.predecessor) {
            if (c.io_bytes == 1) predecessor.encode(reinterpret_cast<const std::uint8_t*>(input.data),
                reinterpret_cast<std::uint8_t*>(payload.data), c.count);
            else predecessor64.encode(reinterpret_cast<const U*>(input.data),
                reinterpret_cast<std::uint8_t*>(payload.data), c.count);
        }
        else if (!c.prior) encoder->encode(input_values(input.data, c.count, c.io_bytes));
#ifdef SERIESPACK_HAS_PRIOR
        else if (c.io_bytes == 1) prior8.encode(reinterpret_cast<const std::uint8_t*>(input.data), reinterpret_cast<std::uint8_t*>(payload.data), c.count);
        else prior.encode(reinterpret_cast<const U*>(input.data), reinterpret_cast<std::uint8_t*>(payload.data), c.count);
#endif
    }
    void decode() {
        if (c.predecessor) {
            if (c.io_bytes == 1) predecessor.decode(reinterpret_cast<const std::uint8_t*>(payload.data),
                reinterpret_cast<std::uint8_t*>(output.data), c.count);
            else predecessor64.decode(reinterpret_cast<const std::uint8_t*>(payload.data),
                reinterpret_cast<U*>(output.data), c.count);
        }
        else if (!c.prior) reader->decode({0, c.count}, output_values(output.data, c.count, c.io_bytes));
#ifdef SERIESPACK_HAS_PRIOR
        else if (c.io_bytes == 1) prior8.decode(reinterpret_cast<const std::uint8_t*>(payload.data), reinterpret_cast<std::uint8_t*>(output.data), c.count);
        else prior.decode(reinterpret_cast<const std::uint8_t*>(payload.data), reinterpret_cast<U*>(output.data), c.count);
#endif
    }
    U get(std::size_t index) {
        if (c.predecessor) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(payload.data);
            return c.width <= 7 ? predecessor.get(p, index) : predecessor64.get(p, index);
        }
        if (!c.prior) return reader->get(index);
#ifdef SERIESPACK_HAS_PRIOR
        return prior.get(reinterpret_cast<const std::uint8_t*>(payload.data), index);
#else
        __builtin_unreachable();
#endif
    }
    void get16(std::size_t index, U* out) {
        if (c.predecessor) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(payload.data);
            if (c.width <= 7) predecessor.get16(p, index, out);
            else predecessor64.get16(p, index, out);
        }
        else if (!c.prior) reader->decode({index, index + 16}, sp::output_values{std::span(out, 16)});
#ifdef SERIESPACK_HAS_PRIOR
        else prior.get16(reinterpret_cast<const std::uint8_t*>(payload.data), index, out);
#endif
    }
    // Select the provider once per query region. Adding a control should not
    // add another provider branch or mutable fixture lookup to every point.
    template<class Consumer>
    void with_point_reader(Consumer consume) {
        if (!c.prior) {
            const auto bound = *reader;
            consume([&bound](std::size_t index) { return bound.get(index); });
            return;
        }
        U (*read)(const std::uint8_t*, std::size_t) = nullptr;
        if (c.predecessor) read = c.width <= 7 ? predecessor.get : predecessor64.get;
#ifdef SERIESPACK_HAS_PRIOR
        else read = prior.get;
#endif
        require(read != nullptr, "point control available");
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data);
        consume([read, bytes](std::size_t index) { return read(bytes, index); });
    }
    template<class Consumer>
    void with_group_reader(Consumer consume) {
        if (!c.prior) {
            const auto bound = *reader;
            consume([&bound](std::size_t index, U* out) {
                bound.decode({index, index + 16}, sp::output_values{std::span(out, 16)});
            });
            return;
        }
        void (*read)(const std::uint8_t*, std::size_t, U*) = nullptr;
        if (c.predecessor) read = c.width <= 7 ? predecessor.get16 : predecessor64.get16;
#ifdef SERIESPACK_HAS_PRIOR
        else read = prior.get16;
#endif
        require(read != nullptr, "group control available");
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data);
        consume([read, bytes](std::size_t index, U* out) { read(bytes, index, out); });
    }
};
std::unique_ptr<fixture> resident;
fixture& get_fixture(configuration c) {
    if (!resident || resident->c != c) {
        // Construction is outside timing. Release the preceding encoded and
        // query arrays before allocating the next large resident workload.
        resident.reset();
        resident = std::make_unique<fixture>(c);
    }
    return *resident;
}
enum class operation { encode, decode, point, get16, dependent };
void run(benchmark::State& state, configuration c, operation operation) {
    auto& f = get_fixture(c);
    constexpr std::size_t batch = 256;
    U checksum = 0;
    auto cursor = f.cursor;
    const auto* indices = f.indices.data();
    const auto query_count = f.indices.size();
    const auto next_index = [&] {
        const auto index = indices[cursor++];
        if (cursor == query_count) cursor = 0;
        return index;
    };
    if (operation == operation::encode) {
        for (auto _ : state) { f.encode(); benchmark::ClobberMemory(); }
    } else if (operation == operation::decode) {
        for (auto _ : state) { f.decode(); benchmark::ClobberMemory(); }
    } else if (operation == operation::point) {
        if (c.point_path) {
            const auto source = f.view->as_const();
            for (auto _ : state) {
                if (cursor + batch > query_count) cursor = 0;
                checksum += f.point_region.sum(source, indices + cursor, batch);
                cursor += batch;
                benchmark::DoNotOptimize(checksum);
            }
        } else {
            f.with_point_reader([&](auto get) {
                U sum = 0;
                for (auto _ : state) {
                    for (std::size_t i = 0; i < batch; ++i) sum += get(next_index());
                    benchmark::DoNotOptimize(sum);
                }
                checksum = sum;
            });
        }
    } else if (operation == operation::get16) {
        alignas(64) std::array<U, 16> result;
        f.with_group_reader([&](auto get16) {
            U sum = 0;
            for (auto _ : state) {
                for (std::size_t i = 0; i < batch; ++i) {
                    get16(next_index() & ~std::size_t{15}, result.data());
                    sum += result[0] + result[15];
                }
                benchmark::DoNotOptimize(sum);
            }
            checksum = sum;
        });
    } else {
        auto walk = f.dependent;
        const auto count = c.count;
        f.with_point_reader([&](auto get) {
            U sum = 0;
            for (auto _ : state) {
                for (std::size_t i = 0; i < batch; ++i) {
                    const auto value = get(walk.index);
                    sum += value;
                    walk.advance(value, seed, count);
                }
                benchmark::DoNotOptimize(sum);
            }
            checksum = sum;
        });
        f.dependent = walk;
    }
    f.cursor = cursor;
    const auto per_iteration = c.random ? batch : c.count;
    state.SetItemsProcessed(state.iterations() * per_iteration);
    state.counters["values_per_item"] = operation == operation::get16 ? 16 : 1;
    state.counters["logical_values"] = c.count;
    state.counters["encoded_bytes"] = f.encoded_bytes;
    state.counters["io_bytes_per_value"] = c.random ? 8 : c.io_bytes;
    state.counters["query_bytes"] = f.indices.size() * sizeof(std::size_t);
    state.counters["bulk_input_output_bytes"] = f.input.size + f.output.size;
    state.counters["checksum"] = static_cast<double>(checksum);
}
std::string name(configuration c, const char* op) {
    return std::string(c.random ? "resident/" : "bulk/") +
        (c.predecessor_region32 ? "predecessor-region32/" : c.predecessor ? "predecessor/" : c.prior ? "calico/" : std::string("series/") + target_name(c.target) + '/') +
        (c.geometry == sp::geometry::local8 ? "local/" : "striped/") +
        "k" + std::to_string(c.width) + "/h" + std::to_string(c.head) +
        "/u" + std::to_string(c.io_bytes * 8) + "/" +
        (c.point_path == 1 ? "static-arithmetic/" : c.point_path == 2 ? "static-offsets/" : "") + op;
}
void register_case(configuration c) {
    if (c.random) {
        if (c.point_path) {
            benchmark::RegisterBenchmark(name(c, "point").c_str(),
                [c](benchmark::State& s) { run(s, c, operation::point); });
            return;
        }
        for (auto [op, label] : {std::pair{operation::point, "point"},
                                std::pair{operation::get16, "get16"},
                                std::pair{operation::dependent, "dependent"}})
            benchmark::RegisterBenchmark(name(c, label).c_str(), [c, op](benchmark::State& s) { run(s, c, op); });
    } else {
        benchmark::RegisterBenchmark(name(c, "encode").c_str(), [c](benchmark::State& s) { run(s, c, operation::encode); });
        benchmark::RegisterBenchmark(name(c, "decode").c_str(), [c](benchmark::State& s) { run(s, c, operation::decode); });
    }
}

void plain_copy(benchmark::State& state, std::size_t count, unsigned bytes) {
    buffer input(count * bytes), output(count * bytes);
    for (std::size_t i = 0; i < count; ++i) store_value(input.data, i, bytes, mix(i + seed));
    for (auto _ : state) {
        std::memcpy(output.data, input.data, input.size);
        benchmark::ClobberMemory();
    }
    require(std::memcmp(input.data, output.data, input.size) == 0, "plain copy");
    state.SetItemsProcessed(state.iterations() * count);
    state.counters["logical_values"] = count;
    state.counters["io_bytes_per_value"] = bytes;
    state.counters["bulk_input_output_bytes"] = input.size + output.size;
}
} // namespace

int main(int argc, char** argv) {
    const auto cpu = pin();
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 2;
    const auto bulk_count = option_size("SERIESPACK_BULK_VALUES", 8192);
    const auto resident_bytes = option_size("SERIESPACK_RESIDENT_BYTES", 128ULL << 20);
    require(bulk_count % 256 == 0 && bulk_count <= (1U << 24), "bulk count must be a bounded multiple of 256");
    require(resident_bytes >= 4096 && resident_bytes <= (1ULL << 30), "resident bytes outside 4KiB..1GiB");
    benchmark::AddCustomContext("affinity_cpu", std::to_string(cpu));
    benchmark::AddCustomContext("bulk_values", std::to_string(bulk_count));
    benchmark::AddCustomContext("requested_encoded_resident_bytes", std::to_string(resident_bytes));
    benchmark::AddCustomContext("resident_access_contract", "resident/*: exact dense arrays; heads separate; random indices carry across batches; resident size does not assert cache residency");
    benchmark::AddCustomContext("dependent_access_contract", "dependent: index0/nonce0 per fixture; mix(index+decoded_value+seed+(++nonce)*0x9e3779b97f4a7c15) modulo count; both states persist across batches and fixture reuse; unsigned nonce wraps; calibrated providers may cover different prefix lengths");
    benchmark::AddCustomContext("bulk_codec_contract", "bulk/*: trusted bound array calls; full cell extent; no effects or validation charged; Calico shape specialized per width");
    benchmark::AddCustomContext("composition_contract", "composition/*: immediate authored/direct or materialize-then-consume; identical predicate and reduction; case selects plan and active mask");
    benchmark::AddCustomContext("casing_contract", "casing/*: case selects placement, extent, source carrier and checked/bound/effects operation; no allocation timed");
    benchmark::AddCustomContext("predecessor_contract", "LocalPack/ScanPack k1..7 u8 and wide56 u64; direct native bodies; same wire; width-valid input; count multiple256; ordinary compiler flags");
    benchmark::AddCustomContext("predecessor_target", seriespack_measurement::predecessor_target());
#ifdef SERIESPACK_HAS_PRIOR
    benchmark::AddCustomContext("calico_target", seriespack_measurement::prior_target());
#endif
    for (unsigned bytes : {1U, 2U, 4U, 8U})
        benchmark::RegisterBenchmark(("bulk/plain/u" + std::to_string(bytes * 8) + "/copy").c_str(),
            [bulk_count, bytes](benchmark::State& state) { plain_copy(state, bulk_count, bytes); });
    std::vector<sp::execution_target> targets;
    auto empty = sp::const_view::assume_valid({1, 0, sp::geometry::local8}, 0, {});
    for (auto t : {sp::execution_target::scalar, sp::execution_target::avx2,
                   sp::execution_target::avx512, sp::execution_target::neon}) {
        if (sp::bind_reader(empty, t)) targets.push_back(t);
    }
    for (unsigned width = 1; width <= 64; ++width) {
        if (width <= 7 && seriespack_measurement::predecessor(width, false).encode) {
            for (auto geometry : {sp::geometry::local8, sp::geometry::striped}) {
                configuration c{width, 0, 1, geometry, sp::execution_target::automatic, true, false, bulk_count};
                c.predecessor = true;
                register_case(c);
                c.random = true; c.io_bytes = 8;
                c.count = std::max<std::size_t>(256, (resident_bytes * 8 / width) & ~std::size_t{255});
                register_case(c);
            }
        }
        if (width == 56) {
            for (bool region : {false, true}) {
                if (!seriespack_measurement::predecessor56(region).encode) continue;
                configuration c{width, 0, 8, sp::geometry::local8, sp::execution_target::automatic, true, false, bulk_count};
                c.predecessor = true; c.predecessor_region32 = region;
                register_case(c);
                if (!region) {
                    c.random = true;
                    c.count = std::max<std::size_t>(256, (resident_bytes * 8 / width) & ~std::size_t{255});
                    register_case(c);
                }
            }
        }
        for (unsigned head : {0U, 8U, 16U}) {
            if (head > width) continue;
            for (auto geometry : {sp::geometry::local8, sp::geometry::striped}) {
                if (!sp::validate({width, head, geometry})) continue;
                for (auto target : targets) {
                    configuration c{width, head, 8, geometry, target, false, false, bulk_count};
                    register_case(c);
                    if (element_size(width) != 8) { c.io_bytes = element_size(width); register_case(c); }
                    if (head == 0 && target != sp::execution_target::scalar) {
                        c.io_bytes = 8; c.random = true;
                        c.count = std::max<std::size_t>(256, (resident_bytes * 8 / width) & ~std::size_t{255});
                        register_case(c);
                        // Static point code is target-independent; bind it
                        // once under the strongest compiled target label.
                        if (target == targets.back()) {
                            c.point_path = 1; register_case(c);
                            c.point_path = 2; register_case(c);
                        }
                    }
                }
            }
        }
#ifdef SERIESPACK_HAS_PRIOR
        for (auto geometry : {sp::geometry::local8, sp::geometry::striped}) {
            configuration c{width, 0, 8, geometry, sp::execution_target::automatic, true, false, bulk_count};
            register_case(c);
            if (width <= 7) { c.io_bytes = 1; register_case(c); }
            c.io_bytes = 8; c.random = true;
            c.count = std::max<std::size_t>(256, (resident_bytes * 8 / width) & ~std::size_t{255});
            register_case(c);
        }
#endif
    }
    seriespack_measurement::register_composition_benchmarks();
    seriespack_measurement::register_casing_benchmarks();
    seriespack_measurement::register_ordinary_runtime_benchmarks();
    seriespack_measurement::register_boundary_probe();
    seriespack_measurement::register_head_placement_benchmarks();
    seriespack_measurement::register_checked_point_benchmarks();
    benchmark::RunSpecifiedBenchmarks();
    resident.reset();
    benchmark::Shutdown();
}
