using SelectionStats = ht::lookup_statistics;
using PredictabilitySelector = ht::lookup_selector;
SelectionStats last_selection;
template <bool Prefetch, bool Full, bool Unpredictable, class T>
[[gnu::noinline]] Result hint_range(const T& t, const word* first, const word* last,
                                  const word* hint_end) {
    Result result;
    for (auto p = first; p != last; ++p) {
        if constexpr (Prefetch)
            if (hint_end - p > 32) prefetch(t, p[32]);
        const auto key = *p;
        struct Match { Value value{}; bool found = false; };
        const auto match = [&] {
            if constexpr (core<T>) {
                constexpr auto hint = Unpredictable ? ht::lookup_hint::unpredictable
                                                     : ht::lookup_hint::predictable;
                return t.template get<hint>(key);
            } else {
                Match out;
                out.found = find(t, key, [&](const Value& v) { out.value = v; });
                return out;
            }
        }();
        if constexpr (!Unpredictable) {
            if (match.found) {
                ++result.hits;
                if constexpr (Full) {
                    result.keys += key;
                    for (unsigned j = 0; j < V; j += 8)
                        result.values += ht::load<8>(match.value.data() + j);
                }
            }
        } else {
            result.hits += match.found;
            if constexpr (Full) {
                result.keys += key & -word(match.found);
                for (unsigned j = 0; j < V; j += 8)
                    result.values += ht::load<8>(match.value.data() + j);
            }
        }
    }
    return result;
}
template <bool Prefetch, bool Full, class T>
[[gnu::noinline]] Result hint_requests(const T& t, const std::vector<word>& queries,
                                      PredictabilitySelector* stream = nullptr) {
    const auto* first = queries.data();
    const auto* last = first + queries.size();
    if constexpr (Prefetch)
        for (std::size_t i = 0; i < std::min(std::size_t(32), queries.size()); ++i)
            prefetch(t, queries[i]);
    if constexpr (HINT_MODE < 2) {
        const auto result = hint_range<Prefetch, Full, HINT_MODE == 1>(t, first, last, last);
        last_selection = HINT_MODE == 1 ? SelectionStats{0, word(queries.size()), 0, 0}
                                        : SelectionStats{word(queries.size()), 0, 0, 0};
        return result;
    } else {
        PredictabilitySelector local;
        auto& selector = stream ? *stream : local;
        const auto before = selector.statistics();
        Result result;
        selector.run(queries.size(), [&](auto hint, std::size_t at, std::size_t count) {
            constexpr bool method = decltype(hint)::value == ht::lookup_hint::unpredictable;
            const auto piece = hint_range<Prefetch, Full, method>(t, first + at, first + at + count, last);
            result.hits += piece.hits; result.keys += piece.keys; result.values += piece.values;
        });
        const auto after = selector.statistics();
        last_selection = {after.predictable - before.predictable,
                          after.unpredictable - before.unpredictable,
                          after.probes - before.probes,
                          after.switches - before.switches};
        return result;
    }
}
template <class T>
Result read_requests(const T& t, const std::vector<word>& q, unsigned distance, bool full) {
    if (full) return distance ? hint_requests<true, true>(t, q) : hint_requests<false, true>(t, q);
    return distance ? hint_requests<true, false>(t, q) : hint_requests<false, false>(t, q);
}
