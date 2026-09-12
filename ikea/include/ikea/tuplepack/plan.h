#pragma once
#include <ikea/tuplepack/detail/plan.h>
#include <ikea/tuplepack/detail/packet_plan.h>
#include <ikea/tuplepack/detail/window.h>
#include <bit>

namespace ikea::tuplepack {
/// Prepared ordered code projection. Owns its controls; the source description
/// and map can be released after make(). Width is the outer packet byte count.
template <unsigned N, unsigned Rows = 1> class reader;
template <unsigned N> class reader<N, 1> {
    static_assert(N == 8 || N == 64);
    detail::read_control<N> controls_;
    detail::read8_function scalar_ = nullptr;
    explicit reader(detail::read_control<N> controls) : controls_(std::move(controls)) {
        if constexpr (N == 8)
            scalar_ = detail::select_read8(controls_.slots);
    }

  public:
    static constexpr unsigned slots = N, rows = 1, bytes_per_row = N;
    [[nodiscard]] static std::expected<reader, error> make(const layout& format,
                                                           std::span<const byte> map) {
        auto prepared = [&] {
            if constexpr (N == 8)
                return detail::prepare_read8(format, map);
            else
                return detail::prepare_read64(format, map);
        }();
        if (!prepared)
            return std::unexpected(prepared.error());
        return reader(std::move(*prepared));
    }
    /// Byte-position masks relative to one unit. Native read64 may load whole
    /// 16-byte source chunks, bounded by the described unit's exact extent.
    std::uint64_t read_bytes() const noexcept {
        if constexpr (N == 8)
            return controls_.reads;
        else
            return controls_.native_reads;
    }
    unsigned unit_bytes() const noexcept {
        return controls_.bytes;
    }
    /// Trusted exact unit storage, encoded using this reader's description.
    packet<N> get_unchecked(const byte* row) const {
        if constexpr (N == 8)
            return scalar_(controls_, row);
        else
            return detail::read_buffered(controls_, row);
    }
    packet<N> get_unchecked(const byte* row, std::size_t, std::uint64_t active) const {
        return active ? get_unchecked(row) : packet<N>{};
    }
    /// Authoring access; control representation is internal and not serialized.
    const detail::read_control<N>& controls() const noexcept {
        return controls_;
    }
};

/// Prepared replacement map. Holes are ignored; duplicate destinations are
/// rejected at preparation. All unselected bits, including padding, survive.
template <unsigned N, unsigned Rows = 1> class writer;
template <unsigned N> class writer<N, 1> {
    static_assert(N == 8 || N == 64);
    detail::write_control<N> controls_;
    detail::write8_function scalar_ = nullptr;
    explicit writer(detail::write_control<N> controls) : controls_(std::move(controls)) {
        if constexpr (N == 8) {
            scalar_ = detail::select_write8(controls_);
        }
    }

  public:
    static constexpr unsigned slots = N, rows = 1, bytes_per_row = N;
    [[nodiscard]] static std::expected<writer, error> make(const layout& format,
                                                           std::span<const byte> map) {
        auto prepared = [&] {
            if constexpr (N == 8)
                return detail::prepare_write8(format, map);
            else
                return detail::prepare_write64(format, map);
        }();
        if (!prepared)
            return std::unexpected(prepared.error());
        return writer(std::move(*prepared));
    }
    unsigned unit_bytes() const noexcept {
        return controls_.read.bytes;
    }
    std::uint64_t write_bytes() const noexcept {
        return controls_.writes;
    }
    /// Possible old-data reads, including word-coalesced point loads. Owners
    /// admit this independently of issued stores and semantic observation maps.
    std::uint64_t read_bytes() const noexcept {
        if constexpr (N == 8) {
            const auto& w = controls_.word;
            if (w.bytes && w.selected > 1)
                return ((std::uint64_t(1) << w.bytes) - 1) << w.offset;
        } else {
            if (controls_.needs_old)
                return controls_.read.bytes == 64 ? ~std::uint64_t(0)
                                                  : (std::uint64_t(1) << controls_.read.bytes) - 1;
        }
        std::uint64_t result = 0;
        for (unsigned i = 0; i < controls_.count; ++i)
            if (controls_.stores[i].mask != 255)
                result |= std::uint64_t(1) << controls_.stores[i].offset;
        return result;
    }
    /// Upper bound before journal coalescing: consecutive issued bytes form a run.
    unsigned effect_capacity() const noexcept {
        return controls_.runs;
    }
    bool accepts(const packet<N>& input) const noexcept {
        return detail::fits<N>(controls_, input);
    }
    bool accepts(const packet<N>& input, std::uint64_t active) const noexcept {
        return !active || accepts(input);
    }
    void set_unchecked(byte* row, std::size_t, const packet<N>& input, std::uint64_t active) const {
        if (active)
            set_unchecked(row, input);
    }
    /// Trusted widths and readable/writable unit; caller emits effects before
    /// entering. This body cannot fail, allocate, suspend or publish.
    void set_unchecked(byte* row, const packet<N>& input) const {
        if constexpr (N == 8)
            scalar_(controls_, row, input);
        else
            detail::write_buffered(controls_, row, input);
    }
    const detail::write_control<N>& controls() const noexcept {
        return controls_;
    }
};
/// One 64-byte packet projects Rows consecutive original rows, in row-major
/// order. Rows is 2/4/8/16/32/64; each row has 64/Rows map slots. The map is shared
/// by the rows, while placement and the active row mask remain independent.
template <unsigned N, unsigned Rows> class reader {
    static_assert(N == 64 && Rows >= 2 && Rows <= 64 && std::has_single_bit(Rows));
    detail::packet_read controls_;
    explicit reader(detail::packet_read controls) : controls_(std::move(controls)) {}

  public:
    /// Total packet slots, original rows, and the map capacity for each row.
    static constexpr unsigned slots = N, rows = Rows, bytes_per_row = N / Rows;
    [[nodiscard]] static std::expected<reader, error> make(const layout& format,
                                                           std::span<const byte> map) {
        auto prepared = detail::prepare_packet_read(format, map, Rows);
        if (!prepared)
            return std::unexpected(prepared.error());
        return reader(std::move(*prepared));
    }
    unsigned unit_bytes() const noexcept {
        return controls_.bytes;
    }
    std::uint64_t read_bytes() const noexcept {
        return controls_.native_reads;
    }
    packet<N> get_unchecked(const byte* first, std::size_t stride,
                            std::uint64_t active = detail::all_rows<Rows>) const {
        return detail::read_packet_buffered(controls_, Rows, first, stride, active);
    }
    const detail::packet_read& controls() const noexcept {
        return controls_;
    }
};
/// Prepared packet replacement. Selected code widths and holes have the same
/// meaning as a one-row writer. Access/effect masks describe one physical unit;
/// operation admission multiplies capacity by the number of active rows.
template <unsigned N, unsigned Rows> class writer {
    static_assert(N == 64 && Rows >= 2 && Rows <= 64 && std::has_single_bit(Rows));
    detail::packet_write controls_;
    explicit writer(detail::packet_write controls) : controls_(std::move(controls)) {}

  public:
    /// Total packet slots, original rows, and the map capacity for each row.
    static constexpr unsigned slots = N, rows = Rows, bytes_per_row = N / Rows;
    [[nodiscard]] static std::expected<writer, error> make(const layout& format,
                                                           std::span<const byte> map) {
        auto prepared = detail::prepare_packet_write(format, map, Rows);
        if (!prepared)
            return std::unexpected(prepared.error());
        return writer(std::move(*prepared));
    }
    unsigned unit_bytes() const noexcept {
        return controls_.read.bytes;
    }
    std::uint64_t write_bytes() const noexcept {
        return controls_.writes;
    }
    std::uint64_t read_bytes() const noexcept {
        return controls_.native_reads;
    }
    unsigned effect_capacity() const noexcept {
        return controls_.runs;
    }
    bool accepts(const packet<N>& input,
                 std::uint64_t active = detail::all_rows<Rows>) const noexcept {
        if (active == detail::all_rows<Rows>)
            return detail::fits<64>(controls_, input);
        for (unsigned r = 0; r < Rows; ++r) {
            if (!(active & (std::uint64_t(1) << r)))
                continue;
            byte invalid = 0;
            for (unsigned i = 0; i < bytes_per_row; ++i)
                invalid |= input[r * bytes_per_row + i] & controls_.invalid[r * bytes_per_row + i];
            if (invalid)
                return false;
        }
        return true;
    }
    void set_unchecked(byte* first, std::size_t stride, const packet<N>& input,
                       std::uint64_t active = detail::all_rows<Rows>) const {
        detail::write_packet_buffered(controls_, Rows, first, stride, input, active);
    }
    const detail::packet_write& controls() const noexcept {
        return controls_;
    }
};
} // namespace ikea::tuplepack
