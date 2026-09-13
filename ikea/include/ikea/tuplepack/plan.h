#pragma once
#include <bit>
#include <ikea/tuplepack/detail/packet_plan.h>
#include <ikea/tuplepack/detail/plan.h>
#include <ikea/tuplepack/detail/window.h>

namespace ikea::tuplepack {
namespace detail {
// Distinct from empty controls nested inside write64, so the unused endpoint
// occupies no extra cache line in an aligned 64-byte packet plan.
struct no_kernel_entry {};
template <unsigned N, unsigned Rows>
using read_control =
    std::conditional_t<N == 8, gpr_read, std::conditional_t<Rows == 1, read64, packet_read>>;
template <unsigned N, unsigned Rows>
using write_control =
    std::conditional_t<N == 8, gpr_write, std::conditional_t<Rows == 1, write64, packet_write>>;
} // namespace detail
/// Prepared ordered code projection. Owns its controls; the description and map
/// may be released after make(). N is 8 or 64 output bytes; Rows is a power of two
/// in 1..N; a map has at most N/Rows slots. Groups partition those slots, with
/// rows repeated inside each group. The default is one group of map.size();
/// unused packet capacity is trailing zero space. Physical extent and stride
/// are independent of this decoded byte order.
template <unsigned N, unsigned Rows = 1> class reader {
    static_assert((N == 8 || N == 64) && Rows <= N && std::has_single_bit(Rows));
    detail::read_control<N, Rows> controls_;
    [[no_unique_address]] std::conditional_t<N == 8, detail::gpr_reader<Rows>,
                                             detail::no_kernel_entry> entry_{};
    explicit reader(detail::read_control<N, Rows> controls) : controls_(std::move(controls)) {
        if constexpr (N == 8)
            entry_ = detail::select_gpr_reader<Rows>(controls_);
    }

  public:
    static constexpr unsigned slots = N, rows = Rows, max_map_slots = N / Rows;
    [[nodiscard]] static std::expected<reader, error>
    make(const layout& format, std::span<const byte> map, std::span<const unsigned> groups = {}) {
        if constexpr (N == 64 && Rows == 1) {
            auto ordering = packet_layout<N>::make(Rows, map.size(), groups);
            if (!ordering)
                return std::unexpected(ordering.error());
        }
        auto prepared = [&] {
            if constexpr (N == 8)
                return detail::prepare_read8(format, map, Rows, groups);
            else if constexpr (Rows == 1)
                return detail::prepare_read64(format, map);
            else
                return detail::prepare_packet_read(format, map, Rows, groups);
        }();
        if (!prepared)
            return std::unexpected(prepared.error());
        return reader(std::move(*prepared));
    }
    /// Possible read bytes relative to one physical unit. SIMD may read preserved
    /// neighbors within the unit; allocation padding is never required.
    std::uint64_t read_bytes() const noexcept {
        if constexpr (N == 8)
            return controls_.reads;
        else
            return controls_.native_reads;
    }
    unsigned unit_bytes() const noexcept {
        return controls_.bytes;
    }
    /// Trusted unit storage and active mask. Bit r names first+r; inactive rows
    /// issue no payload access and produce zero slots. The allocation contains
    /// all active units at the supplied stride, with this plan's physical layout.
    packet<N> get_unchecked(const byte* first, std::size_t stride,
                            std::uint64_t active = detail::all_rows<Rows>) const {
        if constexpr (N == 8) {
            if constexpr (Rows == 1)
                return active ? entry_(controls_, first) : packet<N>{};
            else
                return entry_(controls_, first, stride, active);
        } else if constexpr (Rows == 1)
            return active ? detail::read_buffered(controls_, first) : packet<N>{};
        else
            return detail::read_packet_buffered(controls_, Rows, first, stride, active);
    }
    packet<N> get_unchecked(const byte* row) const
        requires(Rows == 1)
    {
        return get_unchecked(row, 0);
    }
    /// Borrowed ordering metadata; its address is stable while this plan is.
    const packet_layout<N>& ordering() const noexcept {
        return controls_.ordering;
    }
    /// Authoring access; controls are internal and are not serialized.
    const auto& controls() const noexcept {
        return controls_;
    }
};
/// Prepared replacement map, with the same packet shape as reader. Holes and
/// trailing slots are ignored; duplicate destinations reject at preparation.
/// Unselected bits survive. Access/effect masks describe one physical unit;
/// operation admission multiplies effect capacity by the active row count.
template <unsigned N, unsigned Rows = 1> class writer {
    static_assert((N == 8 || N == 64) && Rows <= N && std::has_single_bit(Rows));
    detail::write_control<N, Rows> controls_;
    [[no_unique_address]] std::conditional_t<N == 8, detail::gpr_writer<Rows>,
                                             detail::no_kernel_entry> entry_{};
    explicit writer(detail::write_control<N, Rows> controls) : controls_(std::move(controls)) {
        if constexpr (N == 8)
            entry_ = detail::select_gpr_writer<Rows>(controls_);
    }

  public:
    static constexpr unsigned slots = N, rows = Rows, max_map_slots = N / Rows;
    [[nodiscard]] static std::expected<writer, error>
    make(const layout& format, std::span<const byte> map, std::span<const unsigned> groups = {}) {
        if constexpr (N == 64 && Rows == 1) {
            auto ordering = packet_layout<N>::make(Rows, map.size(), groups);
            if (!ordering)
                return std::unexpected(ordering.error());
        }
        auto prepared = [&] {
            if constexpr (N == 8)
                return detail::prepare_write8(format, map, Rows, groups);
            else if constexpr (Rows == 1)
                return detail::prepare_write64(format, map);
            else
                return detail::prepare_packet_write(format, map, Rows, groups);
        }();
        if (!prepared)
            return std::unexpected(prepared.error());
        return writer(std::move(*prepared));
    }
    /// Borrowed ordering metadata; its address is stable while this plan is.
    const packet_layout<N>& ordering() const noexcept {
        return controls_.read.ordering;
    }
    unsigned unit_bytes() const noexcept {
        return controls_.read.bytes;
    }
    std::uint64_t write_bytes() const noexcept {
        return controls_.writes;
    }
    /// Possible old-data reads, independent of issued stores and semantic
    /// observation maps. Owners admit both read and write coverage.
    std::uint64_t read_bytes() const noexcept {
        if constexpr (N == 8 || Rows > 1)
            return controls_.native_reads;
        else {
            if (controls_.needs_old)
                return controls_.read.bytes == 64 ? ~std::uint64_t(0)
                                                  : (std::uint64_t(1) << controls_.read.bytes) - 1;
            return 0;
        }
    }
    /// Conservative runs per active row, before journal coalescing.
    unsigned effect_capacity() const noexcept {
        return controls_.runs;
    }
    bool accepts(const packet<N>& input,
                 std::uint64_t active = detail::all_rows<Rows>) const noexcept {
        if constexpr (N == 8) {
            if (active == detail::all_rows<Rows>)
                return !(input & controls_.invalid_word);
            std::uint64_t mask = 0;
            for (unsigned r = 0; r < Rows; ++r)
                if (active & (std::uint64_t(1) << r))
                    for (unsigned i = 0; i < ordering().map_size(); ++i)
                        mask |= std::uint64_t(255) << (8 * ordering().template offset<Rows>(r, i));
            return !(input & controls_.invalid_word & mask);
        } else {
            if (active == detail::all_rows<Rows>)
                return detail::fits<64>(controls_, input);
            for (unsigned r = 0; r < Rows; ++r) {
                if (!(active & (std::uint64_t(1) << r)))
                    continue;
                byte invalid = 0;
                for (unsigned i = 0; i < ordering().map_size(); ++i)
                    invalid |= input[ordering().template offset<Rows>(r, i)] &
                               controls_.invalid[ordering().template offset<Rows>(r, i)];
                if (invalid)
                    return false;
            }
            return true;
        }
    }
    /// Trusted widths, active rows and readable/writable units. Caller emits
    /// effects before entering. No allocation, failure, suspension or publication.
    void set_unchecked(byte* first, std::size_t stride, const packet<N>& input,
                       std::uint64_t active = detail::all_rows<Rows>) const {
        if constexpr (N == 8) {
            if constexpr (Rows == 1) {
                if (active)
                    entry_(controls_, first, input);
            } else
                entry_(controls_, first, stride, input, active);
        } else if constexpr (Rows == 1) {
            if (active)
                detail::write_buffered(controls_, first, input);
        } else
            detail::write_packet_buffered(controls_, Rows, first, stride, input, active);
    }
    void set_unchecked(byte* row, const packet<N>& input) const
        requires(Rows == 1)
    {
        set_unchecked(row, 0, input);
    }
    const auto& controls() const noexcept {
        return controls_;
    }
};
} // namespace ikea::tuplepack
