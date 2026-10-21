#define main captured_memory_driver_main
#include PRIMARY_HINT_BENCH_FILE
#undef main

int main(int argc, char** argv) {
    ht::require(argc == 2, "seed argument");
    const word seed = std::strtoull(argv[1], nullptr, 10);
    Core table(seed);
    constexpr std::size_t population = 13926, requests = 262144;
    for (std::size_t i = 0; i < population; ++i) {
        const auto key = key_at(i, seed);
        table.insert(key, payload(key, 0));
    }
    std::cout << "seed,v,batch,pattern,trial,rep,method,ns,hits,keys,values,p,u,probes,switches\n";
    for (auto batch_size : {1u, 8u, 32u, 64u, 256u, 1024u, 4096u}) {
        for (unsigned pattern = 0; pattern < 5; ++pattern) {
            std::mt19937_64 rng(seed ^ 0x998b12);
            std::vector<std::vector<word>> batches;
            Result expected;
            for (std::size_t at = 0; at < requests; at += batch_size) {
                auto& q = batches.emplace_back();
                for (std::size_t i = at; i < std::min(at + batch_size, requests); ++i) {
                    const unsigned phase = ((i + 7759) / 16381) % 4;
                    const bool hit = pattern == 0 || (pattern == 2 && (rng() & 1)) ||
                                     (pattern == 3 && (i & 1)) ||
                                     (pattern == 4 && (phase == 0 || (phase == 1 && (rng() & 1)) ||
                                                       (phase == 3 && (i & 1))));
                    const auto rank = rng() % population;
                    const auto key = key_at(hit ? rank : population + rank, seed);
                    q.push_back(key);
                    if (hit) {
                        ++expected.hits;
                        expected.keys += key;
                        const auto value = payload(key, 0);
                        if constexpr (V)
                            expected.values += ht::load<8>(value.data());
                    }
                }
            }
            auto evaluate = [&](unsigned method) {
                Result result;
                PredictabilitySelector stream;
                const auto start = Clock::now();
                for (const auto& q : batches) {
                    Result part;
                    if (method == 0)
                        part = read_loop<false, true>(table, q);
                    if (method == 1)
                        part = hint_range<false, true, false>(table, q.data(), q.data() + q.size(),
                                                              q.data() + q.size());
                    if (method == 2)
                        part = hint_range<false, true, true>(table, q.data(), q.data() + q.size(),
                                                             q.data() + q.size());
                    if (method == 3)
                        part = hint_requests<false, true>(table, q, &stream);
                    result.hits += part.hits;
                    result.keys += part.keys;
                    result.values += part.values;
                }
                const auto ns = elapsed(start);
                ht::require(result == expected, "batched-stream complete result oracle");
                return std::tuple{ns / requests, result, stream.stats};
            };
            for (unsigned method = 0; method < 4; ++method)
                (void)evaluate(method);
            for (unsigned trial = 0; trial < 2; ++trial) {
                std::array<unsigned, 4> methods{0, 1, 2, 3};
                std::rotate(methods.begin(), methods.begin() + (trial + seed) % 4, methods.end());
                if (trial)
                    std::reverse(methods.begin(), methods.end());
                for (auto method : methods)
                    for (unsigned rep = 0; rep < 3; ++rep) {
                        const auto [ns, result, s] = evaluate(method);
                        std::cout << seed << ',' << V << ',' << batch_size << ',' << pattern << ','
                                  << trial << ',' << rep << ',' << method << ',' << ns << ','
                                  << result.hits << ',' << result.keys << ',' << result.values
                                  << ',' << s.predictable << ',' << s.unpredictable << ','
                                  << s.probes << ',' << s.switches << '\n';
                    }
            }
        }
    }
}
