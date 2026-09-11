#pragma once
#include <ikea2/seriespack/status.h>
#include <ikea2/seriespack/effects.h>
#include <ikea2/seriespack/selection.h>
#include <expected>
#include <type_traits>
namespace ikea2::seriespack::composition {
/// Borrowed whole-operation binding. Its prepared expression, actual named leaf
/// views, byte owners and effect/summary resources outlive invocation. Coordinates
/// count original logical values. No endpoint allocates, suspends or publishes.
/// U is an unsigned input type; selected values must fit the expression width.
template <class U, class Summary> class mutation_operation {
    static_assert(std::is_integral_v<U> && std::is_unsigned_v<U> && !std::is_same_v<U, bool> &&
                  sizeof(U) <= 8);

  public:
    using region_function = void (*)(const void*, std::size_t, const U*, std::uint16_t, Summary&,
                                     write_journal&);
    using range_function = void (*)(const void*, std::size_t, std::size_t, const U*, row_selection,
                                    Summary&, write_journal&);
    using checked_function = std::expected<void, error> (*)(const void*, std::size_t,
                                                            std::span<const U>, row_selection,
                                                            Summary&, write_journal&);
    using point_function = std::expected<void, error> (*)(const void*, std::size_t, U, Summary&,
                                                          write_journal&);
    using initialize_function = std::expected<void, error> (*)(const void*, std::span<const U>,
                                                               write_journal&);

  private:
    const void* operation_;
    std::size_t size_;
    region_function region_;
    range_function range_;
    checked_function checked_;
    point_function point_;
    initialize_function initialize_;

  public:
    mutation_operation(const void* operation, std::size_t size, region_function region,
                       range_function range, checked_function checked, point_function point,
                       initialize_function initialize)
        : operation_(operation), size_(size), region_(region), range_(range), checked_(checked),
          point_(point), initialize_(initialize) {}
    std::size_t size() const {
        return size_;
    }
    /// Trusted operation boundary. Admission and lifetime belong to the caller;
    /// the selected code retains its concrete expression and native execution.
    /// first is divisible by 16, with sixteen logical and readable input slots.
    /// Active slots fit the domain; a zero mask reads/writes nothing. Caller has
    /// reserved effect capacity and write isolation before entering.
    void replace16_unchecked(std::size_t first, const U* input, std::uint16_t active,
                             Summary& summary, write_journal& effects) const {
        region_(operation_, first, input, active, summary, effects);
    }
    /// Trusted range. Input, coordinates, stable selection coverage, disjointness
    /// and effect capacity are already admitted. Empty regions skip all work;
    /// nonempty interior regions may read all sixteen input slots.
    void replace_unchecked(std::size_t first, std::size_t count, const U* input,
                           row_selection selected, Summary& summary, write_journal& effects) const {
        range_(operation_, first, count, input, selected, summary, effects);
    }
    /// Checked replacement of input.size() original positions. Range, selected
    /// value and capacity errors leave data, summary and effects unchanged.
    /// Lifetime, write isolation and command/output disjointness remain borrowed
    /// owner obligations; successful execution is not atomic publication.
    [[nodiscard]] std::expected<void, error> replace(std::size_t first, std::span<const U> input,
                                                     row_selection selected, Summary& summary,
                                                     write_journal& effects) const {
        return checked_(operation_, first, input, selected, summary, effects);
    }
    /// Checked point replacement; passes value directly. Same failure guarantee
    /// and borrowed-owner obligations as replace(). Shared bytes may be written
    /// while preserving adjacent values; effects report issued byte spans.
    [[nodiscard]] std::expected<void, error> set(std::size_t row, U value, Summary& summary,
                                                 write_journal& effects) const {
        return point_(operation_, row, value, summary, effects);
    }
    /// Checked complete initialization. Requires exact leaf extents; zeroes owned
    /// final-tile slack and preserves unrelated fields/gaps. No old-value summary
    /// law is applied. Any reported failure leaves bytes and effects unchanged.
    [[nodiscard]] std::expected<void, error> initialize(std::span<const U> input,
                                                        write_journal& effects) const {
        return initialize_(operation_, input, effects);
    }
};

} // namespace ikea2::seriespack::composition
