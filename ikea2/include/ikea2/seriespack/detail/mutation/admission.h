#pragma once
#include <ikea2/seriespack/detail/mutation/fields.h>
#include <ikea2/seriespack/detail/mutation/assignment.h>
#include <ikea2/seriespack/detail/mutation/admission_state.h>
namespace ikea2::seriespack::composition {
namespace detail {
struct write_admission : writable_fields {
    using writable_fields::writable_fields;
    template <unsigned, unsigned> write_token project(write_token) const {
        return {};
    }
    template <class S, field_kind Field>
    void write(const leaf<S, Field>& leaf, std::size_t, write_token) {
        static_assert(std::is_same_v<typename S::byte_type, std::uint8_t>,
                      "A writable expression needs mutable admitted leaf views");
        using F = typename S::format_type;
        const auto& source = leaf.source;
        if (count > source.size() && valid) {
            valid = false;
            diagnostic = {&source};
        }
        if (!count || !valid)
            return;
        const auto tiles = count / F::tile_rows + (count % F::tile_rows != 0);
        ikea2::seriespack::detail::tile_fields<F, write_field<Field>>([&](unsigned plane,
                                                                          std::size_t offset,
                                                                          std::size_t bytes) {
            const auto& stream = source.stream(plane);
            writable_fields::add({reinterpret_cast<std::uintptr_t>(stream.bytes.data()) + offset,
                                  stream.stride, bytes, tiles},
                                 &source, plane);
        });
    }
};
struct count_writes {
    std::size_t count = 0;
    template <class S> void before(const S&, byte_write) {
        ++count;
    }
};
} // namespace detail

} // namespace ikea2::seriespack::composition
