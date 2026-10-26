#pragma once
#include <ikea/frame/image.h>
#include <ikea/intervals/types.h>
#include <ikea/seriespack/selection.h>

namespace ikea::frame {
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
    struct fine_request { bitset256 active; block_rows_set rows; std::size_t count{}; };
    std::expected<fine_request, error> selected_rows(const bitset256&, ikea::seriespack::selection) const;
  public:
    std::size_t size() const noexcept;
    std::span<const section_key> available_sections() const noexcept;
    /// Conservative point/read closure in binding-unit row coordinates, for
    /// Loom/owner acquisition. The returned keys do not grant access.
    [[nodiscard]] std::expected<std::vector<section_key>, error> required(std::span<const row_range>) const;
    [[nodiscard]] std::expected<std::uint64_t, error> get(std::size_t row) const;
    [[nodiscard]] std::expected<void, error> read(std::size_t first, std::span<std::uint64_t> output) const;
    /// Coarse selection coordinates are block numbers, starting at zero.
    [[nodiscard]] std::expected<u128, error> sum(ikea::intervals::selection blocks = ikea::intervals::selection::all()) const;
    [[nodiscard]] std::expected<std::uint64_t, error> count(comparison, std::uint64_t threshold,
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
