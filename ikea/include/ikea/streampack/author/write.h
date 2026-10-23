#pragma once
#include <cstring>
#include <ikea/effects.h>
#include <ikea/streampack/author/read.h>
#include <ikea/streampack/detail/compact/author/write.h>

namespace ikea::streampack::author {
/// These bodies borrow an admitted writable named view. Command, capacity,
/// aliasing, exclusion and infallible effects are proved before entry. Hooks
/// cannot allocate, fail, suspend or invalidate any borrowed input.
template <class Effects> requires ikea::effects<Effects, view>
inline void emit(const view &dst, Effects &effects, plane stream, unsigned offset, unsigned size) noexcept {
    if (size)
        effects.before(dst, unsigned(stream), offset, size);
}
template <class Effects> requires ikea::effects<Effects, view>
struct metadata_effects {
    Effects &output;
    template <class Source>
    void before(const Source &source, unsigned plane, std::size_t offset,
                std::size_t size) noexcept {
        // Sidecar metadata 0/1/2 -> StreamPack 1/2/3.
        output.before(source, plane + 1, offset, size);
    }
};
/// Temporary admitted append computation. Carries geometry/count across a
/// complete unpublished run; it owns no storage or publication. Inputs, ranges,
/// aliases and effect capacity are admitted before constructing it. Do not read
/// the in-progress source or escape the cursor; finish publishes controls once.
template <class Access, class Effects> requires ikea::effects<Effects, view> class value_encoder {
    // Borrow the owner policy directly. A mapped policy stored inside this cursor
    // and borrowed by geometry creates an address-escaped reference cycle.
    Effects &effects_;
    ikea::streampack::detail::compact::author::encoder<Access> geometry_;

  public:
    value_encoder(Access a, unsigned first, unsigned begin, Effects &effects)
        : effects_(effects), geometry_(a, first, begin) {}
    value_encoder(const value_encoder &) = delete;
    value_encoder(value_encoder &&) = delete;
    [[gnu::always_inline]] void append(bytes input, unsigned reservation) {
        const auto a = geometry_.access();
        const auto &dst = a.source();
        const auto &l = a.format();
        const auto n = geometry_.rank(), begin = geometry_.position();
        auto &effects = effects_;
        metadata_effects mapped{effects};
        using W = typename Access::width_type;
        using U = typename Access::used_type;
        // Capture count/extent before additional stores: payload aliases cannot
        // force another dependent control load across this composition seam.
        emit(dst, effects, plane::payload, begin, reservation);
        if (!input.empty())
            std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
        if (reservation > input.size())
            std::memset(dst.windows().payload.data() + begin + input.size(), 0,
                        reservation - input.size());
        const bool r = !l.fixed() && Access::family != geometry::absolute_ends;
        if (!(n % 16)) {
            if (r && l.has_used() && l.schema().lengths == placement::grouped) {
                const auto group = a.width_offset(n);
                emit(dst, effects, plane::lengths, group, l.group_stride());
                std::memset(dst.windows().lengths.data() + group, 0, l.group_stride());
            } else {
                if (r) {
                    const auto group = a.width_offset(n);
                    emit(dst, effects, plane::lengths, group, 16 * sizeof(W));
                    std::memset(dst.windows().lengths.data() + group, 0, 16 * sizeof(W));
                }
                if (l.has_used()) {
                    const auto group = a.used_offset(n);
                    emit(dst, effects, plane::lengths, group, 16 * sizeof(U));
                    std::memset(dst.windows().lengths.data() + group, 0, 16 * sizeof(U));
                }
            }
        } else {
            if (r)
                emit(dst, effects, plane::lengths, a.width_offset(n), sizeof(W));
            if (l.has_used())
                emit(dst, effects, plane::lengths, a.used_offset(n), sizeof(U));
        }
        geometry_.append(reservation, mapped, r);
        if (l.has_used())
            detail::store(dst.windows().lengths.data() + a.used_offset(n), U(input.size()));
        if (l.has_tombstones())
            ikea::streampack::detail::compact::author::make_live_unchecked(dst, n, 1, mapped);
    }
    [[gnu::always_inline]] void finish() {
        metadata_effects mapped{effects_};
        ikea::streampack::detail::compact::author::publish_size_unchecked(geometry_.access(), geometry_.rank(),
                                                           geometry_.position(), mapped);
    }
};
template <class Access, class Effects> requires ikea::effects<Effects, view>
void append_fixed_run_unchecked(Access a, bytes input, unsigned count, Effects &effects) {
    // Admitted fixed() && !has_used() layout and nonempty in-capacity run;
    // input.size() == count * fixed_width() and the whole payload fits. The
    // enclosing command proves exclusion, aliasing and effect capacity.
    const auto &dst = a.source();
    const auto &l = a.format();
    const auto n = dst.size(), begin = dst.extent();
    emit(dst, effects, plane::payload, begin, input.size());
    if (!input.empty())
        std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
    metadata_effects mapped{effects};
    if (l.has_tombstones())
        ikea::streampack::detail::compact::author::make_live_unchecked(dst, n, count, mapped);
    ikea::streampack::detail::compact::author::publish_size_unchecked(a, n + count,
                                                       begin + count * l.fixed_width(), mapped);
}
template <class Access, class Effects> requires ikea::effects<Effects, view>
void replace_fixed_run_unchecked(Access a, unsigned rank, bytes input, Effects &effects) {
    // Admitted nonempty contiguous live fixed() && !has_used() ranks. Input contains whole
    // values and fits this run; replacement does not alter its rank count.
    const auto &dst = a.source();
    const auto begin = a.start(rank);
    emit(dst, effects, plane::payload, begin, input.size());
    if (!input.empty())
        std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
}
template <class Access, class Effects> requires ikea::effects<Effects, view>
[[gnu::always_inline]] void set_used_unchecked(Access a, unsigned rank, unsigned length,
                                               Effects &effects) {
    using W = typename Access::used_type;
    const auto offset = a.used_offset(rank);
    emit(a.source(), effects, plane::lengths, offset, sizeof(W));
    detail::store(a.source().windows().lengths.data() + offset, W(length));
}
template <class Access, class Effects> requires ikea::effects<Effects, view>
[[gnu::always_inline]] void replace_used_unchecked(Access a, unsigned rank, unsigned length,
                                                   Effects &effects) {
    if (a.format().has_used())
        set_used_unchecked(a, rank, length, effects);
}
template <class Access, class Effects> requires ikea::effects<Effects, view>
void extend_unchecked(Access a, unsigned rank, bytes suffix, Effects &effects) {
    // An admitted live rank with has_used(), U <= R, and suffix.size() <= R-U.
    // U+suffix fits the schema maximum and width carrier. Source/effect alias
    // admission and both write ranges precede entry; hooks cannot invalidate
    // borrowed input. The rank's reservation, geometry and liveness stay fixed.
    if (suffix.empty())
        return;
    const auto &dst = a.source();
    const auto used = a.length(rank), begin = a.start(rank);
    emit(dst, effects, plane::payload, begin + used, suffix.size());
    std::memmove(dst.windows().payload.data() + begin + used, suffix.data(), suffix.size());
    set_used_unchecked(a, rank, used + suffix.size(), effects);
}
} // namespace ikea::streampack::author
