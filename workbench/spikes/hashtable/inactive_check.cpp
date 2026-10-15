#include "record_table.h"
#include <map>
#include <sys/mman.h>
#include <unistd.h>

namespace {
using namespace ht;
namespace ls=layout_solver;
struct guard {
    std::size_t page=std::size_t(sysconf(_SC_PAGESIZE));
    byte* data=static_cast<byte*>(mmap(nullptr,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    guard(){require(data!=MAP_FAILED&&mprotect(data+page,page,PROT_READ|PROT_WRITE)==0,"inactive guard");}
    ~guard(){require(munmap(data,page*3)==0,"inactive unmap");}
};
struct observer {
    struct range{unsigned offset,size;};
    byte* base;const byte* old;
    std::array<range,5> wanted{};
    unsigned size=0,seen=0,room=5;
    unsigned remaining()const{return room-seen;}
    template<class Source> void before(const Source& source,ikea::byte_write write) {
        require(source.base==base&&write.plane==0&&seen<size,"inactive event identity");
        auto r=wanted[seen++];require(write.offset==r.offset&&write.size==r.size,"inactive exact damage");
        require(std::memcmp(base+r.offset,old+r.offset,r.size)==0,"inactive damage before store");
    }
    void add(unsigned at,unsigned bytes){if(bytes)wanted[size++]={at,bytes};}
};
template<unsigned N,container_kind Kind,unsigned K=32,unsigned A=3,bool Rows=false,
         bool Direct=false,record_access Access=record_access::exact> void wire() {
    constexpr bool mm=Kind==container_kind::multimap;
    constexpr unsigned F=std::min(8u,K-A),C=K-A-F,S=(C+7)/8,V=Kind==container_kind::set?0:mm?8:2;
    constexpr unsigned stride=S+V+mm,bytes=5+N*(1+stride);
    // Independent byte coordinates for exact row/plane fixtures; the unit
    // touches a guard page so broader access must prove its own fallback.
    constexpr unsigned sf_at=5+N,sf_stride=Rows?stride:S;
    constexpr unsigned value_at=Rows?sf_at+S:sf_at+N*S,value_stride=Rows?stride:V+mm;
    static constexpr auto plan=[=] {
        auto p=ls::make({.fingerprint_bits=F,.suffix_bits=C,.value_bytes=V,.kind=Kind,
            .where=ls::tier::primary,.target=record_platform,.count_bytes=1,.descendant_bytes=4,
            .head_bytes=0,.chain_reference_bytes=4,.allocation_alignment=64,.tag_bits=2},
            {.slots=N,.unit_bytes=bytes,.fp=ls::fingerprint::exact_bytes,.suffix_encoding=ls::suffix::bytes,
             .fields=Rows?ls::body::rows:ls::body::planes,.tag=mm?ls::tags::row_byte:ls::tags::none,
             .selector_lanes=16,.value_alignment=1});
        if(!p)throw "inactive fixture";return *p;
    }();
    using view=recipe_view<K,Kind,V,A,plan,Direct,Access,true>;
    view::prepare(N);guard memory;
    for(bool right:{false,true})for(unsigned query=0;query<256;++query) {
        auto* p=memory.data+memory.page+(right?memory.page-bytes:0);
        std::memset(memory.data+memory.page,0x69,memory.page);
        std::array<byte,bytes> expected;expected.fill(0x69);
        view v{p};v.initialize_empty();std::fill_n(expected.data()+5,N,255);
        std::array<byte,N> fps{};std::array<word,N> suffixes{};
        std::array<typename view::record,N> records{};
        auto search=[&](byte fp,word sf,unsigned count){return v.find(fp,sf,fp==255?count:N);};
        // A 255 query can match stale suffix bytes, but no inactive rank.
        require(search(255,word(0x69696969696969)&mask(S*8),0)==-1,"empty 255 fallback");
        require(search(byte(query&mask(F)),0,0)==-1,"empty ordinary query");
        ikea::no_coverage none;
        auto key_bytes=[&](unsigned rank,byte fp,word sf){expected[5+rank]=fp;
            for(unsigned b=0;b<S;++b)expected[sf_at+rank*sf_stride+b]=byte(sf>>(8*b));};
        auto value_bytes=[&](unsigned rank,const auto& record){
            for(unsigned b=0;b<V;++b)expected[value_at+rank*value_stride+b]=record.payload[b];
            if constexpr(mm)expected[value_at+rank*value_stride+V]=record.tag;};
        for(unsigned rank=0;rank<N;++rank) {
            fps[rank]=byte((query+19*rank)&mask(F));suffixes[rank]=(rank+1)&mask(C);
            for(unsigned b=0;b<V;++b)records[rank].payload[b]=byte(rank*37+b*71+query);
            records[rank].tag=byte(rank%4);
            v.key(rank,fps[rank],suffixes[rank],none);v.write(rank,records[rank],none);
            key_bytes(rank,fps[rank],suffixes[rank]);value_bytes(rank,records[rank]);
            require(search(fps[rank],suffixes[rank],rank+1)==int(rank),"append/query every fingerprint");
        }
        require(std::memcmp(p,expected.data(),bytes)==0,"inactive initial independent wire");
        for(unsigned count=N;count;--count) {
            const unsigned rank=count/3,last=count-1;auto previous=expected;
            observer events{p,previous.data()};
            if(rank!=last){events.add(5+rank,1);events.add(sf_at+rank*sf_stride,S);
                events.add(value_at+rank*value_stride,V);if constexpr(mm)events.add(value_at+rank*value_stride+V,1);}
            events.add(5+last,1);events.room=events.size-1;
            auto refused=v.erase(rank,count,events);
            require(!refused&&refused.error()==mutation_error::effects&&events.seen==0,"inactive erase refusal including last");
            require(std::memcmp(p,expected.data(),bytes)==0,"inactive refusal preserves bytes");
            events.room=events.size;
            require(bool(v.erase(rank,count,events))&&events.seen==events.size,"inactive erase events");
            if(rank!=last){key_bytes(rank,fps[last],suffixes[last]);value_bytes(rank,records[last]);
                fps[rank]=fps[last];suffixes[rank]=suffixes[last];records[rank]=records[last];}
            expected[5+last]=255;
            require(std::memcmp(p,expected.data(),bytes)==0,"swap-last changes exact fields and vacated fingerprint");
            for(unsigned i=0;i<count-1;++i)require(search(fps[i],suffixes[i],count-1)==int(i),"survivor lookup");
            require(search(255,suffixes[last],count-1)==([&]{
                for(unsigned i=0;i<count-1;++i)if(fps[i]==255&&suffixes[i]==suffixes[last])return int(i);
                return -1;}()),"255 inactive tail excluded");
        }
        // Reinitializing used storage represents private clear/reuse, not an
        // implicit observer bypass for a published owner.
        v.key(0,byte(query&mask(F)),0,none);v.initialize_empty();
        require(search(byte(query&mask(F)),0,0)==-1,"clear/reuse invalidates live fingerprints");
        for(auto* at=memory.data+memory.page;at<p;++at)require(*at==0x69,"guard prefix unchanged");
        for(auto* at=p+bytes;at<memory.data+2*memory.page;++at)require(*at==0x69,"guard suffix unchanged");
    }
}
template<unsigned K,container_kind Kind> void owner() {
    constexpr unsigned V=Kind==container_kind::set?0:2;
    using table=record_table<K,Kind,V,2,0,64,64,HT_RECORD_CONSUME?record_format::rows:record_format::exact>;
    using row=typename table::record;
    pool_owner pool;table body(*pool.pool,19,71);std::map<word,row> expected;
    auto equal=[](const row& a,const row& b){return a.payload==b.payload&&(Kind!=container_kind::multimap||a.tag==b.tag);};
    auto key=[](unsigned i){return word(i)*0xd6e8feb86659fd93ULL&mask(K);};
    auto verify=[&] {
        body.validate();unsigned seen=0;
        body.each_record([&](word k,const row& r){require(expected.contains(k)&&equal(r,expected.at(k)),"owner traversal oracle");++seen;});
        require(seen==expected.size(),"owner cardinality");
        for(unsigned i=0;i<256;++i){const auto k=key(i);unsigned calls=0;auto found=body.get_record(k,[&](const row& r){
            require(expected.contains(k)&&equal(r,expected.at(k)),"owner full-byte lookup");++calls;});
            require(found==expected.contains(k)&&calls==unsigned(found),"owner hit/miss oracle");}
    };
    verify();
    for(unsigned pass=0;pass<3;++pass) {
        for(unsigned i=0;i<240;++i){row r;for(unsigned b=0;b<r.payload.size();++b)r.payload[b]=byte(i+pass*31+b*19);r.tag=byte(i&1);
            require(bool(body.mutate(key(i),[&](const auto&)->std::optional<row>{return r;})),"owner mutation");expected[key(i)]=r;}
        verify();const auto pop=body.population();require(pop[1]||pop[2],"owner downstream exercised");
        for(unsigned i=0;i<240;i+=2){require(bool(body.erase(key(i))),"owner erase");expected.erase(key(i));}
        verify();
    }
    for(unsigned i=0;i<240;++i){body.erase(key(i));expected.erase(key(i));}verify();
}
template<unsigned N> void window(){
    wire<N,container_kind::map>();wire<N,container_kind::map,32,3,false,true,record_access::both>();
    wire<N,container_kind::multimap,32,3,true,false,record_access::both>();
    wire<N,container_kind::set,16,9>();
}
}
int main(){
    static_assert(HT_BUCKET_SENTINEL);
    window<7>();window<16>();window<17>();window<31>();window<32>();
    lanes<8>([](auto i){constexpr unsigned k=8*(i+1);owner<k,container_kind::set>();owner<k,container_kind::map>();owner<k,container_kind::multimap>();});
    std::puts("inactive fingerprints: guarded tails, all fingerprints, exact wire/damage/refusal, clear/reuse, all domains/kinds and downstream histories passed");
}
