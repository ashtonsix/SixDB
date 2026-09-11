#include "pipeline_recipe.h"
#include <optional>
namespace pp = pipeline_probe;
using Frame = pp::frame<32>;
static const auto recipes = [] {
    std::array<std::optional<Frame::Plan>, 36> plans;
    ikea::seriespack::detail::each<36>(
        [&](auto i) { plans[i].emplace(Frame::bind<i / 18, (i / 6) % 3, (i / 2) % 3, i % 2>()); });
    return plans;
}();
std::uint64_t catalog_cps(unsigned recipe, Frame& frame, std::size_t count) {
    return pp::execute_cps(frame, *recipes[recipe], count);
}
