// Caller-owned per-stream selector. No state is added to the hash table.
struct SelectionStats {
    word predictable = 0, unpredictable = 0, probes = 0, switches = 0;
};
SelectionStats last_selection;
[[gnu::always_inline]] word selector_clock() {
#if defined(__x86_64__)
    _mm_lfence();
    const auto ticks = __rdtsc();
    _mm_lfence();
    return ticks;
#else
    return Clock::now().time_since_epoch().count();
#endif
}
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
struct PredictabilitySelector {
    // Probe each specialization on useful work once per 64 blocks (16K keys).
    // Bias close/noisy measurements towards predictable; switch only at blocks.
    static constexpr unsigned block = 256, period = 64;
    bool unpredictable = false;
    word predictable_ticks = 0, unpredictable_ticks = 0;
    SelectionStats stats;
    unsigned epoch = 0;

    bool method() const {
        const auto phase = epoch % period;
        return phase < 2 ? bool(phase) : unpredictable;
    }
    bool sampling() const { return epoch % period < 2; }
    void observed(bool method, word ticks, std::size_t count) {
        (method ? stats.unpredictable : stats.predictable) += count;
        if (sampling()) {
            ++stats.probes;
            (method ? unpredictable_ticks : predictable_ticks) = (ticks << 10) / count;
            if (epoch % period == 1) {
                const bool next = unpredictable_ticks * 110 < predictable_ticks * 100;
                stats.switches += next != unpredictable;
                unpredictable = next;
            }
        }
        ++epoch;
    }
};
static_assert(sizeof(PredictabilitySelector) == 64);
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
        const auto before = selector.stats;
        Result result;
        while (first != last) {
            const auto count = std::min(std::size_t(last - first),
                                        std::size_t(PredictabilitySelector::block));
            const bool method = selector.method();
            const bool sampling = selector.sampling();
            const auto start = sampling ? selector_clock() : 0;
            const auto piece = method ? hint_range<Prefetch, Full, true>(t, first, first + count, last)
                                      : hint_range<Prefetch, Full, false>(t, first, first + count, last);
            const auto ticks = sampling ? selector_clock() - start : 0;
            selector.observed(method, ticks, count);
            result.hits += piece.hits; result.keys += piece.keys; result.values += piece.values;
            first += count;
        }
        last_selection = {selector.stats.predictable - before.predictable,
                          selector.stats.unpredictable - before.unpredictable,
                          selector.stats.probes - before.probes,
                          selector.stats.switches - before.switches};
        return result;
    }
}
template <class T>
Result read_requests(const T& t, const std::vector<word>& q, unsigned distance, bool full) {
    if (full) return distance ? hint_requests<true, true>(t, q) : hint_requests<false, true>(t, q);
    return distance ? hint_requests<true, false>(t, q) : hint_requests<false, false>(t, q);
}
