#include "binding.h"
#include "../proto/stages.h"
#include <limits>
#include <new>

namespace frame3_owner {
namespace {
image_preset image_layout(pack_preset p) {
    switch (p) {
    case pack_preset::compact: return image_preset::compact;
    case pack_preset::bulk_x86: return image_preset::bulk_x86;
    case pack_preset::bulk_arm: return image_preset::bulk_arm;
    }
    return image_preset::compact;
}
frame3::bitset256 group_blocks(unsigned group, std::size_t rows) {
    frame3::bitset256 blocks;
    for (unsigned b = group * 16; b < (group + 1) * 16 && std::size_t{b} * 256 < rows; ++b) blocks.set(b);
    return blocks;
}
} // namespace
std::expected<std::unique_ptr<frame3::stage>, binding_error> make_tree(const description &d, fitting_policy policy) try {
    const auto r = binding_recipe(d);
    if (!r) return std::unexpected(binding_error::description);
    if (policy.exception_cap > 256 || policy.slack > 4096) return std::unexpected(binding_error::resources);
    using namespace frame3;
    const auto preset = r->preset == pack_preset::compact ? sp::preset::compact :
        r->preset == pack_preset::bulk_x86 ? sp::preset::bulk_x86 : sp::preset::bulk_arm;
    if (!width_preset_available(preset)) return std::unexpected(binding_error::unsupported);
    std::unique_ptr<stage> leaf;
    switch (r->preset) {
    case pack_preset::compact: leaf = std::make_unique<pack<sp::preset::compact>>(16); break;
    case pack_preset::bulk_x86: leaf = std::make_unique<pack<sp::preset::bulk_x86>>(16); break;
    case pack_preset::bulk_arm: leaf = std::make_unique<pack<sp::preset::bulk_arm>>(16); break;
    }
    if (r->bounded) leaf = std::make_unique<bound>(std::move(leaf), policy.penalty, policy.exception_cap, policy.slack);
    return leaf;
} catch (const std::bad_alloc &) { return std::unexpected(binding_error::resources); }
binding::binding(std::unique_ptr<frame3::stage> root, std::size_t rows, frame3::bitset256 blocks)
    : root_(std::move(root)), rows_(rows), available_(blocks) {}
bool binding::admitted(const frame3::bitset256 &blocks) const {
    return frame3::operator&(blocks, available_) == blocks;
}
std::expected<std::unique_ptr<binding>, binding_error> binding::attach(const description &d,
    std::size_t rows, std::span<const borrowed_group> groups) try {
    if (rows == 0 || rows > 65536) return std::unexpected(binding_error::range);
    const auto recipe = binding_recipe(d);
    if (!recipe) return std::unexpected(binding_error::description);
    auto root = make_tree(d);
    if (!root) return std::unexpected(root.error());
    frame3::bitset256 available;
    unsigned seen = 0;
    for (const auto &g : groups) {
        if (g.group >= (rows + 4095) / 4096 || (seen & (1u << g.group)))
            return std::unexpected(binding_error::range);
        if (!validate_group(g.image, image_layout(recipe->preset), recipe->bounded, rows, g.group))
            return std::unexpected(binding_error::image);
        seen |= 1u << g.group;
    }
    for (const auto &g : groups) {
        if ((*root)->attach(0, rows, g.group, g.image, {}) != g.image.size())
            return std::unexpected(binding_error::image);
        const auto blockset = group_blocks(g.group, rows);
        for (unsigned w = 0; w < 4; ++w) available.words[w] |= blockset.words[w];
    }
    return std::unique_ptr<binding>(new binding(std::move(*root), rows, available));
} catch (const std::bad_alloc &) { return std::unexpected(binding_error::resources); }
std::expected<frame3::u64, binding_error> binding::get(unsigned row) const {
    if (row >= rows_) return std::unexpected(binding_error::range);
    if (!available_.contains(row / 256)) return std::unexpected(binding_error::unavailable);
    return root_->get(0, row);
}
std::expected<void, binding_error> binding::read(unsigned first, std::span<frame3::u64> out) const {
    if (first > rows_ || out.size() > rows_ - first) return std::unexpected(binding_error::range);
    if (out.empty()) return {};
    for (unsigned b = first / 256; b <= (first + out.size() - 1) / 256; ++b)
        if (!available_.contains(b)) return std::unexpected(binding_error::unavailable);
    root_->read(0, first, static_cast<unsigned>(out.size()), out.data());
    return {};
}
std::expected<frame3::u128, binding_error> binding::sum(const frame3::bitset256 &blocks) const {
    if (!admitted(blocks)) return std::unexpected(binding_error::unavailable);
    return blocks.empty() ? 0 : root_->sum(0, blocks);
}
std::expected<frame3::u64, binding_error> binding::count(ikea::comparison op, const frame3::bitset256 &blocks, frame3::u64 t) const {
    if (!admitted(blocks)) return std::unexpected(binding_error::unavailable);
    return blocks.empty() ? 0 : root_->count_uniform(op, 0, blocks, t);
}
std::expected<frame3::u128, binding_error> binding::sum_selected(const frame3::bitset256 &blocks,
    const frame3::block_rows_set &selected) const {
    if (!admitted(blocks)) return std::unexpected(binding_error::unavailable);
    bool valid = true;
    frame3::each(blocks, [&](unsigned b) {
        const auto n = static_cast<unsigned>(std::min(std::size_t{256}, rows_ - std::size_t{b} * 256));
        if (!(selected[b] == frame3::operator&(selected[b], frame3::first_n(n)))) valid = false;
    });
    if (!valid) return std::unexpected(binding_error::range);
    return blocks.empty() ? 0 : root_->sum_selected(0, blocks, selected);
}

std::expected<prepared_group, binding_error> prepare_group(const description &d, std::size_t rows, unsigned group,
    std::span<const std::byte> source, std::span<const command> commands, preparation_budget budget, fitting_policy policy) try {
    if (!budget.source_readable) return std::unexpected(binding_error::authority);
    if (source.size() > budget.source_copy_bytes || commands.size() > budget.commands)
        return std::unexpected(binding_error::resources);
    if (rows == 0 || rows > 65536 || group >= (rows + 4095) / 4096) return std::unexpected(binding_error::range);
    for (auto c : commands)
        if (c.row >= rows || c.row / 4096 != group) return std::unexpected(binding_error::range);
    const auto recipe = binding_recipe(d);
    if (!recipe) return std::unexpected(binding_error::description);
    if (!validate_group(source, image_layout(recipe->preset), recipe->bounded, rows, group))
        return std::unexpected(binding_error::image);
    auto tree = make_tree(d, policy);
    if (!tree) return std::unexpected(tree.error());
    std::vector<std::byte> draft(source.begin(), source.end());
    (*tree)->attach(0, rows, group, draft, draft);
    auto &root = **tree;
    if (budget.effect_records > std::vector<ikea::journal::entry>{}.max_size())
        return std::unexpected(binding_error::resources);
    std::vector<ikea::journal::entry> records(budget.effect_records);
    prepared_group result;
    result.copied_bytes = source.size();
    const unsigned first = group * 4096;
    const auto n = static_cast<unsigned>(std::min(std::size_t{4096}, rows - first));
    for (auto c : commands) {
        const auto old = root.get(0, c.row);
        if (root.classify(0, c.row, c.value) == frame3::verdict::refuse) {
            if (!budget.allow_rebuild || !root.rebuilds_groups()) return std::unexpected(binding_error::unsupported);
            std::vector<frame3::u64> values(n);
            root.read(0, first, n, values.data());
            values[c.row - first] = c.value;
            if (!root.rebuild(0, group * 16, {values})) return std::unexpected(binding_error::unsupported);
            ++result.rebuilds;
        } else {
            frame3::envelope declared;
            root.declare(0, c.row, c.value, declared);
            if (declared.records > budget.effect_records) return std::unexpected(binding_error::effects);
            ikea::journal effects{records};
            root.apply(0, c.row, c.value, effects);
            for (const auto &effect : effects.entries()) {
                const auto *at = root.resolve(effect);
                if (!at) return std::unexpected(binding_error::effects);
                frame_change::ranges issued;
                issued.add(at, effect.size);
                if (!declared.stores.contains(issued)) return std::unexpected(binding_error::effects);
                result.issued_bytes += effect.size;
            }
            result.records += effects.entries().size();
        }
        result.contribution += static_cast<__int128>(c.value) - static_cast<__int128>(old);
    }
    std::vector<frame3::u64> values(n);
    root.read(0, first, n, values.data());
    result.min = ~frame3::u64{0};
    for (auto v : values) result.sum += v, result.min = std::min(result.min, v), result.max = std::max(result.max, v);
    root.emit(0, group, result.image);
    if (result.image.size() > budget.output_bytes) return std::unexpected(binding_error::resources);
    if (!validate_group(result.image, image_layout(recipe->preset), recipe->bounded, rows, group))
        return std::unexpected(binding_error::image);
    result.new_bytes = result.image.size();
    return result;
} catch (const std::bad_alloc &) { return std::unexpected(binding_error::resources); }
} // namespace frame3_owner
