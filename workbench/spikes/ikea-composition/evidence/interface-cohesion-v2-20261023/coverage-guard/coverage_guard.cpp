#include <ikea/seriespack/writer.h>
#include <array>
#include <cassert>
#include <cstddef>
#include <initializer_list>

namespace sp = ikea::seriespack;
namespace cp = sp::author;

// A bounded hook preserves every issued event, without journal coalescing.
// Expected geometry below is independent of the production footprint visitor.
struct observations {
    std::array<ikea::journal::entry, 8> entries{};
    unsigned used = 0;
    template <class Source>
    void before(const Source& source, unsigned plane, std::size_t offset,
                std::size_t size) noexcept {
        assert(used < entries.size());
        entries[used++] = {&source, plane, offset, size};
    }
    void expect(std::initializer_list<ikea::journal::entry> wanted) {
        assert(used == wanted.size());
        unsigned i = 0;
        for (const auto& e : wanted) {
            const auto& a = entries[i++];
            assert(a.source == e.source && a.plane == e.plane &&
                   a.offset == e.offset && a.size == e.size);
        }
        used = 0;
    }
};

int main() {
    constexpr std::size_t count = 256;
    using Tail = sp::layout<7, sp::geometry::striped>;
    using Other = sp::layout<3, sp::geometry::striped>;
    using Original = sp::layout<31, sp::geometry::local, 8>;
    alignas(64) std::array<std::byte, count * 7 / 8> tail_bytes{};
    alignas(64) std::array<std::byte, count * 7 / 8> second_bytes{};
    alignas(64) std::array<std::byte, count * 3 / 8> other_bytes{};
    alignas(64) std::array<std::byte, count * 23 / 8> payload{};
    alignas(64) std::array<std::byte, count> head{};
    auto target = *sp::view<Tail>::attach(count, {{{tail_bytes, Tail::tile_bytes},{},{}}});
    auto second = *sp::view<Tail>::attach(count, {{{second_bytes, Tail::tile_bytes},{},{}}});
    auto alias = target; // Same placement, different named source identity.
    auto other = *sp::view<Other>::attach(count, {{{other_bytes, Other::tile_bytes},{},{}}});
    auto original = *sp::view<Original>::attach(count,
        {{{payload, Original::tile_bytes},{head, Original::tile_rows},{}}});
    observations effects;
    static_assert(ikea::effects<observations, sp::view<Tail>>);
    cp::range_detail::other_coverage<observations, sp::view<Tail>> coverage{effects, target};

    const auto target_leaf = cp::expression(target).payload.tail;
    coverage.write(target_leaf, 0, cp::write_token{});
    coverage.write(target_leaf, 32, cp::write_token{});
    effects.expect({}); // Already covered by the enclosing whole-tail batch.

    coverage.write(cp::expression(second).payload.tail, 0, cp::write_token{});
    effects.expect({{&second, 0, 0, 16}});
    coverage.write(cp::expression(second).payload.tail, 32, cp::write_token{});
    effects.expect({{&second, 0, 0, 16}, {&second, 0, 32, 16}});

    coverage.write(cp::expression(alias).payload.tail, 0, cp::write_token{});
    effects.expect({{&alias, 0, 0, 16}}); // Do not widen the skip to address equality.
    coverage.write(cp::expression(other).payload.tail, 0, cp::write_token{});
    effects.expect({{&other, 0, 0, 16}}); // Different source type retains its path.

    const auto fields = cp::expression(original);
    coverage.write(fields.payload.body, 16, cp::write_token{});
    effects.expect({{&original, 0, 46, 16}, {&original, 0, 69, 16}});
    coverage.write(fields.head0, 16, cp::write_token{});
    effects.expect({{&original, 1, 16, 8}, {&original, 1, 24, 8}});
}
