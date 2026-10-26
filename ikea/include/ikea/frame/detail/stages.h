// The landing stages, constructed by make_stages from a description.
#pragma once
#include <ikea/frame/author/stage.h>
#include <ikea/frame/build.h>

#include <memory>

namespace ikea::frame::detail {

/// The leaf: blocks of equal width in one group share a SeriesPack array, so
/// one compiled range operation covers every selected block of that width.
/// Width 0 is a constant block with no body. Geometry per width follows the
/// description's frozen packing rule for preset.
std::unique_ptr<author::stage> make_pack(pack_preset preset);

/// A per-block window [lo, lo + 2^width) over one child; values outside it
/// are exceptions (position, value) in a per-group area, and their child
/// fields hold 0.
std::unique_ptr<author::stage> make_bound(std::unique_ptr<author::stage> child, const fitting_policy &);

enum class interval_form : std::uint8_t { sizes, relative_ends };
/// A checkpoint (the value) every spacing rows, as a series of its own in its
/// own stream, and offset-binary fields between them: each row's difference
/// from the previous row (sizes) or from its checkpoint (relative ends).
std::unique_ptr<author::stage> make_intervals(std::unique_ptr<author::stage> fields,
                                              std::unique_ptr<author::stage> checkpoints, unsigned spacing,
                                              interval_form form);

/// Bound's per-block state for a bind-time point plan.
struct bound_point_entry {
    std::uint64_t lo;
    author::point_leaf leaf;
    const std::uint8_t *positions;
    const std::uint64_t *values;
    unsigned count;
};
/// Whether s is a bound stage over a leaf with point entries, and if so its
/// entry for one block.
bool bound_entry(const author::stage &s, unsigned unit, unsigned block, bound_point_entry &out);
bool is_bound(const author::stage &s) noexcept;

} // namespace ikea::frame::detail
