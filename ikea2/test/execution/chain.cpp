#include "../support.h"
#include "../fixture.h"
#include <ikea2/seriespack/author/chain.h>
#include <ikea2/seriespack/detail/mutation/physical.h>

#if defined(__aarch64__) || defined(__AVX2__)
template <unsigned K> void check_compact_predicate() {
#if defined(__AVX2__)
    constexpr auto limit = ~std::uint64_t{0} >> (64 - K);
    std::array<sp::uint_for<K>, 16> input{};
    for (unsigned j = 0; j < 16; ++j)
        input[j] = j % 4 == 0 ? 0 : j % 4 == 1 ? limit : j % 4 == 2 ? limit / 2 : limit / 2 + 1;
    const auto native = sp::native::narrow<K>(sp::native::load_values(input.data()));
    for (const auto cutoff :
         {std::uint64_t{0}, std::uint64_t{1}, limit / 2, limit, ~std::uint64_t{0}})
        for (const std::uint16_t active : {0, 1, 0x8000, 0xb6db, 0xffff}) {
            std::uint16_t expected = 0;
            for (unsigned j = 0; j < 16; ++j)
                if ((active & (1u << j)) && input[j] < cutoff)
                    expected |= 1u << j;
            IKEA2_CHECK(sp::native::less_bits(native, cutoff, active) == expected);
        }
#endif
}
template <unsigned K> struct chain_fixture {
    using F = sp::format<K>;
    using Plan = sp::chain<K>;
    using Values = sp::native::values<K>;
    placed<F> storage;
    sp::view<F, std::uint8_t> destination =
        *sp::view<F, std::uint8_t>::attach(storage.count, storage.planes);
    sp::sum_change summary;
    std::array<sp::byte_write, 16> effects;
    sp::write_journal journal{effects};
    unsigned calls = 0, finishes = 0, stop_at = 100;
    std::uint16_t final_mask = 0;
    std::array<sp::uint_for<K>, 16> expected;
    static typename Plan::result body(void* opaque, std::size_t row, std::uint16_t active,
                                      Values values, unsigned ordinal) {
        auto& frame = *static_cast<chain_fixture*>(opaque);
        IKEA2_CHECK(row == 32);
        ++frame.calls;
        // A stage can prune all rows without any destination/effect activity.
        if (ordinal == frame.stop_at)
            return {values, 0, true};
        return {values, active, false};
    }
    static typename Plan::result mutation(void* opaque, std::size_t row, std::uint16_t active,
                                          Values values, unsigned) {
        auto& frame = *static_cast<chain_fixture*>(opaque);
        ++frame.calls;
        sp::replace_native16_unchecked(frame.destination, row, values, active, frame.summary,
                                       frame.journal);
        return {values, active, false};
    }
    static void finish(void* opaque, std::size_t row, std::uint16_t active, Values values) {
        auto& frame = *static_cast<chain_fixture*>(opaque);
        ++frame.finishes;
        frame.final_mask = active;
        std::array<sp::uint_for<K>, 16> actual;
        sp::native::store16(actual.data(), values);
        IKEA2_CHECK(row == 32 && actual == frame.expected);
    }
};
template <unsigned K> void check_chain_width() {
    using Frame = chain_fixture<K>;
    using Plan = typename Frame::Plan;
    Frame frame;
    std::mt19937_64 random(K);
    for (auto& x : frame.expected)
        x = random() & (~std::uint64_t{0} >> (64 - K));
    const auto input = [&] {
        const auto value = sp::native::load_values(frame.expected.data());
        return sp::native::narrow<K>(value);
    }();
    constexpr auto step = &Plan::template stage<&Frame::body>;
    constexpr auto mutate = &Plan::template stage<&Frame::mutation>;
    constexpr auto finish = &Plan::template completion<&Frame::finish>;
    static_assert(sizeof(Plan) == 64 && alignof(Plan) == 64);
    IKEA2_CHECK(!Plan::prepare({}, finish));
    std::array<typename Plan::function, 8> too_many;
    too_many.fill(step);
    IKEA2_CHECK(!Plan::prepare(too_many, finish));
    for (unsigned depth = 1; depth <= 7; ++depth) {
        std::array<typename Plan::function, 7> stages;
        stages.fill(step);
        stages[depth - 1] = mutate;
        auto plan = Plan::prepare(std::span(stages).first(depth), finish);
        IKEA2_CHECK(plan);
        for (unsigned stop = 0; stop < depth; ++stop) {
            frame.stop_at = stop;
            frame.calls = frame.finishes = 0;
            frame.journal.used = 0;
            const auto delta = frame.summary.finish();
            const auto before = std::vector<std::uint8_t>(frame.storage.planes[0].bytes.begin(),
                                                          frame.storage.planes[0].bytes.end());
            auto copied = *plan; // all early-return cursors must locate this copy
            copied.run(&frame, 32, 0xb6db, input);
            IKEA2_CHECK(frame.finishes == 1);
            if (stop < depth - 1) {
                IKEA2_CHECK(frame.final_mask == 0 && frame.calls == stop + 1 &&
                            frame.journal.used == 0);
                IKEA2_CHECK(frame.summary.finish() == delta);
                IKEA2_CHECK(std::equal(before.begin(), before.end(),
                                       frame.storage.planes[0].bytes.begin()));
            } else {
                IKEA2_CHECK(frame.final_mask == 0xb6db && frame.calls == depth);
                std::uint64_t expected_delta = 0;
                for (unsigned j = 0; j < 16; ++j) {
                    const auto wanted =
                        (0xb6db & (1u << j)) ? frame.expected[j] : frame.storage.truth[32 + j];
                    expected_delta += std::uint64_t(wanted) - frame.storage.truth[32 + j];
                    frame.storage.truth[32 + j] = wanted;
                    IKEA2_CHECK(sp::get_unchecked(frame.destination, 32 + j) == wanted);
                }
                IKEA2_CHECK(frame.summary.finish() - delta == expected_delta);
            }
        }
    }
}
#endif
void check_chain() {
#if defined(__aarch64__) || defined(__AVX2__)
    sp::detail::each<64>([](auto k) { check_compact_predicate<k + 1>(); });
    check_chain_width<7>();
    check_chain_width<12>();
    check_chain_width<31>();
    check_chain_width<64>();
#endif
}
