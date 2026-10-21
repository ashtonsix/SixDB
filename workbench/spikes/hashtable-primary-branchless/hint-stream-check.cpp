#define main captured_memory_driver_main
#include PRIMARY_HINT_BENCH_FILE
#undef main

int main() {
    // The preference must follow a real cost reversal, including unequal-sized
    // batches. A small advantage must not displace the predictable method.
    PredictabilitySelector choice;
    auto feed = [&](word p_cost, word u_cost) {
        for (unsigned i = 0; i < 192; ++i) {
            const auto method = choice.method();
            const auto count = i % 2 ? 37u : 251u;
            choice.observed(method, (method ? u_cost : p_cost) * count, count);
        }
    };
    feed(100, 20);
    ht::require(choice.unpredictable, "select materially cheaper unpredictable method");
    feed(20, 100);
    ht::require(!choice.unpredictable, "return to cheaper predictable method after shift");
    feed(100, 95);
    ht::require(!choice.unpredictable, "prefer predictable for a small/noisy advantage");

    for (word seed : {713ULL, 991ULL, 1729ULL}) {
        Core table(seed);
        constexpr std::size_t population = 1024;
        for (std::size_t i = 0; i < population; ++i) {
            const auto key = key_at(i, seed);
            table.insert(key, payload(key, 0));
        }
        PredictabilitySelector stream;
        word processed = 0;
        for (unsigned epoch = 0; epoch < 200; ++epoch) {
            const std::array<std::size_t, 9> lengths{1, 31, 32, 33, 127, 255, 256, 257, 4097};
            std::vector<word> q;
            Result expected;
            for (std::size_t i = 0; i < lengths[epoch % lengths.size()]; ++i) {
                const bool hit = epoch % 4 == 0 || (epoch % 4 == 1 && (bits(i + epoch) & 1)) ||
                                 (epoch % 4 == 3 && (i & 1));
                const auto key = key_at(hit ? i % population : population + epoch * 8192 + i, seed);
                q.push_back(key);
                if (hit) {
                    ++expected.hits;
                    expected.keys += key;
                    const auto value = payload(key, 0);
                    if constexpr (V)
                        expected.values += ht::load<8>(value.data());
                }
            }
            const auto got = epoch % 2 ? hint_requests<true, true>(table, q, &stream)
                                       : hint_requests<false, true>(table, q, &stream);
            ht::require(got == expected, "persistent short-batch result oracle");
            ht::require(last_selection.predictable + last_selection.unpredictable == q.size(),
                        "each useful request consumed exactly once");
            processed += q.size();
        }
        ht::require(stream.stats.predictable + stream.stats.unpredictable == processed,
                    "caller-owned stream accounts for all batches");
    }
    std::cout << "cost reversal, predictable bias, wrong-hint semantics and persistent short "
                 "batches passed\n";
}
