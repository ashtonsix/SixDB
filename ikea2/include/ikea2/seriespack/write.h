#pragma once
#include <ikea2/seriespack/author/write.h>

namespace ikea2::seriespack {
#if defined(__aarch64__) || defined(__AVX2__)
/// The named admitted view and its byte owners outlive the returned binding.
template <class F>
[[nodiscard]] auto bind_mutation(const view<F, std::uint8_t>& destination,
                                 mutation_diagnostic* diagnostic = nullptr) {
    return composition::prepare_mutation(composition::describe(destination), destination.size(),
                                         diagnostic);
}
template <class F>
auto bind_mutation(const view<F, std::uint8_t>&&, mutation_diagnostic* = nullptr) = delete;
#endif
} // namespace ikea2::seriespack
