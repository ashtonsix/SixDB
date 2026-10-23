#include "runtime.h"
#include <string_view>

// Historical paired results use the source archived with evidence/neon-mask.
// Production now uses a full-word weighted sum. Current builds compare that
// reduction with the initial byte-narrowing fix, not the old scalarizing body.

// Diagnostic control: keep the decoder, comparison and public result identical.
// Only replace the four-lane predicate-to-mask reduction. No production change.
template <unsigned K, runtime::C Op, bool Control>
[[gnu::always_inline]] unsigned mask_call(const runtime::Fixture& f, unsigned w,
                                        std::uint16_t active) {
    using namespace runtime;
    asm("" : "+r"(w));
    const auto q = f.queries[w];
    if constexpr (!Control || K <= 16 || K > 32)
        return inline_fused<K, Op>(f.bytes.data(), w * 16, q, active);
    else {
        if (!active) return 0;
        if (q >= (word{1} << K)) return ikea::compare<Op>(0, q) ? active : 0;
        const Source<K> source{std::as_bytes(std::span{f.bytes}).data(), windows * 16};
        sp::author::native_ops ops;
        const auto x = sp::author::read(ops, sp::author::expression(source), w * 16, active);
        unsigned result = 0;
        sp::detail::each<4>([&](auto part) {
            const auto p = ikea::detail::filter::lanes<4, Op>(x.v[part], q);
            // A comparison duplicates its predicate in both halves of each u32.
            // Narrow the eight u16 halves once, then weight every other byte.
            const auto packed = vmovn_u16(vreinterpretq_u16_u8(p));
            const auto weighted = vand_u8(packed, vreinterpret_u8_u64(vcreate_u64(0x0008000400020001ULL)));
            result |= unsigned(vaddv_u8(weighted)) << (part * 4);
        });
        return result & active;
    }
}

template <unsigned K, runtime::C Op, bool Control, bool Distinct>
[[gnu::noinline]] void measure_mask(const runtime::Fixture& f, std::uint16_t active) {
    using namespace runtime;
    for (bool serial : {true, false})
        for (unsigned rep = 0; rep < repeats; ++rep) {
            std::array<unsigned, 8> cursor{0, 7, 13, 21, 32, 41, 54, 63};
            auto step = [&](unsigned j, unsigned i) __attribute__((always_inline)) {
                const auto bits = mask_call<K, Op, Control>(f, cursor[j], active);
                cursor[j] = next(bits, i + (Distinct ? j * 0x9e37u : 0));
            };
            for (unsigned i = 0; i < 512; ++i)
                for (unsigned j = 0; j < 8; ++j) step(j, i);
            auto begin = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) {
                if (serial) step(0, i);
                else sp::detail::each<8>([&](auto j) { step(j, i); });
            }
            auto end = std::chrono::steady_clock::now();
            for (auto x : cursor) observed += x;
            const double ns = std::chrono::duration<double, std::nano>(end - begin).count() /
                              (double(iterations) * (serial ? 1 : 8));
            std::printf("%u,%u,%u,%s,%s,%s,%u,%.6f\n", K, unsigned(Op), unsigned(active),
                        Control ? "narrow_mask" : "production", Distinct ? "distinct" : "original",
                        serial ? "dependent" : "independent8", rep, ns);
        }
}

template <unsigned K, runtime::C Op> void check_mask() {
    using namespace runtime;
    Fixture f(K);
    const word limit = ~word{0} >> (64 - K);
    for (auto q : {word(0), word(1), limit / 2, limit, limit + 1, ~word(0)}) {
        f.queries.fill(q);
        for (unsigned w = 0; w < windows; ++w)
            for (unsigned active : {0u, 1u, 0x8000u, 0xa53cu, 0xffffu}) {
                unsigned wanted = 0;
                for (unsigned i = 0; i < 16; ++i)
                    wanted |= unsigned((active >> i & 1) && oracle(unsigned(Op), f.truth[w * 16 + i], q)) << i;
                assert((mask_call<K, Op, false>(f, w, active) == wanted));
                assert((mask_call<K, Op, true>(f, w, active) == wanted));
            }
    }
}

template <unsigned K, runtime::C Op> void timed_case() {
    runtime::Fixture f(K);
    measure_mask<K, Op, false, false>(f, 65535);
    measure_mask<K, Op, true, false>(f, 65535);
    measure_mask<K, Op, false, true>(f, 65535);
    measure_mask<K, Op, true, true>(f, 65535);
}

// Isolated witnesses for assembly and static models, not benchmark entry points.
extern "C" unsigned current32(uint32x4_t x, std::uint64_t q) {
    return ikea::detail::filter::bits<4, runtime::C::equal>(vreinterpretq_u8_u32(x), q);
}
extern "C" unsigned vector32(uint32x4_t x, std::uint64_t q) {
    const auto p = vceqq_u32(x, vdupq_n_u32(q));
    return vaddv_u8(vand_u8(vmovn_u16(vreinterpretq_u16_u32(p)),
                          vreinterpret_u8_u64(vcreate_u64(0x0008000400020001ULL))));
}

int main(int argc, char**) {
    using namespace runtime;
    iterations = 500000; repeats = 7;
    sp::detail::each<16>([](auto k) {
        sp::detail::each<5>([](auto op) {
            check_mask<17 + decltype(k)::value, C(decltype(op)::value)>();
        });
    });
    std::fprintf(stderr, "independent oracle passed for K17–32, all five predicates, boundary queries and sparse masks\n");
    if (argc > 1) return 0;
    std::puts("width,op,active,candidate,trace,metric,rep,ns_window");
    timed_case<16, C::equal>(); timed_case<17, C::equal>();
    timed_case<24, C::equal>(); timed_case<32, C::equal>();
    timed_case<33, C::equal>(); timed_case<64, C::equal>();
    timed_case<32, C::less>(); timed_case<32, C::less_equal>();
    timed_case<32, C::greater>(); timed_case<32, C::greater_equal>();
    std::fprintf(stderr, "checksum=%llu\n", (unsigned long long)observed);
}
