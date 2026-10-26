#include <ikea/frame/detail/execution.h>
#include <ikea/frame/detail/stages.h>

#include <algorithm>

namespace ikea::frame::detail {

std::optional<point_plan> point_plan::make(const author::stage &root, unsigned unit) {
    if (!is_bound(root))
        return std::nullopt;
    point_plan plan;
    plan.leaves_.push_back({}); // leaf 0: width 0, no body
    std::vector<std::pair<const void *, std::uint16_t>> seen;
    for (unsigned b = 0; b < root.blocks_in(unit); ++b) {
        bound_point_entry e;
        if (!bound_entry(root, unit, b, e) || e.count > 255 || e.leaf.base > ~std::uint32_t{0})
            return std::nullopt;
        std::uint16_t leaf = 0;
        if (e.leaf.get) {
            auto it = std::find_if(seen.begin(), seen.end(), [&](const auto &x) { return x.first == e.leaf.resource; });
            if (it == seen.end()) {
                if (!admits(plan.leaves_.size() + 1, plan.pos_.size()))
                    return std::nullopt;
                seen.emplace_back(e.leaf.resource, static_cast<std::uint16_t>(plan.leaves_.size()));
                plan.leaves_.push_back(e.leaf);
                it = seen.end() - 1;
            }
            leaf = it->second;
        }
        if (!admits(plan.leaves_.size(), plan.pos_.size() + e.count))
            return std::nullopt;
        plan.entries_.push_back({e.lo, static_cast<std::uint32_t>(e.leaf.base), static_cast<std::uint32_t>(plan.pos_.size()),
                                 leaf, static_cast<std::uint8_t>(e.count)});
        plan.pos_.insert(plan.pos_.end(), e.positions, e.positions + e.count);
        plan.val_.insert(plan.val_.end(), e.values, e.values + e.count);
    }
    return plan;
}

} // namespace ikea::frame::detail
