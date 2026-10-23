#include <roaring.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <vector>
using word = std::uint32_t;
using set = std::vector<word>;
struct release_bitmap { void operator()(roaring_bitmap_t* p) const { roaring_bitmap_free(p); } };
using bitmap = std::unique_ptr<roaring_bitmap_t, release_bitmap>;
unsigned images = 0, operations = 0, truncated = 0;
void need(bool ok, const char* why) { if (!ok) { std::fprintf(stderr,"CRoaring control: %s\n",why); std::exit(1); } }
set canonical(set v) { std::sort(v.begin(),v.end()); v.erase(std::unique(v.begin(),v.end()),v.end()); return v; }
bitmap build(const set& values) {
    bitmap p(roaring_bitmap_create()); need(bool(p),"allocate");
    // Deliberately unsorted and duplicated input, checked against a set oracle.
    for(auto i=values.rbegin();i!=values.rend();++i)roaring_bitmap_add(p.get(),*i);
    if(!values.empty())roaring_bitmap_add(p.get(),values.front());
    return p;
}
void verify(const roaring_bitmap_t* p,const set& expected) {
    need(roaring_bitmap_get_cardinality(p)==expected.size(),"independent cardinality");
    set extracted(expected.size()+1,0xdecafbad); roaring_bitmap_to_uint32_array(p,extracted.data());
    need(extracted.back()==0xdecafbad,"enumeration canary");extracted.resize(expected.size());
    need(extracted==expected,"sorted-vector enumeration");
    roaring_uint32_iterator_t it; roaring_iterator_init(p,&it);
    std::size_t i=0;
    while(it.has_value) { need(i<expected.size() && it.current_value==expected[i++],"iterator"); roaring_uint32_iterator_advance(&it); }
    need(i==expected.size(),"iterator cardinality");
    for(auto value:expected) {
        need(roaring_bitmap_contains(p,value),"present membership");
        if(value) need(roaring_bitmap_contains(p,value-1)==std::binary_search(expected.begin(),expected.end(),value-1),"left neighbour");
        if(value!=UINT32_MAX)need(roaring_bitmap_contains(p,value+1)==std::binary_search(expected.begin(),expected.end(),value+1),"right neighbour");
    }
    for(word q:{0u,63u,64u,255u,256u,4095u,4096u,65535u,65536u,65537u,UINT32_MAX}) {
        need(roaring_bitmap_contains(p,q)==std::binary_search(expected.begin(),expected.end(),q),"boundary membership");
        need(roaring_bitmap_rank(p,q)==std::uint64_t(std::upper_bound(expected.begin(),expected.end(),q)-expected.begin()),"inclusive rank");
    }
    if(!expected.empty())for(auto rank:{std::size_t{0},expected.size()/2,expected.size()-1}) {
        word value=0;need(roaring_bitmap_select(p,word(rank),&value)&&value==expected[rank],"zero-based select");
    }
}
std::size_t wire(const roaring_bitmap_t* p,const set& expected) {
    const auto bytes=roaring_bitmap_portable_size_in_bytes(p);
    std::vector<char> data(bytes+32,char(0x5a));
    need(roaring_bitmap_portable_serialize(p,data.data())==bytes,"portable serialize extent");
    for(std::size_t i=bytes;i<data.size();++i)need(data[i]==char(0x5a),"serialize canary");
    bitmap restored(roaring_bitmap_portable_deserialize_safe(data.data(),bytes));need(bool(restored),"portable deserialize");verify(restored.get(),expected);
    need(roaring_bitmap_portable_deserialize_size(data.data(),bytes)==bytes,"portable complete size");
    // Every truncated prefix must fail safe decoding; selected dense images can
    // be large, so probe header and final boundaries rather than quadratic work.
    for(auto n:{std::size_t{0},std::min<std::size_t>(3,bytes-1),bytes/2,bytes-1}) {
        bitmap bad(roaring_bitmap_portable_deserialize_safe(data.data(),n));need(!bad,"truncated portable image rejected");++truncated;
    }
    return bytes;
}
void image(const std::string& name,const set& values) {
    const auto expected=canonical(values);auto p=build(values);verify(p.get(),expected);
    roaring_statistics_t before{},after{};roaring_bitmap_statistics(p.get(),&before);
    const auto normal=wire(p.get(),expected);
    const bool changed=roaring_bitmap_run_optimize(p.get());
    verify(p.get(),expected);const auto optimized=wire(p.get(),expected);roaring_bitmap_statistics(p.get(),&after);
    std::printf("{\"case\":\"%s\",\"cardinality\":%zu,\"normal_portable_bytes\":%zu,\"run_optimized_portable_bytes\":%zu,\"run_optimize_result\":%s,\"normal_containers\":%u,\"normal_runs\":%u,\"optimized_runs\":%u}\n",name.c_str(),expected.size(),normal,optimized,changed?"true":"false",before.n_containers,before.n_run_containers,after.n_run_containers);
    ++images;
}
void binary(const set& a,const set& b) {
    const auto av=canonical(a),bv=canonical(b);
    for(unsigned optimized=0;optimized<4;++optimized) {
    auto x=build(a),y=build(b);
    if(optimized&1)roaring_bitmap_run_optimize(x.get());
    if(optimized&2)roaring_bitmap_run_optimize(y.get());
    for(unsigned op=0;op<3;++op) {
        set expected;
        if(op==0)std::set_intersection(av.begin(),av.end(),bv.begin(),bv.end(),std::back_inserter(expected));
        if(op==1)std::set_union(av.begin(),av.end(),bv.begin(),bv.end(),std::back_inserter(expected));
        if(op==2)std::set_difference(av.begin(),av.end(),bv.begin(),bv.end(),std::back_inserter(expected));
        bitmap out(op==0?roaring_bitmap_and(x.get(),y.get()):op==1?roaring_bitmap_or(x.get(),y.get()):roaring_bitmap_andnot(x.get(),y.get()));
        need(bool(out),"allocate Boolean result");verify(out.get(),expected);
        const auto count=op==0?roaring_bitmap_and_cardinality(x.get(),y.get()):op==1?roaring_bitmap_or_cardinality(x.get(),y.get()):roaring_bitmap_andnot_cardinality(x.get(),y.get());
        need(count==expected.size(),"Boolean cardinality");++operations;
    }
    }
}
set run(word first,std::uint64_t count) {set out;for(std::uint64_t i=0;i<count;++i)out.push_back(word(first+i));return out;}
void full_universe() {
    // An analytic range oracle avoids materializing four billion row ids.
    bitmap p(roaring_bitmap_from_range(0,std::uint64_t{1}<<32,1));need(bool(p),"full32 construction");
    need(roaring_bitmap_get_cardinality(p.get())==(std::uint64_t{1}<<32),"full32 cardinality does not truncate");
    for(word q:{0u,1u,65535u,65536u,UINT32_MAX}) {
        word value=0;
        need(roaring_bitmap_contains(p.get(),q)&&roaring_bitmap_rank(p.get(),q)==std::uint64_t(q)+1,"full32 membership/rank");
        need(roaring_bitmap_select(p.get(),q,&value)&&value==q,"full32 select");
    }
    const auto bytes=roaring_bitmap_portable_size_in_bytes(p.get());
    need(bytes<2*1024*1024,"bounded full32 run image");
    std::vector<char> wire(bytes);need(roaring_bitmap_portable_serialize(p.get(),wire.data())==bytes,"full32 serialize");
    bitmap restored(roaring_bitmap_portable_deserialize_safe(wire.data(),wire.size()));
    need(bool(restored)&&roaring_bitmap_get_cardinality(restored.get())==(std::uint64_t{1}<<32)&&roaring_bitmap_contains(restored.get(),UINT32_MAX),"full32 deserialize");
    bitmap empty(roaring_bitmap_andnot(p.get(),restored.get()));
    need(bool(empty)&&roaring_bitmap_get_cardinality(empty.get())==0,"full32 difference identity");
    std::printf("{\"case\":\"full_u32_universe_analytic\",\"cardinality\":4294967296,\"range_prepared_portable_bytes\":%zu,\"enumerated\":false}\n",bytes);
}
int main(int argc,char**argv) {
    full_universe();
    std::vector<set> inputs={{},{0},{UINT32_MAX},{0,UINT32_MAX,65535,65536,65537,63,64,255,256},run(0,65536),run(65530,32),run(UINT32_MAX-255,256)};
    for(unsigned cardinality:{4095u,4096u,4097u}) {
        set dispersed;for(unsigned i=0;i<cardinality;++i)dispersed.push_back(i*13);inputs.push_back(dispersed);
    }
    set cluster,scatter;
    for(unsigned i=0;i<8192;++i){cluster.push_back(17+i);scatter.push_back(word((std::uint64_t(i)*524287u)&UINT32_MAX));}
    inputs.push_back(cluster);inputs.push_back(scatter);
    for(unsigned i=0;i<inputs.size();++i)image("structural"+std::to_string(i),inputs[i]);
    // Array/bitset/run combinations, both operand orders and empty identities.
    for(const auto& a:inputs)for(const auto& b:inputs)binary(a,b);
    auto ranged=build({});roaring_bitmap_add_range(ranged.get(),65530,65542);verify(ranged.get(),run(65530,12));
    roaring_bitmap_add_range(ranged.get(),std::uint64_t{UINT32_MAX}-2,std::uint64_t{1}<<32);
    set expected=run(65530,12);auto tail=run(UINT32_MAX-2,3);expected.insert(expected.end(),tail.begin(),tail.end());verify(ranged.get(),expected);
    roaring_bitmap_add_range(ranged.get(),4,4);verify(ranged.get(),expected);
    // Optional real windows: eight 8192-byte bit-wire windows per archive,
    // retaining each entire window including empty 256-position cells.
    for(int arg=1;arg<argc;++arg) {
        std::ifstream f(argv[arg],std::ios::binary|std::ios::ate);need(bool(f),"real window open");
        const auto n=f.tellg();need(n>0 && n%8192==0 && n<=8192*8,"bounded real windows");f.seekg(0);
        set previous;
        for(unsigned w=0;w<unsigned(n/8192);++w) {
            std::array<unsigned char,8192> bits{};f.read(reinterpret_cast<char*>(bits.data()),bits.size());need(bool(f),"real window bytes");
            set values;for(unsigned i=0;i<65536;++i)if(bits[i/8]>>(i%8)&1)values.push_back(i);
            image(std::string("real")+std::to_string(arg)+"_"+std::to_string(w),values);
            binary(previous,values);binary(values,previous);previous=std::move(values);
        }
    }
#ifdef CROARING_USENEON
    constexpr bool neon=true;
#else
    constexpr bool neon=false;
#endif
    std::printf("{\"peer\":\"CRoaring\",\"version\":\"%s\",\"images\":%u,\"Boolean_operations\":%u,\"truncation_negatives\":%u,\"neon_compile_path\":%s,\"pass\":true,\"timing_claim\":false}\n",ROARING_VERSION,images,operations,truncated,neon?"true":"false");
}
