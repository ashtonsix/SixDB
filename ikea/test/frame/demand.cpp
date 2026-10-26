// Demand follows the same lowering as execution: for one span with a coarse
// hole, required(span, blocks) names no section of a group whose blocks are
// all excluded, while required({span}) does; SUM over the span and holes
// answers with only the demanded sections available.
#include <ikea/frame.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace fr = ikea::frame;
using u64 = std::uint64_t;
namespace {
void check(bool ok, const char *what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", what);
        std::abort();
    }
}
bool has_group(const std::vector<fr::section_key> &keys, std::uint16_t node, std::size_t group) {
    return std::any_of(keys.begin(), keys.end(), [&](const auto &k) { return k.root_node == node && k.group == group; });
}
void witness(bool intervals) {
    const std::size_t n = 3 * 4096 + 100;
    std::vector<u64> values(n);
    for (std::size_t i = 0; i < n; ++i)
        values[i] = 1000 + i * 3;
    fr::description d;
    d.nodes = {{fr::node_kind::pack, 0, 0, {}}, {fr::node_kind::bound, 0, 0, {{0, fr::coordinate::identity}}}};
    d.root = 1;
    if (intervals) {
        d.nodes.push_back({fr::node_kind::pack, 0, 0, {}});
        d.nodes.push_back({fr::node_kind::bound, 0, 0, {{2, fr::coordinate::identity}}});
        d.nodes.push_back({fr::node_kind::intervals_relative_ends, 64, 0, {{1, fr::coordinate::identity}, {3, fr::coordinate::checkpoints}}});
        d.root = 4;
    }
    const auto built = fr::build(d, values, {0, 64, 0});
    check(bool(built), "build");
    std::vector<std::size_t> extents;
    std::vector<fr::stream_manifest> streams;
    std::vector<fr::borrowed_part> parts;
    std::vector<fr::section_key> all;
    for (const auto &s : built->streams) {
        fr::stream_manifest m{s.root_node, s.rows, {}};
        for (std::size_t g = 0; g < s.groups.size(); ++g) {
            const auto id = static_cast<std::uint32_t>(parts.size());
            m.groups.push_back({id, 0, s.groups[g].size()});
            extents.push_back(s.groups[g].size()), parts.push_back({id, s.groups[g]}), all.push_back({s.root_node, g});
        }
        streams.push_back(std::move(m));
    }
    const auto full = fr::bind_reader(d, {n, extents, streams, parts, all});
    check(bool(full), "bind");
    // [3900, 8500) touches root groups 0, 1 and 2; the coarse mask drops every
    // block of group 1 (blocks 16..31).
    const fr::row_range span{3900, 4600};
    ikea::bitset256 holes = ikea::bitset256::first_n(unsigned((n + 255) / 256));
    for (unsigned b = 16; b < 32; ++b)
        holes.set(b, false);
    const auto coarse = ikea::intervals::selection::bits(0, holes);
    const auto broad = full->required(std::span<const fr::row_range>(&span, 1));
    const auto lowered = full->required(span, coarse);
    check(broad && lowered, "required");
    check(has_group(*broad, d.root, 1), "broad range demands the middle group");
    check(!has_group(*lowered, d.root, 1) && has_group(*lowered, d.root, 0) && has_group(*lowered, d.root, 2),
          "lowered demand skips the masked group and keeps the edges");
    // Bind with only the lowered demand available: the span SUM answers,
    // while the broad SUM without the mask is refused.
    std::vector<fr::borrowed_part> some;
    for (const auto &k : *lowered)
        for (std::size_t i = 0; i < all.size(); ++i)
            if (all[i].root_node == k.root_node && all[i].group == k.group)
                some.push_back(parts[i]);
    const auto sparse = fr::bind_reader(d, {n, extents, streams, some, *lowered});
    check(bool(sparse), "sparse bind");
    fr::u128 want = 0;
    for (std::size_t i = span.first; i < span.first + span.count; ++i)
        want += holes.contains(unsigned(i / 256)) ? values[i] : 0;
    const auto got = sparse->sum(span, coarse);
    check(got && *got == want, "span SUM over the lowered demand");
    check(!sparse->sum(span), "unmasked span over the absent group refused");
}
} // namespace
int main() {
    witness(false);
    witness(true);
    std::puts("demand_check: lowered demand skips masked groups; span SUM answers from it");
}
