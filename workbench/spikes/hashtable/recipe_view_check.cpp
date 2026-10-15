#include "recipe_view.h"
#include <sys/mman.h>
#include <unistd.h>

namespace {
using namespace ht;
namespace ls=layout_solver;

// The accessible page has a guard on each side. Run each case at both ends:
// the preceding/following bytes of the physical unit are never required padding.
struct guarded {
    std::size_t page=std::size_t(sysconf(_SC_PAGESIZE));
    byte* mapping=static_cast<byte*>(mmap(nullptr,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    guarded() {
        require(mapping!=MAP_FAILED,"recipe guard mapping");
        require(mprotect(mapping+page,page,PROT_READ|PROT_WRITE)==0,"recipe guard protection");
    }
    ~guarded(){require(munmap(mapping,page*3)==0,"recipe guard release");}
    byte* place(unsigned bytes,bool right) {
        require(bytes<=page,"recipe unit fits guard page");
        std::memset(mapping+page,0x6d,page);
        return mapping+page+(right?page-bytes:0);
    }
    void outside(const byte* base,unsigned bytes)const {
        for(const byte* p=mapping+page;p<base;++p)require(*p==0x6d,"preserve prefix outside recipe");
        for(const byte* p=base+bytes;p<mapping+2*page;++p)require(*p==0x6d,"preserve suffix outside recipe");
    }
};

template<unsigned Bytes,bool Direct> void selector_check() {
    guarded guard;
    for(bool right:{false,true}) {
        auto* data=guard.place(Bytes,right);
        for(unsigned pattern=0;pattern<3;++pattern) {
            for(unsigned i=0;i<Bytes;++i)data[i]=pattern==0?0:pattern==1?255:byte(i&1?255:0);
            for(unsigned count=0;count<=Bytes;++count)for(byte fp:{byte(0),byte(255),byte(71)}) {
                auto found=[&] {
#if defined(__AVX2__)
                    if constexpr(Bytes>16)return recipe_matches32<Bytes,Direct>(data,fp,count);
                    else
#endif
                    return recipe_matches<Bytes,Direct>(data,fp,count);
                }();
                for(unsigned i=0;i<count;++i)if(data[i]==fp) {
                    require(found!=0 && take_match(found)==i,"exact selector lane/order");
                }
                require(found==0,"selector excludes every inactive lane");
            }
        }
        guard.outside(data,Bytes);
    }
}

// This wire oracle deliberately does not read candidate fields, selector_window,
// recipe_view addresses, or TP controls. SuffixBits is an independent fixture
// input, checked against K/A but never taken from the consumer's hash type.
// The known five-byte header and manually counted row/plane widths determine
// all bytes, including untouched padding.
template<unsigned Slots,container_kind Kind,unsigned ValueBytes,bool Rows,
         ls::tags Tags,bool Padded=false,unsigned K=24,unsigned A=3,unsigned Alignment=1,
         unsigned SuffixBits=(K==24?13:0),unsigned TailPadding=13,unsigned TagBits=1>
struct fixture {
    static_assert(K==A+8+SuffixBits);
    static constexpr bool mm=Kind==container_kind::multimap;
    static constexpr unsigned tag_bits=TagBits;
    static constexpr unsigned slots=Slots,suffix_bits=SuffixBits,sf=(SuffixBits+7)/8;
    static constexpr unsigned payload=ValueBytes;
    static constexpr unsigned fp_at=5,fp_bytes=Padded?((Slots+15)/16)*16:Slots;
    static constexpr unsigned align(unsigned n){return (n+Alignment-1)/Alignment*Alignment;}
    static constexpr unsigned row_bytes=align(align(sf)+payload+(Tags==ls::tags::row_byte));
    static constexpr unsigned sf_at=Rows?align(fp_at+fp_bytes):fp_at+fp_bytes;
    static constexpr unsigned sf_stride=Rows?row_bytes:sf;
    static constexpr unsigned value_at=Rows?sf_at+align(sf):align(sf_at+Slots*sf);
    static constexpr unsigned value_stride=Rows?row_bytes:align(payload+(Tags==ls::tags::row_byte));
    static constexpr unsigned body_end=Rows?sf_at+Slots*row_bytes:value_at+Slots*value_stride;
    static constexpr unsigned tag_bit=Tags==ls::tags::row_byte?(value_at+payload)*8:body_end*8;
    static constexpr unsigned tag_stride=Tags==ls::tags::row_byte?value_stride*8:Tags==ls::tags::bit_plane?TagBits:8;
    static constexpr unsigned used=body_end+(Tags==ls::tags::byte_plane?Slots:Tags==ls::tags::bit_plane?(Slots*TagBits+7)/8:0);
    static constexpr unsigned bytes=align(used+TailPadding);
    static constexpr auto plan=[] {
        auto p=ls::make({.fingerprint_bits=8,.suffix_bits=SuffixBits,
            .value_bytes=ValueBytes,.kind=Kind,.where=ls::tier::primary,.target=record_platform,
            .count_bytes=1,.descendant_bytes=4,.head_bytes=0,.chain_reference_bytes=4,.allocation_alignment=64,.tag_bits=TagBits},
            {.slots=Slots,.unit_bytes=bytes,.fp=Padded?ls::fingerprint::byte_tiles:ls::fingerprint::exact_bytes,
             .suffix_encoding=ls::suffix::bytes,.fields=Rows?ls::body::rows:ls::body::planes,
             .tag=Tags,.selector_lanes=16,.value_alignment=Alignment});
        if(!p)throw "recipe fixture does not fit";return *p;
    }();
    template<bool Direct,record_access Access=record_access::exact>using view=recipe_view<K,Kind,ValueBytes,A,plan,Direct,Access>;
    using record=typename view<false>::record;
    static void key(std::array<byte,bytes>& out,unsigned slot,byte fp,word suffix) {
        out[fp_at+slot]=fp;
        for(unsigned b=0;b<sf;++b)out[sf_at+slot*sf_stride+b]=byte(suffix>>(b*8));
    }
    static void value(std::array<byte,bytes>& out,unsigned slot,const record& row) {
        for(unsigned b=0;b<payload;++b)out[value_at+slot*value_stride+b]=row.payload[b];
        if constexpr(mm) {
            const unsigned bit=tag_bit+slot*tag_stride;
            if constexpr(Tags==ls::tags::bit_plane) {
                const byte m=byte(((1u<<TagBits)-1)<<(bit%8));out[bit/8]=byte((out[bit/8]&~m)|((row.tag&((1u<<TagBits)-1))<<(bit%8)));
            }else out[bit/8]=row.tag;
        }
    }
};

struct observations {
    struct interval {std::size_t offset,size;bool operator==(const interval&)const=default;};
    std::array<interval,4> wanted{};
    unsigned expected=0,seen=0,room=4;
    byte* base=nullptr;
    const byte* previous=nullptr;
    std::size_t remaining()const noexcept{return room-seen;}
    template<class Source> void before(const Source& source,ikea::byte_write w)noexcept {
        require(source.base==base && w.plane==0,"recipe damage source");
        require(seen<expected && seen<room,"recipe damage preadmission");
        require(wanted[seen++]==interval{w.offset,w.size},"exact recipe damage window/order");
        require(std::memcmp(base+w.offset,previous+w.offset,w.size)==0,"damage precedes physical store");
    }
    void done()const{require(seen==expected,"all issued field writes reported");}
};

template<class F> observations watch(byte* base,const byte* old,unsigned slot,bool key,bool value) {
    observations out;out.base=base;out.previous=old;
    auto add=[&](unsigned at,unsigned bytes){if(bytes)out.wanted[out.expected++]={at,bytes};};
    if(key){add(F::fp_at+slot,1);add(F::sf_at+slot*F::sf_stride,F::sf);}
    if(value) {
        add(F::value_at+slot*F::value_stride,F::payload);
        if constexpr(F::mm)add((F::tag_bit+slot*F::tag_stride)/8,1);
    }
    return out;
}

template<class F,bool Direct,record_access Access=record_access::exact> void physical_check() {
    using view=typename F::template view<Direct,Access>;
    constexpr auto slots=F::slots;
    view::prepare(slots);
    guarded guard;
    for(bool right:{false,true}) {
        auto* data=guard.place(F::bytes,right);std::memset(data,0xa5,F::bytes);
        view v{data};std::array<byte,F::bytes> expected;expected.fill(0xa5);
        std::array<typename F::record,slots> rows{};
        std::array<byte,slots> fps{};std::array<word,slots> suffixes{};
        for(unsigned slot=0;slot<slots;++slot) {
            fps[slot]=byte(slot&1?255:0);suffixes[slot]=(0x55aacc993311eeULL^(slot*131+7))&mask(F::suffix_bits);
            for(unsigned b=0;b<F::payload;++b)rows[slot].payload[b]=byte(43*slot+29*b+11);
            rows[slot].tag=byte(slot&((1u<<F::tag_bits)-1));
            auto old=expected;auto seen=watch<F>(data,old.data(),slot,true,true);
            v.key(slot,fps[slot],suffixes[slot],seen);v.write(slot,rows[slot],seen);seen.done();
            F::key(expected,slot,fps[slot],suffixes[slot]);F::value(expected,slot,rows[slot]);
            require(std::memcmp(data,expected.data(),F::bytes)==0,"independent full recipe wire oracle");
        }
        // Update every tag in both directions, including two slots sharing a byte.
        for(unsigned slot=0;slot<slots;++slot) {
            rows[slot].tag^=byte((1u<<F::tag_bits)-1);for(auto& b:rows[slot].payload)b^=0x96;
            auto old=expected;auto seen=watch<F>(data,old.data(),slot,false,true);
            v.write(slot,rows[slot],seen);seen.done();F::value(expected,slot,rows[slot]);
            require(std::memcmp(data,expected.data(),F::bytes)==0,"update preserves neighbors and padding");
            auto read=v.read(slot);require(read.payload==rows[slot].payload,"opaque payload read");
            if constexpr(F::mm)require(read.tag==rows[slot].tag,"tag read");
        }
        for(unsigned count=0;count<=slots;++count)for(unsigned query=0;query<slots+2;++query) {
            const byte fp=query<slots?fps[query]:byte(query==slots?0:255);
            const word sf=query<slots?suffixes[query]:mask(F::suffix_bits);
            int wanted=-1;for(unsigned slot=0;slot<count;++slot)if(fps[slot]==fp && suffixes[slot]==sf){wanted=slot;break;}
            require(v.find(fp,sf,count)==wanted,"occupied-prefix exact lookup oracle");
            unsigned consumed=0;
            require(v.find_read(fp,sf,count,[&](typename view::record&& row){
                require(wanted>=0&&row.payload==rows[wanted].payload,"immediate full payload from matching row");
                if constexpr(F::mm)require(row.tag==rows[wanted].tag,"immediate row tag");
                ++consumed;
            })==wanted&&consumed==unsigned(wanted>=0),"immediate consumer only for exact match");
        }
        if constexpr(slots>1) {
            // Refusal must precede the first store/event, including a shared tag byte.
            auto old=expected;auto refused=watch<F>(data,old.data(),0,true,true);
            refused.room=refused.expected-1;
            auto no=v.erase(0,slots,refused);
            require(!no && no.error()==mutation_error::effects && refused.seen==0,"erase damage refusal");
            require(std::memcmp(data,expected.data(),F::bytes)==0,"refused erase preserves entire unit");
            unsigned count=slots;
            while(count>1) {
                const unsigned slot=(count/2)-1,last=count-1;
                old=expected;auto seen=watch<F>(data,old.data(),slot,true,true);
                auto yes=v.erase(slot,count,seen);require(bool(yes)&&*yes,"swap-last erase");seen.done();
                F::key(expected,slot,fps[last],suffixes[last]);F::value(expected,slot,rows[last]);
                fps[slot]=fps[last];suffixes[slot]=suffixes[last];rows[slot]=rows[last];--count;
                require(std::memcmp(data,expected.data(),F::bytes)==0,"erase preserves stale/inactive bytes and padding");
                for(unsigned i=0;i<count;++i) {
                    auto r=v.read(i);require(r.payload==rows[i].payload,"erased payload oracle");
                    if constexpr(F::mm)require(r.tag==rows[i].tag,"erased tag oracle");
                    int first=0;while(fps[first]!=fps[i]||suffixes[first]!=suffixes[i])++first;
                    require(v.find(fps[i],suffixes[i],count)==first,"lookup after repeated swap erase");
                }
            }
        }
        auto old=expected;observations none;none.base=data;none.previous=old.data();none.room=0;
        auto last=v.erase(0,1,none);require(bool(last)&&*last&&none.seen==0,"last erase has no packed store");
        require(std::memcmp(data,expected.data(),F::bytes)==0,"last erase leaves count publication to caller");
        guard.outside(data,F::bytes);
    }
}

template<class F> void both(){physical_check<F,false>();physical_check<F,true>();}
template<unsigned Slots> void irregular() {
    both<fixture<Slots,container_kind::map,16,false,ls::tags::none>>();
    both<fixture<Slots,container_kind::map,16,true,ls::tags::none>>();
}
template<unsigned Slots,bool Rows> void tags() {
    both<fixture<Slots,container_kind::multimap,8,Rows,ls::tags::row_byte>>();
    both<fixture<Slots,container_kind::multimap,8,Rows,ls::tags::byte_plane>>();
    both<fixture<Slots,container_kind::multimap,8,Rows,ls::tags::bit_plane>>();
}

// A shared driver keeps the boundary matrix from duplicating the whole mutation
// oracle. Its coordinates and expected values come from fixture inputs, while
// only these tiny endpoints bind the actual candidate and access mode.
struct access_spec {
    unsigned slots,bytes,fp_at,sf_at,sf_stride,sf_bytes,sf_bits;
};
struct access_ops {
    word (*suffix)(byte*,unsigned);
    int (*find)(byte*,byte,word,unsigned);
    unsigned suffix_bytes;
};
template<class F,bool Direct,record_access Access> access_ops bind_access() {
    using view=typename F::template view<Direct,Access>;
    view::prepare(F::slots);
    return {+[](byte* p,unsigned slot){return view{p}.suffix(slot);},
        +[](byte* p,byte fp,word sf,unsigned count){return view{p}.find(fp,sf,count);},view::suffix_read_bytes};
}
void check_access(access_spec s,const std::array<access_ops,8>& ops,unsigned widened_suffix) {
    for(unsigned i=0;i<ops.size();++i)
        require(ops[i].suffix_bytes==((i%4)>=2?widened_suffix:s.sf_bytes),"all-slot suffix admission/fallback");
    guarded guard;
    for(bool right:{false,true})for(unsigned pattern=0;pattern<3;++pattern) {
        auto* data=guard.place(s.bytes,right);
        std::vector<byte> expected(s.bytes,0x55),fps(s.slots);
        std::vector<word> suffixes(s.slots);
        for(unsigned slot=0;slot<s.slots;++slot) {
            fps[slot]=pattern==0?byte(slot*7+1):pattern==1?0:255;
            // Uniform FP patterns stress collision confirmation; the low suffix
            // byte deliberately equals FP, so widened selectors see more matches.
            suffixes[slot]=(((0x55aa773399cc00ULL^(word(slot+1)*0x01010101010100ULL))&~word(255))|
                fps[slot])&mask(s.sf_bits);
            expected[s.fp_at+slot]=fps[slot];
            for(unsigned b=0;b<s.sf_bytes;++b)expected[s.sf_at+slot*s.sf_stride+b]=byte(suffixes[slot]>>(8*b));
        }
        // Change every non-key byte, including exact-tail neighbors, padded FP
        // lanes, values and tags. Reads must depend only on active key fields.
        for(byte poison:{byte(0),byte(255),byte(0x69)}) {
            for(unsigned at=0;at<s.bytes;++at) {
                bool key=at>=s.fp_at && at<s.fp_at+s.slots;
                for(unsigned slot=0;slot<s.slots;++slot)
                    key|=at>=s.sf_at+slot*s.sf_stride && at<s.sf_at+slot*s.sf_stride+s.sf_bytes;
                if(!key)expected[at]=poison;
            }
            std::memcpy(data,expected.data(),s.bytes);
            for(const auto& op:ops) {
                for(unsigned slot=0;slot<s.slots;++slot)
                    require(op.suffix(data,slot)==suffixes[slot],"widened suffix discards adjacent bytes");
                for(unsigned count=0;count<=s.slots;++count)for(unsigned query=0;query<s.slots+4;++query) {
                    const byte fp=query<s.slots?fps[query]:byte((query-s.slots)&1?255:0);
                    const word sf=query<s.slots?suffixes[query]:query<s.slots+2?mask(s.sf_bits):0;
                    int wanted=-1;
                    for(unsigned slot=0;slot<count;++slot)
                        if(fps[slot]==fp && suffixes[slot]==sf){wanted=slot;break;}
                    require(op.find(data,fp,sf,count)==wanted,"widened read preserves active-key lookup");
                }
                require(std::memcmp(data,expected.data(),s.bytes)==0,"read access does not mutate owned bytes");
            }
            guard.outside(data,s.bytes);
        }
    }
}
template<class F,unsigned WidenedSuffix> void access_case() {
    const std::array ops{
        bind_access<F,false,record_access::exact>(),bind_access<F,false,record_access::selectors>(),
        bind_access<F,false,record_access::suffixes>(),bind_access<F,false,record_access::both>(),
        bind_access<F,true,record_access::exact>(),bind_access<F,true,record_access::selectors>(),
        bind_access<F,true,record_access::suffixes>(),bind_access<F,true,record_access::both>()};
    check_access({F::slots,F::bytes,F::fp_at,F::sf_at,F::sf_stride,F::sf,F::suffix_bits},ops,WidenedSuffix);
}
template<unsigned Tail,unsigned Group> void selector_thresholds() {
    constexpr unsigned n=16*Group+Tail;
    // No suffix/value bytes: the extra accessible bytes are precisely the
    // explicit unit tail. 5+16*Group is the final group's hand-counted origin.
    using below=fixture<n,container_kind::set,0,false,ls::tags::none,false,8,0,1,0,15-Tail>;
    using exact=fixture<n,container_kind::set,0,false,ls::tags::none,false,8,0,1,0,16-Tail>;
    using above=fixture<n,container_kind::set,0,false,ls::tags::none,false,8,0,1,0,17-Tail>;
    static_assert(below::bytes==20+16*Group && exact::bytes==21+16*Group && above::bytes==22+16*Group);
    access_case<below,0>();access_case<exact,0>();access_case<above,0>();
}
template<unsigned K,unsigned Bits,unsigned Bytes,unsigned Wide> void suffix_thresholds() {
    static_assert((Bits+7)/8==Bytes && Wide>Bytes);
    // Sets leave the final suffix at the unit edge. Earlier slots would permit
    // broader reads even in 'below'; admission must still fall back for ALL slots.
    using below=fixture<17,container_kind::set,0,false,ls::tags::none,false,K,3,1,Bits,Wide-Bytes-1>;
    using exact=fixture<17,container_kind::set,0,false,ls::tags::none,false,K,3,1,Bits,Wide-Bytes>;
    using above=fixture<17,container_kind::set,0,false,ls::tags::none,false,K,3,1,Bits,Wide-Bytes+1>;
    access_case<below,Bytes>();access_case<exact,Wide>();access_case<above,Wide>();
}
template<class F> void all_physical_access() {
    lanes<4>([](auto mode){
        constexpr auto access=static_cast<record_access>(decltype(mode)::value);
        physical_check<F,false,access>();physical_check<F,true,access>();
    });
}
}

int main() {
    lanes<16>([](auto i){selector_check<i+1,false>();selector_check<i+1,true>();});
#if defined(__AVX2__)
    lanes<16>([](auto i){selector_check<i+17,false>();selector_check<i+17,true>();});
#endif
    irregular<7>();irregular<9>();irregular<12>();irregular<15>();irregular<17>();irregular<23>();irregular<31>();irregular<32>();
    tags<9,false>();tags<9,true>();tags<17,false>();tags<17,true>();tags<31,false>();tags<31,true>();tags<32,false>();tags<32,true>();
    both<fixture<15,container_kind::multimap,8,false,ls::tags::bit_plane,true>>();
    both<fixture<9,container_kind::multimap,16,false,ls::tags::row_byte,false,24,3,1,13,0,2>>();
    both<fixture<9,container_kind::multimap,16,true,ls::tags::byte_plane,false,24,3,1,13,0,2>>();
    both<fixture<17,container_kind::multimap,16,false,ls::tags::bit_plane,false,24,3,1,13,0,2>>();
    both<fixture<17,container_kind::multimap,8,true,ls::tags::row_byte,true>>();
    both<fixture<7,container_kind::set,0,false,ls::tags::none>>();
    both<fixture<31,container_kind::set,0,true,ls::tags::none>>();
    both<fixture<32,container_kind::set,0,false,ls::tags::none>>();
    both<fixture<9,container_kind::map,3,false,ls::tags::none,false,8,0>>();
    both<fixture<9,container_kind::map,3,true,ls::tags::none,false,8,0>>();
    both<fixture<17,container_kind::multimap,4,true,ls::tags::row_byte>>();
    both<fixture<17,container_kind::multimap,4,true,ls::tags::bit_plane>>();
    both<fixture<17,container_kind::set,0,true,ls::tags::none,false,8,0>>();
    using aligned_plane=fixture<7,container_kind::map,16,false,ls::tags::none,false,24,3,8>;
    using aligned_row=fixture<7,container_kind::map,16,true,ls::tags::none,false,24,3,8>;
    using aligned_tag=fixture<9,container_kind::multimap,8,false,ls::tags::bit_plane,false,24,3,8>;
    // Hand-counted anchors keep the independent coordinate oracle honest.
    static_assert(aligned_plane::sf_at==12 && aligned_plane::value_at==32 && aligned_plane::bytes==160);
    static_assert(aligned_row::sf_at==16 && aligned_row::value_at==24 && aligned_row::value_stride==24 && aligned_row::bytes==200);
    static_assert(aligned_tag::value_at==32 && aligned_tag::tag_bit==104*8 && aligned_tag::bytes==120);
    both<aligned_plane>();both<aligned_row>();both<aligned_tag>();
    lanes<15>([](auto i){selector_thresholds<i+1,0>();selector_thresholds<i+1,1>();});
    suffix_thresholds<32,21,3,4>();suffix_thresholds<48,37,5,8>();
    suffix_thresholds<56,45,6,8>();suffix_thresholds<64,53,7,8>();
    using odd_map=fixture<17,container_kind::map,3,true,ls::tags::none,false,32,3,1,21,0>;
    using odd_bits=fixture<17,container_kind::multimap,8,false,ls::tags::bit_plane,false,48,3,1,37,0>;
    using odd_row=fixture<9,container_kind::multimap,8,true,ls::tags::row_byte,false,56,3,1,45,0>;
    using odd_plane=fixture<7,container_kind::multimap,8,false,ls::tags::byte_plane,false,64,3,1,53,0>;
    using odd_aligned=fixture<7,container_kind::map,16,true,ls::tags::none,false,64,3,8,53,0>;
    all_physical_access<odd_map>();all_physical_access<odd_bits>();all_physical_access<odd_row>();
    all_physical_access<odd_plane>();all_physical_access<odd_aligned>();
    // Actual K40/A9 map control: 20 fingerprints followed by 20 five-byte
    // rows. The final row begins at120. Three tail bytes admit eight-byte
    // reads; two do not. Both guard placements and every active count execute
    // the immediate consumer, including collisions and the final row.
    using row_below=fixture<20,container_kind::map,2,true,ls::tags::none,false,40,9,1,23,2>;
    using row_exact=fixture<20,container_kind::map,2,true,ls::tags::none,false,40,9,1,23,3>;
    using row_above=fixture<20,container_kind::map,2,true,ls::tags::none,false,40,9,1,23,4>;
    static_assert(row_exact::sf_at==25 && row_exact::sf_stride==5 && row_exact::bytes==128);
    static_assert(row_exact::view<false>::row_consumption_bytes==5);
    static_assert(row_below::view<false,record_access::both>::row_consumption_bytes==5);
    static_assert(row_exact::view<false,record_access::both>::row_consumption_bytes==8);
    static_assert(row_above::view<false,record_access::both>::row_consumption_bytes==8);
    all_physical_access<row_below>();all_physical_access<row_exact>();all_physical_access<row_above>();
    all_physical_access<fixture<17,container_kind::multimap,4,true,ls::tags::row_byte>>();
    all_physical_access<fixture<17,container_kind::multimap,4,true,ls::tags::bit_plane>>();
    access_case<odd_map,4>();access_case<odd_bits,8>();access_case<odd_row,8>();
    access_case<odd_plane,8>();access_case<odd_aligned,8>();
    std::puts("recipe exact and widened access, guard thresholds, poisoned neighbors, independent bytes, damage and refusal passed");
}
