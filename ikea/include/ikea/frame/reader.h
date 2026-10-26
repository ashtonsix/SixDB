#pragma once
#include <ikea/frame/image.h>
#include <ikea/intervals/types.h>
#include <ikea/seriespack/selection.h>

namespace ikea::frame {
namespace author { class request; }
/// Bound immutable interpretation and operation state; payload remains borrowed.
/// The owner keeps every granted section initialized, read-isolated, immutable
/// and at its admitted address through all copies of this reader/image.
/// Selection backing must remain stable and outputs disjoint from all sources,
/// this binding and input masks. Each complete command is admitted before output.
class reader {
    std::shared_ptr<const detail::image_state> state_;
    explicit reader(const validated_image& image) : state_(image.state_) {}
    friend reader bind_reader(const validated_image&);
    std::expected<bitset256, error> blocks(ikea::intervals::selection) const;
    std::expected<void, error> admit_range(std::size_t, std::size_t) const;
    std::expected<void, error> admit_blocks(const bitset256&) const;
    std::expected<author::request, error> lower(row_range, ikea::intervals::selection, ikea::seriespack::selection) const;
  public:
    std::size_t size() const noexcept;
    std::span<const section_key> available_sections() const noexcept;
    /// Conservative point/read closure in binding-unit row coordinates, for
    /// Loom/owner acquisition. The returned keys do not grant access.
    [[nodiscard]] std::expected<std::vector<section_key>, error> required(std::span<const row_range>) const;
    /// The same closure for one span narrowed by coarse blocks, lowered as
    /// sum(span, blocks) lowers it: a block with no requested row demands
    /// nothing, so a masked-out group in the middle of the span is not acquired.
    [[nodiscard]] std::expected<std::vector<section_key>, error> required(row_range span,
        ikea::intervals::selection blocks) const;
    [[nodiscard]] std::expected<std::uint64_t, error> get(std::size_t row) const;
    [[nodiscard]] std::expected<void, error> read(std::size_t first, std::span<std::uint64_t> output) const;
    /// Coarse selection coordinates are block numbers, starting at zero.
    [[nodiscard]] std::expected<u128, error> sum(ikea::intervals::selection blocks = ikea::intervals::selection::all()) const;
    [[nodiscard]] std::expected<std::uint64_t, error> count(comparison, std::uint64_t threshold,
        ikea::intervals::selection blocks = ikea::intervals::selection::all()) const;
    /// SUM over the rows of span [first, first + count) that lie in the coarse
    /// blocks and, when given, in the fine selection. Coarse coordinates are
    /// block numbers; fine coordinates are original unit rows, and only rows of
    /// the span's coarse blocks need be covered. The span is lowered once; a
    /// block with no selected row needs no payload. Refuses before any work on
    /// a span outside the reader, an uncovered selection or an unavailable group.
    [[nodiscard]] std::expected<u128, error> sum(row_range span,
        ikea::intervals::selection blocks = ikea::intervals::selection::all(),
        ikea::seriespack::selection rows = ikea::seriespack::selection::all()) const;
    /// COUNT of the rows of span within the coarse blocks satisfying op.
    [[nodiscard]] std::expected<std::uint64_t, error> count(comparison, std::uint64_t threshold, row_range span,
        ikea::intervals::selection blocks = ikea::intervals::selection::all()) const;
    /// Writes only selected coarse slots output[block]; clears logical tail
    /// high bits, leaves other slots untouched. Capacity covers selected slots.
    [[nodiscard]] std::expected<void, error> filter(comparison, std::uint64_t threshold,
        std::span<bitset256> output, ikea::intervals::selection blocks = ikea::intervals::selection::all()) const;
    /// Fine selection coordinates are ORIGINAL unit rows, independent of the
    /// coarse block mask. A coarse block with no selected rows needs no payload.
    [[nodiscard]] std::expected<u128, error> sum_selected(ikea::seriespack::selection rows,
        ikea::intervals::selection blocks = ikea::intervals::selection::all()) const;
    [[nodiscard]] std::expected<summary, error> summarise_selected(ikea::seriespack::selection rows,
        ikea::intervals::selection blocks = ikea::intervals::selection::all()) const;
    /// Outputs selected values in original row order. Remaining output is
    /// untouched; insufficient capacity refuses before the first result write.
    [[nodiscard]] std::expected<std::size_t, error> select(ikea::seriespack::selection rows,
        std::span<std::uint64_t> output, ikea::intervals::selection blocks = ikea::intervals::selection::all()) const;
};
/// Reuses content proof and immutable execution state; no payload access.
[[nodiscard]] reader bind_reader(const validated_image&);
[[nodiscard]] std::expected<reader, error> bind_reader(const description&, image_view);
} // namespace ikea::frame
