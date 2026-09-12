#pragma once
#include <ikea/tuplepack/write.h>

namespace ikea::tuplepack {
/// An observation projection is independent of the write map. It can include
/// unchanged dependencies and read-only aliases. Law owns semantic identity,
/// version and private accumulated state. Persistent outputs need their own
/// preflight and pre-write effects; data-journal capacity does not cover them.
/// Law::needs_before/needs_after independently control value reads. observe is
/// invoked per selected row; packet laws use observe_batch once per nonempty
/// window. Both run after all mutations in their group.
template <class Projection, class Law> class observation {
    const Projection* projection_;
    Law* law_;
    struct empty {};

  public:
    static constexpr unsigned rows = detail::operation_rows<Projection>;
    observation(const Projection& projection, Law& law) : projection_(&projection), law_(&law) {}
    observation(const Projection&&, Law&) = delete;
    observation(const Projection&, Law&&) = delete;
    bool covers(std::size_t first, std::size_t count) const noexcept {
        return first <= projection_->size() && count <= projection_->size() - first;
    }
    auto before(std::size_t row) const noexcept
        requires(detail::operation_rows<Projection> == 1)
    {
        if constexpr (Law::needs_before)
            return projection_->get_unchecked(row);
        else
            return empty{};
    }
    template <class Before>
    void after(std::size_t row, Before&& before) const noexcept
        requires(detail::operation_rows<Projection> == 1)
    {
        if constexpr (Law::needs_before && Law::needs_after)
            law_->observe(row, before, projection_->get_unchecked(row));
        else if constexpr (Law::needs_before)
            law_->observe(row, before);
        else if constexpr (Law::needs_after)
            law_->observe(row, projection_->get_unchecked(row));
        else
            law_->observe(row);
    }
    /// Packet-shaped laws receive the original first row and active row mask.
    /// They can reduce/transform native projections without per-row extraction.
    auto before_batch(std::size_t first, std::uint64_t active) const noexcept
        requires(detail::operation_rows<Projection> > 1)
    {
        if constexpr (Law::needs_before)
            return projection_->get_unchecked(first, active);
        else
            return empty{};
    }
    template <class Before>
    void after_batch(std::size_t first, std::uint64_t active, Before&& before) const noexcept
        requires(detail::operation_rows<Projection> > 1)
    {
        if constexpr (Law::needs_before && Law::needs_after)
            law_->observe_batch(first, active, before, projection_->get_unchecked(first, active));
        else if constexpr (Law::needs_before)
            law_->observe_batch(first, active, before);
        else if constexpr (Law::needs_after)
            law_->observe_batch(first, active, projection_->get_unchecked(first, active));
        else
            law_->observe_batch(first, active);
    }
};
} // namespace ikea::tuplepack
