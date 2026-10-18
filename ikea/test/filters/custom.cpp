#include <ikea/seriespack/author/expression.h>
#include <cassert>

// A same-named member is not permission to discard an authored node's meaning.
namespace ikea::seriespack::composition {
struct decoration {
    static constexpr unsigned bit_width = 12;
};
struct authored_values {
    static constexpr unsigned bit_width = 12;
    decoration expression;
    const std::uint64_t* values;
};
struct absent_tail {
    static constexpr unsigned bit_width = 0;
};
inline scalar_value<12> read(scalar_ops&, const decoration&, std::size_t, bool) {
    return {0};
}
inline scalar_value<12> read(scalar_ops&, const authored_values& source, std::size_t row, bool) {
    return {source.values[row]};
}
} // namespace ikea::seriespack::composition
#include <ikea/seriespack/author/indexof.h>

void check_authored_filters() {
    namespace cp = ikea::seriespack::composition;
    constexpr std::uint64_t values[]{0, 1, 15, 16, 255, 256, 4094, 4095};
    const cp::authored_values source{{}, values};
    assert(cp::filter8_unchecked<ikea::comparison::equal>(source, 0, 0) == 1);
    assert(cp::filter8_unchecked<ikea::comparison::less>(source, 0, 256) == 31);
    assert(cp::filter8_unchecked<ikea::comparison::greater_equal>(source, 0, 256) == 224);
    assert(cp::filter8_unchecked<ikea::comparison::equal>(source, 0, 4095, 127) == 0);
    const cp::authored_values absent{{}, nullptr};
    assert(cp::filter8_unchecked<ikea::comparison::equal>(absent, 0, 0, 0) == 0);
    assert(cp::filter8_unchecked<ikea::comparison::less>(absent, 0, 4096) == 255);

    // A dense 17-bit source's 16-bit body is not a contiguous u16 array:
    // residual bytes separate its tiles. Substitution preserves that geometry.
    namespace sp = ikea::seriespack;
    std::uint8_t bytes[51]{};
    for(unsigned i=0;i<24;++i) bytes[(i/8)*17+(i%8)*2]=i;
    const sp::dense_source<sp::format<17>> dense{bytes,24};
    const sp::dense_source<sp::format<16>> outer{nullptr,24};
    using Body = cp::leaf<decltype(dense),cp::field_kind::body>;
    const cp::payload_expression<16,Body,cp::absent_tail> body{{dense},{}};
    const auto substituted=cp::with_payload(cp::describe(outer),body);
    assert(cp::filter_unchecked<ikea::comparison::equal>(substituted,0,24,8)==256);
    assert(cp::filter_unchecked<ikea::comparison::less>(substituted,0,24,20)==0xfffff);
    assert(cp::indexof_unchecked(substituted,0,24,8)==8);
    assert(cp::indexof_unchecked(substituted,8,16,23)==15);
    assert(cp::indexof_unchecked(substituted,0,24,8,255)==24);
}
