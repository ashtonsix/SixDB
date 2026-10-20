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
void append_unchecked(Access a, bytes input, unsigned reservation, Effects& effects) {
    const auto& dst = a.source();
    metadata_effects mapped{effects};
    boundlist::sidecar::author::append_unchecked(
        a, input.size(), reservation, mapped, [&](unsigned begin) __attribute__((always_inline)) {
            emit(dst, effects, plane::payload, begin, reservation);
            if (!input.empty())
                std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
            if (reservation > input.size())
                std::memset(dst.windows().payload.data() + begin + input.size(), 0,
                            reservation - input.size());
        });
}
template <class Access, class Effects>
void append_fixed_run_unchecked(Access a, bytes input, unsigned count, Effects& effects) {
    // Admitted fixed() && !has_used() ranks in one real view: count is nonzero, the entire
    // run fits, and input.size() == count * fixed_width(). All source/effect
    // aliases are admitted before entry. The cursor/recipe may be snapshots;
    // effects still identify the original named view through a.source().
    const auto& dst = a.source();
    metadata_effects mapped{effects};
    boundlist::sidecar::author::append_fixed_run_unchecked(
        a, count, mapped, [&](unsigned begin) __attribute__((always_inline)) {
            emit(dst, effects, plane::payload, begin, input.size());
            if (!input.empty())
                std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
        });
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
    metadata_effects mapped{effects};
    boundlist::sidecar::author::set_used_unchecked(a, rank, used + suffix.size(), mapped);
}
template <class Access, class Effects>
void replace_unchecked(Access a, unsigned rank, bytes input, Effects& effects) {
    const auto& dst = a.source();
    const auto begin = a.start(rank);
    emit(dst, effects, plane::payload, begin, input.size());
    if (!input.empty())
        std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
    metadata_effects mapped{effects};
    boundlist::sidecar::author::replace_used_unchecked(a, rank, input.size(), mapped);
}
template <class Effects> void retire_unchecked(const view& dst, unsigned rank, Effects& effects) {
    metadata_effects mapped{effects};
    boundlist::sidecar::author::retire_unchecked(dst, rank, mapped);
}
template <class Effects> void retire_unchecked(const view&&, unsigned, Effects&) = delete;
template <class Access, class Effects>
void soft_pack_unchecked(Access a, bool shrink, Effects& effects) {
    const auto& dst = a.source();
    metadata_effects mapped{effects};
    boundlist::sidecar::author::soft_pack_unchecked(
        a, shrink, mapped,
        [&](unsigned from, unsigned to, unsigned width) __attribute__((always_inline)) {
            emit(dst, effects, plane::payload, to, width);
            std::memmove(dst.windows().payload.data() + to, dst.windows().payload.data() + from,
                         width);
        });
}
} // namespace ikea::streampack::author
