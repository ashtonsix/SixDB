#include <ikea/seriespack.h>

#if defined(__aarch64__)
#include <ikea/seriespack/composition_neon.h>
#elif defined(__AVX2__)
#include <ikea/seriespack/composition_x86.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

namespace pack = ikea::seriespack;

#if defined(__aarch64__)
namespace native = pack::neon;
constexpr const char* native_name = "NEON";
#elif defined(__AVX512BW__) && defined(__AVX512VBMI__)
namespace native = pack::avx512;
constexpr const char* native_name = "AVX-512";
#elif defined(__AVX2__)
namespace native = pack::avx2;
constexpr const char* native_name = "AVX2";
#endif

int main() {
    using Format = pack::format<12>;
    const std::array<std::uint16_t, 10> values{7, 19, 42, 0, 4095, 3, 8, 91, 12, 6};
    // Ten logical values occupy two complete eight-value tiles.
    alignas(64) std::array<std::byte, 2 * Format::payload::tile_bytes> bytes{};
    auto attached = pack::static_mutable_view<Format>::attach(
        values.size(), {{bytes, Format::payload::tile_bytes}, {}});
    if (!attached) return 1;
    auto writable = attached->as_dynamic();
    auto readable = writable.as_const();
    if (!pack::encode(writable, std::span{values})) return 2;
    auto value = pack::get(readable, 2);
    if (!value || *value != 42) return 3;

    std::array<std::uint16_t, 10> decoded{};
    if (!pack::decode(readable, {0, values.size()}, std::span{decoded})) return 4;
    std::uint64_t scalar_sum = 0;
    for (auto x : decoded)
        if (x < 100) scalar_sum += x;
    if (scalar_sum != 188) return 5;

#if defined(__aarch64__) || defined(__AVX2__)
    namespace comp = pack::composition;
    // parts borrows this view object; both it and bytes remain alive.
    auto source = attached->as_const();
    auto parts = comp::describe(source);
    using Ops = native::composition_ops<Format, 0>;
    static_assert(Format::payload::tile_values == 8 && Ops::lanes == 8 && Ops::L == 2);
    Ops ops;

    auto first = comp::selected_sum(ops, parts, native::tile_position{0},
                                   Ops::active_all(), std::uint64_t{100});
    // Tile 1 is readable in full, but only lanes 0 and 1 are logical values.
    auto last = comp::selected_sum(ops, parts, native::tile_position{1},
                                  Ops::active_bits(0b11), std::uint64_t{100});
    auto native_sum = first + last;
    if (first != 170 || last != 18 || native_sum != scalar_sum) return 6;
    std::printf("SeriesPack query: scalar %llu; %s %llu (170 + 18)\n",
        static_cast<unsigned long long>(scalar_sum), native_name,
        static_cast<unsigned long long>(native_sum));
#else
    std::printf("SeriesPack query: scalar %llu; native portion skipped "
                "(no supported native family enabled at compile time)\n",
        static_cast<unsigned long long>(scalar_sum));
#endif
}
