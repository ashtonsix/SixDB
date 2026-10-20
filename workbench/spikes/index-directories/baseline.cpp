// First storage-only control. Complete Object/Part publication is a separate arm.
#include <ikea/boundlist/author/search.h>
#include <ikea/hashtable.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace bl = ikea::boundlist;
namespace ht = ikea::hashtable;
using u64 = std::uint64_t;
struct alignas(64) Dirent {
    std::array<u64, 6> parts{};
    std::uint32_t representation{};
    std::uint16_t flags{}, overflow_rank{};
    std::uint32_t overflow_block{}, revision{};
};
static_assert(sizeof(Dirent) == 64);
u64 consume(const Dirent& d, unsigned projection) {
    u64 result = d.representation;
    for (unsigned i = 0; i < projection; ++i)
        result += *reinterpret_cast<const u64*>(d.parts[i]);
    return result;
}
struct Fixture {
    std::vector<u64> maxima, payload;
    std::vector<Dirent> entries;
    ht::table<32, 12, 64> hash{713};
    explicit Fixture(unsigned count) : maxima(count), payload(std::size_t(count) * 6), entries(count) {
        for (unsigned i = 0; i < count; ++i) {
            // Occupied local logical interval [16i,16i+7]; physical segment
            // prefixes are sparse. This is one selected layer of a J=0 index.
            maxima[i] = u64(i) * 16 + 7;
            auto& d = entries[i]; d.representation = 1; d.revision = 1;
            for (unsigned p = 0; p < 6; ++p) {
                payload[std::size_t(i) * 6 + p] = i * 31 + p;
                d.parts[p] = reinterpret_cast<u64>(&payload[std::size_t(i) * 6 + p]);
            }
            ht::bytes<64> value{}; std::memcpy(value.data(), &d, sizeof(d));
            hash.insert(std::uint32_t(i * 2), value);
        }
    }
};
unsigned ordinary(const Fixture& f, u64 q) {
    return std::lower_bound(f.maxima.begin(), f.maxima.end(), q) - f.maxima.begin();
}
struct Trace { std::string name; std::vector<u64> keys; };
std::vector<Trace> traces(unsigned n, unsigned count) {
    std::vector<Trace> result;
    std::mt19937_64 rng(9567);
    for (std::string name : {"uniform-hit", "logical-gap", "hot-tenant", "ordered", "duplicate-boundary"}) {
        Trace t{name, {}}; t.keys.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            unsigned rank = rng() % n;
            if (name == "hot-tenant" && i % 10) rank %= std::min(n, 64u);
            if (name == "ordered") rank = i % n;
            // Repeated access to either edge of a group: a routing pressure
            // control, not a claim that this fixture already stores duplicates.
            if (name == "duplicate-boundary") rank = ((rng() % ((n + 15) / 16)) * 16) % n;
            t.keys.push_back(u64(rank) * 16 + (name == "logical-gap" ? 12 : 3));
        }
        result.push_back(std::move(t));
    }
    return result;
}
template<class Search> u64 loop(const Fixture& f, const Trace& trace, unsigned projection,
    bool dependent, Search search) {
    u64 checksum = 0;
    for (unsigned i = 0; i < trace.keys.size(); ++i) {
        // The serial consumer's next request depends on the previous result.
        const unsigned ti = dependent ? (i + (checksum & 1023)) % trace.keys.size() : i;
        const auto r = search(trace.keys[ti]);
        if (r < f.entries.size()) checksum += consume(f.entries[r], projection);
        else checksum += 97;
    }
    return checksum;
}
template<class F> double timed(F&& fn, u64& result) {
    const auto start = std::chrono::steady_clock::now();
    result = fn();
    asm volatile("" : "+r"(result) : : "memory");
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
}
void check() {
    for (unsigned n : {0, 1, 15, 16, 17, 255, 256, 257, 4096}) {
        Fixture f(n);
        const bl::direct_source<u64> source{f.maxima.data(), n};
        auto bound = bl::author::bind_search(source);
        for (u64 q = 0; q < u64(n) * 16 + 32; ++q) {
            // Linear oracle, independent of the searched storage.
            unsigned expected = 0;
            while (expected < n && u64(expected) * 16 + 7 < q) ++expected;
            assert(ordinary(f,q) == expected);
            assert(bl::lower_bound(source,q) == expected);
            assert(bl::lower_bound(bound,q) == expected);
        }
        for (unsigned i = 0; i < n; ++i) {
            bool visited = false;
            assert(f.hash.find(i * 2, [&](const auto& bytes) {
                Dirent d; std::memcpy(&d, bytes.data(), sizeof(d));
                assert(consume(d,6) == consume(f.entries[i],6)); visited = true;
            }));
            assert(visited && !f.hash.find(i * 2 + 1, [](auto&) {}));
        }
    }
    std::puts("storage oracle: empty/full boundaries, native search, prepared binding, 64-byte hash values and six Part reads passed");
}
template<unsigned B,unsigned E> void geometry(const Fixture& f,const Trace& trace,unsigned grain,const char* name,unsigned trial) {
    using Table=ht::table<32,B,64,E>;
    const unsigned n=f.entries.size(),groups=(n+grain-1)/grain;
    auto build=[&](unsigned group,unsigned changed,unsigned revision) {
        auto owner=std::make_unique<Table>(713);
        for(unsigned i=group*grain;i<std::min(n,(group+1)*grain);++i) {
            auto d=f.entries[i];if(i==changed)d.revision=revision;
            ht::bytes<64> value{};std::memcpy(value.data(),&d,64);assert(owner->insert(i,value));
        }
        return owner;
    };
    std::vector<std::unique_ptr<Table>> owners;owners.reserve(groups);
    const auto start=std::chrono::steady_clock::now();
    for(unsigned g=0;g<groups;++g)owners.push_back(build(g,n,0));
    const auto setup=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
    std::size_t allocated=0,spills=0;for(const auto& owner:owners){allocated+=owner->bytes_used();spills+=owner->spill_size();}
    u64 result;const auto ns=timed([&]{u64 sum=0;for(auto q:trace.keys){const unsigned p=q/16;
        const bool found=owners[p/grain]->find(p,[&](const auto& value){Dirent d;std::memcpy(&d,value.data(),64);sum+=consume(d,1);});assert(found);}return sum;},result);
    u64 expected=0;for(auto q:trace.keys)expected+=consume(f.entries[q/16],1);assert(result==expected);
    std::printf("hash-geometry,%u,uniform-hit,1,0,%s,%u,%zu,%.3f,%llu,%zu,%zu,%zu,%zu,%.0f\n",n,name,trial,trace.keys.size(),ns/trace.keys.size(),
        (unsigned long long)result,f.entries.size()*64,f.payload.size()*8,allocated,spills,setup);
    // A typed owner construction control, not a complete Object publication.
    // Retain each replacement across this short burst; consume the changed row.
    constexpr unsigned writes=16;std::vector<std::unique_ptr<Table>> replacements;replacements.reserve(writes);
    const auto rebuild=timed([&]{u64 sum=0;for(unsigned w=0;w<writes;++w){const unsigned p=(w*251)%n;
        auto owner=build(p/grain,p,w+17);const bool found=owner->find(p,[&](const auto& value){Dirent d;std::memcpy(&d,value.data(),64);assert(d.revision==w+17);sum+=consume(d,1)+d.revision;});assert(found);
        replacements.push_back(std::move(owner));}return sum;},result);
    std::printf("hash-rebuild,%u,one-owner-rebuild,1,0,%s,%u,%u,%.3f,%llu,%zu,%zu,%zu,%zu,0\n",n,name,trial,writes,rebuild/writes,
        (unsigned long long)result,f.entries.size()*64,f.payload.size()*8,allocated,spills);
}
int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--check") { check(); return 0; }
    const unsigned ops = argc > 1 ? std::stoul(argv[1]) : 131072;
    const unsigned reps = argc > 2 ? std::stoul(argv[2]) : 3;
    std::puts("phase,entries,trace,projection,dependent,arm,trial,operations,ns_per_operation,checksum,descriptor_bytes,payload_bytes,hash_bytes,spills,setup_ns");
    for (unsigned n : {256, 4096, 65536}) {
        Fixture f(n);
        const bl::direct_source<u64> source{f.maxima.data(), n};
        const auto start = std::chrono::steady_clock::now();
        auto bound = bl::author::bind_search(source);
        const auto setup = std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
        for (const auto& trace : traces(n, ops)) for (unsigned projection : {1,6}) for (bool dependent : {false,true}) {
            std::array<u64,3> checksums{};
            for (unsigned trial = 0; trial < reps; ++trial) for (unsigned offset = 0; offset < 3; ++offset) {
                const unsigned arm = (offset + trial) % 3;
                auto run = [&] {
                    if (!arm) return loop(f,trace,projection,dependent,[&](u64 q){ return ordinary(f,q); });
                    if (arm == 1) return loop(f,trace,projection,dependent,[&](u64 q){ return bl::lower_bound(source,q); });
                    return loop(f,trace,projection,dependent,[&](u64 q){ return bl::lower_bound(bound,q); });
                };
                u64 value; const auto ns = timed(run,value); checksums[arm] = value;
                std::printf("storage,%u,%s,%u,%u,%s,%u,%u,%.3f,%llu,%zu,%zu,%zu,%zu,%.0f\n", n,trace.name.c_str(),projection,dependent,
                    std::array{"std-lower-bound","boundlist","boundlist-bound"}[arm],trial,ops,ns/ops,
                    static_cast<unsigned long long>(value),f.entries.size()*sizeof(Dirent)+f.maxima.size()*8,
                    f.payload.size()*8,f.hash.bytes_used(),f.hash.spill_size(),arm==2 ? setup:0);
            }
            assert(checksums[0] == checksums[1] && checksums[1] == checksums[2]);
        }
        // Known physical-prefix resolution is a different operation from logical
        // seek. Give both controls exactly the same hit/miss request stream.
        const auto trace = traces(n,ops).front();
        for (unsigned trial=0;trial<reps;++trial) for(unsigned offset=0;offset<2;++offset) {
            const unsigned arm=(trial+offset)%2; u64 value;
            const auto ns=timed([&]{u64 sum=0; for(auto q:trace.keys) {
                auto prefix=std::uint32_t(q/16*2 + (q%32==3));
                if(!arm) {if(!(prefix%2) && prefix/2<n) sum+=consume(f.entries[prefix/2],1); else sum+=97;}
                else if(!f.hash.find(prefix,[&](const auto& bytes){Dirent d; std::memcpy(&d,bytes.data(),64);sum+=consume(d,1);})) sum+=97;
            }return sum;},value);
            std::printf("prefix,%u,half-miss,1,0,%s,%u,%u,%.3f,%llu,%zu,%zu,%zu,%zu,0\n",n,arm?"ikea-hash":"direct-prefix-control",trial,ops,ns/ops,
                static_cast<unsigned long long>(value),f.entries.size()*64,f.payload.size()*8,f.hash.bytes_used(),f.hash.spill_size());
        }
    }
    Fixture f(4096);const auto trace=traces(4096,ops).front();
    for(unsigned trial=0;trial<reps;++trial)for(unsigned offset=0;offset<5;++offset) {
        switch((trial+offset)%5) {
        case 0:geometry<8,24>(f,trace,4096,"whole-b8-e24",trial);break;
        case 1:geometry<9,24>(f,trace,4096,"whole-b9-e24",trial);break;
        case 2:geometry<8,24>(f,trace,64,"pages64-b8-e24-control",trial);break;
        case 3:geometry<2,30>(f,trace,64,"pages64-b2-e30",trial);break;
        case 4:geometry<3,30>(f,trace,64,"pages64-b3-e30",trial);break;
        }
    }
}
