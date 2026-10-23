#include <ikea/intervals.h>
#include <array>
#include <cstring>
#include <random>
#include <stdexcept>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>

namespace in = ikea::intervals;
using u64 = std::uint64_t;
void need(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }

struct Key { const std::uint8_t *data; unsigned width;
    friend bool operator<(Key a, Key b) { return std::memcmp(a.data,b.data,a.width)<0; }
};
struct Keys {
    const std::uint8_t *data; unsigned count, width;
    unsigned size() const { return count; }
    Key at(unsigned rank) const { return {data + std::size_t(rank) * width, width}; }
};
void directory() {
    for (unsigned width : {1u,4u,16u}) for (unsigned n : {0u,1u,17u,128u,512u,1024u}) {
        std::vector<std::uint8_t> keys(n * width);
        for (unsigned r=0;r<n;++r) {
            const u64 value=width==1?r/8:r*3;
            for (unsigned i=0;i<std::min(8u,width);++i)
                keys[(r+1)*width-1-i]=value>>(i*8);
        }
        Keys source{keys.data(),n,width};
        for (unsigned q=0;q<512;q+=7) {
            std::array<std::uint8_t,16> query{};
            for (unsigned i=0;i<std::min(8u,width);++i) query[width-1-i]=u64(q)>>(i*8);
            Key key{query.data(),width};
            unsigned lo=0,hi=0;
            while(lo<n&&source.at(lo)<key) ++lo;
            while(hi<n&&!(key<source.at(hi))) ++hi;
            need(in::lower_bound(source,key)==lo&&in::upper_bound(source,key)==hi,
                 "Directory lexical endpoint domain changed");
        }
    }
}

void frame_runs() {
    for(unsigned n=0;n<=256;++n) {
        std::array<std::uint8_t,256> bytes{};
        std::array<unsigned,256> ends{};
        for(unsigned r=0;r<n;++r) {ends[r]=(r+1)*256/n; bytes[r]=ends[r]-1;}
        in::run_end_source source{bytes.data(),n};
        for(long long q=-1;q<=257;++q) {
            unsigned lo=0,hi=0;
            while(lo<n&&ends[lo]<q) ++lo;
            while(hi<n&&ends[hi]<=q) ++hi;
            need(in::lower_bound(source,q)==lo&&in::upper_bound(source,q)==hi,
                 "Frame translated-byte query mismatch");
            need(in::reverse(source,q)==(q<0?n:hi),"Frame owner mismatch");
        }
        need(in::upper_bound(source,~u64{})==n,"Frame wide query narrowed");
        u64 weighted=0,expected=0;
        need(bool(in::range(source,0,n,[&](unsigned rank,auto bounds) {
            weighted+=u64(rank+1)*(bounds.limit-bounds.first);
        })),"Frame run range refused");
        for(unsigned r=0;r<n;++r) expected+=u64(r+1)*(ends[r]-(r?ends[r-1]:0));
        need(weighted==expected,"Frame run-weighted aggregate mismatch");
    }
}

template<unsigned G,class W> void edits() {
    std::array<W,256> fields{};
    std::array<std::uint32_t,256/G> checkpoints{};
    std::array<unsigned,256> model{};
    std::array<std::byte,32> tombstones{};
    const auto original_filter=tombstones;
    unsigned n=0; std::uint32_t limit=700;
    in::size_editor<G,W> editor{fields,checkpoints,n,limit,700,20000000,
                              std::numeric_limits<W>::max()};
    std::array<ikea::journal::entry,8> entries;
    auto verify=[&] {
        need(bool(editor.validate()),"edited geometry failed validation");
        unsigned position=700;
        const auto source=editor.source();
        for(unsigned r=0;r<n;++r) {
            const auto bounds=in::locate(source,r);
            need(bounds&&bounds->first==position&&bounds->limit==position+model[r],
                 "editor differs from independent logical sizes");
            position+=model[r];
        }
        need(position==limit&&tombstones==original_filter,"editor changed companion filter");
    };
    for(unsigned r=0;r<256;++r) {
        ikea::journal effects{entries};
        const auto capacity = editor.effect_capacity_append();
        model[r]=r%5?std::min(17u,editor.maximum):0;
        need(bool(editor.append(model[r],effects)),"valid append refused");
        need(effects.used <= capacity,"append exceeded reserved effects");
        for(auto record:effects.entries()) need(record.source==&editor,"effect lost source identity");
        verify();
    }
    const auto retained_fields=fields; const auto retained_checkpoints=checkpoints;
    const auto retained_limit=limit;
    std::mt19937 random(713);
    for(unsigned attempt=0;attempt<500;++attempt) {
        const unsigned rank=random()%n,value=random()%(editor.maximum+1);
        const auto before=fields; const auto before_cp=checkpoints; const auto before_limit=limit;
        ikea::journal effects{entries};
        const auto capacity = editor.effect_capacity_resize(rank);
        need(bool(capacity),"valid resize capacity refused");
        need(bool(editor.resize(rank,value,effects)),"valid resize refused");
        need(effects.used <= *capacity,"resize exceeded reserved effects");
        model[rank]=value;
        auto covered=[&](unsigned plane,std::size_t offset) {
            for(auto e:effects.entries()) if(e.source==&editor&&e.plane==plane&&
                e.offset<=offset&&offset<e.offset+e.size) return true;
            return false;
        };
        auto changes=[&](const auto &a,const auto &b,unsigned plane) {
            const auto *x=reinterpret_cast<const std::byte*>(&a),*y=reinterpret_cast<const std::byte*>(&b);
            for(std::size_t i=0;i<sizeof(a);++i) if(x[i]!=y[i]) need(covered(plane,i),"unreported store");
        };
        changes(before,fields,0);changes(before_cp,checkpoints,1);changes(before_limit,limit,3);
        verify();
    }
    // Retained old reader uses its own image and interpretation, not mutable basis.
    in::sparse_sizes<G,W,std::uint32_t> retained{retained_fields.data(),retained_checkpoints.data(),
                                               256,700,retained_limit,editor.maximum};
    need(retained.at(255)==retained_limit,"old reader changed after edits");
    const auto saved_fields=fields;const auto saved_cp=checkpoints;const auto saved_limit=limit;
    ikea::journal none{{}};
    need(!editor.effect_capacity_resize(256),"invalid resize capacity admitted");
    need(!editor.append(0,none)&&!editor.resize(256,1,none)&&
         !editor.resize(0,editor.maximum+1,none)&&!editor.resize(0,fields[0]?0:1,none),
         "checked refusal not enforced");
    need(fields==saved_fields&&checkpoints==saved_cp&&limit==saved_limit&&none.used==0,
         "refused edit changed bytes/effects");
}

void strides_and_filters() {
    // StreamPack-style G16 geometry/used-length colocation. Sibling bytes are
    // poison: a geometry walk must not consume them as widths.
    std::array<std::uint16_t,512> fields; fields.fill(60000);
    std::array<std::uint32_t,16> checkpoints; checkpoints.fill(0xffffffffu);
    std::array<unsigned,67> ends{};
    unsigned total=0;
    for(unsigned r=0;r<67;++r) {
        if(r%16==0) checkpoints[r/16]=total;
        fields[r/16*32+r%16]=r%7;ends[r]=total+=r%7;
    }
    for(unsigned r=67;r<80;++r) fields[r/16*32+r%16]=0;
    in::sparse_sizes<16,std::uint16_t,std::uint32_t,16> source{
        fields.data(),checkpoints.data(),67,0,total,6,32};
    std::array<std::byte,9> tombstones{};
    for(unsigned r=0;r<67;++r) if(r%3==1) tombstones[r/8]|=std::byte(1u<<(r%8));
    auto live = in::selection::complement(0,tombstones);
    for(unsigned r=0;r<67;++r) {
        need(source.at(r)==ends[r],"strided geometry read a sibling");
        need(bool(in::locate(source,r,live))==(r%3!=1),"operation filter ignored");
        need(in::start(source,r).value()==(r?ends[r-1]:0),"start-only projection mismatch");
    }
    for(unsigned q=0;q<total;++q) {
        unsigned rank=0;while(ends[rank]<=q) ++rank;
        need(in::reverse(source,q)==rank,"strided inverse mismatch");
        need(in::reverse(source,q,live)==(rank%3==1?67:rank),"filter redirected point owner");
    }
    auto sparse_stride=[&]<class W>() {
        std::array<W,512> values;values.fill(std::numeric_limits<W>::max());
        for(unsigned r=0;r<80;++r) values[r/16*32+r%16]=r<67?r%7:0;
        const std::array<std::uint32_t,4> cp{0,ends[63],0xffffffffu,0xffffffffu};
        const auto bound=in::sparse_sizes<64,W,std::uint32_t,4>{values.data(),cp.data(),67,0,total,6,32}.for_independent_reads();
        for(unsigned r=0;r<67;++r) need(bound.at(r)==ends[r],"batch prefix consumed strided companion");
        for(unsigned q=0;q<total;++q) {
            unsigned upper=0,lower=0;while(ends[upper]<=q) ++upper;
            while(lower<67&&ends[lower]<q) ++lower;
            need(in::reverse(bound,q)==upper && in::prepare_upper_bound(bound,q).finish()==upper &&
                 in::prepare_lower_bound(bound,q).finish()==lower,"strided batch owner/prepared mismatch");
        }
    };
    sparse_stride.template operator()<std::uint8_t>();
    sparse_stride.template operator()<std::uint16_t>();
    // A fully excluded range does not read geometry or invoke its callback.
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto *mapping=static_cast<std::byte*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,
                                              MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    need(mapping!=MAP_FAILED,"mmap failed");
    need(mprotect(mapping+page,page,PROT_NONE)==0,"mprotect failed");
    struct guarded {
        const unsigned *p;
        unsigned size() const { return 67; }
        unsigned origin() const { return *p; }
        unsigned at(unsigned) const { return *p; }
    } inaccessible{reinterpret_cast<unsigned*>(mapping+page)};
    need(bool(in::range(inaccessible,0,67,[](auto,auto){throw std::runtime_error("empty filter emitted");},
                        in::selection{})),"empty filtered range refused");
    need(!in::range(inaccessible,67,1,[](auto,auto){},in::selection{}),"invalid empty-filter range accepted");
    // Exact native tail, with no readable next window at a macrogroup boundary.
    auto *widths=reinterpret_cast<std::uint8_t*>(mapping+page-32);
    std::memset(widths,1,32);const std::uint32_t origin=0;
    in::sparse_sizes<32,std::uint8_t> exact{widths,&origin,32,0,32,1};
    need(exact.at(31)==32,"exact native tail read mismatch");
    for(unsigned n:{16u,32u,48u}) {
        auto *tail=reinterpret_cast<std::uint8_t*>(mapping+page-n);
        std::memset(tail,1,n);
        in::sparse_sizes<64,std::uint8_t> sparse_exact{tail,&origin,n,0,n,1};
        need(sparse_exact.at(n-1)==n && sparse_exact.for_independent_reads().at(n-1)==n &&
             in::reverse(sparse_exact.for_independent_reads(),n-1)==n-1,
             "sparse prefix crossed its exact native-tail grant");
    }
    // A full macrogroup may end at the guard page; it cannot load a fifth group.
    auto *full_widths=reinterpret_cast<std::uint8_t*>(mapping+page-64);
    std::memset(full_widths,1,64);
    in::sparse_sizes<64,std::uint8_t> full_exact{full_widths,&origin,63,0,63,1};
    full_widths[63]=0;
    need(full_exact.for_independent_reads().at(62)==63 && in::reverse(full_exact.for_independent_reads(),62u)==62,
         "full sparse macrogroup tail mismatch");
    munmap(mapping,page*2);
}
int main() {
    try {
        directory();frame_runs();strides_and_filters();
        edits<16,std::uint8_t>();edits<64,std::uint8_t>();edits<256,std::uint8_t>();
        edits<16,std::uint16_t>();edits<64,std::uint16_t>();edits<256,std::uint16_t>();
        std::puts("Directory ordered domains, Frame runs, StreamPack striding/start/filter, protected tails and 4536 checked edit operations passed.");
    } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
