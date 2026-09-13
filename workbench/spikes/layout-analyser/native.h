#pragma once
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace layout_probe {
using byte = std::uint8_t;
using clock = std::chrono::steady_clock;
struct buffer {
    std::unique_ptr<byte, decltype(&std::free)> data{nullptr, &std::free};
    std::size_t size;
    explicit buffer(std::size_t n) : size(n) {
        data.reset(static_cast<byte*>(std::aligned_alloc(128, (n + 127) / 128 * 128)));
        if (!data) std::abort();
        std::memset(data.get(), 0, (n + 127) / 128 * 128);
    }
    byte* get() { return data.get(); }
    const byte* get() const { return data.get(); }
    std::size_t capacity() const { return (size+127)/128*128; }
};
inline std::uint64_t hash(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}
inline std::uint64_t load(const byte* p, unsigned n) {
    std::uint64_t x = 0;
    for (unsigned j = 0; j < n; ++j) x |= std::uint64_t(p[j]) << (8*j);
    return x;
}
inline void store(byte* p, std::uint64_t x, unsigned n) {
    for (unsigned j = 0; j < n; ++j) p[j] = byte(x >> (8*j));
}
struct options {
    std::size_t rows = 4096, queries = 32768;
    unsigned reps = 3, ms = 20;
    std::uint64_t seed = 918273;
    bool check = false;
    std::string filter;
};
inline options parse(int argc, char** argv) {
    options o;
    for (int i=1; i<argc; ++i) {
        std::string k=argv[i];
        if (k=="--check") { o.check=true; continue; }
        if (i+1==argc) std::abort();
        const char* v=argv[++i];
        if (k=="--rows") o.rows=std::stoull(v);
        else if (k=="--queries") o.queries=std::stoull(v);
        else if (k=="--reps") o.reps=unsigned(std::stoul(v));
        else if (k=="--ms") o.ms=unsigned(std::stoul(v));
        else if (k=="--seed") o.seed=std::stoull(v);
        else if (k=="--filter") o.filter=v;
        else std::abort();
    }
    assert(o.rows>=32 && o.queries>0 && o.reps>0 && o.ms>0);
    return o;
}
inline std::vector<std::size_t> row_ids(const options& o) {
    std::vector<std::size_t> ids(o.queries);
    for (std::size_t i=0; i<ids.size(); ++i) ids[i]=hash(i+o.seed)%o.rows;
    return ids;
}
inline volatile std::uint64_t sink = 0;
inline void header() {
    std::puts("case,rows,queries,seed,rep,ns_per_operation,allocated_bytes,conditional_visits,checksum,batches");
}
template<class Fn>
void measure(const std::string& name, const options& o, std::size_t allocated,
             std::size_t visits, Fn&& fn) {
    if (!o.filter.empty() && name.find(o.filter)==std::string::npos) return;
    sink=fn();
    for (unsigned rep=0; rep<o.reps; ++rep) {
        auto start=clock::now();
        std::uint64_t checksum=0, batches=0;
        double ns;
        do {
            asm volatile("" ::: "memory");
            checksum += fn();
            ++batches;
            ns=std::chrono::duration<double,std::nano>(clock::now()-start).count();
        } while (ns < o.ms*1e6);
        sink=checksum;
        std::printf("%s,%zu,%zu,%llu,%u,%.9f,%zu,%zu,%llu,%llu\n",name.c_str(),
            o.rows,o.queries,static_cast<unsigned long long>(o.seed),rep,
            ns/(double(batches)*o.queries),allocated,visits,
            static_cast<unsigned long long>(checksum),static_cast<unsigned long long>(batches));
        std::fflush(stdout);
    }
}
} // namespace layout_probe
