#pragma once
// Pre-extraction native inverse body, frozen from4fb8fcea. Prefix kernel bytes
// remain shared unchanged; routing is the actual old branching bisection.
#include <ikea/streampack/author/read.h>
namespace boundlist_legacy {
template <class Access> [[gnu::always_inline]] ikea::streampack::byte_owner reverse(Access a, unsigned offset) noexcept {
    using Offset = typename Access::offset_type;
        unsigned rank;
        if (a.format().fixed())
            rank = offset / a.format().fixed_width();
        else if constexpr (Access::family == ikea::streampack::geometry::direct) {
            unsigned lo = 0, hi = a.size();
            while (lo < hi) {
                const auto mid = (lo + hi) / 2;
                if (ikea::boundlist::detail::load<Offset>(a.geometry_data() + mid * sizeof(Offset)) <= offset)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            rank = lo;
        } else {
            unsigned lo = 0, hi = (a.size() + 15) / 16;
            while (lo < hi) {
                const auto mid = (lo + hi) / 2;
                if (ikea::boundlist::detail::load<Offset>(a.geometry_data() + mid * sizeof(Offset)) <= offset)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            const auto group = lo - 1;
            const auto relative =
                offset - ikea::boundlist::detail::load<Offset>(a.geometry_data() + group * sizeof(Offset));
            rank = group * 16 + ikea::boundlist::detail::owner16_native(a.widths(group), relative);
        }
        return {std::uint16_t(rank), !a.live(rank)};
    }
} // namespace boundlist_legacy
