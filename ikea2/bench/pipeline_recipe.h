#pragma once
#include <ikea2/seriespack/author/chain.h>
#include <ikea2/seriespack/detail/mutation/physical.h>

namespace pipeline_probe {
namespace sp = ikea2::seriespack;
enum class mode { inline_stages, cps, fused };
template <unsigned N> struct frame {
    using Packet = sp::pipeline_packet<32, N>;
    using Plan = sp::packet_chain<32, N>;
    using V = sp::native::values<32>;
    using A = sp::format<12, sp::geometry::striped>;
    using B = sp::format<23>;
    using D = sp::format<32>;
    sp::view<A> a;
    sp::view<B> b;
    sp::view<D, std::uint8_t> destination;
    sp::write_journal effects;
    sp::sum_change summary;
    std::uint64_t aggregate = 0;
    std::uint32_t parameter = 123, lower = 1000, upper = 4000000;
    static constexpr std::uint64_t incoming = 0xefbdefbdefbdefbdULL >> (64 - N);

    template <unsigned Which> [[gnu::always_inline]] static V load(frame& f, std::size_t row) {
        if constexpr (Which == 0)
            return sp::native::widen<32>(sp::native::read16<A, false>(f.a.stream(0).bytes.data(),
                                                                      f.a.stream(0).stride, row));
        else
            return sp::native::widen<32>(sp::native::read16<B, false>(f.b.stream(0).bytes.data(),
                                                                      f.b.stream(0).stride, row));
    }
    template <unsigned Which>
    [[gnu::always_inline]] static V transform(V value, std::uint32_t parameter) {
        sp::detail::each<V::parts>([&](auto p) {
#if defined(__aarch64__)
            auto x = vreinterpretq_u32_u8(value.v[p]);
            if constexpr (Which == 0)
                x = vaddq_u32(x, vdupq_n_u32(parameter));
            if constexpr (Which == 1)
                x = veorq_u32(x, vdupq_n_u32(parameter));
            if constexpr (Which == 2)
                x = vmulq_u32(x, vdupq_n_u32(parameter));
            value.v[p] = vreinterpretq_u8_u32(x);
#else
            if constexpr (Which == 0)
                value.v[p] = _mm256_add_epi32(value.v[p], _mm256_set1_epi32(parameter));
            if constexpr (Which == 1)
                value.v[p] = _mm256_xor_si256(value.v[p], _mm256_set1_epi32(parameter));
            if constexpr (Which == 2)
                value.v[p] = _mm256_mullo_epi32(value.v[p], _mm256_set1_epi32(parameter));
#endif
        });
        return value;
    }
    [[gnu::always_inline]] static std::uint16_t less(V value, std::uint32_t cutoff,
                                                     std::uint16_t active) {
#if defined(__AVX2__)
        return sp::native::less_bits(value, cutoff, active);
#else
        const auto bytes = sp::native::low_bytes(sp::native::less(value, cutoff, active));
        const auto weighted =
            vandq_u8(bytes, vreinterpretq_u8_u64(vdupq_n_u64(0x8040201008040201ULL)));
        return vaddv_u8(vget_low_u8(weighted)) | (unsigned(vaddv_u8(vget_high_u8(weighted))) << 8);
#endif
    }
    template <unsigned Which>
    [[gnu::always_inline]] static std::uint16_t predicate(frame& f, V value, std::uint16_t active) {
        if constexpr (Which == 0)
            return less(value, f.upper, active);
        if constexpr (Which == 1)
            return less(value, f.upper, active) & ~less(value, f.lower, active);
        if constexpr (Which == 2) {
            sp::detail::each<V::parts>([&](auto p) {
#if defined(__aarch64__)
                value.v[p] = vandq_u8(value.v[p], vreinterpretq_u8_u32(vdupq_n_u32(7)));
#else
                value.v[p] = _mm256_and_si256(value.v[p], _mm256_set1_epi32(7));
#endif
            });
            return less(value, 1, active);
        }
    }
    template <unsigned Which>
    [[gnu::always_inline]] static void consume(frame& f, std::size_t row, V value,
                                               std::uint16_t active) {
        if constexpr (Which == 0)
            f.aggregate += sp::native::sum(value, sp::native::mask16<32>(active)).finish();
        else
            sp::replace_native16_unchecked(f.destination, row, value, active, f.summary, f.effects);
    }
    template <unsigned Which>
    [[gnu::always_inline]] static typename Plan::result
    source(void* pointer, std::size_t row, std::uint64_t active, Packet packet, unsigned) {
        auto& f = *static_cast<frame*>(pointer);
        sp::detail::each<N / 16>([&](auto p) {
            if (std::uint16_t(active >> (p * 16)))
                packet.template set<p>(load<Which>(f, row + p * 16));
        });
        return {packet, active, active == 0};
    }
    template <unsigned Which>
    [[gnu::always_inline]] static typename Plan::result
    map(void* pointer, std::size_t, std::uint64_t active, Packet packet, unsigned) {
        auto& f = *static_cast<frame*>(pointer);
        sp::detail::each<N / 16>([&](auto p) {
            packet.template set<p>(transform<Which>(packet.template get<p>(), f.parameter));
        });
        return {packet, active, false};
    }
    template <unsigned Which>
    [[gnu::always_inline]] static typename Plan::result
    filter(void* pointer, std::size_t, std::uint64_t active, Packet packet, unsigned) {
        auto& f = *static_cast<frame*>(pointer);
        std::uint64_t after = 0;
        sp::detail::each<N / 16>([&](auto p) {
            after |=
                std::uint64_t(predicate<Which>(f, packet.template get<p>(), active >> (p * 16)))
                << (p * 16);
        });
        return {packet, after, after == 0};
    }
    template <unsigned Which>
    [[gnu::always_inline]] static typename Plan::result
    sink(void* pointer, std::size_t row, std::uint64_t active, Packet packet, unsigned) {
        auto& f = *static_cast<frame*>(pointer);
        sp::detail::each<N / 16>([&](auto p) {
            const auto mask = std::uint16_t(active >> (p * 16));
            if (mask)
                consume<Which>(f, row + p * 16, packet.template get<p>(), mask);
        });
        return {packet, active, false};
    }
    static void finish(void*, std::size_t, std::uint64_t, Packet) {}
    template <unsigned Source, unsigned Transform, unsigned Predicate, unsigned Sink>
    static Plan bind() {
        const std::array<typename Plan::function, 4> stages{
            &Plan::template stage<&source<Source>>, &Plan::template stage<&map<Transform>>,
            &Plan::template stage<&filter<Predicate>>, &Plan::template stage<&sink<Sink>>};
        return *Plan::prepare(stages, &Plan::template completion<&finish>);
    }
    void reset() {
        effects.used = 0;
        summary = {};
        aggregate = 0;
    }
    std::uint64_t result() const {
        return aggregate + summary.finish();
    }
};

template <unsigned N>
[[gnu::noinline]] std::uint64_t execute_cps(frame<N>& f, const typename frame<N>::Plan& plan,
                                            std::size_t count) {
    f.reset();
    for (std::size_t row = 0; row < count; row += N)
        plan.run(&f, row, f.incoming, {});
    return f.result();
}
template <unsigned N, unsigned Source, unsigned Transform, unsigned Predicate, unsigned Sink,
          mode Mode>
[[gnu::noinline]] std::uint64_t execute(frame<N>& f, const typename frame<N>::Plan& plan,
                                        std::size_t count) {
    using F = frame<N>;
    if constexpr (Mode == mode::cps)
        return execute_cps(f, plan, count);
    else {
        f.reset();
        for (std::size_t row = 0; row < count; row += N) {
            if constexpr (Mode == mode::inline_stages) {
                auto current = F::template source<Source>(&f, row, F::incoming, {}, 0);
                current = F::template map<Transform>(&f, row, current.active, current.values, 1);
                current = F::template filter<Predicate>(&f, row, current.active, current.values, 2);
                if (current.active)
                    F::template sink<Sink>(&f, row, current.active, current.values, 3);
            } else
                sp::detail::each<N / 16>([&](auto p) {
                    const auto value = F::template transform<Transform>(
                        F::template load<Source>(f, row + p * 16), f.parameter);
                    const auto active = F::template predicate<Predicate>(
                        f, value, static_cast<std::uint16_t>(F::incoming >> (p * 16)));
                    if (active)
                        F::template consume<Sink>(f, row + p * 16, value, active);
                });
        }
        return f.result();
    }
}
} // namespace pipeline_probe
