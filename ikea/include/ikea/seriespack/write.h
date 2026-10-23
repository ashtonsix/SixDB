#pragma once
#include <ikea/seriespack/writer.h>

namespace ikea::seriespack {
/// The named admitted view and its byte owners outlive the returned binding.
template <class F>
[[nodiscard]] auto bind_writer(const basic_view<F, ikea::byte>& destination,
                                 mutation_diagnostic* diagnostic = nullptr) {
    return bind_writer(author::expression(destination), destination.size(),
                                         diagnostic);
}
template <class F>
auto bind_writer(const basic_view<F, ikea::byte>&&, mutation_diagnostic* = nullptr) = delete;
} // namespace ikea::seriespack
