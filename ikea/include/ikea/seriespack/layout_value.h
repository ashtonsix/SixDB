#pragma once
#include <ikea/bytes.h>
#include <ikea/seriespack/layout.h>
#include <ikea/seriespack/status.h>
#include <expected>

namespace ikea::seriespack {
/// Shared byte law. Logical count, addresses and actual plane strides
/// belong to the parent placement, not to this reusable layout description.
struct layout_value {
    unsigned width, heads;
    geometry storage;
    bool operator==(const layout_value&) const = default;
    template <class L> static constexpr layout_value of() noexcept {
        return {L::width, L::heads, L::storage};
    }
    static constexpr std::size_t encoded_size() noexcept { return 8; }
    [[nodiscard]] std::expected<std::size_t, error> encode(ikea::mutable_bytes) const;
    [[nodiscard]] static std::expected<layout_value, error> decode(ikea::bytes);
};
constexpr bool valid(const layout_value& law) {
    return law.width >= 1 && law.width <= 64 &&
           (law.heads == 0 || law.heads == 8 || law.heads == 16) && law.heads <= law.width &&
           (law.storage == geometry::local ||
            (law.storage == geometry::striped && striped_width(law.width - law.heads)));
}
} // namespace ikea::seriespack
