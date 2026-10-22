#include <ikea/intervals.h>
#include <ikea/streampack/detail/compact/read.h>
#include <ikea/streampack/detail/compact/write.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <vector>

namespace in=ikea::intervals;
namespace bl=ikea::streampack::detail::compact;
using u64=std::uint64_t;
constexpr unsigned count=256,requests=4096,repeat=16;
volatile u64 edit_sink;
void need(bool ok,const char *why) {if(!ok) throw std::runtime_error(why);}
struct result {u64 checksum,bytes;};
struct arm {const char *name;std::function<result()> run;};
template<unsigned G> struct sparse_image {
    std::array<std::uint8_t,count> fields{};
    std::array<std::uint16_t,count/G> checkpoints{};
    unsigned n=0;std::uint16_t extent=0;
    in::size_editor<G,std::uint8_t,std::uint16_t> editor{
        fields,checkpoints,n,extent,0,65280,255};
    sparse_image() {
        std::array<ikea::owner_write,8> records;
        for(unsigned r=0;r<count;++r) {ikea::source_write_journal e{records};need(bool(editor.append(100+r%100,e)),"initial sparse append refused");}
    }
    result run() {
        result answer{};std::array<ikea::owner_write,8> records;
        for(unsigned q=0;q<requests;++q) {
            const unsigned rank=(q*713)%count,old=fields[rank];
            for(unsigned value:{old+1,old}) {
                ikea::source_write_journal e{records};
                need(bool(editor.resize(rank,value,e)),"sparse edit refused");
                asm volatile("" ::: "memory");answer.checksum+=extent;
                for(auto effect:e.entries()) answer.bytes+=effect.bytes.size;
            }
        }
        return answer;
    }
};
struct compact_image {
    bl::layout format;
    std::vector<std::byte> directory,lengths,control;
    bl::view source;
    compact_image(bl::geometry family)
      :format(*bl::layout::prepare({.family=family,.maximum=255,.extent_limit=65280,.immortal=true})),
       directory(format.directory_bytes()),lengths(format.lengths_bytes(count)),control(format.control_bytes()),
       source(*bl::view::bind(format,{directory,lengths,control})) {
        std::array<ikea::owner_write,16> records;
        {ikea::source_write_journal e{records};need(bool(bl::initialize(source,e)),"compact init refused");}
        for(unsigned r=0;r<count;++r) {ikea::source_write_journal e{records};need(bool(bl::append(source,100+r%100,e)),"initial compact append refused");}
    }
    result run() {
        result answer{};std::array<ikea::owner_write,16> records;
        for(unsigned q=0;q<requests;++q) {
            const unsigned rank=(q*713)%count,old=100+rank%100;
            for(unsigned value:{old+1,old}) {
                ikea::source_write_journal e{records};need(bool(bl::resize(source,rank,value,e)),"compact edit refused");
                asm volatile("" ::: "memory");answer.checksum+=source.extent();
                for(auto effect:e.entries()) answer.bytes+=effect.bytes.size;
            }
        }
        return answer;
    }
};
int main() {
    try {
        sparse_image<16> g16;sparse_image<64> g64;sparse_image<256> g256;
        compact_image sizes(bl::geometry::sizes),ends(bl::geometry::relative_ends),absolute(bl::geometry::absolute_ends);
        std::vector<arm> arms{
            {"maintained_sizes16",[&]{return sizes.run();}},
            {"maintained_relative16",[&]{return ends.run();}},
            {"maintained_absolute",[&]{return absolute.run();}},
            {"intervals_sizes16",[&]{return g16.run();}},
            {"intervals_sizes64",[&]{return g64.run();}},
            {"intervals_sizes256",[&]{return g256.run();}}};
        u64 extent=0;for(unsigned r=0;r<count;++r) extent+=100+r%100;
        const u64 expected=requests*(extent*2+1);
        for(auto &a:arms) need(a.run().checksum==expected,"resize pair differs from oracle");
        for(unsigned trial=0;trial<11;++trial) for(unsigned pos=0;pos<arms.size();++pos) {
            auto &a=arms[(pos+trial)%arms.size()];u64 bytes=0;
            auto begin=std::chrono::steady_clock::now();
            for(unsigned r=0;r<repeat;++r) {auto v=a.run();edit_sink=v.checksum;bytes+=v.bytes;}
            const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
            std::printf("{\"arm\":\"%s\",\"operation\":\"resize_pair\",\"trial\":%u,\"elapsed_ns\":%lld,"
                        "\"operations\":%u,\"effect_bytes\":%llu}\n",a.name,trial,
                        static_cast<long long>(elapsed),requests*repeat,static_cast<unsigned long long>(bytes));
        }
        need(bool(g16.editor.validate())&&bool(g64.editor.validate())&&bool(g256.editor.validate())&&
             bool(sizes.source.validate())&&bool(ends.source.validate())&&bool(absolute.source.validate()),"edited output invalid");
    }catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
