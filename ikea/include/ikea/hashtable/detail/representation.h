#pragma once
#include "bits.h"
#include "../kernel.h"
#include <ikea/seriespack/author/read.h>
#include <ikea/seriespack/author/write.h>
#include <ikea/seriespack/author/local_equal.h>
#include <ikea/seriespack/author/indexof.h>
#include <ikea/tuplepack/author/execution.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <type_traits>
#include <vector>

namespace ikea::hashtable {
namespace sp = ikea::seriespack;
template <unsigned Bits> [[gnu::always_inline]] word low_bits(word value) {
#if defined(__BMI2__) && defined(__x86_64__)
    if constexpr (Bits > 32 && Bits < 64) {
        // Clang can lower a constant mask with MOVB into the previous slot's
        // register, then BZHI. BZHI depends on the full count register even
        // though its mathematical count is the low byte. Define the whole
        // register explicitly with a scratch lifetime limited to this mask.
        word count, result;
        asm("movl %3, %k1\n\tbzhiq %1, %2, %0"
            : "=r"(result), "=&r"(count)
            : "r"(value), "i"(Bits));
        return result;
    }
#endif
    return value & mask<Bits>;
}
inline void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <std::size_t N> using bytes = std::array<byte, N>;
template <unsigned N> inline void store(byte* p, word v) {
    std::memcpy(p, &v, N);
}
// A primary bucket contains complete eight-row SeriesPack tiles. Construction
// initializes the assigned bucket; tile accesses stay within its byte extent.
template <unsigned Bits> auto placed(byte* p, unsigned rows) {
    using F = sp::format<Bits, sp::geometry::local, 0>;
    return sp::view<F, byte>::assume_valid(rows,
                                           {{{{p, std::size_t(rows / 8) * Bits}, Bits}, {}, {}}});
}
template <unsigned Bits> inline word point(const byte* p, unsigned row, unsigned rows) {
    using F = sp::format<Bits, sp::geometry::local, 0>;
    const sp::dense_source<F> source{p, rows};
    sp::composition::scalar_ops ops;
    return sp::composition::read(ops, sp::composition::describe(source), row, true).value;
}
struct fused_scalar_ops : sp::composition::scalar_ops {
    template <class S, sp::composition::field_kind Field>
    auto read(const sp::composition::leaf<S, Field>& field, std::size_t row, bool active) const {
        using F = typename S::format_type;
        if constexpr (requires { field.source.data; } &&
                      Field == sp::composition::field_kind::body && F::tail == 0 &&
                      F::storage == sp::geometry::local)
            return sp::composition::scalar_value<F::width>{
                load<F::body>(field.source.data + row * F::body)};
        else
            return sp::composition::scalar_ops::read(field, row, active);
    }
};
template <unsigned Bits> inline word direct_point(const byte* p, unsigned row, unsigned rows) {
    using F = sp::format<Bits, sp::geometry::local, 0>;
    const sp::dense_source<F> source{p, rows};
    fused_scalar_ops ops;
    return sp::composition::read(ops, sp::composition::describe(source), row, true).value;
}
// The Local residual consists of R adjacent bytes, one bit per row in each.
// Update exactly those bytes together; no readable padding or extra issued
// footprint is assumed. Other lane bits survive the read/modify/write.
struct word_tail_write_ops : sp::composition::scalar_write_ops {
    template <class S, sp::composition::field_kind Field, unsigned Bits>
    void write(const sp::composition::leaf<S, Field>& destination, std::size_t row,
               sp::composition::scalar_value<Bits> value) const {
#if defined(__BMI2__) && defined(__x86_64__)
        using F = typename S::format_type;
        if constexpr (Field == sp::composition::field_kind::tail &&
                      F::storage == sp::geometry::local) {
            constexpr auto R = F::tail;
            constexpr word columns = 0x0101010101010101ULL >> (8 * (8 - R));
            const auto& plane = destination.source.stream(0);
            auto* tail = plane.bytes.data() + (row / 8) * plane.stride + 8 * F::body;
            const auto lane = row % 8;
            const auto expanded = _pdep_u64(value.value, columns) << lane;
            store<R>(tail, (load<R>(tail) & ~(columns << lane)) | expanded);
        } else
#endif
            sp::composition::scalar_write_ops::write(destination, row, value);
    }
};
template <unsigned Bits> void set_point(byte* p, unsigned row, unsigned rows, word v) {
    auto view = placed<Bits>(p, rows);
    word_tail_write_ops ops;
    sp::composition::assign(ops, sp::composition::describe(view), row,
                            sp::composition::scalar_value<Bits>{v});
}
// With no residual plane, complete Local tiles are one contiguous byte array.
// Preserve the SeriesPack assignment operation while making that address law
// visible to the compiler, as the maintained reader already does.
struct contiguous_write_ops : word_tail_write_ops {
    template <class S, sp::composition::field_kind Field, unsigned Bits>
    void write(const sp::composition::leaf<S, Field>& destination, std::size_t row,
               sp::composition::scalar_value<Bits> value) const {
        using F = typename S::format_type;
        if constexpr (Field == sp::composition::field_kind::body && F::tail == 0 &&
                      F::storage == sp::geometry::local)
            store<F::body>(destination.source.stream(0).bytes.data() + row * F::body, value.value);
        else
            word_tail_write_ops::write(destination, row, value);
    }
};
template <unsigned Bits>
[[gnu::always_inline]] void assign_point(byte* p, unsigned row, unsigned rows, word value) {
    auto view = placed<Bits>(p, rows);
    contiguous_write_ops ops;
    sp::composition::assign(ops, sp::composition::describe(view), row,
                            sp::composition::scalar_value<Bits>{value});
}

/// Mixed upserts share key/value stores; existing-heavy upserts avoid them.
enum class write_strategy { mixed, existing };
[[gnu::always_inline]] inline unsigned select_candidates(bool fresh, unsigned found,
                                                         unsigned vacancy) {
#if defined(__x86_64__)
    asm("testb %b2, %b2\n\tcmovnel %k1, %k0" : "+r"(found) : "r"(vacancy), "r"(fresh) : "cc");
    return found;
#elif defined(__aarch64__)
    asm("cmp %w2, #0\n\tcsel %w0, %w1, %w0, ne" : "+r"(found) : "r"(vacancy), "r"(fresh) : "cc");
    return found;
#else
    return fresh ? vacancy : found;
#endif
}
// TuplePack projections use bounded contiguous fields. A wide tuple is composed
// of adjacent units (TuplePack admits at most 64 physical bytes per unit).
template <unsigned Bytes> struct tuple_field {
    inline static const auto plan = [] {
        std::array<ikea::tuplepack::code, Bytes> codes;
        std::array<byte, Bytes> map;
        for (unsigned i = 0; i < Bytes; ++i) {
            codes[i] = {byte(i), 0, 8};
            map[i] = byte(i);
        }
        auto layout = ikea::tuplepack::layout::make(Bytes, codes);
        require(bool(layout), "TuplePack layout");
        auto read = ikea::tuplepack::reader<8>::make(*layout, map);
        require(bool(read), "TuplePack projection");
        const auto& transfer = read->controls().transfer;
        require(transfer.bytes == Bytes, "TuplePack contiguous transfer");
        return *read;
    }();
    [[gnu::always_inline]] static word read(const byte* p) {
        return ikea::tuplepack::native::read_transfer_body<1, Bytes>(
            plan.controls(), [&](unsigned) { return p; }, 1, Bytes);
    }
};

} // namespace ikea::hashtable
