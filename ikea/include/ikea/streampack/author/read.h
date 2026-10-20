#pragma once
#include <ikea/boundlist/author/read.h>
#include <ikea/streampack/view.h>

namespace ikea::streampack {
using boundlist::sidecar::byte_owner;
using boundlist::sidecar::location;
namespace author {
template <geometry Family, class Width, class Offset, class Byte>
class access : public boundlist::sidecar::author::access<Family, Width, Offset, basic_view<Byte>> {
    using base = boundlist::sidecar::author::access<Family, Width, Offset, basic_view<Byte>>;
    access(const basic_view<Byte>& source, const layout& format) : base(source, format) {}

  public:
    using base::base;
    using base::format;
    using base::length;
    using base::live;
    using base::locate;
    using base::size;
    using base::source;
    using base::start;
    access rebind(const basic_view<Byte>& source) const {
        return {source, format()};
    }
    access rebind(const basic_view<Byte>&&) const = delete;
    // Value is the fused hot-path boundary, just like get: outlining even this
    // span return adds a call per lookup and prevents schema facts from hoisting.
    [[gnu::always_inline]] bytes value(unsigned rank) const noexcept {
        if (format().has_used())
            return source().windows().payload.subspan(start(rank), length(rank));
        const auto p = locate(rank);
        return source().windows().payload.subspan(p.offset, p.length);
    }
    /// Keep cheap admission with its consumer: outlining expected<span> creates
    /// a hidden result buffer and prevents shared schema facts from hoisting.
    [[nodiscard, gnu::always_inline]] std::expected<bytes, error> get(unsigned rank) const {
        if (rank >= size())
            return std::unexpected(error::rank);
        if (!live(rank))
            return std::unexpected(error::retired);
        [[clang::always_inline]] return value(rank);
    }
    template <class Sink>
    [[gnu::always_inline]] void range(unsigned first, unsigned count, Sink&& sink) const {
        base::range(first, count, [&](unsigned rank, location p) __attribute__((always_inline)) {
            sink(rank, source().windows().payload.subspan(p.offset, p.length));
        });
    }
    /// Sparse selections retain a point path; duplicates and order are retained.
    template <class Sink> void select(std::span<const std::uint16_t> ranks, Sink&& sink) const {
        for (auto rank : ranks)
            if (live(rank))
                sink(rank, value(rank));
    }
};
/// Resolve schema-sized lanes once around a complete authored operation. The
/// caller's body receives a concrete geometry; no erased per-rank callback.
template <class Byte, class Body>
decltype(auto) dispatch(const basic_view<Byte>& source, Body&& body) {
    return boundlist::sidecar::detail::dispatch(
        source.format(), [&]<geometry G, class W, class O>() -> decltype(auto) {
            return body(access<G, W, O, Byte>(source));
        });
}
} // namespace author

} // namespace ikea::streampack
