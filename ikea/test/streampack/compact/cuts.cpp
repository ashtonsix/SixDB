#include <ikea/streampack/detail/compact/read.h>
#include <ikea/streampack/detail/compact/write.h>
#include "../../intervals/cuts_model.h"
#include <ikea/effects.h>
#include <array>
#include <cassert>
#include <string_view>
#include <vector>

namespace {
namespace bl = ikea::streampack::detail::compact;
struct packed {
    bl::layout format;
    alignas(64) std::array<std::byte, 4096> directory{}, lengths{};
    std::array<std::byte, 6> control{};
    bl::view source;
    packed(bl::geometry family, const std::vector<unsigned> &cuts, unsigned dead)
        : format(*bl::layout::prepare({family, 2, 512, {}, false})),
          source(*bl::view::bind(format, {std::span(directory).first(format.directory_bytes()),
                                         std::span(lengths).first(format.lengths_bytes()), control})) {
        ikea::no_coverage effects;
        assert(bl::initialize(source, effects));
        for (unsigned r = 0; r + 1 < cuts.size(); ++r)
            assert(bl::append(source, cuts[r + 1] - cuts[r], effects));
        for (unsigned r = 0; r + 1 < cuts.size(); ++r)
            if (dead & (1u << r)) assert(bl::retire(source, r, effects));
    }
};
void exhaustive() {
    for (auto family : {bl::geometry::absolute_ends, bl::geometry::sizes, bl::geometry::relative_ends})
        for (unsigned encoded = 0; encoded < 81; ++encoded) {
            unsigned value = encoded;
            std::vector<unsigned> cuts{0};
            for (unsigned r = 0; r < 4; ++r) { cuts.push_back(cuts.back() + value % 3); value /= 3; }
            for (unsigned mask = 0; mask < 16; ++mask) {
                packed f(family, cuts, mask);
                cuts_model::check(f.source, cuts, 0); // retirement never changes raw geometry
                const bl::selected_source live{f.source, f.source.live_ranks()};
                cuts_model::check(live, cuts, mask);
            }
        }
}
}
int main() { exhaustive(); }
