#include <ikea/frame/description.h>
#include <ikea/frame/detail/recipe.h>

#include <ikea/description.h>

#include <array>
#include <utility>

namespace ikea::frame {
namespace {
constexpr ikea::description::tag module_tag{'F', 'r'};
constexpr unsigned version = 1;
constexpr std::size_t body_header = 9, node_header = 6, edge_bytes = 3;
static_assert(body_header + maximum_nodes * node_header + maximum_edges * edge_bytes <= ikea::description::maximum_body_size);
enum class output_kind { series, bitmap, record };

bool known(node_kind kind) noexcept {
    const auto k = static_cast<unsigned>(kind);
    return k >= static_cast<unsigned>(node_kind::pack) && k <= static_cast<unsigned>(node_kind::runs);
}
bool known(coordinate mapping) noexcept {
    return static_cast<unsigned>(mapping) <= static_cast<unsigned>(coordinate::run_ordinal);
}
output_kind output(node_kind kind) noexcept {
    return kind == node_kind::bec ? output_kind::bitmap :
           kind == node_kind::tuple_record ? output_kind::record : output_kind::series;
}
std::size_t size_unchecked(const description &d) noexcept {
    std::size_t bytes = ikea::description::header_size + body_header + node_header * d.nodes.size();
    for (const node &n : d.nodes)
        bytes += edge_bytes * n.children.size();
    return bytes;
}
struct writer {
    std::span<std::byte> bytes;
    std::size_t at{};
    void u8(unsigned x) noexcept { bytes[at++] = std::byte(x); }
    void u16(unsigned x) noexcept { u8(x & 255), u8(x >> 8); }
};
struct reader {
    std::span<const std::byte> bytes;
    std::size_t at{};
    bool u8(unsigned &x) noexcept {
        if (at == bytes.size()) return false;
        x = std::to_integer<unsigned>(bytes[at++]);
        return true;
    }
    bool u16(unsigned &x) noexcept {
        if (bytes.size() - at < 2) return false;
        x = std::to_integer<unsigned>(bytes[at]) | (std::to_integer<unsigned>(bytes[at + 1]) << 8);
        at += 2;
        return true;
    }
};
} // namespace

std::string_view describe(description_error e) noexcept {
    switch (e) {
    case description_error::envelope: return "wrong description tag or envelope";
    case description_error::version: return "unknown description version";
    case description_error::domain: return "unsupported logical domain";
    case description_error::geometry: return "invalid block/group geometry";
    case description_error::limit: return "description exceeds supported bounds";
    case description_error::kind: return "unknown node kind";
    case description_error::parameters: return "invalid node parameters or arity";
    case description_error::coordinates: return "invalid child coordinate mapping or output kind";
    case description_error::topology: return "dangling, cyclic or non-topological child identity";
    case description_error::root: return "invalid root identity or output kind";
    case description_error::unreachable: return "unreachable node identity";
    case description_error::truncated: return "truncated description";
    case description_error::trailing: return "trailing description bytes";
    case description_error::capacity: return "description output is too short";
    case description_error::unsupported: return "description has no executable binding in this implementation";
    }
    return "unknown description error";
}

std::expected<void, description_error> validate(const description &d) noexcept {
    if (d.domain != logical_domain::uint64) return std::unexpected(description_error::domain);
    if (d.block_rows != 256 || d.group_blocks != 16) return std::unexpected(description_error::geometry);
    if (d.nodes.empty() || d.nodes.size() > maximum_nodes) return std::unexpected(description_error::limit);
    if (d.root >= d.nodes.size()) return std::unexpected(description_error::root);
    std::size_t edges = 0;
    for (std::size_t i = 0; i < d.nodes.size(); ++i) {
        const node &n = d.nodes[i];
        if (!known(n.kind)) return std::unexpected(description_error::kind);
        if (n.heads && (n.kind != node_kind::pack || (n.heads != 8 && n.heads != 16)))
            return std::unexpected(description_error::parameters);
        if (n.children.size() > maximum_children || n.children.size() > maximum_edges - edges)
            return std::unexpected(description_error::limit);
        edges += n.children.size();
        for (const edge &e : n.children) {
            if (e.child >= i) return std::unexpected(description_error::topology);
            if (!known(e.mapping)) return std::unexpected(description_error::coordinates);
        }
        auto shape = [&](std::size_t arity) { return n.children.size() == arity; };
        auto child = [&](unsigned j, coordinate mapping, output_kind kind) {
            const edge &e = n.children[j];
            return e.mapping == mapping && output(d.nodes[e.child].kind) == kind;
        };
        switch (n.kind) {
        case node_kind::pack:
            if (n.parameter > static_cast<unsigned>(pack_preset::bulk_arm) || !shape(0))
                return std::unexpected(description_error::parameters);
            break;
        case node_kind::bound:
            if (n.parameter || !shape(1)) return std::unexpected(description_error::parameters);
            if (!child(0, coordinate::identity, output_kind::series)) return std::unexpected(description_error::coordinates);
            break;
        case node_kind::intervals_sizes:
        case node_kind::intervals_relative_ends:
            if (n.parameter < 2 || n.parameter > d.block_rows || d.block_rows % n.parameter || !shape(2))
                return std::unexpected(description_error::parameters);
            if (!child(0, coordinate::identity, output_kind::series) ||
                !child(1, coordinate::checkpoints, output_kind::series)) return std::unexpected(description_error::coordinates);
            break;
        case node_kind::bec:
            if ((n.parameter != 256 && n.parameter != 4096) || !shape(0)) return std::unexpected(description_error::parameters);
            break;
        case node_kind::presence:
            if (n.parameter || !shape(2)) return std::unexpected(description_error::parameters);
            if (!child(0, coordinate::identity, output_kind::bitmap) ||
                !child(1, coordinate::present_rank, output_kind::series)) return std::unexpected(description_error::coordinates);
            break;
        case node_kind::tuple_record:
            if (!n.parameter || n.parameter > 64 || !shape(0)) return std::unexpected(description_error::parameters);
            break;
        case node_kind::tuple_field:
            if (!shape(1)) return std::unexpected(description_error::parameters);
            if (!child(0, coordinate::record_field, output_kind::record)) return std::unexpected(description_error::coordinates);
            if (n.parameter >= d.nodes[n.children[0].child].parameter) return std::unexpected(description_error::parameters);
            break;
        case node_kind::split:
            if (!n.parameter || n.parameter >= 64 || !shape(2)) return std::unexpected(description_error::parameters);
            if (!child(0, coordinate::identity, output_kind::series) ||
                !child(1, coordinate::identity, output_kind::series)) return std::unexpected(description_error::coordinates);
            break;
        case node_kind::runs:
            if (n.parameter || !shape(2)) return std::unexpected(description_error::parameters);
            if (!child(0, coordinate::run_ordinal, output_kind::series) ||
                !child(1, coordinate::run_ordinal, output_kind::series)) return std::unexpected(description_error::coordinates);
            break;
        }
    }
    if (output(d.nodes[d.root].kind) != output_kind::series) return std::unexpected(description_error::root);
    std::array<bool, maximum_nodes> reached{};
    reached[d.root] = true;
    for (std::size_t i = d.nodes.size(); i-- > 0;)
        if (reached[i])
            for (const edge &e : d.nodes[i].children) reached[e.child] = true;
    for (std::size_t i = 0; i < d.nodes.size(); ++i)
        if (!reached[i]) return std::unexpected(description_error::unreachable);
    return {};
}

std::expected<std::size_t, description_error> encoded_size(const description &d) noexcept {
    if (auto v = validate(d); !v) return std::unexpected(v.error());
    return size_unchecked(d);
}
std::expected<std::size_t, description_error> encode(const description &d, std::span<std::byte> output) noexcept {
    const auto bytes = encoded_size(d);
    if (!bytes) return std::unexpected(bytes.error());
    if (output.size() < *bytes) return std::unexpected(description_error::capacity);
    const auto body_size = *bytes - ikea::description::header_size;
    if (!ikea::description::write_header(output, module_tag, version, body_size)) return std::unexpected(description_error::limit);
    writer w{output, ikea::description::header_size};
    w.u8(static_cast<unsigned>(d.domain));
    w.u16(d.block_rows), w.u16(d.group_blocks), w.u16(static_cast<unsigned>(d.nodes.size())), w.u16(d.root);
    for (const node &n : d.nodes) {
        w.u8(static_cast<unsigned>(n.kind));
        w.u16(n.parameter), w.u16(n.heads), w.u8(static_cast<unsigned>(n.children.size()));
        for (const edge &e : n.children)
            w.u16(e.child), w.u8(static_cast<unsigned>(e.mapping));
    }
    return *bytes;
}
std::expected<std::vector<std::byte>, description_error> encode(const description &d) {
    const auto bytes = encoded_size(d);
    if (!bytes) return std::unexpected(bytes.error());
    std::vector<std::byte> output(*bytes);
    if (auto result = encode(d, output); !result) return std::unexpected(result.error());
    return output;
}

std::expected<description, description_error> decode(std::span<const std::byte> input) {
    if (input.size() < ikea::description::header_size) return std::unexpected(description_error::truncated);
    if (input[0] != std::byte('F') || input[1] != std::byte('r')) return std::unexpected(description_error::envelope);
    if (input[2] != std::byte(version)) return std::unexpected(description_error::version);
    const std::size_t length = std::to_integer<unsigned>(input[3]) | (std::to_integer<unsigned>(input[4]) << 8);
    const std::size_t actual = input.size() - ikea::description::header_size;
    if (actual < length) return std::unexpected(description_error::truncated);
    if (actual > length) return std::unexpected(description_error::trailing);
    const auto body = ikea::description::read_body(input, module_tag, version);
    if (!body) return std::unexpected(description_error::envelope);
    reader r{*body};
    unsigned domain, block_rows, group_blocks, nodes, root;
    if (!r.u8(domain) || !r.u16(block_rows) || !r.u16(group_blocks) || !r.u16(nodes) || !r.u16(root))
        return std::unexpected(description_error::truncated);
    if (!nodes || nodes > maximum_nodes) return std::unexpected(description_error::limit);
    if (nodes > (r.bytes.size() - r.at) / node_header) return std::unexpected(description_error::truncated);
    description d{static_cast<logical_domain>(domain), static_cast<std::uint16_t>(block_rows),
                  static_cast<std::uint16_t>(group_blocks), static_cast<std::uint16_t>(root), {}};
    d.nodes.reserve(nodes);
    std::size_t edges = 0;
    for (unsigned i = 0; i < nodes; ++i) {
        unsigned kind, parameter, heads, children;
        if (!r.u8(kind) || !r.u16(parameter) || !r.u16(heads) || !r.u8(children))
            return std::unexpected(description_error::truncated);
        if (!known(static_cast<node_kind>(kind))) return std::unexpected(description_error::kind);
        if (children > maximum_children || children > maximum_edges - edges) return std::unexpected(description_error::limit);
        edges += children;
        if (children > (r.bytes.size() - r.at) / edge_bytes) return std::unexpected(description_error::truncated);
        node n{static_cast<node_kind>(kind), static_cast<std::uint16_t>(parameter), static_cast<std::uint16_t>(heads), {}};
        n.children.reserve(children);
        for (unsigned j = 0; j < children; ++j) {
            unsigned id, mapping;
            if (!r.u16(id) || !r.u8(mapping)) return std::unexpected(description_error::truncated);
            n.children.push_back({static_cast<std::uint16_t>(id), static_cast<coordinate>(mapping)});
        }
        d.nodes.push_back(std::move(n));
    }
    if (r.at != r.bytes.size()) return std::unexpected(description_error::trailing);
    if (auto v = validate(d); !v) return std::unexpected(v.error());
    return d;
}

namespace detail {
std::expected<bound_pack_recipe, description_error> binding_recipe(const description &d) noexcept {
    return binding_recipe(d, d.root);
}
std::expected<bound_pack_recipe, description_error> binding_recipe(const description &d, std::uint16_t root_node) noexcept {
    if (auto v = validate(d); !v) return std::unexpected(v.error());
    if (root_node >= d.nodes.size()) return std::unexpected(description_error::root);
    const node *leaf = &d.nodes[root_node];
    bool bounded = leaf->kind == node_kind::bound;
    if (bounded) leaf = &d.nodes[leaf->children[0].child];
    if (leaf->kind != node_kind::pack || leaf->heads != 0) return std::unexpected(description_error::unsupported);
    return bound_pack_recipe{static_cast<pack_preset>(leaf->parameter), bounded};
}
} // namespace detail
} // namespace ikea::frame
