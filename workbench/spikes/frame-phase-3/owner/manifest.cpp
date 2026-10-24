#include "manifest.h"

#include <algorithm>
#include <array>
#include <new>

namespace frame3_owner {
namespace {
constexpr std::size_t maximum_rows = 65536, group_rows = 4096, maximum_groups = 16;
struct program_shape {
    std::array<std::uint16_t, 2> roots{};
    std::array<std::size_t, 2> rows{};
    std::size_t streams = 1, spacing = 0;
};
std::size_t groups_for(std::size_t rows) noexcept { return rows / group_rows + (rows % group_rows != 0); }

// Admit a basic subtree with its original identity; no copied descriptor tree.
std::expected<bound_pack_recipe, manifest_error> basic_recipe(const description &d, std::uint16_t root) {
    const auto recipe = binding_recipe(d, root);
    if (!recipe) return std::unexpected(manifest_error::unsupported);
    return *recipe;
}
std::expected<program_shape, manifest_error> analyze(const description &d, std::size_t rows) {
    if (!validate(d)) return std::unexpected(manifest_error::description);
    if (rows > maximum_rows) return std::unexpected(manifest_error::range);
    program_shape shape{{d.root, 0}, {rows, 0}};
    const node &root = d.nodes[d.root];
    if (root.kind != node_kind::intervals_sizes) {
        if (!binding_recipe(d)) return std::unexpected(manifest_error::unsupported);
        return shape;
    }
    const auto deltas = root.children[0].child, checkpoints = root.children[1].child;
    if (!basic_recipe(d, deltas) || !basic_recipe(d, checkpoints))
        return std::unexpected(manifest_error::unsupported);
    // One node cannot own both row domains. This is stricter than syntax:
    // even a shared pack below two different bound nodes must be refused.
    const auto overlaps = [&](std::uint16_t a, std::uint16_t b) {
        const auto leaf_a = d.nodes[a].kind == node_kind::bound ? d.nodes[a].children[0].child : a;
        const auto leaf_b = d.nodes[b].kind == node_kind::bound ? d.nodes[b].children[0].child : b;
        return a == b || a == leaf_b || leaf_a == b || leaf_a == leaf_b;
    };
    if (overlaps(deltas, checkpoints)) return std::unexpected(manifest_error::unsupported);
    shape.streams = 2;
    shape.spacing = root.parameter;
    shape.roots[1] = checkpoints;
    shape.rows[1] = rows / shape.spacing + (rows % shape.spacing != 0);
    return shape;
}
std::expected<void, manifest_error> check_streams(const program_shape &shape,
    std::span<const stream_manifest> streams) noexcept {
    if (streams.size() != shape.streams) return std::unexpected(manifest_error::streams);
    std::array<bool, 2> found{};
    for (const auto &s : streams) {
        std::size_t i = 0;
        while (i < shape.streams && s.root_node != shape.roots[i]) ++i;
        if (i == shape.streams || found[i]) return std::unexpected(manifest_error::streams);
        found[i] = true;
        if (s.rows != shape.rows[i]) return std::unexpected(manifest_error::rows);
        if (s.groups.size() != groups_for(shape.rows[i])) return std::unexpected(manifest_error::groups);
    }
    return {};
}
} // namespace

std::expected<void, manifest_error> validate_program_shape(const description &d) noexcept {
    const auto shape = analyze(d, 0);
    if (!shape) return std::unexpected(shape.error());
    return {};
}

std::string_view describe(manifest_error error) noexcept {
    switch (error) {
    case manifest_error::description: return "invalid canonical program";
    case manifest_error::unsupported: return "program shape has no admitted manifest recipe";
    case manifest_error::range: return "root extent or requested rows are out of range";
    case manifest_error::streams: return "missing, duplicate or unexpected stream node identity";
    case manifest_error::rows: return "stream row count disagrees with the coordinate law";
    case manifest_error::groups: return "stream group count disagrees with its row count";
    case manifest_error::placement: return "section does not fit its declared owner Part";
    case manifest_error::overlap: return "distinct owning sections overlap";
    case manifest_error::unavailable: return "required section lacks owner availability";
    case manifest_error::resources: return "manifest preparation allocation refused";
    }
    return "unknown manifest error";
}

std::expected<void, manifest_error> validate_manifest(const description &d, std::size_t rows,
    std::span<const std::size_t> parts, std::span<const stream_manifest> streams) noexcept try {
    const auto shape = analyze(d, rows);
    if (!shape) return std::unexpected(shape.error());
    if (auto checked = check_streams(*shape, streams); !checked) return checked;
    std::array<section_location, maximum_groups * 2> seen{};
    std::size_t count = 0;
    for (const auto &s : streams)
        for (const auto &section : s.groups) {
            if (section.owner_part >= parts.size() || !section.extent)
                return std::unexpected(manifest_error::placement);
            const auto part_bytes = parts[section.owner_part];
            if (section.offset > part_bytes || section.extent > part_bytes - section.offset)
                return std::unexpected(manifest_error::placement);
            for (std::size_t i = 0; i < count; ++i) {
                const auto &other = seen[i];
                if (section.owner_part == other.owner_part && section.offset < other.offset + other.extent &&
                    other.offset < section.offset + section.extent)
                    return std::unexpected(manifest_error::overlap);
            }
            seen[count++] = section;
        }
    return {};
} catch (const std::bad_alloc &) { return std::unexpected(manifest_error::resources); }

std::expected<std::vector<section_key>, manifest_error> required_sections(const description &d,
    std::size_t rows, std::span<const stream_manifest> streams, std::span<const row_range> ranges) try {
    const auto shape = analyze(d, rows);
    if (!shape) return std::unexpected(shape.error());
    if (auto checked = check_streams(*shape, streams); !checked) return std::unexpected(checked.error());
    // Check the entire request before making a result, using subtraction so
    // hostile first+count values cannot wrap into an apparently valid window.
    for (const auto &range : ranges)
        if (range.first > rows || range.count > rows - range.first)
            return std::unexpected(manifest_error::range);
    std::array<std::array<bool, maximum_groups>, 2> needed{};
    for (const auto &range : ranges) {
        if (!range.count) continue;
        const auto last = range.first + range.count - 1;
        for (auto g = range.first / group_rows; g <= last / group_rows; ++g) needed[0][g] = true;
        if (shape->streams == 2)
            for (auto g = (range.first / shape->spacing) / group_rows;
                 g <= (last / shape->spacing) / group_rows; ++g) needed[1][g] = true;
    }
    std::vector<section_key> result;
    for (std::size_t i = 0; i < shape->streams; ++i)
        for (std::size_t g = 0; g < groups_for(shape->rows[i]); ++g)
            if (needed[i][g]) result.push_back({shape->roots[i], g});
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.root_node < b.root_node || (a.root_node == b.root_node && a.group < b.group);
    });
    return result;
} catch (const std::bad_alloc &) { return std::unexpected(manifest_error::resources); }

std::expected<void, manifest_error> admit_sections(std::span<const section_key> required,
    std::span<const section_key> available) noexcept {
    for (const auto &key : required)
        if (std::find(available.begin(), available.end(), key) == available.end())
            return std::unexpected(manifest_error::unavailable);
    return {};
}
} // namespace frame3_owner
