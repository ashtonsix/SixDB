#include <ikea/hashtable.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <span>
#include <vector>

namespace ht = ikea::hashtable;
struct Result {
    ht::word hits = 0, keys = 0, values = 0;
    bool operator==(const Result&) const = default;
    void add(Result piece) { hits += piece.hits; keys += piece.keys; values += piece.values; }
};
// Specialize the COMPLETE operation. Arbitrary hit-only side effects must still
// branch, whereas this reduction can consume zero on a miss without branching.
// The full input end keeps lookahead continuous across selector range boundaries.
template <ht::lookup_hint Hint, unsigned Distance, class View>
[[gnu::noinline]] Result range(View view, const ht::word* first, const ht::word* last,
                               const ht::word* full_end) {
    Result out;
    for (auto p = first; p != last; ++p) {
        if constexpr (Distance)
            if (full_end - p > Distance) view.prefetch(p[Distance]);
        const auto key = *p;
        const auto match = view.template get<Hint>(key);
        if constexpr (Hint == ht::lookup_hint::predictable) {
            if (match.found) {
                ++out.hits;
                out.keys += key;
                out.values += ht::load<8>(match.value.data());
            }
        } else {
            out.hits += match.found;
            out.keys += key & -ht::word(match.found);
            out.values += ht::load<8>(match.value.data());
        }
    }
    return out;
}
template <unsigned Distance, class View>
Result automatic(View view, std::span<const ht::word> keys, ht::lookup_selector& selector) {
    // Retain selector across comparable batches. Use a separate one for a
    // different working set/operator, and reset it after a material change.
    for (std::size_t i = 0; i < std::min<std::size_t>(Distance, keys.size()); ++i)
        view.prefetch(keys[i]);
    Result out;
    selector.run(keys.size(), [&](auto hint, std::size_t first, std::size_t count) {
        out.add(range<decltype(hint)::value, Distance>(
            view, keys.data() + first, keys.data() + first + count, keys.data() + keys.size()));
    });
    return out;
}
int main() {
    using Owner = ht::table<32, 11, 8, 24>;
    auto owner = ht::descriptor<32, 8>::explicit_encoding<11, 24>(713);
    const Owner::mix_type mix{713};
    // Deliberate spills, zero quotient and a zero-valued hit. No presence hint
    // can skip an actual check, and zero alone does not indicate absence.
    for (unsigned i = 0; i < 300; ++i) {
        ht::bytes<8> value{};
        ht::store<8>(value.data(), i * 7);
        owner.insert(mix.recover(0, i), value);
    }
    auto held = owner.read_native<11, 24>();
    auto view = held.borrow();
    ht::lookup_selector direct, prefetched;
    ht::word processed = 0;
    for (unsigned epoch = 0; epoch < 80; ++epoch) {
        constexpr std::array counts{0u, 1u, 31u, 32u, 33u, 255u, 256u, 257u, 4097u};
        const auto count = counts[epoch % counts.size()];
        std::vector<ht::word> queries;
        Result expected;
        for (unsigned i = 0; i < count; ++i) {
            const unsigned q = epoch % 4 == 0 ? i % 300 : epoch % 4 == 1 ? 300 + i
                                                                           : (i * 131 + epoch) % 600;
            const auto key = mix.recover(0, q);
            queries.push_back(key);
            if (q < 300) { ++expected.hits; expected.keys += key; expected.values += q * 7; }
        }
        ht::require(automatic<0>(view, queries, direct) == expected, "direct automatic oracle");
        ht::require(automatic<32>(view, queries, prefetched) == expected, "PF32 automatic oracle");
        processed += count;
    }
    for (const auto* selector : {&direct, &prefetched}) {
        const auto s = selector->statistics();
        ht::require(s.predictable + s.unpredictable == processed, "persistent stream exact count");
    }
    std::cout << "PASS: native capture, complete automatic consumers, zero hit, spills, shifts and PF32 tails\n";
}
