#pragma once
#include <ikea/seriespack/detail/mutation/fields.h>
#include <ikea/seriespack/detail/mutation/composed.h>
#include <ikea/seriespack/detail/mutation/construction.h>

namespace ikea::seriespack::composition {
#if defined(__aarch64__) || defined(__AVX2__)
namespace construction_detail {
template <class S, field_kind Field, class Emit>
void field_tiles(const leaf<S, Field>& field, std::size_t first, std::size_t count, Emit&& emit) {
    if (!count)
        return;
    using F = typename S::format_type;
    ikea::seriespack::detail::tile_fields<F, write_field<Field>>(
        [&](unsigned p, std::size_t offset, std::size_t size) {
            const auto stride = field.source.stream(p).stride;
            if (stride == size)
                emit(byte_write{p, first * stride + offset, count * size});
            else
                for (std::size_t t = first; t < first + count; ++t)
                    emit(byte_write{p, t * stride + offset, size});
        });
}
struct extent_check {
    std::size_t count;
    bool valid = true;
    template <unsigned, unsigned> write_token project(write_token) const {
        return {};
    }
    template <class S, field_kind Field>
    void write(const leaf<S, Field>& field, std::size_t, write_token) {
        valid &= field.source.size() == count;
    }
};
template <class Coverage> struct storage_coverage {
    std::size_t count;
    Coverage& coverage;
    template <unsigned, unsigned> write_token project(write_token) const {
        return {};
    }
    template <class S, field_kind Field>
    void write(const leaf<S, Field>& field, std::size_t, write_token) const {
        using F = typename S::format_type;
        const auto tiles = count / F::tile_rows + (count % F::tile_rows != 0);
        field_tiles(field, 0, tiles,
                    [&](byte_write bytes) { coverage.before(field.source, bytes); });
    }
};
struct clear_last {
    std::size_t count;
    template <unsigned, unsigned> write_token project(write_token) const {
        return {};
    }
    template <class S, field_kind Field>
    void write(const leaf<S, Field>& field, std::size_t, write_token) const {
        using F = typename S::format_type;
        if (count && (count % F::tile_rows || (F::tile_rows == 8 && count % 16)))
            field_tiles(field, (count - 1) / F::tile_rows, 1, [&](byte_write bytes) {
                std::memset(field.source.stream(bytes.plane).bytes.data() + bytes.offset, 0,
                            bytes.size);
            });
    }
};
struct no_effects {
    template <class S> void before(const S&, byte_write) const noexcept {}
};
} // namespace construction_detail

template <class E, class Coverage>
void visit_storage(const E& expression, std::size_t count, Coverage& coverage) {
    construction_detail::storage_coverage<Coverage> ops{count, coverage};
    assign(ops, expression, 0, write_token{});
}

/// Fresh/reinitialized owned fields. Each leaf's logical extent equals count;
/// unrelated fields and placement gaps are preserved. Last-tile slack is zero.
template <class E, class U, class Coverage>
void initialize_unchecked(const E& expression, std::size_t count, const U* input,
                          Coverage& coverage) {
    if (!count)
        return;
    composition::visit_storage(expression, count, coverage);
    construction_detail::clear_last clear{count};
    assign(clear, expression, 0, write_token{});
    no_summary summary;
    construction_detail::no_effects recorded;
    composition::replace_unchecked(
        expression, 0, count, input, [](std::size_t) { return std::uint16_t{0xffff}; }, summary,
        recorded);
}
#endif
} // namespace ikea::seriespack::composition
