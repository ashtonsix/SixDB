#if defined(__AVX2__)
// Project complete source values before narrowing: low payload bits must not
// enter the high-byte plane. The register result holds consecutive 16-bit heads.
template<class Ops, unsigned W>
[[gnu::always_inline]] inline auto x86_head_words(const std::uint64_t* input) {
    static_assert(W <= 48);
    if constexpr (Ops::target == execution_target::avx2) {
        const auto part = [&] [[gnu::always_inline]] (auto i) {
            return Ops::template right<8, W>(Ops::template load<8, 8, 4>(
                reinterpret_cast<const std::uint8_t*>(input + i * 4)));
        };
        return seriespack::avx2::native_detail::narrow_pair<4>(
            seriespack::avx2::native_detail::narrow_pair<8>(part(0), part(1)),
            seriespack::avx2::native_detail::narrow_pair<8>(part(2), part(3)));
    }
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
    else {
        const auto part = [&] [[gnu::always_inline]] (auto i) {
            return _mm512_cvtepi64_epi16(Ops::template right<8, W>(
                _mm512_loadu_si512(input + i * 8)));
        };
        auto words = _mm512_castsi128_si512(part(0));
        static_for<3>([&](auto after) {
            words = _mm512_inserti32x4(words, part(after + 1), after + 1);
        });
        return words;
    }
#endif
}

// One non-suspending array seam. Both byte planes keep independent placement;
// compact projection is shared only within each jointly contiguous region.
template<class Ops, unsigned W, unsigned T>
[[gnu::noinline]] void encode_x86_head16(const std::uint64_t* input, std::size_t n,
    std::uint8_t* head0, std::size_t stride0, std::uint8_t* head1, std::size_t stride1) {
    constexpr unsigned C = Ops::register_bytes / 2, N = Ops::register_bytes / 8;
    const auto run = [](const std::uint64_t* source, std::uint8_t* high,
                        std::uint8_t* low, std::size_t count) {
        std::size_t i = 0;
#pragma clang loop unroll(disable)
        for (; count - i >= C; i += C) {
            const auto words = x86_head_words<Ops, W>(source + i);
            Ops::template store<1, 2, C>(high + i, Ops::template right<2, 8>(words));
            Ops::template store<1, 2, C>(low + i, words);
        }
        for (; count - i >= N; i += N) {
            const auto words = Ops::template right<8, W>(Ops::template load<8, 8, N>(
                reinterpret_cast<const std::uint8_t*>(source + i)));
            Ops::template store<1, 8, N>(high + i, Ops::template right<8, 8>(words));
            Ops::template store<1, 8, N>(low + i, words);
        }
        // The native remainder leaves fewer than N values. Express that
        // bound directly; a generic loop otherwise creates an unreachable
        // vectorized suffix and its alias checks in each head specialization.
        static_for<N - 1>([&](auto after) {
            if (i + after < count) {
                std::uint64_t value;
                std::memcpy(&value, source + i + after, sizeof value);
                high[i + after] = static_cast<std::uint8_t>(value >> (W + 8));
                low[i + after] = static_cast<std::uint8_t>(value >> W);
            }
        });
    };
    if (stride0 == T && stride1 == T) {
        run(input, head0, head1, n);
        if (const auto left = n % T; left != 0) {
            std::memset(head0 + n, 0, T - left);
            std::memset(head1 + n, 0, T - left);
        }
    } else {
        const auto full = n / T;
#pragma clang loop unroll(disable)
        for (std::size_t tile = 0; tile < full; ++tile)
            run(input + tile * T, head0 + tile * stride0, head1 + tile * stride1, T);
        if (const auto left = n % T; left != 0) {
            auto* high = head0 + full * stride0;
            auto* low = head1 + full * stride1;
            run(input + full * T, high, low, left);
            std::memset(high + left, 0, T - left);
            std::memset(low + left, 0, T - left);
        }
    }
}
#endif

