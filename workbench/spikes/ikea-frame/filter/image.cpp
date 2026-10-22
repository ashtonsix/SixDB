#include "image.h"
#include <array>
#include <cassert>

namespace frame_filter {
namespace fe = frame_encoding;
image::image(const fe::column &original, unsigned heads, split_stage stage)
    : original_(original), split_(original.blocks.size()), stage_(stage) {
    assert(heads == 0 || heads == 8 || heads == 16);
    std::array<u64, fe::block_rows> residuals;
    for (std::size_t b = 0; b < split_.size(); ++b) {
        const auto &d = original.blocks[b];
        if (d.type != fe::kind::packed && d.type != fe::kind::runs) continue;
        auto &s = split_[b];
        s.width = stage == split_stage::after_for ? d.width : 64;
        s.heads = static_cast<std::uint8_t>(s.width >= heads ? heads : 0);
        if (payload_.size() > UINT32_MAX || head0_.size() > UINT32_MAX || head1_.size() > UINT32_MAX)
            std::abort();
        s.payload_offset = static_cast<std::uint32_t>(payload_.size());
        s.head0_offset = static_cast<std::uint32_t>(head0_.size());
        s.head1_offset = static_cast<std::uint32_t>(head1_.size());
        payload_.resize(payload_.size() + fe::plane_bytes(s.width - s.heads, d.count));
        if (s.heads >= 8) head0_.resize(head0_.size() + fe::plane_bytes(8, d.count));
        if (s.heads == 16) head1_.resize(head1_.size() + fe::plane_bytes(8, d.count));
        original.decoders[b].read_unchecked(0, d.count, residuals.data());
        if (stage == split_stage::working64)
            for (unsigned r = 0; r < d.count; ++r) residuals[r] += d.base;
        kernels(s.width, s.heads).pack({residuals.data(), d.count},
            payload_.empty() ? nullptr : payload_.data() + s.payload_offset,
            s.heads >= 8 ? head0_.data() + s.head0_offset : nullptr,
            s.heads == 16 ? head1_.data() + s.head1_offset : nullptr);
    }
}
u64 image::count_ge(const fe::block_mask &mask, u64 threshold, bool guard) const {
    u64 result = 0;
    fe::for_each_block(mask, [&](std::size_t b) __attribute__((always_inline)) {
        const auto &d = original_.blocks[b];
        if (d.type == fe::kind::constant) {
            result += d.base >= threshold ? fe::block_rows : 0;
            return;
        }
        if (d.type != fe::kind::packed && d.type != fe::kind::runs) {
            // Prediction residual heads are not logical-value heads. Keep the
            // owner's actual operation; do not force complete reconstruction
            // for a patched body its existing predicate can answer directly.
            result += fe::block_count_ge(original_, b, threshold);
            return;
        }
        if (d.base >= threshold) { result += fe::block_rows; return; }
        const auto residual_rhs = threshold - d.base;
        if (d.width < 64 && residual_rhs >> d.width) return;
        const auto rhs = stage_ == split_stage::after_for ? residual_rhs : threshold;
        const auto &s = split_[b];
        const auto &ops = kernels(s.width, s.heads);
        const planes source{payload_.empty() ? nullptr : payload_.data() + s.payload_offset,
                            s.heads >= 8 ? head0_.data() + s.head0_offset : nullptr,
                            s.heads == 16 ? head1_.data() + s.head1_offset : nullptr, d.count};
        std::array<unsigned, fe::block_rows> sizes;
        const bool compact_runs = d.type == fe::kind::runs && bool(original_.runs[b]);
        if (compact_runs)
            fe::each_run(original_, b, [&](unsigned rank, unsigned n) { sizes[rank] = n; });
        const auto *ends = original_.payload.data() + d.offset + fe::plane_bytes(d.width, d.count);
        if (!compact_runs) {
            result += ops.count_ge(source, rhs, guard, d.type == fe::kind::runs ? ends : nullptr);
            return;
        }
        for (unsigned first = 0; first < d.count; first += 64) {
            const auto count = std::min(64u, unsigned(d.count) - first);
            auto selected = ops.filter(source, first, count, rhs,
                                       ikea::comparison::greater_equal, guard);
            if (d.type == fe::kind::packed) result += std::popcount(selected);
            else {
                // Weight the selected run values with their original extents.
                // No row-mask expansion or per-row payload movement.
                while (selected) {
                    const auto rank = first + std::countr_zero(selected);
                    result += compact_runs ? sizes[rank]
                        : unsigned(ends[rank]) + 1 - (rank ? unsigned(ends[rank - 1]) + 1 : 0);
                    selected &= selected - 1;
                }
            }
        }
    });
    return result;
}
std::size_t image::split_bytes() const {
    return payload_.size() + head0_.size() + head1_.size();
}
} // namespace frame_filter
