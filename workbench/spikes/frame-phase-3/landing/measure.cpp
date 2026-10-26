// Same-input public Frame / frozen research read driver / qualified typed
// native controls. Timers exclude fitting, placement and validation. No LTO.
#include <ikea/frame.h>
#include "../proto/trees.h"
#include "../../frame-phase-2/instruments/cells/cells.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <numeric>
#include <random>
#include <string>

namespace fr=ikea::frame;
namespace fc=frame_cells;
using u64=std::uint64_t;
using u128=fr::u128;
namespace {
volatile u64 sink;
void check(bool ok) { if(!ok) std::abort(); }
template<class T,class E> T take(std::expected<T,E> value) { check(bool(value)); return std::move(*value); }
u64 fold(u128 x) { return u64(x)^u64(x>>64); }
struct unit {
    fr::built_unit image;
    std::vector<std::size_t> extents;
    std::vector<fr::stream_manifest> streams;
    std::vector<fr::borrowed_part> parts;
    std::vector<fr::section_key> keys;
    std::optional<fr::reader> reader;
    unit(const fr::description& recipe,std::span<const u64> values) : image(take(fr::build(recipe,values,{0,64,0}))) {
        for(const auto& s:image.streams) {
            fr::stream_manifest m{s.root_node,s.rows,{}};
            for(std::size_t g=0;g<s.groups.size();++g) {
                const auto id=static_cast<std::uint32_t>(parts.size());
                m.groups.push_back({id,0,s.groups[g].size()});
                extents.push_back(s.groups[g].size());parts.push_back({id,s.groups[g]});keys.push_back({s.root_node,g});
            }
            streams.push_back(std::move(m));
        }
        reader.emplace(take(fr::bind_reader(recipe,{values.size(),extents,streams,parts,keys})));
    }
    std::size_t bytes() const { return std::accumulate(extents.begin(),extents.end(),std::size_t{0}); }
};
fr::description recipe(unsigned form,fr::pack_preset p) {
    fr::description d;
    d.nodes={{fr::node_kind::pack,std::uint16_t(p),0,{}},{fr::node_kind::bound,0,0,{{0,fr::coordinate::identity}}}};
    d.root=1;
    if(form) {
        d.nodes.push_back({fr::node_kind::pack,std::uint16_t(p),0,{}});
        d.nodes.push_back({fr::node_kind::bound,0,0,{{2,fr::coordinate::identity}}});
        d.nodes.push_back({form==1?fr::node_kind::intervals_sizes:fr::node_kind::intervals_relative_ends,16,0,
                          {{1,fr::coordinate::identity},{3,fr::coordinate::checkpoints}}});d.root=4;
    }
    return d;
}
template<ikea::seriespack::preset P> std::unique_ptr<frame3::stage> old_tree(unsigned form) {
    const auto basic=[]()->std::unique_ptr<frame3::stage> {
        return std::make_unique<frame3::bound>(std::make_unique<frame3::pack<P>>(16),0,64,0);
    };
    if(!form) return basic();
    return std::make_unique<frame3::intervals>(basic(),basic(),16,
        form==1?frame3::interval_form::sizes:frame3::interval_form::relative_ends);
}
std::unique_ptr<frame3::stage> old_tree(unsigned form,fr::pack_preset p) {
    switch(p) {
    case fr::pack_preset::compact:return old_tree<ikea::seriespack::preset::compact>(form);
    case fr::pack_preset::bulk_x86:return old_tree<ikea::seriespack::preset::bulk_x86>(form);
    case fr::pack_preset::bulk_arm:return old_tree<ikea::seriespack::preset::bulk_arm>(form);
    }
    std::abort();
}
std::vector<u64> load(const std::string& dir,const std::string& name,std::size_t n) {
    std::vector<u64> values(n);
    if(name=="gen_ts") {
        std::mt19937_64 rng(5);u64 v=u64{1}<<40;
        for(auto& x:values) x=(v+=rng()%1000);
    } else {
        std::ifstream in(dir+"/"+name+".u64",std::ios::binary|std::ios::ate);check(bool(in));
        values.resize(std::min(n,std::size_t(in.tellg())/8));in.seekg(0);
        in.read(reinterpret_cast<char*>(values.data()),std::streamsize(values.size()*8));check(bool(in));
    }
    return values;
}
void run(const std::string& dir,const std::string& name,unsigned form,unsigned rounds) {
    auto values=load(dir,name,fr::unit_rows);
    check(!values.empty());
#if defined(__aarch64__)
    constexpr auto p=fr::pack_preset::bulk_arm;
#else
    constexpr auto p=fr::pack_preset::bulk_x86;
#endif
    auto d=recipe(form,p);
    unit maintained(d,values);
    frame3::column spike(old_tree(form,p),values);
    fc::native native(values);
    const std::vector<u64> all((values.size()+16383)/16384,~u64{0});
    const auto coarse=fc::block_mask::bind(all,values.size());
    fc::validate_native_domains(native,coarse);
    std::vector<u64> sorted=values;std::sort(sorted.begin(),sorted.end());const u64 threshold=sorted[sorted.size()/2];
    std::array<ikea::bitset256,fr::unit_blocks> fine{};
    std::vector<u64> native_words((values.size()+63)/64);
    frame3::block_rows_set old_fine{};
    u128 expected_sum=0,expected_selected=0;u64 expected_count=0;
    for(std::size_t r=0;r<values.size();++r) {
        expected_sum+=values[r];expected_count+=values[r]>=threshold;
        if(r%3==1) {
            fine[r/256].set(unsigned(r%256));old_fine[r/256].set(unsigned(r%256));
            native_words[r/64]|=u64{1}<<(r%64);expected_selected+=values[r];
        }
    }
    const auto rows=ikea::seriespack::selection::bits(0,std::span<const ikea::bitset256>(fine));
    const auto native_rows=fc::row_mask::bind(native_words,values.size());
    fc::validate_native_selection(native,coarse,native_rows);
    const auto old_blocks=ikea::bitset256::first_n(unsigned((values.size()+255)/256));
    std::mt19937 rng(17);std::vector<std::size_t> points(4096);
    for(auto& r:points) r=rng()%values.size();
    const auto requests=fc::point_requests::bind(points,values.size());
    std::array<std::vector<u64>,3> outputs;
    for(auto& out:outputs) out.resize(points.size());
    for(auto& out:outputs) fc::validate_native_gather(native,coarse,requests,out);
    std::vector<std::byte> eviction(256u<<20);
    auto condition=[&] { u64 sum=0;const volatile auto* bytes=reinterpret_cast<const volatile unsigned char*>(eviction.data());for(std::size_t at=0;at<eviction.size();at+=64) sum+=bytes[at];sink=sum; };
    const auto execute=[&](unsigned op,unsigned arm)->u128 {
        if(op==0) return arm==0?take(maintained.reader->sum()):arm==1?spike.sum():fc::native_sum_once(native,coarse).sum;
        if(op==1) return arm==0?take(maintained.reader->count(ikea::comparison::greater_equal,threshold)):
            arm==1?spike.count(ikea::comparison::greater_equal,threshold):fc::native_count_once(native,threshold,coarse);
        if(op==2) return arm==0?take(maintained.reader->sum_selected(rows)):
            arm==1?spike.root().sum_selected(0,old_blocks,old_fine):fc::native_selected_sum_dense_once(native,coarse,native_rows).sum;
        auto& out=outputs[arm];
        if(arm==0) for(unsigned i=0;i<points.size();++i) out[i]=take(maintained.reader->get(points[i]));
        else if(arm==1) for(unsigned i=0;i<points.size();++i) out[i]=spike.get(points[i]);
        else fc::native_gather_once(native,coarse,requests,out);
        return out.back();
    };
    const std::array<const char*,4> ops{"sum","count_ge","selected_sum_period3","points_materialized"};
    const std::array<const char*,3> arms{"ikea","spike","native_typed_for"};
    for(unsigned op=0;op<4;++op) for(unsigned arm=0;arm<3;++arm) {
        const auto answer=execute(op,arm);
        check(answer==(op==0?expected_sum:op==1?u128{expected_count}:op==2?expected_selected:u128{values[points.back()]}));
        if(op==3) for(unsigned i=0;i<points.size();++i) check(outputs[arm][i]==values[points[i]]);
    }
    for(unsigned cold=0;cold<2;++cold) for(unsigned op=0;op<4;++op) for(unsigned round=0;round<rounds;++round) {
        for(unsigned turn=0;turn<3;++turn) {
            const unsigned arm=(turn+round)%3,repeats=cold?1:32;
            if(cold) condition();
            u128 answer=0;
            const auto begin=std::chrono::steady_clock::now();
            for(unsigned r=0;r<repeats;++r) { answer=execute(op,arm);sink=fold(answer); }
            const double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count()/repeats;
            check(answer==(op==0?expected_sum:op==1?u128{expected_count}:op==2?expected_selected:u128{values[points.back()]}));
            if(op==3) for(unsigned i=0;i<points.size();++i) check(outputs[arm][i]==values[points[i]]);
            std::printf("{\"column\":\"%s\",\"form\":%u,\"op\":\"%s\",\"arm\":\"%s\",\"conditioning\":\"%s\",\"rows\":%zu,\"requests\":%zu,\"round\":%u,\"ns\":%.3f,\"ikea_image_bytes\":%zu,\"spike_image_bytes\":%zu,\"native_value_bytes\":%zu}\n",
                name.c_str(),form,ops[op],arms[arm],cold?"eviction_sweep_256MiB":"repeated",values.size(),op==3?points.size():values.size(),round,ns,
                maintained.bytes(),spike.bytes(),values.size()*native.lane());
        }
    }
}
}
int main(int argc,char**argv) {
    check(argc>=2);
    const unsigned rounds=argc>2?unsigned(std::stoul(argv[2])):5;
    for(const auto& name:{"OS","UserID","URLHash","EventTime","gen_ts"})
        run(argv[1],name,std::string(name)=="EventTime"?2:std::string(name)=="gen_ts"?1:0,rounds);
}
