#include "grain.h"
#include <benchmark/benchmark.h>
#include <ikea/seriespack.h>
#include "../../../benchmarks/seriespack/calico.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace {
namespace sp = ikea::seriespack;
using function = void (*)(const std::uint64_t*, std::uint8_t*, std::size_t);

struct aligned_buffer {
    std::uint8_t* data;
    explicit aligned_buffer(std::size_t bytes) {
        void* p = nullptr;
        if (posix_memalign(&p, 64, bytes)) std::abort();
        data = static_cast<std::uint8_t*>(p);
        std::memset(data, 0, bytes);
    }
    ~aligned_buffer() { std::free(data); }
    aligned_buffer(const aligned_buffer&) = delete;
};

struct buffers {
    std::size_t count;
    aligned_buffer input, output;
    std::vector<std::uint8_t> expected;
    std::optional<sp::bound_encoder> local, head;
    function calico;
    explicit buffers(std::size_t n) : count(n), input(n * 8), output(n), expected(n),
        calico(seriespack_measurement::prior(8, false).encode) {
        auto* values = reinterpret_cast<std::uint64_t*>(input.data);
        std::uint64_t state = 0x839abe484f851d11ULL;
        for (std::size_t i = 0; i < n; ++i) {
            state ^= state >> 12; state ^= state << 25; state ^= state >> 27;
            values[i] = (state * 0x2545f4914f6cdd1dULL) & 255;
            expected[i] = std::uint8_t(values[i]);
        }
        std::span<std::byte> bytes{reinterpret_cast<std::byte*>(output.data), n};
        const auto local_view = sp::mutable_view::attach({8, 0, sp::geometry::local8}, n,
            {{bytes, 8}, {}});
        const auto head_view = sp::mutable_view::attach({8, 8, sp::geometry::local8}, n,
            {{}, {{{bytes, 8}, {}}}});
        if (!local_view || !head_view || !calico) std::abort();
        const auto local_bound = sp::bind_encoder(*local_view, sp::execution_target::neon);
        const auto head_bound = sp::bind_encoder(*head_view, sp::execution_target::neon);
        if (!local_bound || !head_bound) std::abort();
        local = *local_bound; head = *head_bound;
    }
    const std::uint64_t* values() const { return reinterpret_cast<const std::uint64_t*>(input.data); }
    void verify() const {
        if (std::memcmp(output.data, expected.data(), count)) std::abort();
    }
};

buffers& shared(std::size_t count) {
    static buffers small(256), medium(8192), large(65536);
    return count == 256 ? small : count == 8192 ? medium : large;
}

// Each timed invocation makes one opaque array call. Raw native/candidate and
// Calico arms use the same function signature; bound arms call the real bound
// encoder directly so an extra forwarding wrapper is not their measured cost.
template<unsigned Arm>
void measure(benchmark::State& state) {
    const auto count = std::size_t(state.range(0));
    auto& data = shared(count);
    const auto input = sp::input_values{std::span(data.values(), count)};
    if constexpr (Arm <= 1) {
        const auto& encoder = Arm == 0 ? *data.local : *data.head;
        encoder.encode(input); data.verify();
        for (auto _ : state) {
            encoder.encode(input);
            benchmark::DoNotOptimize(data.output.data); benchmark::ClobberMemory();
        }
    } else {
        function f = nullptr;
        if constexpr (Arm == 2) f = byte_grain::native8;
        if constexpr (Arm == 3) f = byte_grain::array<32, false>;
        if constexpr (Arm == 4) f = byte_grain::array<32, true>;
        if constexpr (Arm == 5) f = byte_grain::array<64, false>;
        if constexpr (Arm == 6) f = byte_grain::array<64, true>;
        if constexpr (Arm == 7) f = byte_grain::array<256, false>;
        if constexpr (Arm == 8) f = byte_grain::array<256, true>;
        if constexpr (Arm == 9) f = data.calico;
        benchmark::DoNotOptimize(f);
        f(data.values(), data.output.data, count); data.verify();
        for (auto _ : state) {
            f(data.values(), data.output.data, count);
            benchmark::DoNotOptimize(data.output.data); benchmark::ClobberMemory();
        }
    }
    data.verify();
    state.SetItemsProcessed(state.iterations() * count);
    state.counters["values_per_batch"] = count;
    state.counters["source_bytes"] = count * 8;
    state.counters["encoded_bytes"] = count;
    state.counters["input_page_offset"] = reinterpret_cast<std::uintptr_t>(data.input.data) % 4096;
    state.counters["output_page_offset"] = reinterpret_cast<std::uintptr_t>(data.output.data) % 4096;
}

[[maybe_unused]] const bool registered = [] {
    constexpr std::array names{"bound_local8", "bound_head8", "native8", "gather32_d", "gather32_q",
                               "gather64_d", "gather64_q", "gather256_d", "gather256_q", "calico"};
    sp::detail::static_for<names.size()>([&](auto arm) {
        char name[80]; std::snprintf(name, sizeof(name), "byte_grain/%s", names[arm]);
        benchmark::RegisterBenchmark(name, &measure<arm>)->Arg(256)->Arg(8192)->Arg(65536);
    });
    return true;
}();
}
