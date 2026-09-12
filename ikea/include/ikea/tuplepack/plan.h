#pragma once
#include <ikea/tuplepack/detail/plan.h>
#include <bit>

namespace ikea::tuplepack {
/// Prepared ordered code projection. Owns its controls; the source description
/// and map can be released after make(). Width is the outer packet byte count.
template <unsigned N> class reader {
    static_assert(N == 8 || N == 64);
    detail::read_control<N> controls_;
    detail::read8_function scalar_ = nullptr;
    explicit reader(detail::read_control<N> controls) : controls_(std::move(controls)) {
        if constexpr (N == 8)
            scalar_ = detail::select_read8(controls_.slots);
    }

  public:
    static constexpr unsigned slots = N;
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
    /// Authoring access; control representation is internal and not serialized.
    const detail::read_control<N>& controls() const noexcept {
        return controls_;
    }
};

/// Prepared replacement map. Holes are ignored; duplicate destinations are
/// rejected at preparation. All unselected bits, including padding, survive.
template <unsigned N> class writer {
    static_assert(N == 8 || N == 64);
    detail::write_control<N> controls_;
    detail::write8_function scalar_ = nullptr;
    explicit writer(detail::write_control<N> controls) : controls_(std::move(controls)) {
        if constexpr (N == 8) {
            scalar_ = detail::select_write8(controls_);
        }
    }

  public:
    static constexpr unsigned slots = N;
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
            if (controls_.dense && controls_.needs_old)
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
} // namespace ikea::tuplepack
