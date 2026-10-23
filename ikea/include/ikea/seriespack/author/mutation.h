#pragma once
#include <ikea/seriespack/author/expression.h>
#include <ikea/seriespack/detail/effects.h>
#include <ikea/seriespack/detail/wire.h>
#include <ikea/seriespack/view.h>
#include <ikea/seriespack/detail/mutation/fields.h>
#include <ikea/seriespack/author/summaries.h>
#if defined(__aarch64__)
#include <ikea/seriespack/detail/native/neon/write.h>
#elif defined(__AVX2__)
#include <ikea/seriespack/detail/native/avx2/write.h>
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
#include <ikea/seriespack/detail/native/avx512/write.h>
#endif
#endif

namespace ikea::seriespack::author {

/// This footprint belongs to write16: complete Local packets, contiguous
/// body bytes, and the one or two striped residual fields it actually stores.
/// Masks suppress an empty region; nonempty selected writes preserve inactive
/// values in native lanes and may issue stores covering those neighbors.
template <class F, unsigned Fields = 15, class Emit>
[[gnu::always_inline]] inline void visit_writes16(const basic_view<F, ikea::byte>& v, std::size_t i, Emit&& emit) {
    const auto lane = i % F::tile_rows, tile = i / F::tile_rows;
    if constexpr (F::payload) {
        const auto offset = tile * v.stream(0).stride;
        if constexpr (F::storage == geometry::local) {
            ikea::seriespack::detail::each<2>([&](auto p) {
                const auto start = offset + p * v.stream(0).stride;
                if constexpr ((Fields & 3) == 3)
                    emit(ikea::detail::write_span{0, start, F::tile_bytes});
                else {
                    if constexpr (F::body && (Fields & 1))
                        emit(ikea::detail::write_span{0, start, 8 * F::body});
                    if constexpr (F::tail && (Fields & 2))
                        emit(ikea::detail::write_span{0, start + 8 * F::body, F::tail});
                }
            });
        } else {
            if constexpr (F::body && (Fields & 1))
                emit(ikea::detail::write_span{0, offset + ikea::seriespack::detail::body_offset<F>(lane), 16 * F::body});
            if constexpr (F::tail && (Fields & 2))
                ikea::seriespack::detail::tail_fragments<F::tail>(lane / 32, [&](unsigned s, unsigned, unsigned) {
                    emit(ikea::detail::write_span{0, offset + ikea::seriespack::detail::stripe_offset<F>(s) + lane % 32, 16});
                });
        }
    }
    ikea::seriespack::detail::each<F::heads / 8>([&](auto p) {
        if constexpr (Fields & (4u << p)) {
            const auto& head = v.stream(p + 1);
            const auto offset = tile * head.stride + lane;
            if constexpr (F::storage == geometry::local) {
                emit(ikea::detail::write_span{p + 1, offset, 8});
                emit(ikea::detail::write_span{p + 1, offset + head.stride, 8});
            } else
                emit(ikea::detail::write_span{p + 1, offset, 16});
        }
    });
}

/// Exact scalar-store footprint, including bytes with preserved neighboring
/// bits. A physical point update does not imply exclusive ownership of a byte.
template <class F, unsigned Fields = 15, class Emit>
[[gnu::always_inline]] inline void visit_writes_point(const basic_view<F, ikea::byte>& v, std::size_t i, Emit&& emit) {
    const auto lane = i % F::tile_rows, tile = i / F::tile_rows;
    if constexpr (F::payload) {
        const auto offset = tile * v.stream(0).stride;
        if constexpr (F::body && (Fields & 1))
            emit(ikea::detail::write_span{0, offset + ikea::seriespack::detail::body_offset<F>(lane), F::body});
        if constexpr (F::tail && (Fields & 2)) {
            if constexpr (F::storage == geometry::local)
                emit(ikea::detail::write_span{0, offset + 8 * F::body, F::tail});
            else
                ikea::seriespack::detail::tail_fragments<F::tail>(lane / 32, [&](unsigned stripe, unsigned,
                                                               unsigned) {
                    emit(ikea::detail::write_span{0, offset + ikea::seriespack::detail::stripe_offset<F>(stripe) + lane % 32, 1});
                });
        }
    }
    ikea::seriespack::detail::each<F::heads / 8>([&](auto p) {
        if constexpr (Fields & (4u << p))
            emit(ikea::detail::write_span{p + 1, tile * v.stream(p + 1).stride + lane, 1});
    });
}


#if defined(__aarch64__) || defined(__AVX2__)
namespace physical_detail {
template <class F, class Summary>
[[gnu::always_inline]] inline native::values<F::width>
select_replacement16(const basic_view<F, ikea::byte>& destination, std::size_t first,
                     native::values<F::width> after, std::uint16_t selected, Summary& summary) {
    __builtin_assume(first % 16 == 0);
    if constexpr (Summary::needs_before) {
        const auto expression = author::expression(destination);
        author::native_ops ops;
        const auto before = author::read(ops, expression, first, selected);
        if (selected != 0xffff)
            after = native::choose(selected, after, before);
        summary.observe(before, after, selected);
    } else if (selected != 0xffff) {
        const auto expression = author::expression(destination);
        author::native_ops ops;
        after =
            native::choose(selected, after, author::read(ops, expression, first, selected));
    }
    return after;
}

template <class F, class U, class Summary>
[[gnu::always_inline]] inline native::values<F::width>
replacement_values16(const basic_view<F, ikea::byte>& destination, std::size_t first, const U* input,
                     std::uint16_t selected, Summary& summary) {
    auto after = [&] {
        const auto in = native::load_values(input);
        if constexpr (sizeof(U) >= sizeof(uint_for<F::width>))
            return native::narrow<F::width>(in);
        else
            return native::widen<F::width>(in);
    }();
    return select_replacement16(destination, first, after, selected, summary);
}

} // namespace physical_detail

template <class F, class Summary, class Coverage>
    requires ikea::effects<Coverage, basic_view<F, ikea::byte>>
[[gnu::always_inline]] inline void
replace_native16_unchecked(const basic_view<F, ikea::byte>& destination, std::size_t first,
                           native::values<F::width> input, std::uint16_t selected, Summary& summary,
                           Coverage& coverage) {
    if (!selected)
        return;
    const auto after = physical_detail::select_replacement16(destination, first, input, selected, summary);
    visit_writes16(destination, first, [&](ikea::detail::write_span w) { coverage.before(destination, w.plane, w.offset, w.size); });
    native::write16(destination, first, after);
}

template <class F, class U, class Summary, class Coverage>
    requires ikea::effects<Coverage, basic_view<F, ikea::byte>>
[[gnu::always_inline]] inline void
replace16_unchecked(const basic_view<F, ikea::byte>& destination, std::size_t first, const U* input,
                    std::uint16_t selected, Summary& summary, Coverage& coverage) {
    if (!selected)
        return;
    const auto after = physical_detail::replacement_values16(destination, first, input, selected, summary);
    // Capacity and failure behavior are admitted before entry. Coverage reports
    // issued stores; the owner's version-preservation mechanism is independent.
    visit_writes16(destination, first, [&](ikea::detail::write_span w) { coverage.before(destination, w.plane, w.offset, w.size); });
    native::write16(destination, first, after);
}

/// Immediate driver for an admitted range of complete native regions. The
/// prefilter names original coordinates; acquisition, stopping and publication
/// are responsibilities of the enclosing owner. No lifecycle frame is created.
namespace physical_detail {
template <bool Dense, class F, class U, class Prefilter, class Summary, class Coverage>
    requires ikea::effects<Coverage, basic_view<F, ikea::byte>>
[[gnu::always_inline]] inline void
replace_regions_impl(basic_view<F, ikea::byte> destination, std::size_t first, std::size_t count,
                     const U* __restrict input, Prefilter&& mask, Summary& __restrict summary,
                     Coverage& __restrict coverage) {
    __builtin_assume(first % 16 == 0 && count % 16 == 0);
    if constexpr (Dense) {
        const auto stride = destination.stream(0).stride;
        __builtin_assume(stride == F::tile_bytes);
    }
    std::size_t offset = 0;
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
    // Whole-operation maintenance follows the native execution grain used by
    // reads. It must not force four independent reductions through a 16-row
    // helper merely because the physical writer can also serve that request.
    constexpr unsigned grain = F::width <= 8 ? 64 : F::width <= 16 ? 32 : 16;
    if constexpr (Dense && F::storage == geometry::local &&
                  requires(zmm::values<F::width, grain> v) {
                      summary.observe_group(v, v, std::uint64_t{});
                  }) {
        for (; count - offset >= grain; offset += grain) {
            const auto origin = first + offset;
            std::uint64_t active = 0;
            bool complete = true;
            ikea::seriespack::detail::each<grain / 16>([&](auto p) {
                const auto m = static_cast<std::uint16_t>(mask(origin + p * 16));
                active |= std::uint64_t(m) << (p * 16);
                complete &= m != 0;
            });
            if (complete) {
                const auto before = zmm::read<F, true, grain>(ikea::to_u8(destination.stream(0).bytes).data(),
                                                              F::tile_bytes, origin);
                auto after = zmm::load_values<F::width, grain>(input + offset);
                constexpr auto all = ~std::uint64_t{0} >> (64 - grain);
                if (active != all)
                    after = zmm::choose(active, after, before);
                summary.observe_group(before, after, active);
                coverage.before(destination, 0, origin / 8 * F::tile_bytes, grain / 8 * F::tile_bytes);
                if constexpr (F::width < 8)
                    native::write_local64<F>(ikea::to_u8(destination.stream(0).bytes).data() +
                                                 origin / 8 * F::tile_bytes,
                                             [&](auto p) { return zmm::part16<p>(after); });
                else
                    ikea::seriespack::detail::each<grain / 16>([&](auto p) {
                        native::write16(destination, origin + p * 16, zmm::part16<p * 16>(after));
                    });
            } else
                ikea::seriespack::detail::each<grain / 16>([&](auto p) {
                    replace16_unchecked(destination, origin + p * 16, input + offset + p * 16,
                                        static_cast<std::uint16_t>(active >> (p * 16)), summary,
                                        coverage);
                });
        }
    }
#endif
    if constexpr (Dense && F::storage == geometry::local && F::width < 8) {
        for (; count - offset >= 64; offset += 64) {
            const auto origin = first + offset;
            std::array<std::uint16_t, 4> masks;
            bool complete = true;
            ikea::seriespack::detail::each<4>([&](auto p) {
                masks[p] = mask(origin + p * 16);
                complete &= masks[p] != 0;
            });
            if (complete) {
                const auto start = (origin / 8) * F::tile_bytes;
                coverage.before(destination, 0, start, 8 * F::tile_bytes);
                native::write_local64<F>(ikea::to_u8(destination.stream(0).bytes).data() + start, [&](auto p) {
                    return replacement_values16(destination, origin + p, input + offset + p,
                                                masks[p / 16], summary);
                });
            } else
                ikea::seriespack::detail::each<4>([&](auto p) {
                    replace16_unchecked(destination, origin + p * 16, input + offset + p * 16,
                                        masks[p], summary, coverage);
                });
        }
    }
    if constexpr (F::storage == geometry::striped) {
        while (offset < count && (first + offset) % F::tile_rows) {
            replace16_unchecked(destination, first + offset, input + offset,
                                static_cast<std::uint16_t>(mask(first + offset)), summary,
                                coverage);
            offset += 16;
        }
        for (; count - offset >= F::tile_rows; offset += F::tile_rows) {
            const auto origin = first + offset;
            std::array<std::uint16_t, F::tile_rows / 16> masks;
            bool complete = true;
            ikea::seriespack::detail::each<F::tile_rows / 16>([&](auto p) {
                masks[p] = mask(origin + p * 16);
                complete &= masks[p] != 0;
            });
            if (complete) {
                // Every region issues stores, so the occupied tile is the
                // exact union of the writer's footprint, even with lane masks.
                coverage.before(destination, 0, (origin / F::tile_rows) * destination.stream(0).stride, F::tile_bytes);
                ikea::seriespack::detail::each<F::heads / 8>([&](auto p) {
                    coverage.before(destination, p + 1,
                                     (origin / F::tile_rows) * destination.stream(p + 1).stride,
                                     F::tile_rows);
                });
                native::write_striped_tile(destination, origin, [&](auto p) {
                    return replacement_values16(destination, origin + p, input + offset + p,
                                                masks[p / 16], summary);
                });
            } else
                ikea::seriespack::detail::each<F::tile_rows / 16>([&](auto p) {
                    replace16_unchecked(destination, origin + p * 16, input + offset + p * 16,
                                        masks[p], summary, coverage);
                });
        }
    }
    for (; offset < count; offset += 16)
        replace16_unchecked(destination, first + offset, input + offset,
                            static_cast<std::uint16_t>(mask(first + offset)), summary, coverage);
}

} // namespace physical_detail

/// Freeze placement metadata at the call boundary. Mutable byte stores cannot
/// alias this private copy and force pointer/stride reloads in every packet.
template <class F, class U, class Prefilter, class Summary, class Coverage>
    requires ikea::effects<Coverage, basic_view<F, ikea::byte>>
[[gnu::always_inline]] inline void
replace_regions_unchecked(const basic_view<F, ikea::byte>& named_destination, std::size_t first, std::size_t count,
                          const U* input, Prefilter&& mask, Summary& summary, Coverage& effects) {
    auto destination = named_destination;
    decltype(auto) coverage = ikea::seriespack::detail::name_effects(named_destination, effects);
    __builtin_assume(first % 16 == 0 && count % 16 == 0);
    if constexpr (F::heads == 0)
        if (destination.stream(0).stride == F::tile_bytes) {
            if constexpr (F::storage == geometry::local &&
                          !std::is_same_v<std::remove_cvref_t<Coverage>, no_effects>) {
                // Each nonempty Local region stores complete adjacent packets.
                // Stage its maximal contiguous run once, before executing it. This
                // removes journal work from the native loop without claiming bytes
                // in an empty region or forcing per-packet beforeimage callbacks.
                std::size_t offset = 0;
                while (offset < count) {
                    while (offset < count && !static_cast<std::uint16_t>(mask(first + offset)))
                        offset += 16;
                    const auto begin = offset;
                    while (offset < count && static_cast<std::uint16_t>(mask(first + offset)))
                        offset += 16;
                    if (begin != offset) {
                        coverage.before(destination, 0, (first + begin) / 8 * F::tile_bytes,
                                         (offset - begin) / 8 * F::tile_bytes);
                        no_effects recorded;
                        physical_detail::replace_regions_impl<true>(destination, first + begin, offset - begin,
                                                   input + begin, mask, summary, recorded);
                    }
                }
            } else if constexpr (F::storage == geometry::striped &&
                                 !std::is_same_v<std::remove_cvref_t<Coverage>, no_effects>) {
                std::size_t offset = 0;
                while (offset < count && (first + offset) % F::tile_rows) {
                    replace16_unchecked(destination, first + offset, input + offset,
                                        mask(first + offset), summary, coverage);
                    offset += 16;
                }
                auto complete = [&](std::size_t at) {
                    bool all = true;
                    ikea::seriespack::detail::each<F::tile_rows / 16>([&](auto p) {
                        all &= static_cast<std::uint16_t>(mask(first + at + p * 16)) != 0;
                    });
                    return all;
                };
                while (count - offset >= F::tile_rows) {
                    const auto begin = offset;
                    while (count - offset >= F::tile_rows && complete(offset))
                        offset += F::tile_rows;
                    if (begin != offset) {
                        coverage.before(destination, 0, (first + begin) / F::tile_rows * F::tile_bytes,
                                         (offset - begin) / F::tile_rows * F::tile_bytes);
                        no_effects recorded;
                        physical_detail::replace_regions_impl<true>(destination, first + begin, offset - begin,
                                                   input + begin, mask, summary, recorded);
                    } else {
                        physical_detail::replace_regions_impl<true>(destination, first + offset, F::tile_rows,
                                                   input + offset, mask, summary, coverage);
                        offset += F::tile_rows;
                    }
                }
                if (offset < count)
                    physical_detail::replace_regions_impl<true>(destination, first + offset, count - offset,
                                               input + offset, mask, summary, coverage);
            } else
                physical_detail::replace_regions_impl<true>(destination, first, count, input, mask, summary,
                                           coverage);
            return;
        }
    physical_detail::replace_regions_impl<false>(destination, first, count, input, mask, summary, coverage);
}

template <class F, class Summary, class Coverage>
    requires ikea::effects<Coverage, basic_view<F, ikea::byte>>
[[gnu::always_inline]] inline void replace_point_unchecked(const basic_view<F, ikea::byte>& destination,
                                                           std::size_t first, std::uint64_t value,
                                                           Summary& summary, Coverage& coverage) {
    if constexpr (Summary::needs_before)
        summary.observe_scalar(get_unchecked(destination, first), value);
    visit_writes_point(destination, first, [&](ikea::detail::write_span write) { coverage.before(destination, write.plane, write.offset, write.size); });
    author::store_unchecked(destination, first, value);
}

/// Full logical-range driver. Prefilters are pure/stable masks in original
/// coordinates. Scalar edges only read selected inputs; complete native regions
/// require all 16 slots readable, including inactive slots.
template <class F, class U, class Prefilter, class Summary, class Coverage>
    requires ikea::effects<Coverage, basic_view<F, ikea::byte>>
[[gnu::always_inline]] inline void
replace_unchecked(const basic_view<F, ikea::byte>& named_destination, std::size_t first, std::size_t count,
                  const U* input, Prefilter&& mask, Summary& summary, Coverage& effects) {
    auto destination = named_destination;
    decltype(auto) coverage = ikea::seriespack::detail::name_effects(named_destination, effects);
    std::size_t offset = 0;
    while (offset < count && (first + offset) % 16) {
        const auto row = first + offset;
        if (static_cast<std::uint16_t>(mask(row - row % 16)) & (1u << (row % 16)))
            replace_point_unchecked(destination, row, input[offset], summary, coverage);
        ++offset;
    }
    const auto regions = (count - offset) / 16 * 16;
    if (regions)
        replace_regions_unchecked(destination, first + offset, regions, input + offset, mask,
                                  summary, coverage);
    offset += regions;
    while (offset < count) {
        const auto row = first + offset;
        if (static_cast<std::uint16_t>(mask(row - row % 16)) & (1u << (row % 16)))
            replace_point_unchecked(destination, row, input[offset], summary, coverage);
        ++offset;
    }
}

/// Preflight the complete operation. Conservative effect capacity counts the
/// bounded writers; whole-tile lowering/coalescing may consume fewer records.
/// Failure changes neither bytes nor effect/summary outputs. This checks local
/// admission; version pins, isolation and publication belong to the owner.
template <class F, class U, class Prefilter, class Summary>
std::expected<void, error> replace(const basic_view<F, ikea::byte>& destination, std::size_t first,
                                   std::span<const U> input, Prefilter&& mask, Summary& summary,
                                   journal& journal) {
    if (first > destination.size() || input.size() > destination.size() - first)
        return std::unexpected(error::range);
    if (journal.used > journal.storage.size())
        return std::unexpected(error::effects);
    std::size_t remaining = journal.storage.size() - journal.used;
    bool fits = true;
    auto reserve = [&](ikea::detail::write_span) {
        if (remaining)
            --remaining;
        else
            fits = false;
    };
    std::size_t offset = 0;
    while (offset < input.size()) {
        const auto row = first + offset, origin = row - row % 16;
        const auto selected = static_cast<std::uint16_t>(mask(origin));
        const auto n = std::min<std::size_t>(16 - row % 16, input.size() - offset);
        if constexpr (F::width < sizeof(U) * 8)
            for (std::size_t j = 0; j < n; ++j)
                if ((selected & (1u << (row % 16 + j))) && (input[offset + j] >> F::width))
                    return std::unexpected(error::value);
        if (row % 16 == 0 && n == 16) {
            if (selected)
                visit_writes16(destination, row, reserve);
        } else
            for (std::size_t j = 0; j < n; ++j)
                if (selected & (1u << (row % 16 + j)))
                    visit_writes_point(destination, row + j, reserve);
        offset += n;
    }
    if (!fits)
        return std::unexpected(error::effects);
    if (!input.empty())
        replace_unchecked(destination, first, input.size(), input.data(), mask, summary, journal);
    return {};
}

/// Checked bounded mutation. Destination, input, journal and summary state have
/// disjoint storage and an enclosing write-isolation/lifetime contract. Failures
/// leave all of them unchanged. This neither allocates nor suspends nor publishes.
template <class F, class U, class Summary>
std::expected<void, error> replace16(const basic_view<F, ikea::byte>& destination, std::size_t first,
                                     std::span<const U> input, std::uint16_t selected,
                                     Summary& summary, journal& journal) {
    if (first % 16 || first > destination.size() || destination.size() - first < 16)
        return std::unexpected(error::range);
    if (input.size() < 16)
        return std::unexpected(error::capacity);
    if (!selected)
        return {};
    if constexpr (F::width < sizeof(U) * 8)
        for (unsigned j = 0; j < 16; ++j)
            if ((selected & (1u << j)) && (input[j] >> F::width))
                return std::unexpected(error::value);
    std::size_t records = 0;
    visit_writes16(destination, first, [&](ikea::detail::write_span) { ++records; });
    if (journal.used > journal.storage.size() || records > journal.storage.size() - journal.used)
        return std::unexpected(error::effects);
    replace16_unchecked(destination, first, input.data(), selected, summary, journal);
    return {};
}
#endif

/// The complete physical fields of a new array, including final-tile slack.
/// Interleaved owners' stride gaps are excluded. Construction writes this union.
template <class F, class Emit>
void visit_storage(const basic_view<F, ikea::byte>& destination, Emit&& emit) {
    const auto count = destination.size();
    if (!count)
        return;
    const auto tiles = count / F::tile_rows + (count % F::tile_rows != 0);
    ikea::seriespack::detail::tile_fields<F, 15>([&](unsigned p, std::size_t offset, std::size_t bytes) {
        const auto stride = destination.stream(p).stride;
        if (stride == bytes)
            emit(ikea::detail::write_span{p, offset, tiles * bytes});
        else
            for (std::size_t t = 0; t < tiles; ++t)
                emit(ikea::detail::write_span{p, t * stride + offset, bytes});
    });
}

#if defined(__aarch64__) || defined(__AVX2__)
/// Fresh/reinitialized storage, disjoint admitted inputs, no prior-value law.
/// Padding becomes zero; complete native regions write once. Coverage does not
/// choose isolation, allocation, page COW or publication for the new array.
template <class F, class U, class Coverage>
void initialize_unchecked(const basic_view<F, ikea::byte>& named_destination, const U* input, Coverage& effects) {
    auto destination = named_destination;
    decltype(auto) coverage = ikea::seriespack::detail::name_effects(named_destination, effects);
    const auto count = destination.size();
    if (!count)
        return;
    visit_storage(destination, [&](ikea::detail::write_span bytes) { coverage.before(destination, bytes.plane, bytes.offset, bytes.size); });
    // Partial striped tiles share residual bytes between native regions. A
    // final Local octet can also fall through the scalar edge writer. Initialize
    // only that last tile before any preserving edge/fragment stores can read it.
    if (count % F::tile_rows || (F::tile_rows == 8 && count % 16)) {
        const auto last = (count - 1) / F::tile_rows;
        ikea::seriespack::detail::tile_fields<F, 15>([&](unsigned p, std::size_t offset, std::size_t bytes) {
            const auto& plane = destination.stream(p);
            std::memset(ikea::to_u8(plane.bytes).data() + last * plane.stride + offset, 0, bytes);
        });
    }
    author::no_summary summary;
    no_effects recorded;
    replace_unchecked(
        destination, 0, count, input, [](std::size_t) { return std::uint16_t{0xffff}; }, summary,
        recorded);
}

template <class F, class U>
std::expected<void, error> initialize(const basic_view<F, ikea::byte>& destination,
                                      std::span<const U> input, journal& effects) {
    if (input.size() != destination.size())
        return std::unexpected(error::range);
    if constexpr (F::width < sizeof(U) * 8)
        for (const auto value : input)
            if (value >> F::width)
                return std::unexpected(error::value);
    std::size_t needed = 0;
    visit_storage(destination, [&](ikea::detail::write_span) { ++needed; });
    if (effects.used > effects.storage.size() || needed > effects.storage.size() - effects.used)
        return std::unexpected(error::effects);
    initialize_unchecked(destination, input.data(), effects);
    return {};
}
#endif
} // namespace ikea::seriespack::author
