#pragma once
#include <ikea/tuplepack/write.h>

namespace ikea::tuplepack {
/// An observation projection is independent of the write map. It can include
/// unchanged dependencies and read-only aliases. Law owns semantic identity,
/// version and private accumulated state. Persistent outputs need their own
/// preflight and pre-write effects; data-journal capacity does not cover them.
/// Law::needs_before/needs_after independently control value reads. observe is
/// always invoked once for each selected row, after all mutations in its group.
template <class Projection, class Law> class observation {
    const Projection* projection_;
    Law* law_;
    struct empty {};

  public:
    observation(const Projection& projection, Law& law) : projection_(&projection), law_(&law) {}
    observation(const Projection&&, Law&) = delete;
    observation(const Projection&, Law&&) = delete;
    bool covers(std::size_t first, std::size_t count) const noexcept {
        return first <= projection_->size() && count <= projection_->size() - first;
    }
    auto before(std::size_t row) const noexcept {
        if constexpr (Law::needs_before)
            return projection_->get_unchecked(row);
        else
            return empty{};
    }
    template <class Before> void after(std::size_t row, Before&& before) const noexcept {
        if constexpr (Law::needs_before && Law::needs_after)
            law_->observe(row, before, projection_->get_unchecked(row));
        else if constexpr (Law::needs_before)
            law_->observe(row, before);
        else if constexpr (Law::needs_after)
            law_->observe(row, projection_->get_unchecked(row));
        else
            law_->observe(row);
    }
};
} // namespace ikea::tuplepack
