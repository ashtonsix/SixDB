#include <ikea/frame/description.h>
#include <ikea/frame/detail/recipe.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
#include <sys/mman.h>
#include <unistd.h>

namespace fo = ikea::frame;
namespace {
unsigned checks = 0, failures = 0;
void expect(bool ok, const char *what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}
template <class T> void refused(const std::expected<T, fo::description_error> &result, fo::description_error error, const char *what) {
    expect(!result && result.error() == error, what);
}
fo::description packed(fo::pack_preset preset = fo::pack_preset::compact, bool bounded = false, std::uint16_t heads = 0) {
    fo::description d;
    d.nodes.push_back({fo::node_kind::pack, static_cast<std::uint16_t>(preset), heads, {}});
    if (bounded) {
        d.nodes.push_back({fo::node_kind::bound, 0, 0, {{0, fo::coordinate::identity}}});
        d.root = 1;
    }
    return d;
}
std::vector<std::byte> bytes(const fo::description &d) {
    auto encoded = fo::encode(d);
    if (!encoded) {
        std::fprintf(stderr, "fixture refused: %.*s\n", static_cast<int>(fo::describe(encoded.error()).size()), fo::describe(encoded.error()).data());
        std::exit(2);
    }
    return std::move(*encoded);
}
void roundtrip(const fo::description &d, bool executable) {
    const auto wire = bytes(d);
    const auto decoded = fo::decode(wire);
    expect(decoded && *decoded == d, "description round trip retains node identities");
    expect(decoded && bytes(*decoded) == wire, "decoded description re-encodes canonically");
    const auto recipe = fo::detail::binding_recipe(d);
    expect(recipe.has_value() == executable, "syntax and binding admission remain distinct");
    if (!executable) refused(recipe, fo::description_error::unsupported, "placeholder factory refuses before payload attachment");
}
std::vector<std::byte> literal(std::initializer_list<unsigned> values) {
    std::vector<std::byte> result;
    for (unsigned x : values) result.push_back(std::byte(x));
    return result;
}
void wire_contract() {
    // Independent lists fix Fr/v1 geometry even if a future library's tuning
    // recommendations choose a different preset mapping.
    for (unsigned k = 1; k <= 64; ++k) {
        const bool x = (k >= 1 && k <= 7) || k == 12 || k == 20;
        const bool a = x || k == 10 || k == 14 || k == 15;
        expect(fo::packing_geometry(fo::pack_preset::compact, k) == fo::pack_geometry::local, "Fr/v1 compact geometry is fixed");
        expect((fo::packing_geometry(fo::pack_preset::bulk_x86, k) == fo::pack_geometry::striped) == x, "Fr/v1 x86 family is a resolved law");
        expect((fo::packing_geometry(fo::pack_preset::bulk_arm, k) == fo::pack_geometry::striped) == a, "Fr/v1 ARM family is a resolved law");
    }
    // Written independently from the serializer: Fr/v1, body15, UInt64,
    // block256/group16, one node/root0, pack compact with no children.
    const auto golden = literal({0x46, 0x72, 1, 15, 0, 1, 0, 1, 16, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0});
    expect(bytes(packed()) == golden, "known little-endian pack schema bytes");
    const auto decoded = fo::decode(golden);
    expect(decoded && *decoded == packed(), "independent golden input decodes");
    // Bec4096's parameter is 0x1000, checking a nonzero high parameter byte.
    fo::description presence;
    presence.nodes = {{fo::node_kind::bec, 4096, 0, {}},
                      {fo::node_kind::pack, 0, 0, {}},
                      {fo::node_kind::presence, 0, 0, {{0, fo::coordinate::identity}, {1, fo::coordinate::present_rank}}}};
    presence.root = 2;
    const auto p = bytes(presence);
    expect(p[15] == std::byte{0} && p[16] == std::byte{0x10}, "parameter high byte is little-endian");
    roundtrip(presence, false);
    for (auto preset : {fo::pack_preset::compact, fo::pack_preset::bulk_x86, fo::pack_preset::bulk_arm})
        for (bool bounded : {false, true}) {
            const auto d = packed(preset, bounded);
            roundtrip(d, true);
            const auto recipe = fo::detail::binding_recipe(d);
            expect(recipe && recipe->preset == preset && recipe->bounded == bounded, "executable recipe is exact");
        }
    // Fit policy is deliberately not accepted by description encode or decode.
    struct fit_policy { unsigned penalty, exception_cap, spare_slots; };
    const fo::description d = packed(fo::pack_preset::bulk_arm, true);
    const auto before = bytes(d);
    fit_policy policy{0, 64, 8};
    policy = {1000, 16, 512};
    expect(policy.spare_slots == 512 && bytes(d) == before, "policy change cannot alter the interpretation description");
}
void placeholder_contract() {
    // One physical record is shared by the high and low field projections;
    // split joins them into the sole UInt64 logical root. No orphan nodes.
    fo::description shared;
    shared.nodes = {{fo::node_kind::tuple_record, 2, 0, {}},
                    {fo::node_kind::tuple_field, 0, 0, {{0, fo::coordinate::record_field}}},
                    {fo::node_kind::tuple_field, 1, 0, {{0, fo::coordinate::record_field}}},
                    {fo::node_kind::split, 8, 0, {{1, fo::coordinate::identity}, {2, fo::coordinate::identity}}}};
    shared.root = 3;
    roundtrip(shared, false);
    const auto recovered = fo::decode(bytes(shared));
    expect(recovered && recovered->nodes.size() == 4 &&
               recovered->nodes[1].children[0].child == recovered->nodes[2].children[0].child,
           "shared child is recovered once, retaining both field identities");
    auto distinct = shared;
    distinct.nodes = {{fo::node_kind::tuple_record, 2, 0, {}},
                      {fo::node_kind::tuple_record, 2, 0, {}},
                      {fo::node_kind::tuple_field, 0, 0, {{0, fo::coordinate::record_field}}},
                      {fo::node_kind::tuple_field, 1, 0, {{1, fo::coordinate::record_field}}},
                      {fo::node_kind::split, 8, 0, {{2, fo::coordinate::identity}, {3, fo::coordinate::identity}}}};
    distinct.root = 4;
    roundtrip(distinct, false);
    expect(bytes(distinct) != bytes(shared), "distinct physical identities are not structurally deduplicated");
    for (auto kind : {fo::node_kind::intervals_sizes, fo::node_kind::intervals_relative_ends})
        for (unsigned spacing : {2u, 16u, 64u, 256u}) {
            fo::description intervals;
            intervals.nodes = {{fo::node_kind::pack, 0, 0, {}},
                               {fo::node_kind::pack, 0, 0, {}},
                               {kind, static_cast<std::uint16_t>(spacing), 0,
                                {{0, fo::coordinate::identity}, {1, fo::coordinate::checkpoints}}}};
            intervals.root = 2;
            roundtrip(intervals, false);
            expect(fo::detail::binding_recipe(intervals, 0).has_value() && fo::detail::binding_recipe(intervals, 1).has_value(),
                   "basic subtree admission preserves original node identities");
            refused(fo::detail::binding_recipe(intervals, 65535), fo::description_error::root, "subtree root is range checked");
        }
    auto bad = shared;
    bad.nodes[2].parameter = 2;
    refused(fo::validate(bad), fo::description_error::parameters, "field rank cannot equal record field count");
    bad = shared;
    bad.nodes[1].children[0].mapping = fo::coordinate::identity;
    refused(fo::validate(bad), fo::description_error::coordinates, "record projection requires record-field coordinates");
    bad = shared;
    bad.nodes[3].parameter = 64;
    refused(fo::validate(bad), fo::description_error::parameters, "split cannot shift by the full UInt64 width");
    fo::description intervals;
    intervals.nodes = {{fo::node_kind::pack, 0, 0, {}},
                       {fo::node_kind::pack, 0, 0, {}},
                       {fo::node_kind::intervals_sizes, 16, 0,
                        {{0, fo::coordinate::identity}, {1, fo::coordinate::checkpoints}}}};
    intervals.root = 2;
    for (unsigned invalid : {0u, 1u, 3u, 257u, 65535u}) {
        intervals.nodes[2].parameter = static_cast<std::uint16_t>(invalid);
        refused(fo::validate(intervals), fo::description_error::parameters, "invalid checkpoint spacing is refused");
    }
    intervals.nodes[2].parameter = 16;
    intervals.nodes[2].children[1].mapping = fo::coordinate::identity;
    refused(fo::validate(intervals), fo::description_error::coordinates, "checkpoints cannot inherit parent row coordinates");
    fo::description presence;
    presence.nodes = {{fo::node_kind::bec, 256, 0, {}},
                      {fo::node_kind::pack, 0, 0, {}},
                      {fo::node_kind::presence, 0, 0,
                       {{0, fo::coordinate::identity}, {1, fo::coordinate::identity}}}};
    presence.root = 2;
    refused(fo::validate(presence), fo::description_error::coordinates, "present values require rank coordinates");
    presence.nodes[2].children[1].mapping = fo::coordinate::present_rank;
    presence.nodes[0].kind = fo::node_kind::pack;
    presence.nodes[0].parameter = 0;
    refused(fo::validate(presence), fo::description_error::coordinates, "a numeric child cannot silently become the presence bitmap");
}
void runs_contract() {
    fo::description runs;
    runs.nodes = {{fo::node_kind::pack, 0, 0, {}},
                  {fo::node_kind::pack, 2, 0, {}},
                  {fo::node_kind::runs, 0, 0,
                   {{0, fo::coordinate::run_ordinal}, {1, fo::coordinate::run_ordinal}}}};
    runs.root = 2;
    // Independent Fr/v1 wire: three nodes, root2; pack compact, pack ARM,
    // block-local runs(kind10/mode0), sizes-minus-one/value operand order.
    // Both edges explicitly use run_ordinal(4), not parent rows or checkpoints.
    const auto golden = literal({0x46, 0x72, 1, 33, 0,
                                  1, 0, 1, 16, 0, 3, 0, 2, 0,
                                  1, 0, 0, 0, 0, 0,
                                  1, 2, 0, 0, 0, 0,
                                  10, 0, 0, 0, 0, 2, 0, 0, 4, 1, 0, 4});
    expect(bytes(runs) == golden, "block-local run placeholder has known wire bytes");
    const auto recovered = fo::decode(golden);
    expect(recovered && *recovered == runs, "independent run-ordinal golden input decodes");
    roundtrip(runs, false);
    expect(fo::detail::binding_recipe(runs, 0).has_value() && fo::detail::binding_recipe(runs, 1).has_value(),
        "run placeholder retains its independent basic child identities");
    for (auto mapping : {fo::coordinate::identity, fo::coordinate::checkpoints,
                         fo::coordinate::present_rank, fo::coordinate::record_field})
        for (unsigned child : {0u, 1u}) {
            auto bad = runs;
            bad.nodes[2].children[child].mapping = mapping;
            refused(fo::validate(bad), fo::description_error::coordinates, "each run child requires its parent's run-ordinal domain");
        }
    auto bad_wire = golden;
    bad_wire[34] = std::byte{0};
    refused(fo::decode(bad_wire), fo::description_error::coordinates, "wire run edge cannot inherit parent row coordinates");
    auto bad = runs;
    bad.nodes[2].children.pop_back();
    refused(fo::validate(bad), fo::description_error::parameters, "run placeholder requires both ordered operands");
    bad = runs;
    bad.nodes[0] = {fo::node_kind::bec, 256, 0, {}};
    refused(fo::validate(bad), fo::description_error::coordinates, "run sizes-minus-one must be a numeric series");
    bad = runs;
    bad.nodes[1] = {fo::node_kind::tuple_record, 1, 0, {}};
    refused(fo::validate(bad), fo::description_error::coordinates, "run values must be a numeric series, not a record");
    for (unsigned global_mode : {1u, 65535u}) {
        bad = runs;
        bad.nodes[2].parameter = static_cast<std::uint16_t>(global_mode);
        refused(fo::validate(bad), fo::description_error::parameters, "unresearched global-run mode is not block-local mode0");
    }
    bad = packed(fo::pack_preset::compact, true);
    bad.nodes[1].children[0].mapping = fo::coordinate::run_ordinal;
    refused(fo::validate(bad), fo::description_error::coordinates, "run ordinal is not an identity coordinate for another stage");
    auto wrapped = runs;
    wrapped.nodes.push_back({fo::node_kind::bound, 0, 0, {{2, fo::coordinate::identity}}});
    wrapped.root = 3;
    roundtrip(wrapped, false);
    bad = runs;
    bad.nodes[2].heads = 8;
    refused(fo::validate(bad), fo::description_error::parameters, "run node cannot carry a pack head parameter");
}
void heads_contract() {
    // H0 stays byte-for-byte identical to the existing golden pack schema.
    // H8/H16 occupy only the old reserved u16, independent of preset.
    const auto golden8 = literal({0x46, 0x72, 1, 15, 0,
                                   1, 0, 1, 16, 0, 1, 0, 0, 0,
                                   1, 0, 0, 8, 0, 0});
    const auto golden16 = literal({0x46, 0x72, 1, 15, 0,
                                    1, 0, 1, 16, 0, 1, 0, 0, 0,
                                    1, 2, 0, 16, 0, 0});
    const auto head8 = packed(fo::pack_preset::compact, false, 8);
    const auto head16 = packed(fo::pack_preset::bulk_arm, false, 16);
    expect(bytes(head8) == golden8 && bytes(head16) == golden16, "H8/H16 have independent little-endian golden bytes");
    const auto recovered8 = fo::decode(golden8), recovered16 = fo::decode(golden16);
    expect(recovered8 && *recovered8 == head8 && recovered16 && *recovered16 == head16,
        "headed-leaf golden inputs preserve preset and head count independently");
    for (auto preset : {fo::pack_preset::compact, fo::pack_preset::bulk_x86, fo::pack_preset::bulk_arm})
        for (bool bounded : {false, true})
            for (std::uint16_t heads : {std::uint16_t{8}, std::uint16_t{16}}) {
                const auto d = packed(preset, bounded, heads);
                roundtrip(d, false);
                refused(fo::detail::binding_recipe(d, 0), fo::description_error::unsupported, "headed basic leaf cannot silently bind as H0");
                auto h0 = d;
                h0.nodes[0].heads = 0;
                expect(bytes(h0) == bytes(packed(preset, bounded)), "clearing heads restores the original H0 description bytes");
            }
    for (unsigned invalid : {1u, 7u, 9u, 15u, 17u, 64u, 65535u}) {
        auto bad = packed();
        bad.nodes[0].heads = static_cast<std::uint16_t>(invalid);
        refused(fo::validate(bad), fo::description_error::parameters, "unsupported head count is refused");
    }
    auto bad_wire = golden8;
    bad_wire[18] = std::byte{1};
    refused(fo::decode(bad_wire), fo::description_error::parameters, "head parameter is validated as full u16, not a truncated byte");
    auto bad = packed(fo::pack_preset::compact, true);
    bad.nodes[1].heads = 8;
    refused(fo::validate(bad), fo::description_error::parameters, "bound cannot carry a pack head parameter");
    fo::description field;
    field.nodes = {{fo::node_kind::tuple_record, 1, 16, {}},
                   {fo::node_kind::tuple_field, 0, 0, {{0, fo::coordinate::record_field}}}};
    field.root = 1;
    refused(fo::validate(field), fo::description_error::parameters, "record node cannot carry a pack head parameter");
    fo::description intervals;
    intervals.nodes = {{fo::node_kind::pack, 0, 0, {}}, {fo::node_kind::pack, 0, 0, {}},
                       {fo::node_kind::intervals_sizes, 16, 8,
                        {{0, fo::coordinate::identity}, {1, fo::coordinate::checkpoints}}}};
    intervals.root = 2;
    refused(fo::validate(intervals), fo::description_error::parameters, "interval stage cannot carry a pack head parameter");
    // Remaining-payload geometry: these legal (K,H) pairs differ from the
    // geometry one would choose by mistakenly applying a preset to full K.
    expect(fo::packing_geometry(fo::pack_preset::bulk_arm, 18 - 8) == fo::pack_geometry::striped,
        "H8 leaves a ten-bit ARM-striped payload");
    expect(fo::packing_geometry(fo::pack_preset::bulk_arm, 26 - 16) == fo::pack_geometry::striped,
        "H16 leaves a ten-bit ARM-striped payload");
    expect(fo::packing_geometry(fo::pack_preset::bulk_x86, 18 - 8) == fo::pack_geometry::local,
        "the same H8 payload remains Local in the x86 family");
    expect(fo::packing_geometry(fo::pack_preset::bulk_x86, 28 - 8) == fo::pack_geometry::striped,
        "H8 leaves a twenty-bit x86-striped payload");
}
void malformed_contract() {
    const auto valid = bytes(packed(fo::pack_preset::bulk_x86, true));
    for (std::size_t n = 0; n < valid.size(); ++n)
        expect(!fo::decode(std::span(valid).first(n)), "every truncated schema is refused");
    auto bad = valid;
    bad.push_back(std::byte{0});
    refused(fo::decode(bad), fo::description_error::trailing, "bytes after the envelope are refused");
    // A longer envelope does not legitimize trailing body bytes.
    bad[3] = std::byte(std::to_integer<unsigned>(bad[3]) + 1);
    refused(fo::decode(bad), fo::description_error::trailing, "unused bytes inside the envelope are refused");
    bad = valid;
    bad[0] = std::byte{'X'};
    refused(fo::decode(bad), fo::description_error::envelope, "wrong tag is refused");
    bad = valid;
    bad[2] = std::byte{255};
    refused(fo::decode(bad), fo::description_error::version, "unknown version is refused");
    bad = valid;
    bad[5] = std::byte{255};
    refused(fo::decode(bad), fo::description_error::domain, "unknown logical domain is refused");
    bad = valid;
    bad[14] = std::byte{255};
    refused(fo::decode(bad), fo::description_error::kind, "unknown node kind is refused");
    bad = valid;
    bad[15] = std::byte{255};
    refused(fo::decode(bad), fo::description_error::parameters, "unknown pack preset is refused");
    bad = valid;
    bad[17] = std::byte{1};
    refused(fo::decode(bad), fo::description_error::parameters, "illegal one-bit head separation is refused");
    bad = valid;
    bad[10] = bad[11] = std::byte{255};
    refused(fo::decode(bad), fo::description_error::limit, "hostile node count is bounded before allocation");
    bad = valid;
    bad[19] = std::byte{255};
    refused(fo::decode(bad), fo::description_error::limit, "hostile child count is bounded before allocation");
    bad = valid;
    bad[3] = bad[4] = std::byte{255};
    refused(fo::decode(bad), fo::description_error::truncated, "extreme envelope length is refused without reading outside input");
    bad = valid;
    bad[12] = bad[13] = std::byte{255};
    refused(fo::decode(bad), fo::description_error::root, "hostile root index is refused");
    bad = valid;
    bad[28] = std::byte{3};
    refused(fo::decode(bad), fo::description_error::coordinates, "bound edge cannot be a record projection");
    bad = valid;
    bad[26] = bad[27] = std::byte{255};
    refused(fo::decode(bad), fo::description_error::topology, "wire dangling child identity is refused");
    bad = valid;
    bad[26] = std::byte{1};
    refused(fo::decode(bad), fo::description_error::topology, "wire self-cycle is refused");
    auto d = packed(fo::pack_preset::compact, true);
    d.nodes[0].kind = static_cast<fo::node_kind>(255);
    refused(fo::detail::binding_recipe(d), fo::description_error::kind, "factory validates before recognizing a recipe");
    d = packed();
    d.nodes.push_back({fo::node_kind::pack, 0, 0, {}});
    refused(fo::validate(d), fo::description_error::unreachable, "unused node identities are noncanonical");
    d = packed();
    d.block_rows = 0;
    refused(fo::validate(d), fo::description_error::geometry, "zero block geometry is refused");
    d = packed();
    d.group_blocks = std::numeric_limits<std::uint16_t>::max();
    refused(fo::validate(d), fo::description_error::geometry, "extreme group geometry is refused");
    d = packed();
    d.nodes.clear();
    refused(fo::validate(d), fo::description_error::limit, "empty graph is refused");
    d = packed();
    for (unsigned i = 1; i < fo::maximum_nodes; ++i)
        d.nodes.push_back({fo::node_kind::bound, 0, 0, {{static_cast<std::uint16_t>(i - 1), fo::coordinate::identity}}});
    d.root = static_cast<std::uint16_t>(d.nodes.size() - 1);
    roundtrip(d, false);
    d.nodes.push_back({fo::node_kind::bound, 0, 0, {{d.root, fo::coordinate::identity}}});
    ++d.root;
    refused(fo::validate(d), fo::description_error::limit, "node bound is exact");
}
void output_contract() {
    const auto d = packed(fo::pack_preset::bulk_arm, true);
    const auto golden = bytes(d);
    const auto size = fo::encoded_size(d);
    expect(size && *size == golden.size(), "encoded_size agrees with known schema");
    for (std::size_t n = 0; n < golden.size(); ++n) {
        std::vector<std::byte> out(golden.size() + 16, std::byte{0xA5});
        const auto original = out;
        refused(fo::encode(d, std::span(out).first(n)), fo::description_error::capacity, "short encode refuses");
        expect(out == original, "capacity refusal leaves all output untouched");
    }
    std::vector<std::byte> out(golden.size() + 16, std::byte{0xA5});
    const auto encoded = fo::encode(d, out);
    expect(encoded && *encoded == golden.size() && std::equal(golden.begin(), golden.end(), out.begin()), "encode writes canonical prefix");
    const auto suffix = std::span(out).subspan(golden.size());
    expect(std::all_of(suffix.begin(), suffix.end(), [](std::byte b) { return b == std::byte{0xA5}; }), "encode preserves output suffix");
    auto invalid = d;
    invalid.nodes[1].children[0].child = 1;
    const auto original = out;
    refused(fo::encode(invalid, out), fo::description_error::topology, "invalid description refuses before encode");
    expect(out == original, "invalid description leaves output untouched");
    // Every accepted one-byte mutation must be fully represented by the
    // recovered description: ignored wire bits would violate canonicality.
    bool canonical = true;
    std::size_t accepted = 0;
    for (std::size_t at = 0; at < golden.size(); ++at)
        for (unsigned value = 0; value < 256; ++value) {
            auto input = golden;
            input[at] = std::byte(value);
            if (auto recovered = fo::decode(input)) {
                ++accepted;
                const auto encoded_again = fo::encode(*recovered);
                canonical &= encoded_again && *encoded_again == input;
            }
        }
    expect(accepted > 0 && canonical, "accepted byte mutations have no ignored or noncanonical bytes");
}
void guarded_input_contract() {
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) std::abort();
    const auto page = static_cast<std::size_t>(page_size);
    void *allocation = mmap(nullptr, 3 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (allocation == MAP_FAILED) std::abort();
    auto *middle = static_cast<std::byte *>(allocation) + page;
    if (mprotect(middle, page, PROT_READ | PROT_WRITE)) std::abort();
    const auto wire = bytes(packed(fo::pack_preset::bulk_x86, true));
    for (std::size_t n = 0; n <= wire.size(); ++n) {
        std::copy_n(wire.begin(), n, middle + page - n);
        const auto result = fo::decode({middle + page - n, n});
        expect(result.has_value() == (n == wire.size()), "exact input suffix against inaccessible page");
    }
    std::copy(wire.begin(), wire.end(), middle);
    expect(fo::decode({middle, wire.size()}).has_value(), "exact input origin against inaccessible page");
    const auto encoded = fo::encode(packed(fo::pack_preset::bulk_x86, true), {middle + page - wire.size(), wire.size()});
    expect(encoded && *encoded == wire.size() && std::equal(wire.begin(), wire.end(), middle + page - wire.size()), "exact output suffix against inaccessible page");
    if (munmap(allocation, 3 * page)) std::abort();
}
} // namespace

int main() {
    wire_contract();
    placeholder_contract();
    runs_contract();
    heads_contract();
    malformed_contract();
    output_contract();
    guarded_input_contract();
    std::printf("description_check: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
