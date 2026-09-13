#pragma once
#include <bit>
#include <ikea/tuplepack/description.h>

namespace ikea::tuplepack {
/// Byte order of a decoded packet, independent of its physical storage.
/// Groups partition the ordered map slots (including holes). Within a group,
/// slots for row 0 precede slots for row 1, etc. Groups follow one another;
/// unused packet capacity is trailing zero space, ignored on write.
template <unsigned N> class packet_layout {
    static_assert(N == 8 || N == 64);
    std::array<byte, N> offsets_{};
    std::array<byte, N> owners_ = [] {
        std::array<byte, N> result;
        result.fill(255);
        return result;
    }();
    unsigned rows_ = 1, slots_ = 0;
    bool row_major_ = true, single_group_ = true;

  public:
    /// Empty groups means one group of map_slots. Explicit groups must have
    /// positive lengths summing exactly to map_slots. The empty map needs no groups.
    [[nodiscard]] static std::expected<packet_layout, error>
    make(unsigned rows, unsigned map_slots, std::span<const unsigned> groups = {});
    unsigned rows() const noexcept {
        return rows_;
    }
    unsigned map_size() const noexcept {
        return slots_;
    }
    unsigned used_bytes() const noexcept {
        return rows_ * slots_;
    }
    /// Trusted original row and map slot; both must be within this layout.
    unsigned offset(unsigned row, unsigned slot) const noexcept {
        return offsets_[row * (N / rows_) + slot];
    }
    /// Authoring form: Rows must equal rows(); avoids runtime division.
    template <unsigned Rows> unsigned offset(unsigned row, unsigned slot) const noexcept {
        return offsets_[row * (N / Rows) + slot];
    }
    /// Authoring metadata: 0..rows-1 for mapped bytes, 255 for trailing capacity.
    const std::array<byte, N>& row_indices() const noexcept {
        return owners_;
    }
    /// Lowering proof: mapped offsets equal row*(N/rows())+slot.
    bool row_major() const noexcept {
        return row_major_;
    }
    bool single_group() const noexcept {
        return single_group_;
    }
};
} // namespace ikea::tuplepack
