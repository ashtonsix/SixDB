#include "kernels.h"
#include <benchmark/benchmark.h>
#include <ikea/seriespack.h>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace {
namespace sp = ikea::seriespack;
using function = void (*)(const std::uint8_t*, std::uint8_t*, std::size_t);

struct aligned_buffer {
    std::uint8_t* p;
    explicit aligned_buffer(std::size_t n) {
        void* allocation = nullptr;
        if (posix_memalign(&allocation, 64, n)) std::abort();
        p = static_cast<std::uint8_t*>(allocation);
        std::memset(p, 0, n);
    }
    ~aligned_buffer() { std::free(p); }
};

template<unsigned W> struct buffers {
    std::size_t count;
    aligned_buffer input, wire, output;
    std::vector<std::uint8_t> expected;
    std::optional<sp::bound_encoder> encoder;
    std::optional<sp::bound_reader> reader;
    explicit buffers(std::size_t n) : count(n), input(n), wire(n * W / 8), output(n), expected(n * W / 8) {
        std::uint64_t state = 0x5b097c480c23aed6ULL;
        for (std::size_t i = 0; i < n; ++i) {
            state ^= state >> 12; state ^= state << 25; state ^= state >> 27;
            input.p[i] = (state * 0x2545f4914f6cdd1dULL) & ((1u << W) - 1);
        }
        scan_grain::oracle<W>(input.p, expected.data(), n);
        std::memcpy(wire.p, expected.data(), expected.size());
        std::span<std::byte> payload{reinterpret_cast<std::byte*>(wire.p), expected.size()};
        auto view = sp::mutable_view::attach({W, 0, sp::geometry::striped}, n,
            {{payload, scan_grain::tile_bytes<W>}, {}});
        if (!view) std::abort();
        auto e = sp::bind_encoder(*view, sp::execution_target::neon);
        auto r = sp::bind_reader(view->as_const(), sp::execution_target::neon);
        if (!e || !r) std::abort();
        encoder = *e; reader = *r;
    }
    template<bool Encode> void verify() const {
        if constexpr (Encode) {
            if (std::memcmp(wire.p, expected.data(), expected.size())) std::abort();
        } else if (std::memcmp(output.p, input.p, count)) std::abort();
    }
};

template<unsigned W> buffers<W>& shared(std::size_t n) {
    static buffers<W> small(256), medium(8192), large(65536);
    return n == 256 ? small : n == 8192 ? medium : large;
}

template<unsigned W, unsigned Arm, bool Encode>
void measure(benchmark::State& state) {
    const auto n = std::size_t(state.range(0));
    auto& data = shared<W>(n);
    auto* input = Encode ? data.input.p : data.wire.p;
    auto* output = Encode ? data.wire.p : data.output.p;
    if constexpr (Arm == 0) {
        const auto source = sp::input_values{std::span(data.input.p, n)};
        const auto destination = sp::output_values{std::span(data.output.p, n)};
        const auto run = [&] {
            if constexpr (Encode) data.encoder->encode(source);
            else data.reader->decode({0, n}, destination);
        };
        run(); data.template verify<Encode>();
        for (auto _ : state) {
            run(); benchmark::DoNotOptimize(output); benchmark::ClobberMemory();
        }
    } else {
        function f;
        if constexpr (Arm == 1) f = scan_grain::native_region<W, 0, Encode>;
        if constexpr (Arm == 2) f = scan_grain::native_region<W, 128, Encode>;
        if constexpr (Arm == 3) f = scan_grain::native_region<W, 256, Encode>;
        if constexpr (Arm == 4) f = scan_grain::native_region<W, 512, Encode>;
        if constexpr (Arm == 5) f = scan_grain::predecessor<W, scan_grain::tile_values<W>, Encode>;
        if constexpr (Arm == 6) f = scan_grain::predecessor<W, 128, Encode>;
        if constexpr (Arm == 7) f = scan_grain::predecessor<W, 256, Encode>;
        benchmark::DoNotOptimize(f);
        f(input, output, n); data.template verify<Encode>();
        for (auto _ : state) {
            f(input, output, n); benchmark::DoNotOptimize(output); benchmark::ClobberMemory();
        }
    }
    data.template verify<Encode>();
    state.SetItemsProcessed(state.iterations() * n);
    state.counters["values_per_batch"] = n;
    state.counters["source_bytes"] = n;
    state.counters["encoded_bytes"] = n * W / 8;
    state.counters["input_page_offset"] = reinterpret_cast<std::uintptr_t>(input) % 4096;
    state.counters["output_page_offset"] = reinterpret_cast<std::uintptr_t>(output) % 4096;
}

template<unsigned W> void add() {
    constexpr std::array names{"bound", "native", "region128", "region256", "region512",
                               "predecessor_tile", "predecessor128", "predecessor256"};
    sp::detail::static_for<names.size()>([&](auto arm) {
        for (bool encode : {false, true}) {
            char name[100];
            std::snprintf(name, sizeof(name), "scan_grain/k%u/%s/%s", W, names[arm], encode ? "encode" : "decode");
            if (encode) benchmark::RegisterBenchmark(name, &measure<W, arm, true>)->Arg(256)->Arg(8192)->Arg(65536);
            else benchmark::RegisterBenchmark(name, &measure<W, arm, false>)->Arg(256)->Arg(8192)->Arg(65536);
        }
    });
}
[[maybe_unused]] const bool registered = [] { add<4>(); add<6>(); return true; }();
} // namespace
