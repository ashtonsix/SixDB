#include "fixture.h"
#include "native_ops.h"
#include <iostream>
#include <random>
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif

namespace c=ikea::integers::composition;
namespace {
template<class Tail,c::ParentKind Parent> void read_check(const std::uint8_t* tile,std::span<const std::uint16_t,64> expected) {
    c::NativeOps<Tail,Parent> ops{tile};
    for(unsigned group=0;group<64;group+=16) {
        std::uint16_t output[16];c::store_values(output,c::read12(ops,c::Payload12<Tail>{},group));
        assert(std::equal(output,output+16,expected.begin()+group));
    }
}
template<class Tail,c::ParentKind Parent> unsigned prepared_checks() {
    unsigned cases=0;
    c::Column<c::Local4> original{101,{}};
    const auto source=original.with_tail(Tail{},Tail::kind==c::TailKind::local4?101:202);
    for(unsigned phase:{0u,32u}) for(unsigned tiles:{0u,1u,2u,3u,17u,257u}) {
        auto storage=std::make_shared<c::Storage>(phase+96*tiles);
        std::vector<std::uint16_t> values(64*tiles);
        std::mt19937 rng(0x120064+tiles);
        for(auto& value:values)value=rng()&4095;
        for(unsigned t=0;t<tiles;++t)c::fixture::encode(Parent,std::span<const std::uint16_t,64>(values.data()+t*64,64),storage->bytes.get()+phase+t*96);
        auto placement=c::place(storage,Parent,source.source_id,phase,tiles);
        for(unsigned cutoff:{0u,1u,255u,2048u,4095u,4096u}) for(auto mode:{c::Execution::authored_inline,c::Execution::cps}) {
            c::Prepared prepared;std::string error;
            assert(c::prepare(source,placement,cutoff,mode,prepared,error));
            assert(prepared()==c::fixture::sum(values,cutoff));++cases;
            assert(prepared.graph.nodes.size()==5);
            assert(prepared.graph.nodes[3].inputs==std::vector<unsigned>{2});
            assert(prepared.graph.nodes[4].inputs==std::vector<unsigned>({2,3}));
        }
        c::Prepared retained;std::string error;
        assert(c::prepare(source,placement,2048,c::Execution::cps,retained,error));
        auto bad=placement;bad.tail.offset+=32;assert(!c::prepare(source,bad,2048,c::Execution::cps,retained,error));
        bad=placement;bad.tail.owner=std::make_shared<c::Storage>(128);assert(!c::prepare(source,bad,2048,c::Execution::cps,retained,error));
        bad=placement;bad.tile_stride=128;assert(!c::prepare(source,bad,2048,c::Execution::cps,retained,error));
        bad=placement;bad.values_per_tile=32;assert(!c::prepare(source,bad,2048,c::Execution::cps,retained,error));
        bad=placement;bad.body.bytes_per_repeat=7;assert(!c::prepare(source,bad,2048,c::Execution::cps,retained,error));
        bad=placement;++bad.source_id;assert(!c::prepare(source,bad,2048,c::Execution::cps,retained,error));
        bad=placement;++bad.tiles;assert(!c::prepare(source,bad,2048,c::Execution::cps,retained,error));
        assert(!c::prepare(source,placement,4097,c::Execution::cps,retained,error));
        auto edited=c::record(source);edited.nodes[4].inputs={0,3};
        assert(!c::prepare_impl(Tail::kind,source.source_id,edited,placement,2048,c::Execution::cps,retained,error));
        edited=c::record(source);edited.nodes[3].identity="u12>=cutoff";
        assert(!c::prepare_impl(Tail::kind,source.source_id,edited,placement,2048,c::Execution::authored_inline,retained,error));
        bad=placement;bad.parent=Parent==c::ParentKind::packets8?c::ParentKind::body64_tail32:c::ParentKind::packets8;
        assert(!c::prepare(source,bad,2048,c::Execution::cps,retained,error));
        placement={};bad={};storage.reset();
        assert(retained()==c::fixture::sum(values,2048));
    }
    return cases;
}
template<class Tail,c::ParentKind Parent> unsigned actual_footprint_checks() {
    unsigned cases=0;
    for(unsigned phase:{0u,32u}) {
        auto storage=std::make_shared<c::Storage>(phase+96);
        std::array<std::uint16_t,64> values{};for(unsigned i=0;i<64;++i)values[i]=(i*67+15)&4095;
        auto* tile=storage->bytes.get()+phase;
        c::fixture::encode(Parent,values,tile);
        for(unsigned group=0;group<64;group+=16) {
#if __has_feature(address_sanitizer)
            __asan_poison_memory_region(storage->bytes.get(),storage->size);
            if constexpr(Parent==c::ParentKind::packets8) __asan_unpoison_memory_region(tile+group/8*12,24);
            else {
                constexpr bool middle=Parent==c::ParentKind::body32_tail32_body32;
                __asan_unpoison_memory_region(tile+group+(middle && group>=32?32:0),16);
                __asan_unpoison_memory_region(tile+(middle?32:64)+group%32,16);
            }
#endif
            c::NativeOps<Tail,Parent> ops{tile};std::uint16_t output[16];
            c::store_values(output,c::read12(ops,c::Payload12<Tail>{},group));
#if __has_feature(address_sanitizer)
            __asan_unpoison_memory_region(storage->bytes.get(),storage->size);
#endif
            assert(std::equal(output,output+16,values.begin()+group));++cases;
        }
        for(unsigned count:{1u,16u}) for(unsigned i=0;i<64;i+=count) {
            const auto bytes=c::fixture::footprint(Parent,i,count);
            assert((phase+bytes.back())/64-(phase+bytes.front())/64<=1);
        }
        auto p=c::place(storage,Parent,99,1,0);c::Prepared bad;std::string error;
        assert(!c::prepare(c::Column<Tail>{99,{}},p,2048,c::Execution::cps,bad,error));
    }
    return cases;
}
}
int main() {
    unsigned decode_groups=0;
    for(unsigned phase:{0u,32u}) {
        c::Storage storage(phase+96);
        for(unsigned start=0;start<4096;start+=64) {
            std::array<std::uint16_t,64> values{};
            for(unsigned i=0;i<64;++i)values[i]=start+i;
            c::fixture::encode(c::ParentKind::packets8,values,storage.bytes.get()+phase);
            read_check<c::Local4,c::ParentKind::packets8>(storage.bytes.get()+phase,values);decode_groups+=4;
            c::fixture::encode(c::ParentKind::body64_tail32,values,storage.bytes.get()+phase);
            read_check<c::Scan4,c::ParentKind::body64_tail32>(storage.bytes.get()+phase,values);decode_groups+=4;
            c::fixture::encode(c::ParentKind::body32_tail32_body32,values,storage.bytes.get()+phase);
            read_check<c::Scan4,c::ParentKind::body32_tail32_body32>(storage.bytes.get()+phase,values);decode_groups+=4;
        }
    }
    for(unsigned mask=0;mask<65536;++mask) {
        std::uint16_t values[16];
        for(unsigned i=0;i<16;++i)values[i]=(mask>>i)&1?i+1:4095;
#if defined(__aarch64__)
        const c::Values16 v{vld1q_u16(values),vld1q_u16(values+8)};
#else
        const auto v=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(values));
#endif
        assert(c::filter_native(v,2048)==mask);
        assert(c::sum_native(v,mask)==c::fixture::sum(values,2048));
    }
    const auto executions=prepared_checks<c::Local4,c::ParentKind::packets8>()+
        prepared_checks<c::Scan4,c::ParentKind::body64_tail32>()+
        prepared_checks<c::Scan4,c::ParentKind::body32_tail32_body32>();
    const auto footprints=actual_footprint_checks<c::Local4,c::ParentKind::packets8>()+
        actual_footprint_checks<c::Scan4,c::ParentKind::body64_tail32>()+
        actual_footprint_checks<c::Scan4,c::ParentKind::body32_tail32_body32>();
    const c::Column<c::Local4> original{101,{}};
    c::print_graph(std::cout,c::record(original));
    c::print_graph(std::cout,c::record(original.with_tail(c::Scan4{},202)));
    std::cout<<"decode_groups,"<<decode_groups<<"\nmask_patterns,65536\nprepared_cases,"<<executions
             <<"\nread_footprints,"<<footprints<<"\nstatus,pass\n";
}
