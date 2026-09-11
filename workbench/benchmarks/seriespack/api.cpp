// Container-call costs over representative boundaries. The shared benchmark
// main owns affinity, sequential repetitions and JSON output.
#include <ikea/seriespack.h>
#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace seriespack_measurement {
namespace {
namespace sp = ikea::seriespack;
using U = std::uint64_t;
enum class operation { attach, bind_reader, bind_encoder, decode_checked,
    decode_bound, encode_checked, encode_bound, selected_write };
struct configuration {
    sp::description layout;
    std::size_t count, gap;
    sp::execution_target target = sp::execution_target::scalar;
    bool byte_input = false;
};

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
U mix(U x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
struct stream {
    std::vector<std::byte> owner;
    std::size_t offset = 0, stride, tile_bytes, tiles;
    stream(std::size_t count, std::size_t bytes, std::size_t gap)
        : owner(bytes ? (count - 1) * (bytes + gap) + bytes + 128 : 0, std::byte{0xcd}),
          stride(bytes + gap), tile_bytes(bytes), tiles(bytes ? count : 0) {
        if (bytes) offset = 64 + ((-reinterpret_cast<std::uintptr_t>(owner.data())) & 63U);
    }
    sp::basic_plane<std::byte> plane() {
        const auto size = tiles ? (tiles - 1) * stride + tile_bytes : 0;
        return {{tiles ? owner.data() + offset : nullptr, size}, stride};
    }
    bool occupied(std::size_t i) const {
        if (i < offset || !tiles) return false;
        const auto relative = i - offset;
        return relative / stride < tiles && relative % stride < tile_bytes;
    }
};
struct fixture {
    configuration c;
    std::size_t T, B, tiles;
    std::array<stream, 3> streams;
    sp::mutable_view view;
    sp::bound_reader reader;
    sp::bound_encoder encoder;
    sp::index_range rows;
    std::vector<U> original, input, output, masks;
    std::vector<std::uint8_t> byte_input;
    sp::input_values source, selected_source;
    std::vector<sp::byte_span> records;
    sp::selection selected;
    sp::effect_output effects;

    static sp::mutable_view attach(configuration c, std::array<stream, 3>& streams) {
        auto result = sp::mutable_view::attach(c.layout, c.count,
            {streams[0].plane(), {streams[1].plane(), streams[2].plane()}});
        require(result.has_value(), "casing fixture attachment");
        return *result;
    }
    static sp::bound_reader bind_reader(sp::const_view view, sp::execution_target target) {
        auto result = sp::bind_reader(view, target);
        require(result.has_value(), "casing fixture reader binding");
        return *result;
    }
    static sp::bound_encoder bind_encoder(sp::mutable_view view, sp::execution_target target) {
        auto result = sp::bind_encoder(view, target);
        require(result.has_value(), "casing fixture encoder binding");
        return *result;
    }
    explicit fixture(configuration conf, bool reporting)
        : c(conf), T(sp::tile_values(c.layout)), B(sp::tile_bytes(c.layout)),
          tiles((c.count + T - 1) / T),
          streams{stream(tiles, B, c.gap),
                  stream(tiles, c.layout.head_bits >= 8 ? T : 0, c.gap),
                  stream(tiles, c.layout.head_bits == 16 ? T : 0, c.gap)},
          view(attach(c, streams)), reader(bind_reader(view.as_const(), c.target)),
          encoder(bind_encoder(view, c.target)), rows{3, c.count - 2},
          original(c.count), input(c.count), output(rows.size()),
          masks((rows.size() + 63) / 64, 0x1111111111111111ULL),
          byte_input(c.byte_input ? c.count : 0),
          source(c.byte_input ? sp::input_values{std::span(byte_input)} : sp::input_values{std::span(input)}),
          selected_source(c.byte_input ?
              sp::input_values{std::span(byte_input).subspan(rows.begin, rows.size())} :
              sp::input_values{std::span(input).subspan(rows.begin, rows.size())}),
          selected(sp::selection::bitmap(rows.begin, rows.size(), masks)), effects{} {
        const U maximum = ~U{0} >> (64 - c.layout.width);
        for (std::size_t i = 0; i != c.count; ++i) {
            original[i] = mix(0x413ae7c9b0501ef3ULL + i) & maximum;
            input[i] = original[i] ^ maximum;
            if (c.byte_input) byte_input[i] = static_cast<std::uint8_t>(input[i] &= 255);
        }
        require(sp::encode(view, std::span(original), nullptr, sp::execution_target::scalar).has_value(),
                "casing fixture construction");
        if (reporting) {
            auto full = sp::encode_effect_capacity(view.as_const());
            auto sparse = sp::write_effect_capacity(view.as_const(), rows, selected);
            require(full.has_value() && sparse.has_value(), "casing effect preparation");
            records.resize(std::max(*full, *sparse));
        }
        effects = {records};
    }
};

template<operation Op, bool Report, bool Check = false>
void invoke(fixture& f) {
    auto* effects = Report ? &f.effects : nullptr;
    if constexpr (Report) f.effects.size = 0; // Reuse caller-prepared output.
    if constexpr (Op == operation::attach) {
        auto value = sp::mutable_view::attach(f.c.layout, f.c.count, f.view.placement());
        if constexpr (Check) {
            require(value.has_value(), "casing attach rejected");
            require(value->layout() == f.c.layout && value->size() == f.c.count &&
                    value->placement().payload.bytes.data() == f.view.placement().payload.bytes.data(),
                    "casing attachment changed source");
        }
        benchmark::DoNotOptimize(value);
    } else if constexpr (Op == operation::bind_reader) {
        auto value = sp::bind_reader(f.view.as_const(), f.c.target);
        if constexpr (Check) {
            require(value.has_value(), "casing reader binding rejected");
            require(value->target() == f.c.target && value->source().layout() == f.c.layout &&
                    value->source().size() == f.c.count, "casing reader binding changed source/target");
            value->decode(f.rows, std::span(f.output));
            require(std::equal(f.output.begin(), f.output.end(), f.original.begin() + f.rows.begin),
                    "casing reader binding result");
        }
        benchmark::DoNotOptimize(value);
    } else if constexpr (Op == operation::bind_encoder) {
        auto value = sp::bind_encoder(f.view, f.c.target);
        if constexpr (Check) {
            require(value.has_value(), "casing encoder binding rejected");
            require(value->target() == f.c.target && value->destination().layout() == f.c.layout &&
                    value->destination().size() == f.c.count &&
                    value->destination().placement().payload.bytes.data() == f.view.placement().payload.bytes.data(),
                    "casing encoder binding changed source/target");
            value->encode(std::span(f.original));
        }
        benchmark::DoNotOptimize(value);
    } else if constexpr (Op == operation::decode_checked) {
        auto value = sp::decode(f.view.as_const(), f.rows, std::span(f.output), f.c.target);
        if constexpr (Check) require(value.has_value(), "casing decode rejected");
        benchmark::DoNotOptimize(value);
    } else if constexpr (Op == operation::decode_bound) {
        f.reader.decode(f.rows, std::span(f.output));
    } else if constexpr (Op == operation::encode_checked) {
        auto value = sp::encode(f.view, f.source, effects, f.c.target);
        if constexpr (Check) require(value.has_value(), "casing encode rejected");
        benchmark::DoNotOptimize(value);
    } else if constexpr (Op == operation::encode_bound) {
        f.encoder.encode(f.source, effects);
    } else {
        auto value = sp::write(f.view, f.rows, f.selected_source, f.selected, effects);
        if constexpr (Check) require(value.has_value(), "casing selected write rejected");
        benchmark::DoNotOptimize(value);
    }
}

template<operation Op, bool Report>
void verify(fixture& f, const std::array<std::vector<std::byte>, 3>& before) {
    constexpr bool encoding = Op == operation::encode_checked || Op == operation::encode_bound;
    constexpr bool writing = Op == operation::selected_write;
    for (std::size_t i = 0; i != f.c.count; ++i) {
        bool changed = encoding;
        if constexpr (writing)
            changed = i >= f.rows.begin && i < f.rows.end && f.selected.contains(i);
        auto value = sp::get(f.view.as_const(), i);
        require(value && *value == (changed ? f.input[i] : f.original[i]), "casing value oracle");
    }
    if constexpr (Op == operation::decode_checked || Op == operation::decode_bound)
        require(std::equal(f.output.begin(), f.output.end(), f.original.begin() + f.rows.begin),
                "casing materialization oracle");
    std::array<std::vector<bool>, 3> covered;
    for (unsigned s = 0; s != 3; ++s) covered[s].resize(f.streams[s].owner.size());
    if constexpr (Report) {
        require(f.effects.size <= f.records.size(), "casing effect capacity");
        for (const auto record : std::span(f.records).first(f.effects.size)) {
            bool found = false;
            const auto address = reinterpret_cast<std::uintptr_t>(record.data);
            for (unsigned s = 0; s != 3; ++s) {
                const auto base = reinterpret_cast<std::uintptr_t>(f.streams[s].owner.data());
                const auto bytes = f.streams[s].owner.size();
                if (address < base || address - base >= bytes) continue;
                const auto begin = address - base;
                require(record.size && record.size <= bytes - begin, "casing effect bounds");
                for (std::size_t j = begin; j != begin + record.size; ++j) {
                    require(f.streams[s].occupied(j), "casing effect crosses foreign bytes");
                    covered[s][j] = true;
                }
                found = true;
            }
            require(found, "casing effect belongs to storage");
        }
    }
    for (unsigned s = 0; s != 3; ++s)
        for (std::size_t j = 0; j != before[s].size(); ++j) {
            const bool different = before[s][j] != f.streams[s].owner[j];
            if (!f.streams[s].occupied(j)) require(!different, "casing foreign bytes changed");
            if constexpr (Report) {
                if (different || (encoding && f.streams[s].occupied(j)))
                    require(covered[s][j], "casing write missing from effects");
            }
        }
}

template<operation Op, bool Report = false>
void run(benchmark::State& state, configuration c) {
    try {
        fixture f(c, Report);
        const std::array before{f.streams[0].owner, f.streams[1].owner, f.streams[2].owner};
        invoke<Op, Report, true>(f);
        verify<Op, Report>(f, before);
        for (auto _ : state) {
            benchmark::ClobberMemory();
            invoke<Op, Report>(f);
            benchmark::ClobberMemory();
        }
        verify<Op, Report>(f, before);
        // Items are calls, including admission-only cases; values are explicit.
        state.SetItemsProcessed(state.iterations());
        state.counters["logical_values"] = c.count;
        state.counters["range_values"] = f.rows.size();
        state.counters["selected_values"] = (f.rows.size() + 3) / 4;
        state.counters["payload_stride"] = f.streams[0].stride;
        state.counters["occupied_bytes"] = f.tiles * (f.B + f.T * c.layout.head_bits / 8);
        std::size_t envelope = 0;
        for (auto& s : f.streams) envelope += s.plane().bytes.size();
        state.counters["address_envelope_bytes"] = envelope;
        state.counters["effect_records"] = f.effects.size;
        state.counters["effect_capacity_bytes"] = f.records.size() * sizeof(sp::byte_span);
        state.counters["input_element_bytes"] = c.byte_input ? 1 : 8;
        state.SetLabel("calls; u64 output; resident repeated overwrite; allocation and effect preparation excluded");
    } catch (const std::exception& error) { state.SkipWithError(error.what()); }
}

template<operation Op, bool Report = false>
void add(const std::string& prefix, const char* name, configuration c) {
    benchmark::RegisterBenchmark((prefix + name + (Report ? "/effects" : "")).c_str(),
                                 &run<Op, Report>, c);
}
} // namespace

void register_casing_benchmarks() {
    const std::array configurations{
        configuration{{12, 0, sp::geometry::local8}, 17, 0},
        configuration{{12, 0, sp::geometry::local8}, 8193, 0},
        configuration{{28, 8, sp::geometry::striped}, 257, 0},
        configuration{{28, 8, sp::geometry::striped}, 257, 32},
        configuration{{60, 8, sp::geometry::local8}, 8193, 32},
        configuration{{64, 0, sp::geometry::local8}, 8192, 0},
        configuration{{9, 8, sp::geometry::local8}, 17, 0, sp::execution_target::scalar, true},
        configuration{{28, 8, sp::geometry::striped}, 257, 32, sp::execution_target::scalar, true}};
    const std::array targets{sp::execution_target::scalar,
#if defined(__aarch64__)
        sp::execution_target::neon,
#endif
#if defined(__AVX2__)
        sp::execution_target::avx2,
#endif
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        sp::execution_target::avx512,
#endif
    };
    for (auto c : configurations) for (auto target : targets) {
        c.target = target;
        const char* target_name = target == sp::execution_target::scalar ? "scalar" :
            target == sp::execution_target::neon ? "neon" :
            target == sp::execution_target::avx2 ? "avx2" : "avx512";
        const auto prefix = std::string("casing/") + target_name +
            (c.layout.storage == sp::geometry::local8 ? "/local/k" : "/striped/k") +
            std::to_string(c.layout.width) + "/h" + std::to_string(c.layout.head_bits) +
            "/n" + std::to_string(c.count) + "/gap" + std::to_string(c.gap) +
            (c.byte_input ? "/in8/" : "/in64/");
        if (!c.byte_input) {
            if (target == sp::execution_target::scalar) add<operation::attach>(prefix, "attach", c);
            add<operation::bind_reader>(prefix, "bind-reader", c);
            add<operation::bind_encoder>(prefix, "bind-encoder", c);
            add<operation::decode_checked>(prefix, "decode-checked", c);
            add<operation::decode_bound>(prefix, "decode-bound", c);
        }
        add<operation::encode_checked>(prefix, "encode-checked", c);
        add<operation::encode_bound>(prefix, "encode-bound", c);
        add<operation::encode_checked, true>(prefix, "encode-checked", c);
        add<operation::encode_bound, true>(prefix, "encode-bound", c);
        // Selected mutation currently has one physical implementation, so do
        // not pretend target names identify different sparse kernels.
        if (target == sp::execution_target::scalar) {
            add<operation::selected_write>(prefix, "write25-checked", c);
            add<operation::selected_write, true>(prefix, "write25-checked", c);
        }
    }
}
} // namespace seriespack_measurement
