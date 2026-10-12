#include "bucket.h"

template<unsigned K> void check_fingerprints() {
    std::mt19937 rng(71 + K);
    std::array<byte,16> fp;
    std::array<byte,128> storage;
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    auto* pages = static_cast<byte*>(mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(pages != MAP_FAILED);
    assert(mprotect(pages + page, page, PROT_NONE) == 0);
    for (unsigned trial = 0; trial < 24; ++trial) {
        for (auto& x : fp) x = byte(rng() & low(K));
        for (unsigned phase = 0; phase < 65; ++phase) {
            auto* data = phase == 64 ? pages + page - 2 * K : storage.data() + phase;
            encode<K>(data, fp);
            for (unsigned wanted = 0; wanted <= low(K); ++wanted) {
                unsigned expected = 0;
                for (unsigned i = 0; i < 16; ++i) expected |= unsigned(fp[i] == wanted) << i;
                assert(match_ikea<K>(data, byte(wanted)) == expected);
                assert(match_planes<K>(data, byte(wanted)) == expected);
                for (unsigned used = 0; used <= 16; ++used) {
                    unsigned live = (1u << used) - 1;
                    assert((match_ikea<K>(data, byte(wanted)) & live) == (expected & live));
                }
            }
        }
    }
    munmap(pages, page * 2);
}

template<unsigned K, unsigned S> void check_bucket() {
    using B = bucket<K,S>;
    plans p(S);
    std::array<byte,B::bytes + 64> bytes;
    std::array<byte,16> fp;
    for (unsigned phase = 0; phase < 64; ++phase) {
        auto* data = bytes.data() + phase;
        std::fill(data, data + B::bytes, byte{255});
        // All fingerprints deliberately match; force exact suffix checks through
        // every prior candidate, including masked unused bits in final byte.
        fp.fill(byte(low(K))); encode<K>(data + 8, fp);
        for (unsigned i = 0; i < 16; ++i) {
            auto* entry = data + B::entry_offset + i * B::entry_bytes;
            const word suffix = word(i + 1) & low(S), value = 731 + i;
            if constexpr (S) std::memcpy(entry, &suffix, B::suffix_bytes);
            if constexpr (S % 8) entry[B::suffix_bytes-1] |= byte(255u << (S % 8));
            std::memcpy(entry + B::suffix_bytes, &value, 8);
        }
        for (unsigned used = 0; used <= 16; ++used) {
            data[0] = byte(used);
            for (unsigned query = 0; query <= 17; ++query) {
                const word suffix = query & low(S);
                word expected = ~word{};
                for (unsigned i = 0; i < used; ++i)
                    if (((i+1) & low(S)) == suffix) { expected = 731 + i; break; }
                assert((B::template get<false,false>(p,data,byte(low(K)),suffix) == expected));
                assert((B::template get<false,true>(p,data,byte(low(K)),suffix) == expected));
                assert((B::template get<true,false>(p,data,byte(low(K)),suffix) == expected));
                assert((B::template get<true,true>(p,data,byte(low(K)),suffix) == expected));
                assert((B::template get<false,2>(p,data,byte(low(K)),suffix) == expected));
                assert((B::template get<true,2>(p,data,byte(low(K)),suffix) == expected));
            }
        }
    }
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    auto* pages = static_cast<byte*>(mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(pages != MAP_FAILED && B::bytes < page);
    assert(mprotect(pages + page, page, PROT_NONE) == 0);
    auto* end_bucket = pages + page - B::bytes;
    std::fill(end_bucket, end_bucket + B::bytes, byte{});
    end_bucket[0] = 16;
    fp.fill(0); fp[15] = 1; encode<K>(end_bucket + 8, fp);
    const word suffix = low(S), value = 0x4e454f4e41565832ull;
    auto* last = end_bucket + B::entry_offset + 15 * B::entry_bytes;
    if constexpr (S) std::memcpy(last, &suffix, B::suffix_bytes);
    std::memcpy(last + B::suffix_bytes, &value, 8);
    assert((B::template get<false,false>(p,end_bucket,1,suffix) == value));
    assert((B::template get<false,true>(p,end_bucket,1,suffix) == value));
    assert((B::template get<true,false>(p,end_bucket,1,suffix) == value));
    assert((B::template get<true,true>(p,end_bucket,1,suffix) == value));
    assert((B::template get<false,2>(p,end_bucket,1,suffix) == value));
    assert((B::template get<true,2>(p,end_bucket,1,suffix) == value));
    munmap(pages, 2 * page);
}

volatile word observed;
template<unsigned K, unsigned S, bool Planes, unsigned Tuple, bool Serial>
[[gnu::noinline]] word run(const plans& p, const byte* data, std::size_t size,
                          std::size_t rounds) {
    using B = bucket<K,S>;
    word sum = 0;
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < rounds; ++i) {
        if constexpr (!Serial) cursor = (i * 11995408973635179863ull) & (size - 1);
        const unsigned slot = (cursor * 7) & 15;
        const auto value = B::template get<Planes,Tuple>(p, data + cursor * B::bytes,
            byte((cursor + slot * 11) & low(K)), slot + 1);
        if constexpr (Serial) cursor = value >> 4;
        sum += value;
    }
    return sum ^ cursor;
}

template<unsigned K, unsigned S> void bench(std::size_t size, std::size_t rounds) {
    using B = bucket<K,S>;
    plans p(S);
    std::vector<byte> data(size * B::bytes);
    std::vector<std::size_t> order(size);
    std::vector<std::size_t> next_key(size);
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 rng(123); std::shuffle(order.begin(), order.end(), rng);
    for (std::size_t i = 0; i < size; ++i) {
        const auto index = order[i], next = order[(i + 1) & (size - 1)];
        next_key[index] = next;
        auto* b = data.data() + index * B::bytes;
        b[0] = 16;
        std::array<byte,16> fp;
        for (unsigned slot = 0; slot < 16; ++slot) {
            fp[slot] = byte((index + slot * 11) & low(K));
            word suffix = slot + 1, value = (next << 4) | slot;
            auto* entry = b + B::entry_offset + slot * B::entry_bytes;
            std::memcpy(entry, &suffix, B::suffix_bytes);
            std::memcpy(entry + B::suffix_bytes, &value, 8);
        }
        encode<K>(b + 8, fp);
    }
    // Expected work comes from the original logical permutation, independently
    // of either encoded lookup implementation.
    word serial_sum = 0, independent_sum = 0;
    std::size_t serial_cursor = 0, independent_cursor = 0;
    for (std::size_t i = 0; i < rounds; ++i) {
        const unsigned serial_slot = (serial_cursor * 7) & 15;
        serial_cursor = next_key[serial_cursor]; serial_sum += (serial_cursor << 4) | serial_slot;
        independent_cursor = (i * 11995408973635179863ull) & (size - 1);
        independent_sum += (next_key[independent_cursor] << 4) | ((independent_cursor * 7) & 15);
    }
    const auto expected_serial = serial_sum ^ serial_cursor;
    const auto expected_independent = independent_sum ^ independent_cursor;
    // Release reference construction state before measuring the bucket arrays.
    std::vector<std::size_t>().swap(order);
    std::vector<std::size_t>().swap(next_key);
    auto sample = [&]<bool Planes,unsigned Tuple,bool Serial>(unsigned rep) {
        observed = run<K,S,Planes,Tuple,Serial>(p,data.data(),size,size);
        auto begin = std::chrono::steady_clock::now();
        auto result = run<K,S,Planes,Tuple,Serial>(p,data.data(),size,rounds);
        const double ns = std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count();
        assert(result == (Serial ? expected_serial : expected_independent)); observed = result;
        std::printf("%u,%u,%zu,%zu,%u,%u,%u,%u,%.6f,%llu\n",K,S,size,data.size(),
            Planes,Tuple,Serial,rep,ns/rounds,static_cast<unsigned long long>(result));
    };
    for (unsigned rep=0;rep<5;++rep) {
        // Reverse recipe order on alternating repetitions; each sample is pinned
        // by the runner. This is a warmed repeated bucket consumer, not DRAM proof.
        if (rep % 2 == 0) {
            sample.template operator()<false,false,true>(rep); sample.template operator()<false,false,false>(rep);
            sample.template operator()<true,false,true>(rep); sample.template operator()<true,false,false>(rep);
            sample.template operator()<false,true,true>(rep); sample.template operator()<false,true,false>(rep);
            sample.template operator()<true,true,true>(rep); sample.template operator()<true,true,false>(rep);
            sample.template operator()<false,2,true>(rep); sample.template operator()<false,2,false>(rep);
            sample.template operator()<true,2,true>(rep); sample.template operator()<true,2,false>(rep);
        } else {
            sample.template operator()<true,2,false>(rep); sample.template operator()<true,2,true>(rep);
            sample.template operator()<false,2,false>(rep); sample.template operator()<false,2,true>(rep);
            sample.template operator()<true,true,false>(rep); sample.template operator()<true,true,true>(rep);
            sample.template operator()<false,true,false>(rep); sample.template operator()<false,true,true>(rep);
            sample.template operator()<true,false,false>(rep); sample.template operator()<true,false,true>(rep);
            sample.template operator()<false,false,false>(rep); sample.template operator()<false,false,true>(rep);
        }
    }
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1],"check") == 0) {
        [&]<std::size_t... I>(std::index_sequence<I...>) { (check_fingerprints<I+1>(),...); }(std::make_index_sequence<8>{});
        [&]<unsigned... S>(std::integer_sequence<unsigned,S...>) { (check_bucket<3,S>(),...); }
            (std::integer_sequence<unsigned,0,1,7,8,9,15,16,17,23,24,31,32,33,39,40,41,47,48,49,55,56>{});
        check_bucket<8,24>();
        std::puts("fingerprints K=1..8; all occupancies; all 64 phases; guard-page tails; suffix boundaries: passed");
        return 0;
    }
    if (argc != 3) { std::fprintf(stderr,"usage: %s check | buckets rounds\n",argv[0]); return 2; }
    const auto size = std::strtoull(argv[1],nullptr,10), rounds = std::strtoull(argv[2],nullptr,10);
    if (!std::has_single_bit(size) || !rounds || size > (1u<<23)) return 2;
    std::puts("fingerprint_bits,suffix_bits,buckets,payload_bytes,plane_predicate,tuple_recipe,serial,rep,ns_per_op,result");
    bench<3,24>(size,rounds); bench<7,24>(size,rounds); bench<8,24>(size,rounds);
    bench<8,40>(size,rounds);
}
