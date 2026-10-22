#include <ikea/intervals.h>
#include <ikea/streampack.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace in=ikea::intervals;
namespace sp=ikea::streampack;
using u64=std::uint64_t;
constexpr unsigned requests=4096;
volatile u64 consumed_seam;
void need(bool ok,const char *why) {if(!ok) throw std::runtime_error(why);}
struct arm {std::string name;std::function<u64()> run;};
void measure(const std::string &case_name,std::vector<arm> arms,u64 expected,unsigned trials=11) {
    for(auto &a:arms) need(a.run()==expected,"complete seam consumer differs from independent oracle");
    for(unsigned trial=0;trial<trials;++trial) for(unsigned position=0;position<arms.size();++position) {
        auto &a=arms[(position+trial)%arms.size()];
        const auto begin=std::chrono::steady_clock::now();
        for(unsigned repeat=0;repeat<32;++repeat) {
            asm volatile("" ::: "memory");consumed_seam=a.run();
        }
        const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-begin).count();
        std::printf("{\"case\":\"%s\",\"arm\":\"%s\",\"trial\":%u,\"elapsed_ns\":%lld,"
                    "\"operations\":%u,\"checksum\":\"%llu\"}\n",case_name.c_str(),a.name.c_str(),trial,
                    static_cast<long long>(elapsed),requests*32,static_cast<unsigned long long>(expected));
    }
}

struct key_ref {const std::uint8_t *p;unsigned width;
    friend bool operator<(key_ref a,key_ref b) {return std::memcmp(a.p,b.p,a.width)<0;}
};
struct key_source {const std::uint8_t *p;unsigned count,width;
    unsigned size() const {return count;}
    key_ref at(unsigned r) const {return {p+std::size_t(r)*width,width};}
};
void directory() {
    // Same sources and comparison law as engine/src/directory.cpp, followed by
    // actual selected 64-byte dirent data consumption. Object scope costs are
    // covered separately by the maintained integration check, not modeled here.
    for(unsigned n:{128u,512u,1024u}) {
        std::vector<std::uint32_t> prefixes(n);
        std::vector<std::array<u64,8>> dirents(n);
        std::array<unsigned,requests> queries{};
        std::mt19937 random(713);
        for(unsigned r=0;r<n;++r) {prefixes[r]=3*r;dirents[r].fill(random());}
        u64 expected=0;
        for(auto &q:queries) {
            q=random()%(3*n+1);
            const auto r=std::lower_bound(prefixes.begin(),prefixes.end(),q)-prefixes.begin();
            if(r<n) expected+=dirents[r][3];
        }
        in::direct_source source{prefixes.data(),n};
        measure("directory_prefix_child_n"+std::to_string(n),{
            {"maintained",[&] {u64 sum=0;for(auto q:queries) {auto r=ikea::boundlist::lower_bound(source,q);if(r<n) sum+=dirents[r][3];}return sum;}},
            {"intervals",[&] {u64 sum=0;for(auto q:queries) {auto r=in::lower_bound(source,q);if(r<n) sum+=dirents[r][3];}return sum;}}
        },expected);
        for(unsigned width:{4u,16u}) {
            std::vector<std::uint8_t> keys(n*width),points(requests*width);
            for(unsigned r=0;r<n;++r) for(unsigned j=0;j<4;++j)
                keys[(r+1)*width-1-j]=prefixes[r]>>(j*8);
            for(unsigned q=0;q<requests;++q) for(unsigned j=0;j<4;++j)
                points[(q+1)*width-1-j]=queries[q]>>(j*8);
            key_source logical{keys.data(),n,width};
            auto run=[&]<bool Candidate>() {u64 sum=0;for(unsigned q=0;q<requests;++q) {
                key_ref p{points.data()+q*width,width};
                auto r=Candidate?in::lower_bound(logical,p):ikea::boundlist::lower_bound(logical,p);
                if(r<n) sum+=dirents[r][3];
            }return sum;};
            measure("directory_logical_child_n"+std::to_string(n)+"_k"+std::to_string(width),{
                {"maintained",[&] {return run.template operator()<false>();}},
                {"intervals",[&] {return run.template operator()<true>();}}
            },expected);
        }
    }
}
void frame() {
    for(unsigned n:{4u,16u,64u,256u}) {
        std::array<std::uint8_t,256> bytes{};
        std::array<u64,256> values{};
        std::array<unsigned,requests> queries{};
        std::mt19937 random(713);
        for(unsigned r=0;r<n;++r) {bytes[r]=(r+1)*256/n-1;values[r]=random();}
        for(auto &q:queries) q=random()%256;
        struct projection {const std::uint8_t *p;unsigned n;
            unsigned size() const {return n;}
            unsigned at(unsigned rank) const {return unsigned(p[rank])+1;}
        } old{bytes.data(),n};
        in::run_end_source candidate{bytes.data(),n};
        u64 expected=0;for(auto q:queries) {unsigned r=0;while(unsigned(bytes[r])+1<=q) ++r;expected+=values[r];}
        measure("frame_run_value_n"+std::to_string(n),{
            {"maintained_projection",[&] {u64 sum=0;for(auto q:queries) sum+=values[ikea::boundlist::upper_bound(old,q)];return sum;}},
            {"intervals_native_bytes",[&] {u64 sum=0;for(auto q:queries) sum+=values[in::upper_bound(candidate,q)];return sum;}},
            {"intervals_bound_source",[&] {
                return in::with_geometry(candidate,[&](const auto &bound) __attribute__((always_inline)) {
                    u64 sum=0;for(auto q:queries) sum+=values[in::upper_bound(bound,q)];return sum;
                });
            }}
        },expected);
    }
}

void stream() {
    for(auto family:{sp::geometry::absolute_ends,sp::geometry::sizes,sp::geometry::relative_ends})
    for(bool full:{false,true}) for(auto placement:{sp::placement::grouped,sp::placement::separated}) {
        if(full&&placement==sp::placement::separated) continue;
        constexpr unsigned n=256;
        const auto format=sp::layout::prepare({.family=family,.lengths=placement,.maximum=255,
                                              .byte_limit=65280,.full=full});
        need(bool(format),"StreamPack preparation failed");
        std::vector<std::byte> payload(65280),directory(format->directory_bytes()),
                               lengths(format->lengths_bytes(n)),control(format->control_bytes());
        auto binding=sp::view::bind(*format,{payload,directory,lengths,control});
        need(bool(binding),"StreamPack binding failed");
        const auto &source=*binding;
        std::array<ikea::owner_write,16> records;
        {ikea::source_write_journal e{records};need(bool(sp::initialize(source,e)),"StreamPack initialize failed");}
        std::array<unsigned,n> used{},ends{};
        std::array<std::byte,255> value{};
        std::mt19937 random(713);
        unsigned position=0;
        for(unsigned r=0;r<n;++r) {
            const unsigned reserve=random()%256;
            used[r]=full?reserve:random()%(reserve+1);
            std::fill(value.begin(),value.end(),std::byte(r%251));
            ikea::source_write_journal e{records};
            need(bool(sp::append(source,std::span(value).first(used[r]),reserve,e)),"StreamPack append failed");
            ends[r]=position+=reserve;
            if(r%11==1) {e.used=0;need(bool(sp::retire(source,r,e)),"StreamPack retire failed");}
        }
        std::array<unsigned,requests> ranks{};
        u64 expected=0;
        for(auto &r:ranks) {r=random()%n;if(r%11!=1) expected+=used[r]+(used[r]?r%251:0);}
        const auto tag="streampack_value_f"+std::to_string(unsigned(family))+"_full"+
                        std::to_string(full)+"_planar"+std::to_string(placement==sp::placement::separated);
        sp::author::dispatch(source,[&](auto access) {
            // The value owner supplies U/liveness; geometry supplies only start.

            measure(tag,{
                {"maintained_typed",[&] {u64 sum=0;for(auto r:ranks) {
                    auto v=access.get(r);if(v) sum+=v->size()+(v->empty()?0:std::to_integer<unsigned>((*v)[0]));
                }return sum;}},
                {"intervals_fused_owner",[&] {
                    return in::with_geometry(access,[&](const auto &bound) __attribute__((always_inline)) {
                        u64 sum=0;for(auto r:ranks) {
                            auto v=bound.get(r);
                            if(v) sum+=v->size()+(v->empty()?0:std::to_integer<unsigned>((*v)[0]));
                        }return sum;
                    });
                }},
                {"intervals_owner_companion",[&] {u64 sum=0;for(auto r:ranks) {
                    if(r<access.size() && access.live(r)) {
                        const auto begin=in::start_unchecked(access,r);
                        const auto size=access.length(r);
                        sum+=size+(size?std::to_integer<unsigned>(payload[begin]):0);
                    }
                }return sum;}}
            },expected);
        });
    }
}
int main() {
    try {directory();frame();stream();}
    catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
