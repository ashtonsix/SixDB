#pragma once
#include <cstring>
#include <ikea/effects.h>
#include <ikea/streampack/author/read.h>
#include <ikea/boundlist/author/write.h>

namespace ikea::streampack::author {
/// These bodies borrow an admitted writable named view. Command, capacity,
/// aliasing, exclusion and infallible effects are proved before entry. Hooks
/// cannot allocate, fail, suspend or invalidate any borrowed input.
template <class Effects>
inline void emit(const view& dst, Effects& effects, plane stream, unsigned offset, unsigned size) {
    if (size)
        effects.before(dst, byte_write{unsigned(stream), offset, size});
}
template <class Effects> struct metadata_effects {
    Effects& output;
    template <class Source> void before(const Source& source, byte_write w) {
        ++w.plane; // Sidecar metadata 0/1/2 -> StreamPack 1/2/3.
        output.before(source, w);
    }
};
template <class Effects> void initialize_unchecked(const view& dst, Effects& effects) {
    metadata_effects mapped{effects};
    boundlist::sidecar::author::initialize_unchecked(dst, mapped);
}
template <class Effects> void initialize_unchecked(const view&&, Effects&) = delete;
template <class Access, class Effects>
[[gnu::always_inline]] void append_unchecked(Access a, bytes input, unsigned reservation,
                                             Effects& effects) {
    const auto& dst = a.source();
    const auto& l = a.format();
    const auto n = dst.size(), begin = dst.extent();
    using W = typename Access::width_type;
    // Capture count/extent before additional stores: payload aliases cannot
    // force another dependent control load across this composition seam.
    emit(dst, effects, plane::payload, begin, reservation);
    if (!input.empty())
        std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
    if (reservation > input.size())
        std::memset(dst.windows().payload.data() + begin + input.size(), 0,
                    reservation - input.size());
    metadata_effects mapped{effects};
    if (!l.fixed())
        boundlist::sidecar::author::write_origin_unchecked(a, n, begin, reservation, mapped);
    const bool r = !l.fixed() && Access::family == geometry::width;
    if (!(n % 16) && (r || l.has_used())) {
        const auto group = (n / 16) * l.group_stride();
        emit(dst, effects, plane::lengths, group, l.group_stride());
        std::memset(dst.windows().lengths.data() + group, 0, l.group_stride());
        if (r && l.has_used() && l.schema().lengths == placement::separated) {
            emit(dst, effects, plane::lengths, group + l.used_offset(), 16 * sizeof(W));
            std::memset(dst.windows().lengths.data() + group + l.used_offset(), 0, 16 * sizeof(W));
        }
    } else {
        if (r)
            emit(dst, effects, plane::lengths, a.width_offset(n), sizeof(W));
        if (l.has_used())
            emit(dst, effects, plane::lengths, a.used_offset(n), sizeof(W));
    }
    if (r)
        boundlist::sidecar::author::write_width_unchecked(a, n, reservation, mapped, true);
    if (l.has_used())
        detail::store(dst.windows().lengths.data() + a.used_offset(n), W(input.size()));
    if (l.has_tombstones())
        boundlist::sidecar::author::make_live_unchecked(dst, n, 1, mapped);
    boundlist::sidecar::author::publish_size_unchecked(a, n + 1, begin + reservation, mapped);
}
template <class Access, class Effects>
void append_fixed_run_unchecked(Access a, bytes input, unsigned count, Effects& effects) {
    // Admitted fixed() && !has_used() layout and nonempty in-capacity run;
    // input.size() == count * fixed_width() and the whole payload fits. The
    // enclosing command proves exclusion, aliasing and effect capacity.
    const auto& dst = a.source();
    const auto& l = a.format();
    const auto n = dst.size(), begin = dst.extent();
    emit(dst, effects, plane::payload, begin, input.size());
    if (!input.empty())
        std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
    metadata_effects mapped{effects};
    if (l.has_tombstones())
        boundlist::sidecar::author::make_live_unchecked(dst, n, count, mapped);
    boundlist::sidecar::author::publish_size_unchecked(a, n + count,
                                                       begin + count * l.fixed_width(), mapped);
}
template <class Access, class Effects>
void replace_fixed_run_unchecked(Access a, unsigned rank, bytes input, Effects& effects) {
    // Admitted nonempty contiguous live fixed() && !has_used() ranks. Input contains whole
    // values and fits this run; replacement does not alter its rank count.
    const auto& dst = a.source();
    const auto begin = a.start(rank);
    emit(dst, effects, plane::payload, begin, input.size());
    if (!input.empty())
        std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
}
template <class Access, class Effects>
[[gnu::always_inline]] void set_used_unchecked(Access a, unsigned rank, unsigned length,
                                               Effects& effects) {
    using W = typename Access::width_type;
    const auto offset = a.used_offset(rank);
    emit(a.source(), effects, plane::lengths, offset, sizeof(W));
    detail::store(a.source().windows().lengths.data() + offset, W(length));
}
template <class Access, class Effects>
[[gnu::always_inline]] void replace_used_unchecked(Access a, unsigned rank, unsigned length,
                                                   Effects& effects) {
    if (a.format().has_used())
        set_used_unchecked(a, rank, length, effects);
}
template <class Access, class Effects>
void extend_unchecked(Access a, unsigned rank, bytes suffix, Effects& effects) {
    // An admitted live rank with has_used(), U <= R, and suffix.size() <= R-U.
    // U+suffix fits the schema maximum and width carrier. Source/effect alias
    // admission and both write ranges precede entry; hooks cannot invalidate
    // borrowed input. The rank's reservation, geometry and liveness stay fixed.
    if (suffix.empty())
        return;
    const auto& dst = a.source();
    const auto used = a.length(rank), begin = a.start(rank);
    emit(dst, effects, plane::payload, begin + used, suffix.size());
    std::memmove(dst.windows().payload.data() + begin + used, suffix.data(), suffix.size());
    set_used_unchecked(a, rank, used + suffix.size(), effects);
}
template <class Access, class Effects>
void replace_unchecked(Access a, unsigned rank, bytes input, Effects& effects) {
    const auto& dst = a.source();
    const auto begin = a.start(rank);
    emit(dst, effects, plane::payload, begin, input.size());
    if (!input.empty())
        std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
    replace_used_unchecked(a, rank, input.size(), effects);
}
template <class Effects> void retire_unchecked(const view& dst, unsigned rank, Effects& effects) {
    metadata_effects mapped{effects};
    boundlist::sidecar::author::retire_unchecked(dst, rank, mapped);
}
template <class Effects> void retire_unchecked(const view&&, unsigned, Effects&) = delete;
/// One fused value traversal. Capture original R before rewriting preceding
/// endpoints; U determines shrinking only here, above the geometry kernels.
template <class Access, class Effects>
[[gnu::always_inline]] void soft_pack_unchecked(Access a, bool shrink, Effects& effects) {
    const auto& dst = a.source();
    metadata_effects mapped{effects};
    if (a.format().fixed())
        return;
    const auto n = a.size();
    unsigned to = 0;
    a.slots(0, n, 0,
            [&](unsigned rank, unsigned from, unsigned reservation) __attribute__((always_inline)) {
                const auto used = a.format().has_used() ? a.length(rank) : reservation;
                const auto width = a.live(rank) ? (shrink ? used : reservation) : 0;
                if (width && from != to) {
                    emit(dst, effects, plane::payload, to, width);
                    std::memmove(dst.windows().payload.data() + to,
                                 dst.windows().payload.data() + from, width);
                }
                boundlist::sidecar::author::write_slot_unchecked(a, rank, to, width, mapped);
                if (a.format().has_used() && !a.live(rank))
                    set_used_unchecked(a, rank, 0, effects);
                to += width;
            });
    boundlist::sidecar::author::finish_rewrite_unchecked(a, to, mapped);
}
} // namespace ikea::streampack::author
