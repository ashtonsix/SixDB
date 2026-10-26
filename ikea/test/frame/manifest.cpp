#include <ikea/frame/image.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <utility>

namespace fo = ikea::frame;
namespace {
unsigned checks{};
void check(bool ok, const char *what) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", what);
        std::abort();
    }
}
template <class T> void refused(const std::expected<T, fo::manifest_error> &result,
    fo::manifest_error error, const char *what) {
    check(!result && result.error() == error, what);
}
template <class T> T take(std::expected<T, fo::manifest_error> result) {
    check(bool(result), "unexpected manifest refusal");
    return std::move(*result);
}
struct fixture {
    fo::description program;
    std::size_t rows{};
    std::vector<std::size_t> parts;
    std::vector<fo::stream_manifest> streams;
};
std::uint16_t basic(fo::description &d, bool bounded, fo::pack_preset preset = fo::pack_preset::compact) {
    const auto leaf = static_cast<std::uint16_t>(d.nodes.size());
    d.nodes.push_back({fo::node_kind::pack, static_cast<std::uint16_t>(preset), 0, {}});
    if (!bounded) return leaf;
    const auto root = static_cast<std::uint16_t>(d.nodes.size());
    d.nodes.push_back({fo::node_kind::bound, 0, 0, {{leaf, fo::coordinate::identity}}});
    return root;
}
void add_stream(fixture &f, std::uint16_t node, std::size_t rows) {
    fo::stream_manifest stream{node, rows, {}};
    // Synthetic positive section sizes test metadata geometry/placement only.
    // There are deliberately no image bytes, storage pointers or byte-law claims.
    for (std::size_t first = 0; first < rows; first += 4096) {
        const auto part = static_cast<std::uint32_t>(f.parts.size());
        const auto extent = 100 + stream.groups.size();
        stream.groups.push_back({part, 0, extent});
        f.parts.push_back(extent);
    }
    f.streams.push_back(std::move(stream));
}
fixture plain(std::size_t rows, bool bounded) {
    fixture f;
    f.rows = rows;
    f.program.root = basic(f.program, bounded);
    add_stream(f, f.program.root, rows);
    return f;
}
fixture intervals(std::size_t rows, unsigned spacing, bool bounded_deltas = true, bool bounded_checkpoints = true,
    fo::node_kind form = fo::node_kind::intervals_sizes) {
    fixture f;
    f.rows = rows;
    const auto delta = basic(f.program, bounded_deltas, fo::pack_preset::bulk_x86);
    const auto checkpoints = basic(f.program, bounded_checkpoints, fo::pack_preset::bulk_arm);
    f.program.root = static_cast<std::uint16_t>(f.program.nodes.size());
    f.program.nodes.push_back({form, static_cast<std::uint16_t>(spacing), 0,
                              {{delta, fo::coordinate::identity}, {checkpoints, fo::coordinate::checkpoints}}});
    add_stream(f, f.program.root, rows);
    add_stream(f, checkpoints, rows / spacing + (rows % spacing != 0));
    return f;
}
auto validate(const fixture &f) { return fo::validate_manifest(f.program, f.rows, f.parts, f.streams); }
auto required(const fixture &f, std::span<const fo::row_range> ranges) {
    return fo::required_sections(f.program, f.rows, f.streams, ranges);
}
std::vector<fo::section_key> direct_rows(const fixture &f, std::span<const fo::row_range> ranges) {
    // Independent expected answer: enumerate demanded original rows and their
    // checkpoint row, then union the named groups. No range-end algebra.
    std::set<std::pair<std::uint16_t, std::size_t>> wanted;
    const auto &root = f.program.nodes[f.program.root];
    for (auto range : ranges)
        for (std::size_t i = 0; i < range.count; ++i) {
            const auto row = range.first + i;
            wanted.emplace(f.program.root, row / 4096);
            if (root.kind == fo::node_kind::intervals_sizes || root.kind == fo::node_kind::intervals_relative_ends)
                wanted.emplace(root.children[1].child, (row / root.parameter) / 4096);
        }
    std::vector<fo::section_key> result;
    for (auto [node, group] : wanted) result.push_back({node, group});
    return result;
}
void copack(fixture &f) {
    std::size_t cursor = 16;
    for (auto &s : f.streams)
        for (auto &g : s.groups) {
            g.owner_part = 0;
            g.offset = cursor;
            cursor += g.extent + 16;
        }
    f.parts = {cursor};
}
void geometry(fo::node_kind form) {
    for (unsigned spacing : {16u, 64u}) {
        auto f = intervals(4367, spacing, true, true, form);
        check(bool(validate(f)), "nonzero-group partial-tail manifest validates");
        check(f.program.root == 4 && f.streams[1].root_node == 3,
            "fixture's hand-authored root/checkpoint identities changed");
        check(f.streams[1].rows == (spacing == 16 ? 273 : 69) && f.streams[1].groups.size() == 1,
            "partial final checkpoint row is retained in its own domain");
        const std::array tail{fo::row_range{4096, 271}};
        const std::vector<fo::section_key> expected{{3, 0}, {4, 1}};
        check(take(required(f, tail)) == expected, "root group1 requires checkpoint group0");
        const std::array cross{fo::row_range{4095, 2}};
        const std::vector<fo::section_key> cross_expected{{3, 0}, {4, 0}, {4, 1}};
        check(take(required(f, cross)) == cross_expected, "crossing range closes over both root groups");
        const std::array duplicates{fo::row_range{4096, 10}, fo::row_range{4101, 5}, fo::row_range{4096, 10}};
        check(take(required(f, duplicates)) == expected, "overlapping repeated demands deduplicate");
        const std::array last{fo::row_range{4366, 1}};
        check(take(required(f, last)) == expected, "last partial-block row has a checkpoint dependency");
        std::swap(f.streams[0], f.streams[1]);
        check(bool(validate(f)) && take(required(f, tail)) == expected,
            "stream vector order is not its identity");
        copack(f);
        check(bool(validate(f)) && take(required(f, tail)) == expected,
            "disjoint streams and groups can share one owner Part");
    }
    for (bool bounded_deltas : {false, true})
        for (bool bounded_checkpoints : {false, true}) {
            auto f = intervals(8193, 64, bounded_deltas, bounded_checkpoints, form);
            check(bool(validate(f)), "all basic child recipe combinations validate");
            const std::array ranges{fo::row_range{8192, 1}, fo::row_range{4000, 300}, fo::row_range{0, 2}};
            check(take(required(f, ranges)) == direct_rows(f, ranges), "closure matches independent original-row enumeration");
        }
    // The description permits other legal G values too; exercise a checkpoint
    // stream with several own groups, not only the G16/G64 one-group case.
    auto small_g = intervals(65536, 2, true, true, form);
    check(bool(validate(small_g)), "larger checkpoint domain has its own group count");
    const std::array last{fo::row_range{65535, 1}};
    check(take(required(small_g, last)) == std::vector<fo::section_key>{{3, 7}, {4, 15}},
        "checkpoint group index follows its row domain");
    for (unsigned spacing : {2u, 16u, 64u, 256u})
        for (std::size_t rows : {std::size_t{1}, std::size_t{4095}, std::size_t{4096}, std::size_t{4097}, std::size_t{65536}}) {
            auto f = intervals(rows, spacing, true, true, form);
            const std::array all{fo::row_range{0, rows}, fo::row_range{rows, 0}};
            check(bool(validate(f)) && take(required(f, all)) == direct_rows(f, all),
                "full-domain closure matches independent enumerated checkpoints");
        }
    for (bool bounded : {false, true}) {
        auto f = plain(8193, bounded);
        const std::array ranges{fo::row_range{4096, 4097}, fo::row_range{0, 0}};
        check(bool(validate(f)) && take(required(f, ranges)) == direct_rows(f, ranges),
            "pack and bound-pack single-stream closures validate");
    }
}
void unavailable(fo::node_kind form) {
    auto f = intervals(4367, 64, true, true, form);
    copack(f);
    check(bool(validate(f)), "co-packed availability fixture validates");
    const std::array ranges{fo::row_range{4096, 271}};
    const auto keys = take(required(f, ranges));
    const std::array checkpoints_only{fo::section_key{3, 0}};
    const std::array root_only{fo::section_key{4, 1}};
    refused(fo::admit_sections(keys, checkpoints_only), fo::manifest_error::unavailable,
        "checkpoint presence does not authorize root bytes in the same Part");
    refused(fo::admit_sections(keys, root_only), fo::manifest_error::unavailable,
        "root presence does not authorize missing checkpoints");
    refused(fo::admit_sections(keys, {}), fo::manifest_error::unavailable, "missing every source refuses");
    const std::array duplicates{fo::section_key{3, 0}, fo::section_key{3, 0}, fo::section_key{999, 7}};
    refused(fo::admit_sections(keys, duplicates), fo::manifest_error::unavailable,
        "duplicate and unrelated available keys cannot replace a missing dependency");
    check(bool(fo::admit_sections(keys, keys)), "complete dependency availability admits");
    check(bool(fo::admit_sections({}, {})), "empty demand needs no payload availability");
}
void refusals(fo::node_kind form) {
    const auto original = intervals(4367, 64, true, true, form);
    auto f = original;
    f.streams.pop_back();
    refused(validate(f), fo::manifest_error::streams, "missing checkpoint stream refuses");
    f = original;
    f.streams[1].root_node = f.streams[0].root_node;
    refused(validate(f), fo::manifest_error::streams, "duplicate stream identity refuses");
    f = original;
    f.streams[1].root_node = std::numeric_limits<std::uint16_t>::max();
    refused(validate(f), fo::manifest_error::streams, "unknown node identity refuses before indexing");
    f = original;
    f.streams[1].rows = 68;
    refused(validate(f), fo::manifest_error::rows, "floor-rounded checkpoint count refuses");
    f.streams[1].rows = std::numeric_limits<std::size_t>::max();
    refused(validate(f), fo::manifest_error::rows, "extreme child cardinality refuses without overflow");
    f = original;
    f.streams[0].groups.pop_back();
    refused(validate(f), fo::manifest_error::groups, "missing group location refuses");
    f = original;
    f.streams[0].groups.push_back(f.streams[0].groups[0]);
    refused(validate(f), fo::manifest_error::groups, "extra group location refuses");
    f = original;
    f.streams[0].groups[0].owner_part = std::numeric_limits<std::uint32_t>::max();
    refused(validate(f), fo::manifest_error::placement, "unknown owner Part refuses before extent indexing");
    f = original;
    f.streams[0].groups[0].offset = std::numeric_limits<std::size_t>::max();
    refused(validate(f), fo::manifest_error::placement, "extreme offset refuses without address arithmetic");
    f = original;
    ++f.streams[0].groups[0].extent;
    refused(validate(f), fo::manifest_error::placement, "section extending past Part refuses");
    f = original;
    f.streams[0].groups[0].extent = 0;
    refused(validate(f), fo::manifest_error::placement, "nonempty group cannot masquerade as absent zero bytes");
    f = original;
    copack(f);
    auto &a = f.streams[0].groups[0];
    auto &b = f.streams[0].groups[1];
    b.offset = a.offset + a.extent - 1;
    refused(validate(f), fo::manifest_error::overlap, "one-byte overlap between groups refuses");
    b.offset = a.offset + a.extent;
    check(bool(validate(f)), "adjacent independent sections do not overlap");
    f.streams[1].groups[0] = f.streams[0].groups[0];
    refused(validate(f), fo::manifest_error::overlap, "aliasing distinct row domains is not DAG sharing");
    auto boundary = plain(1, false);
    boundary.parts[0] = std::numeric_limits<std::size_t>::max();
    boundary.streams[0].groups[0] = {0, std::numeric_limits<std::size_t>::max() - 1, 1};
    check(bool(validate(boundary)), "declared endpoint SIZE_MAX validates without forming an address");
    boundary.streams[0].groups[0].extent = 2;
    refused(validate(boundary), fo::manifest_error::placement, "wrapped endpoint refuses by subtraction");

    f = original;
    f.rows = 65537;
    refused(validate(f), fo::manifest_error::range, "root extent beyond the admitted part refuses");
    const std::array bad_ranges{fo::row_range{0, 1}, fo::row_range{std::numeric_limits<std::size_t>::max(), 2}};
    refused(required(original, bad_ranges), fo::manifest_error::range, "later invalid range refuses entire closure");
    const std::array wrap{fo::row_range{1, std::numeric_limits<std::size_t>::max()}};
    refused(required(original, wrap), fo::manifest_error::range, "range count overflow refuses");
    const std::array past{fo::row_range{original.rows + 1, 0}};
    refused(required(original, past), fo::manifest_error::range, "empty range beyond logical end refuses");
    f = original;
    f.program.nodes.back().parameter = 3;
    refused(validate(f), fo::manifest_error::description, "malformed coordinate law refuses before manifest use");
}
void unsupported(fo::node_kind form) {
    auto f = intervals(4367, 64, true, true, form);
    f.program.nodes.push_back({fo::node_kind::bound, 0, 0, {{f.program.root, fo::coordinate::identity}}});
    ++f.program.root;
    check(bool(fo::validate(f.program)), "wrapper around intervals is valid syntax");
    refused(validate(f), fo::manifest_error::unsupported, "unimplemented nested stream propagation refuses");
    f = intervals(4367, 64, true, true, form);
    const auto nested = f.program.root;
    const auto delta = basic(f.program, false);
    f.program.root = static_cast<std::uint16_t>(f.program.nodes.size());
    const auto outer_form = form == fo::node_kind::intervals_sizes ? fo::node_kind::intervals_relative_ends :
        fo::node_kind::intervals_sizes;
    f.program.nodes.push_back({outer_form, 16, 0,
                              {{delta, fo::coordinate::identity}, {nested, fo::coordinate::checkpoints}}});
    check(bool(fo::validate(f.program)), "intervals under checkpoints is valid syntax");
    refused(validate(f), fo::manifest_error::unsupported, "nested checkpoint stream recipe refuses");
    f.program.nodes = {{fo::node_kind::pack, 0, 0, {}},
                       {fo::node_kind::bound, 0, 0, {{0, fo::coordinate::identity}}},
                       {fo::node_kind::bound, 0, 0, {{0, fo::coordinate::identity}}},
                       {form, 64, 0,
                        {{1, fo::coordinate::identity}, {2, fo::coordinate::checkpoints}}}};
    f.program.root = 3;
    check(bool(fo::validate(f.program)), "shared leaf has valid DAG syntax");
    refused(validate(f), fo::manifest_error::unsupported, "one pack cannot own distinct root/checkpoint row domains");
    f = intervals(4367, 64, true, true, form);
    f.program.nodes.back().kind = fo::node_kind::split;
    f.program.nodes.back().parameter = 16;
    f.program.nodes.back().children[1].mapping = fo::coordinate::identity;
    check(bool(fo::validate(f.program)), "split placeholder is valid syntax");
    refused(validate(f), fo::manifest_error::unsupported, "admitting both interval forms did not admit arbitrary two-child roots");
}
void empty(fo::node_kind form) {
    for (auto f : {plain(0, false), plain(0, true), intervals(0, 16, true, true, form), intervals(0, 64, true, true, form)}) {
        check(bool(validate(f)) && f.parts.empty(), "empty image has no group locations or owner Parts");
        const std::array none{fo::row_range{0, 0}};
        check(take(required(f, none)).empty(), "empty range on empty image requires no payload");
        const std::array one{fo::row_range{0, 1}};
        refused(required(f, one), fo::manifest_error::range, "nonempty request on empty image refuses");
    }
}
} // namespace

int main() {
    for (auto form : {fo::node_kind::intervals_sizes, fo::node_kind::intervals_relative_ends}) {
        geometry(form);
        unavailable(form);
        refusals(form);
        unsupported(form);
        empty(form);
    }
    std::printf("manifest_check: %u checks, no payload/address access\n", checks);
}
