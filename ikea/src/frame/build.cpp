#include <ikea/frame/build.h>
#include <ikea/frame/detail/recipe.h>
#include <ikea/frame/detail/stages.h>
#include <ikea/frame/image.h>

#include <new>

namespace ikea::frame {
namespace detail {
namespace {
std::expected<std::unique_ptr<author::stage>, error> basic(const description &d, std::uint16_t node,
                                                           const fitting_policy &policy) {
    const auto recipe = binding_recipe(d, node);
    if (!recipe)
        return std::unexpected(error::unsupported);
    auto leaf = make_pack(recipe->preset);
    if (recipe->bounded)
        leaf = make_bound(std::move(leaf), policy);
    return leaf;
}
} // namespace

std::expected<std::unique_ptr<author::stage>, error> make_stages(const description &d, const fitting_policy &policy) try {
    if (!validate(d))
        return std::unexpected(error::description);
    if (!validate_program_shape(d))
        return std::unexpected(error::unsupported);
    const node &root = d.nodes[d.root];
    if (!is_intervals(root.kind))
        return basic(d, d.root, policy);
    auto fields = basic(d, root.children[0].child, policy);
    if (!fields)
        return fields;
    auto checkpoints = basic(d, root.children[1].child, policy);
    if (!checkpoints)
        return checkpoints;
    const auto form = root.kind == node_kind::intervals_relative_ends ? interval_form::relative_ends : interval_form::sizes;
    if (root.parameter < 2 || block_rows % root.parameter)
        return std::unexpected(error::unsupported);
    return make_intervals(std::move(*fields), std::move(*checkpoints), root.parameter, form);
} catch (const std::bad_alloc &) {
    return std::unexpected(error::resources);
}
} // namespace detail

std::expected<built_unit, error> build(const description &d, std::span<const std::uint64_t> values,
                                       const fitting_policy &policy) try {
    if (values.size() > unit_rows)
        return std::unexpected(error::range);
    auto root = detail::make_stages(d, policy);
    if (!root)
        return std::unexpected(root.error());
    author::stage &tree = **root;
    if (auto fitted = tree.fit(0, {values}); !fitted)
        return std::unexpected(fitted.error());
    built_unit out;
    // Stream identities follow the description: the root stream is the root
    // node, and intervals' checkpoint stream its second child's node.
    std::vector<std::pair<std::string, const author::stage *>> streams{{"", &tree}};
    tree.streams("", streams);
    const node &top = d.nodes[d.root];
    for (const auto &[path, s] : streams) {
        stream_images images;
        images.path = path;
        images.root_node = s == &tree ? d.root : top.children[1].child;
        images.rows = s->rows(0);
        images.groups.resize(s->groups_in(0));
        for (unsigned g = 0; g < images.groups.size(); ++g)
            s->emit(0, g, images.groups[g]);
        out.streams.push_back(std::move(images));
    }
    tree.describe(0, out.census);
    return out;
} catch (const std::bad_alloc &) {
    return std::unexpected(error::resources);
}

} // namespace ikea::frame
