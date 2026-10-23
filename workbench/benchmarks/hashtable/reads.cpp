#include "support.h"
namespace screen {
struct contender {
    const char* name;
    unsigned encoding;
    search_kernel kernel;
    std::size_t bytes, spills;
    storage_usage storage;
    std::shared_ptr<void> owner;
    result (*lookup[2])(const void*, const data&);
};
template <unsigned V, unsigned E, search_kernel Kernel> contender make(const data& in) {
    using Owner = table<W, b, V, E, Kernel>;
    auto owner = std::make_shared<Owner>(in.seed & mask<W>);
    for (auto k : in.keys)
        require(owner->insert(k, payload<V>(k, 0)), "Ikea fixture insert");
    owner->validate();
    verify<V>(*owner, in);
    return {"ikea",
            E,
            Kernel,
            owner->bytes_used(),
            owner->spill_size(),
            owner->storage(),
            owner,
            {&run<V, Owner>, &run<V, Owner, true>}};
}
template <unsigned V> contender baseline(const data& in) {
    using Owner = Abseil<V>;
    auto owner = std::make_shared<Owner>();
    owner->reserve(in.n);
    for (auto k : in.keys) {
        if constexpr (V)
            require(owner->emplace(Key(k), payload<V>(k, 0)).second, "Abseil fixture insert");
        else
            require(owner->insert(Key(k)).second, "Abseil fixture insert");
    }
    verify<V>(*owner, in);
    return {"abseil",
            0,
            search_kernel::generic,
            absl::container_internal::AllocatedByteSize(*owner),
            0,
            {},
            owner,
            {&run<V, Owner>, &run<V, Owner, true>}};
}
template <unsigned V>
void surface(const data& in, const options& args, std::span<const std::byte> conditioning) {
    std::vector<contender> owners;
    owners.push_back(baseline<V>(in));
    owners.push_back(bind_measured_encoding<K>(
        V, args.cpu, args.isa, [&](auto encoding, auto kernel, encoding_choice) {
            return make<V, decltype(encoding)::value, decltype(kernel)::value>(in);
        }));
    const auto expected = in.expected<V>();
    std::mt19937_64 rng(in.seed + V);
    for (unsigned rep = 0; rep < args.repetitions; ++rep) {
        std::shuffle(owners.begin(), owners.end(), rng);
        for (unsigned mode = 0; mode < 2; ++mode)
            for (const auto& c : owners) {
                require(c.lookup[mode](c.owner.get(), in) == expected, "warm complete result");
                condition(conditioning);
                const auto start = clock::now();
                auto got = c.lookup[mode](c.owner.get(), in);
                asm volatile(""
                             : "+r"(got.sum), "+r"(got.hits), "+r"(got.keys), "+r"(got.values)
                             :
                             : "memory");
                const auto ns = elapsed(start) / in.requests.size();
                require(got == expected, "timed complete result");
                std::cout << c.name << ',' << (mode ? "prefetch32" : "independent") << ',' << W
                          << ',' << b << ',' << K << ',' << c.encoding << ',' << V << ','
                          << unsigned(c.kernel) << ',' << B << ',' << T << ',' << N << ',' << in.n
                          << ',' << in.requests.size() << ',' << in.seed << ',' << rep << ',' << ns
                          << ',' << c.bytes << ',' << c.spills << ',' << c.storage.assigned << ','
                          << c.storage.accessible << ',' << c.storage.reserved << ',' << in.miss
                          << ',' << conditioning.size() << '\n';
            }
    }
}
} // namespace screen
int main(int argc, char** argv) {
    try {
        auto args = screen::parse(argc, argv);
        ikea::hashtable::require(args.operation == "check" || args.operation == "bench",
                                 "read operation must be check or bench");
        if (args.operation == "check") {
            args.repetitions = 1;
            args.requests = 4097;
            args.miss_percent = 50;
            args.conditioning_bytes = 0;
        }
        screen::data in(args.seed, args.requests, args.miss_percent);
        std::vector<screen::byte> conditioning(args.conditioning_bytes, std::byte{17});
        std::cout
            << "implementation,mode,W,b,K,E,V,kernel,B,T,N,n,requests,seed,rep,ns,bytes,spill,assigned,accessible,reserved,miss_percent,conditioning_bytes\n";
        screen::surface<0>(in, args, conditioning);
        screen::surface<8>(in, args, conditioning);
        screen::surface<64>(in, args, conditioning);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
