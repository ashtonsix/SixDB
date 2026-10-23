#pragma once
#include <ikea/effects.h>
#include <ikea/tuplepack/view.h>
#include <concepts>
#include <cstring>

namespace ikea::tuplepack {
/// Borrowed copy of complete physical units between equal descriptions. Source
/// units, including every spare bit/byte, must be initialized in source_format.
/// The owner grants read access to them and exclusive write access to complete
/// destination units, including spares. Prior destination contents are unused.
/// Views retain extent, not layout identity: the supplied descriptions must be
/// authoritative for those views. Binding retains the named views, not layouts.
/// Keep views, backing and source bytes stable; row selection stays stable too.
/// View/selection metadata is disjoint from issued writes. Effect storage is
/// disjoint from both source and destination payload and from that metadata.
/// Copy does not run replacement maintenance, acquire leases, allocate, suspend
/// or publish. The enclosing owner supplies any semantic maintenance.
template <unsigned Bytes, class Byte> class transfer {
    static_assert(Bytes >= 1 && Bytes <= 64);
    const basic_view<Byte>* source_;
    const view* destination_;
    transfer(const basic_view<Byte>& source, const view& destination)
        : source_(&source), destination_(&destination) {}

  public:
    [[nodiscard]] static std::expected<transfer, error> bind(const layout& source_format,
                                                                  const basic_view<Byte>& source,
                                                                  const layout& destination_format,
                                                                  const view& destination) {
        if (source_format != destination_format || source_format.bytes() != Bytes ||
            source.layout_identity() != source_format.identity() ||
            destination.layout_identity() != destination_format.identity())
            return std::unexpected(error::description);
        return transfer(source, destination);
    }
    static auto bind(const layout&, const basic_view<Byte>&&, const layout&, const view&) = delete;
    static auto bind(const layout&, const basic_view<Byte>&, const layout&, const view&&) = delete;

    // Tight destination units issue one contiguous span. Strided units issue
    // one span per row; neither path includes gap bytes or trailing padding.
    [[nodiscard]] std::expected<std::size_t, error>
    effect_capacity(std::size_t first, std::size_t count) const noexcept {
        if (first > destination_->size() || count > destination_->size() - first)
            return std::unexpected(error::range);
        return count && destination_->stride() == Bytes ? 1 : count;
    }
    /// Admit the whole command before effects or stores. Source rows may repeat
    /// or be reordered; destination rows are consecutive original coordinates.
    /// Selected source units must not intersect the destination window, including
    /// its stride gaps. Disjoint units of one backing allocation are supported.
    /// Copy preserves spare bits; it does not canonicalize or migrate a format.
    template <std::unsigned_integral Row>
    [[nodiscard]] std::expected<void, error>
    admit(std::size_t first, std::span<const Row> source_rows, std::size_t remaining) const {
        if (first > destination_->size() || source_rows.size() > destination_->size() - first)
            return std::unexpected(error::range);
        if (*effect_capacity(first, source_rows.size()) > remaining)
            return std::unexpected(error::effects);
        if (source_rows.empty())
            return {};
        const auto begin = reinterpret_cast<std::uintptr_t>(destination_->row_unchecked(first));
        const auto end = reinterpret_cast<std::uintptr_t>(
                             destination_->row_unchecked(first + source_rows.size() - 1)) +
                         Bytes;
        for (const auto row : source_rows) {
            if (row >= source_->size())
                return std::unexpected(error::range);
            const auto address = reinterpret_cast<std::uintptr_t>(source_->row_unchecked(row));
            // Reject source overlap with the full destination window, including
            // stride gaps. This prevents future reads changing during the copy.
            if (address < end && begin < address + Bytes)
                return std::unexpected(error::overlap);
        }
        return {};
    }
    /// Rejection leaves all destination bytes and journal entries unchanged.
    /// An empty selection may start at the destination end and issues no access.
    template <std::unsigned_integral Row>
    [[nodiscard]] std::expected<void, error>
    copy(std::size_t first, std::span<const Row> source_rows, ikea::journal& effects) const {
        if (effects.used > effects.storage.size())
            return std::unexpected(error::effects);
        if (auto accepted = admit(first, source_rows, effects.remaining()); !accepted)
            return accepted;
        copy_unchecked(first, source_rows, effects);
        return {};
    }
    /// Requires successful admission for this unchanged command and enough
    /// coverage capacity. Hooks cannot allocate/fail/suspend or alter source, views or
    /// selection. No padding/gap access; each effect precedes its covered stores.
    template <std::unsigned_integral Row, class Coverage> requires ikea::effects<Coverage, view>
    void copy_unchecked(std::size_t first, std::span<const Row> source_rows,
                        Coverage& effects) const {
        if (source_rows.empty())
            return;
        const auto copy_row = [&](std::size_t i) {
            std::memcpy(destination_->row_unchecked(first + i),
                        source_->row_unchecked(source_rows[i]), Bytes);
        };
        if (destination_->stride() == Bytes) {
            effects.before(*destination_, 0, destination_->offset() + first * Bytes,
                                                     source_rows.size() * Bytes);
            for (std::size_t i = 0; i < source_rows.size(); ++i)
                copy_row(i);
        } else {
            for (std::size_t i = 0; i < source_rows.size(); ++i) {
                const auto row = first + i;
                effects.before(*destination_, 0, destination_->offset() + row * destination_->stride(), Bytes);
                copy_row(i);
            }
        }
    }
};
template <unsigned Bytes, class Byte>
[[nodiscard]] auto bind_transfer(const layout& source_format, const basic_view<Byte>& source,
                   const layout& destination_format, const view& destination) {
    return transfer<Bytes, Byte>::bind(source_format, source, destination_format, destination);
}
template <unsigned Bytes, class Byte>
auto bind_transfer(const layout&, const basic_view<Byte>&&, const layout&, const view&) = delete;
template <unsigned Bytes, class Byte>
auto bind_transfer(const layout&, const basic_view<Byte>&, const layout&, const view&&) = delete;
} // namespace ikea::tuplepack
