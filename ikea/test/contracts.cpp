// Cross-module binding contracts: a plan or typed access only binds to bytes
// attached with an equal law, and effect records keep the plane origin they
// were issued against. Expected values come from the layouts and buffers here,
// not from the implementation under test.
#include <ikea/bec.h>
#include <ikea/seriespack.h>
#include <ikea/streampack.h>
#include <ikea/tuplepack.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

namespace {
void need(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "contracts: %s\n", what);
        std::abort();
    }
}

void tuplepack_identity() {
    namespace tp = ikea::tuplepack;
    // Equal unit extents, different laws: low nibble versus high nibble.
    const std::array<tp::code, 1> low{{{0, 0, 4}}}, high{{{0, 4, 4}}};
    const auto a = *tp::layout::make(1, low), b = *tp::layout::make(1, high);
    need(a.bytes() == b.bytes() && a.identity() != b.identity(), "distinct laws, equal extents");
    std::array<std::byte, 8> bytes{};
    for (unsigned i = 0; i < bytes.size(); ++i)
        bytes[i] = std::byte(0x30 + i); // high nibble 3, low nibble i
    const auto on_a = *tp::view::attach(a, bytes, bytes.size(), 1);
    const auto on_b = *tp::view::attach(b, bytes, bytes.size(), 1);
    const std::array<std::uint8_t, 1> map{0};
    const auto plan = *tp::read_plan<8>::make(a, map);
    need(tp::bind_reader(plan, on_a).has_value(), "plan binds to its own law");
    const auto refused = tp::bind_reader(plan, on_b);
    need(!refused && refused.error() == tp::error::description, "plan refuses another law");
    const auto writer_plan = *tp::write_plan<8>::make(a, map);
    auto mutable_bytes = bytes;
    const auto destination = *tp::view::attach(b, mutable_bytes, bytes.size(), 1);
    need(!tp::bind_writer(writer_plan, destination), "writer refuses another law");
    need(!tp::bind_predicate(a, 0, on_b), "predicate refuses another law");
    need(tp::bind_predicate(b, 0, on_b).has_value(), "predicate binds its own law");

    // A layout decoded separately (one schema shared by many blocks) is the
    // same law: its plans bind to views attached with the original.
    std::array<std::byte, 16> encoded{};
    const auto size = *a.encode(encoded);
    const auto decoded = *tp::layout::decode(std::span(encoded).first(size));
    need(decoded.identity() == a.identity(), "decoded identity");
    const auto shared = *tp::read_plan<8>::make(decoded, map);
    const auto read = *tp::bind_reader(shared, on_a);
    need(*read.get(5) == 5, "shared plan reads the low nibble");
}

void streampack_rebind() {
    namespace sk = ikea::streampack;
    const auto first = *sk::layout::make({.maximum = 255, .byte_limit = 4096});
    const auto equal = *sk::layout::make({.maximum = 255, .byte_limit = 4096});
    const auto other = *sk::layout::make({.family = sk::geometry::absolute_ends, .maximum = 255,
                                          .byte_limit = 4096});
    need(first == equal && !(first == other), "layout equality follows options");
    struct block {
        std::array<std::byte, 4096> payload{};
        std::array<std::byte, 1024> directory{};
        alignas(64) std::array<std::byte, 512> lengths{};
        std::array<std::byte, 8> control{};
    };
    auto storage = std::make_unique<std::array<block, 3>>();
    auto attach = [&](const sk::layout& law, block& b) {
        auto v = sk::view::attach(law, {b.payload, std::span(b.directory).first(law.directory_bytes()),
                                        std::span(b.lengths).first(law.lengths_bytes()),
                                        std::span(b.control).first(law.control_bytes())});
        need(v.has_value(), "streampack attach");
        return *v;
    };
    auto a = attach(first, (*storage)[0]), b = attach(equal, (*storage)[1]), c = attach(other, (*storage)[2]);
    std::array<ikea::journal::entry, 32> records;
    ikea::journal effects{records};
    const std::array value{std::byte{'o'}, std::byte{'k'}};
    for (auto* v : {&a, &b, &c}) {
        effects.used = 0;
        need(sk::initialize(*v, effects).has_value(), "initialize");
        effects.used = 0;
        need(sk::append(*v, value, 2, effects).has_value(), "append");
    }
    sk::author::visit(a, [&](auto access) {
        const auto same = access.rebind(b);
        need(same.has_value() && same->value(0).size() == 2, "rebind to an equal layout");
        const auto different = access.rebind(c);
        need(!different && different.error() == sk::error::description, "rebind refuses another law");
    });
}

// Every effect source resolves its records through plane_storage: the issued
// span lies inside that plane and covers the bytes the command changed.
void journal_resolution() {
    namespace sp = ikea::seriespack;
    using L = sp::layout<12>;
    constexpr std::size_t count = 64;
    alignas(64) std::array<ikea::byte, count / L::tile_rows * L::tile_bytes> bytes{};
    const auto destination = *sp::view<L>::attach(count, {{{bytes, L::tile_bytes}, {}, {}}});
    const auto writer = *sp::bind_writer(destination);
    std::array<ikea::journal::entry, 8> records;
    ikea::journal effects{records};
    const auto before = bytes;
    need(writer.set(std::size_t{9}, std::uint64_t{0xabc}, effects).has_value() && effects.used >= 1, "set");
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == before[i])
            continue;
        bool covered = false;
        for (const auto& e : effects.entries()) {
            const auto plane = destination.plane_storage(e.plane);
            need(e.source == &destination && e.offset + e.size <= plane.size(), "record inside its plane");
            covered |= &bytes[i] >= plane.data() + e.offset && &bytes[i] < plane.data() + e.offset + e.size;
        }
        need(covered, "changed byte covered by a resolved record");
    }

    std::array<ikea::byte, 64> body{};
    ikea::bec256::view target{body};
    std::array<ikea::byte, 32> plain_bits{};
    plain_bits[3] = ikea::byte{0x81};
    ikea::journal bec{records};
    need(ikea::bec256::encode(plain_bits, 2, target, 5, bec).has_value() && bec.used == 1 &&
             bec.entries()[0].source == &target &&
             target.plane_storage(0).data() + bec.entries()[0].offset == body.data() + 5,
         "bec256 resolution");

    namespace tp = ikea::tuplepack;
    const std::array<tp::code, 1> codes{{{0, 0, 4}}};
    const auto law = *tp::layout::make(2, codes);
    std::array<std::byte, 32> units{};
    const auto v = *tp::view::attach(law, units, 8, 4, 1);
    const std::array<std::uint8_t, 1> map{0};
    const auto plan = *tp::write_plan<8>::make(law, map);
    const auto w = *tp::bind_writer(plan, v);
    ikea::journal tuple{records};
    need(w.set(3, std::uint64_t{9}, tuple).has_value() && tuple.used == 1, "tuple set");
    const auto& e = tuple.entries()[0];
    need(v.plane_storage(e.plane).data() + e.offset == units.data() + 1 + 3 * 4 &&
             std::to_integer<unsigned>(units[1 + 3 * 4]) == 9,
         "tuplepack resolution");
}

void selection_regions() {
    namespace sp = ikea::seriespack;
    std::array<std::uint16_t, 4> words{0x0001, 0x8000, 0xa5a5, 0xffff};
    const auto bits = sp::selection::bits(32, words);
    const auto regions = bits.region_words();
    for (unsigned i = 0; i < words.size(); ++i)
        need(regions(32 + 16 * i) == words[i], "explicit region word");
    ikea::bitset256 block;
    block.set(17);
    block.set(255);
    const auto blocked = sp::selection::bits(256, block);
    const auto block_regions = blocked.region_words();
    need(block_regions(256 + 16) == 0x0002 && block_regions(256 + 240) == 0x8000 &&
             block_regions(256) == 0,
         "bitset256 region word");
    need(sp::selection::all().region_words()(48) == 0xffff &&
             sp::selection::none().region_words()(48) == 0,
         "all and none regions");
}
// Every byte that can change a point read lies inside the spans visit_point_reads
// emits for that row. Found by perturbation, independently of the visitor.
template <class L> void seriespack_point_footprint() {
    namespace sp = ikea::seriespack;
    constexpr std::size_t count = 2 * L::tile_rows;
    constexpr auto heads = L::heads / 8;
    std::vector<ikea::byte> payload(count / L::tile_rows * L::tile_bytes), head0(count), head1(count);
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = ikea::byte((i * 37 + 11) & 255);
    std::array<sp::plane<ikea::byte>, 3> planes{{{payload, L::tile_bytes}, {}, {}}};
    if constexpr (heads >= 1)
        planes[1] = {head0, L::tile_rows};
    if constexpr (heads == 2)
        planes[2] = {head1, L::tile_rows};
    const auto v = *sp::view<L>::attach(count, planes);
    std::array<std::vector<ikea::byte>*, 3> stores{&payload, &head0, &head1};
    for (std::size_t row : {std::size_t{0}, std::size_t{5}, L::tile_rows - 1, L::tile_rows + 3, count - 1}) {
        std::vector<std::pair<const ikea::byte*, std::size_t>> spans;
        sp::visit_point_reads(v, row, [&](const ikea::byte* p, std::size_t n) { spans.push_back({p, n}); });
        auto inside = [&](const ikea::byte* p) {
            for (auto [s, n] : spans)
                if (p >= s && p < s + n)
                    return true;
            return false;
        };
        const auto value = sp::get_unchecked(v, row);
        for (unsigned plane = 0; plane <= heads; ++plane)
            for (auto& b : *stores[plane]) {
                for (unsigned bit = 0; bit < 8; ++bit) {
                    b ^= ikea::byte(1u << bit);
                    const bool changed = sp::get_unchecked(v, row) != value;
                    b ^= ikea::byte(1u << bit);
                    need(!changed || inside(&b), "point footprint covers every read byte");
                }
            }
    }
}

void tuplepack_read_footprint() {
    namespace tp = ikea::tuplepack;
    const std::array<tp::code, 3> codes{{{1, 0, 3}, {3, 2, 5}, {6, 1, 7}}};
    const auto law = *tp::layout::make(8, codes);
    std::array<std::byte, 16 * 12> bytes{};
    for (unsigned i = 0; i < bytes.size(); ++i)
        bytes[i] = std::byte((i * 29 + 3) & 255);
    const auto v = *tp::view::attach(law, bytes, 16, 12, 4);
    const std::array<std::uint8_t, 2> map{2, 0};
    const auto plan = *tp::read_plan<64, 16>::make(law, map);
    const auto read = *tp::bind_reader(plan, v);
    const std::uint64_t active = 0xa5c3;
    std::vector<std::pair<const std::byte*, std::size_t>> spans;
    read.visit_reads(0, active, [&](const std::byte* p, std::size_t n) { spans.push_back({p, n}); });
    need(spans.size() == unsigned(std::popcount(active)), "one span per active row");
    const auto value = read.get_unchecked(0, active);
    for (auto& b : bytes)
        for (unsigned bit = 0; bit < 8; ++bit) {
            b ^= std::byte(1u << bit);
            const bool changed = read.get_unchecked(0, active) != value;
            b ^= std::byte(1u << bit);
            bool inside = false;
            for (auto [s, n] : spans)
                inside |= &b >= s && &b < s + n;
            need(!changed || inside, "packet footprint covers every read byte");
        }
}
// Runtime-width construction writes the same bytes as the typed writer and
// names the caller's dense_output as the effect source.
template <unsigned K, ikea::seriespack::geometry G> void seriespack_runtime_initialize() {
    namespace sp = ikea::seriespack;
    using L = sp::layout<K, G>;
    constexpr std::size_t count = 3 * L::tile_rows + 5;
    constexpr std::size_t bytes = (count + L::tile_rows - 1) / L::tile_rows * L::tile_bytes;
    std::vector<std::uint64_t> values(count);
    for (std::size_t i = 0; i < count; ++i)
        values[i] = (i * 0x9e3779b97f4a7c15ull) >> (64 - K);
    std::vector<ikea::byte> typed(bytes, ikea::byte{0x5a}), runtime(bytes, ikea::byte{0xa5});
    const auto v = *sp::view<L>::attach(count, {{{typed, L::tile_bytes}, {}, {}}});
    std::vector<ikea::journal::entry> a(64), b(64);
    ikea::journal typed_effects{a}, runtime_effects{b};
    need(sp::bind_writer(v)->initialize(std::span<const std::uint64_t>(values), typed_effects).has_value(),
         "typed initialize");
    const sp::dense_output target{K, G, runtime, count};
    need(*sp::effect_capacity_initialize(target) >= 1, "runtime capacity");
    need(sp::initialize(target, std::span<const std::uint64_t>(values), runtime_effects).has_value(),
         "runtime initialize");
    need(typed == runtime, "runtime bytes equal the typed writer's");
    need(runtime_effects.used == typed_effects.used, "same record count");
    for (std::size_t i = 0; i < runtime_effects.used; ++i) {
        const auto& r = runtime_effects.entries()[i];
        need(r.source == &target && target.plane_storage(r.plane).data() == runtime.data() &&
                 r.offset == typed_effects.entries()[i].offset && r.size == typed_effects.entries()[i].size,
             "runtime records name the dense_output");
    }
    // Refusals leave everything untouched.
    const auto before = runtime;
    if constexpr (K < 64) {
        values[1] = std::uint64_t{1} << K;
        need(sp::initialize(target, std::span<const std::uint64_t>(values), runtime_effects).error() ==
                 sp::error::value && runtime == before,
             "runtime value refusal");
    }
    need(!sp::initialize(sp::dense_output{K, G, std::span(runtime).first(bytes - 1), count},
                         std::span<const std::uint64_t>(values), runtime_effects),
         "runtime capacity refusal");
    need(sp::initialize(sp::dense_output{0, G, runtime, count}, std::span<const std::uint64_t>(values),
                        runtime_effects).error() == sp::error::description,
         "runtime width refusal");
}
} // namespace

int main() {
    seriespack_runtime_initialize<13, ikea::seriespack::geometry::local>();
    seriespack_runtime_initialize<12, ikea::seriespack::geometry::striped>();
    seriespack_runtime_initialize<3, ikea::seriespack::geometry::striped>();
    seriespack_runtime_initialize<64, ikea::seriespack::geometry::local>();
    seriespack_point_footprint<ikea::seriespack::layout<20, ikea::seriespack::geometry::local, 8>>();
    seriespack_point_footprint<ikea::seriespack::layout<12, ikea::seriespack::geometry::striped>>();
    seriespack_point_footprint<ikea::seriespack::layout<23, ikea::seriespack::geometry::striped, 16>>();
    seriespack_point_footprint<ikea::seriespack::layout<3, ikea::seriespack::geometry::striped>>();
    tuplepack_read_footprint();
    tuplepack_identity();
    streampack_rebind();
    journal_resolution();
    selection_regions();
    std::puts("contracts: ok");
}
