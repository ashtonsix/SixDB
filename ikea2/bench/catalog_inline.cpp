#include "pipeline_recipe.h"
namespace pp = pipeline_probe;
using Frame = pp::frame<32>;
using Function = std::uint64_t (*)(Frame&, const Frame::Plan&, std::size_t);
// 2 physical sources × 3 transforms × 3 predicates × 2 consumers. Parameters
// stay dynamic in the frame; only operation identity is specialized.
static const auto recipes = [] {
    std::array<Function, 36> functions;
    ikea2::seriespack::detail::each<36>([&](auto i) {
        functions[i] =
            &pp::execute<32, i / 18, (i / 6) % 3, (i / 2) % 3, i % 2, pp::mode::inline_stages>;
    });
    return functions;
}();
std::uint64_t catalog_inline(unsigned recipe, Frame& frame, const Frame::Plan& unused,
                             std::size_t count) {
    return recipes[recipe](frame, unused, count);
}
