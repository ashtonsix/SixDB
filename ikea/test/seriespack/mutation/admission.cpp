#include <ikea/seriespack.h>
#include "../support.h"
#include <cstdlib>
#include <new>

namespace {
bool reject_allocation = false;
}

void* operator new(std::size_t size) {
    if (!reject_allocation)
        if (void* bytes = std::malloc(size ? size : 1))
            return bytes;
    throw std::bad_alloc();
}
void operator delete(void* bytes) noexcept {
    std::free(bytes);
}
void operator delete(void* bytes, std::size_t) noexcept {
    std::free(bytes);
}

namespace sp = ikea::seriespack;
namespace cp = sp::composition;

template <class F> void check_single_view() {
    ikea_test::format_scope<F> context{"bind admitted view under allocation pressure"};
    constexpr auto count = F::tile_rows + 1;
    alignas(64) std::array<std::uint8_t, 8192> payload{}, head0{}, head1{};
    const auto source = sp::view<F, std::uint8_t>::attach(
        count, {{{payload, F::tile_bytes}, {head0, F::tile_rows}, {head1, F::tile_rows}}});
    IKEA_CHECK(source);
    sp::mutation_diagnostic diagnostic{&payload, &head0, 2, 1};
    reject_allocation = true;
    const auto prepared = sp::bind_mutation(*source, &diagnostic);
    reject_allocation = false;
    IKEA_CHECK(prepared && prepared->size() == count);
    IKEA_CHECK(!diagnostic.source && !diagnostic.conflicting_source);
    IKEA_CHECK(!diagnostic.plane && !diagnostic.conflicting_plane);

    reject_allocation = true;
    const auto too_long = cp::prepare_mutation(cp::describe(*source), count + 1, &diagnostic);
    const auto prefix = cp::prepare_mutation(cp::describe(*source), count - 1);
    const auto empty = cp::prepare_mutation(cp::describe(*source), 0);
    reject_allocation = false;
    IKEA_CHECK(!too_long && too_long.error() == sp::error::range);
    IKEA_CHECK(diagnostic.source == &*source && !diagnostic.conflicting_source);
    IKEA_CHECK(prefix && prefix->size() == count - 1 && empty && empty->size() == 0);
    const auto construction = prefix->construction_effect_capacity();
    IKEA_CHECK(!construction && construction.error() == sp::error::range);

}

void check_substituted_views() {
    ikea_test::scope context{"same-format substitution still admits cross-view aliases"};
    using F = sp::format<32, sp::geometry::local, 16>;
    constexpr std::size_t count = 17;
    alignas(64) std::array<std::uint8_t, 256> a{}, b{}, x{}, y{}, z{};
    const auto first = *sp::view<F, std::uint8_t>::attach(
        count, {{{a, F::tile_bytes}, {x, F::tile_rows}, {y, F::tile_rows}}});
    const auto second = *sp::view<F, std::uint8_t>::attach(
        count, {{{b, F::tile_bytes}, {y, F::tile_rows}, {z, F::tile_rows}}});
    const auto overlapping = *sp::view<F, std::uint8_t>::attach(
        count, {{{b, F::tile_bytes}, {y, F::tile_rows}, {x, F::tile_rows}}});
    const auto short_view = *sp::view<F, std::uint8_t>::attach(
        count - 1, {{{b, F::tile_bytes}, {y, F::tile_rows}, {z, F::tile_rows}}});
    const auto expression = cp::describe(first);
    auto substitute = [&](const auto& source) {
        return decltype(expression){expression.payload, expression.head0, cp::describe(source).head1};
    };
    sp::mutation_diagnostic diagnostic;
    IKEA_CHECK(cp::prepare_mutation(substitute(second), count));
    const auto alias = cp::prepare_mutation(substitute(overlapping), count, &diagnostic);
    IKEA_CHECK(!alias && alias.error() == sp::error::overlap);
    IKEA_CHECK(diagnostic.source == &overlapping && diagnostic.conflicting_source == &first);
    IKEA_CHECK(diagnostic.plane == 2 && diagnostic.conflicting_plane == 1);
    const auto short_result = cp::prepare_mutation(substitute(short_view), count, &diagnostic);
    IKEA_CHECK(!short_result && short_result.error() == sp::error::range);
    IKEA_CHECK(diagnostic.source == &short_view);

    // Unused leaves do not acquire obligations; a zero-count command is valid.
    reject_allocation = true;
    const auto empty = cp::prepare_mutation(substitute(overlapping), 0);
    const auto unavailable = cp::prepare_mutation(substitute(second), count, &diagnostic);
    reject_allocation = false;
    IKEA_CHECK(empty);
    IKEA_CHECK(!unavailable && unavailable.error() == sp::error::allocation);
    IKEA_CHECK(diagnostic.source == &first);
}

int main() {
    check_single_view<sp::format<20>>();
    check_single_view<sp::format<31, sp::geometry::striped, 16>>();
    check_single_view<sp::format<16, sp::geometry::local, 16>>();
    check_single_view<sp::format<3, sp::geometry::striped>>();
    check_substituted_views();
}
