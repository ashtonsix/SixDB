#include "../native.h"
#include <ikea/seriespack.h>
#include <ikea/tuplepack.h>
#include <array>
#include <bit>
#include <optional>
#include <span>

namespace lp=layout_probe;
namespace tp=ikea::tuplepack;
namespace sp=ikea::seriespack;
using lp::byte;
constexpr std::uint32_t none=~std::uint32_t{};

std::uint64_t key_hash(std::uint32_t key) {return lp::hash(key+0x6275636b6574ull);}
std::uint32_t value_for(std::uint32_t key) {return std::uint32_t(lp::hash(key))&0xffffff;}
struct stats {std::size_t blocks=0,keys=0,false_keys=0;};

template<unsigned B> struct table {
    using F=sp::format<B>;
    unsigned n, tiles, fp_bytes, stride, entry_offset;
    std::size_t heads, blocks, keys, overflow;
    bool separate, skew;
    lp::buffer bytes, fingerprints;
    tp::layout entry_layout;
    tp::layout metadata_layout;
    tp::view metadata_view;
    tp::reader<8> metadata_plan;
    tp::read_operation<8,byte> metadata_read;
    tp::reader<8> key_plan,value_plan;
    tp::writer<8> value_write_plan;
    std::vector<tp::view> entry_views;
    std::vector<tp::read_operation<8,byte>> key_read,value_read;
    std::vector<tp::mutation_operation<8>> value_write;
    std::optional<sp::view<F,byte>> fp_view;
    std::optional<sp::decoder<std::uint32_t>> fp_read;
    std::vector<std::vector<std::uint32_t>> initial;

    static tp::layout format() {
        const std::array<tp::code,7> codes{{{0,0,8},{1,0,8},{2,0,4},
            {2,4,4},{3,0,8},{4,0,8},{5,0,4}}};
        auto f=tp::layout::make(6,codes);assert(f);return *f;
    }
    static tp::layout metadata_format() {
        std::array<tp::code,8> codes;
        for(unsigned j=0;j<8;++j)codes[j]={byte(j),0,8};
        auto f=tp::layout::make(8,codes);assert(f);return *f;
    }
    static std::size_t head_count(std::size_t keys,unsigned n,unsigned occupancy) {
        return (keys*100+n*occupancy-1)/(n*occupancy);
    }
    static std::vector<std::vector<std::uint32_t>> distribute(std::size_t keys,unsigned n,unsigned occupancy,bool skew) {
        std::vector<std::vector<std::uint32_t>> v(head_count(keys,n,occupancy));
        const auto active=skew?std::max<std::size_t>(1,v.size()/8):v.size();
        for(std::uint32_t key=0;key<keys;++key) v[(key_hash(key)>>16)%active].push_back(key);
        return v;
    }
    static std::size_t block_count(const std::vector<std::vector<std::uint32_t>>& v,unsigned n) {
        std::size_t total=v.size();
        for(const auto& b:v) if(!b.empty())total+=(b.size()-1)/n;
        return total;
    }
    // A delegating constructor computes occupancy once, before allocating bytes.
    table(unsigned count,unsigned pad,bool split,std::size_t key_count,unsigned occupancy,bool skewed)
        : table(count,pad,split,key_count,skewed,distribute(key_count,count,occupancy,skewed)) {}
    table(unsigned count,unsigned pad,bool split,std::size_t key_count,bool skewed,
          std::vector<std::vector<std::uint32_t>> distribution)
        :n(count),tiles((n+7)/8),fp_bytes(tiles*B),
         stride(std::max<unsigned>(pad,8+6*n+(split?0:fp_bytes))),entry_offset(split?0:fp_bytes),
         heads(distribution.size()),blocks(block_count(distribution,n)),keys(key_count),overflow(blocks-heads),
         separate(split),skew(skewed),bytes(blocks*stride),fingerprints(split?blocks*fp_bytes:1),
         entry_layout(format()),metadata_layout(metadata_format()),
         metadata_view(*tp::view::bind(metadata_layout,std::span(bytes.get(),bytes.size),blocks,stride,entry_offset+n*6)),
         metadata_plan(*tp::reader<8>::make(metadata_layout,std::array<byte,8>{0,1,2,3,4,5,6,7})),
         metadata_read(*tp::bind_reader(metadata_plan,metadata_view)),
         key_plan(*tp::reader<8>::make(entry_layout,std::array<byte,3>{0,1,2})),
         value_plan(*tp::reader<8>::make(entry_layout,std::array<byte,4>{3,4,5,6})),
         value_write_plan(*tp::writer<8>::make(entry_layout,std::array<byte,4>{3,4,5,6})),
         initial(std::move(distribution)) {
        assert(blocks<none && keys*2<(1u<<20));
        std::size_t next=heads;
        for(std::size_t h=0;h<heads;++h) {
            std::size_t block=h;
            const auto& list=initial[h];
            const auto parts=std::max<std::size_t>(1,(list.size()+n-1)/n);
            for(std::size_t p=0;p<parts;++p) {
                unsigned used=unsigned(std::min<std::size_t>(n,list.size()-std::min(list.size(),p*n)));
                const auto successor=p+1<parts?std::uint32_t(next++):none;
                auto* meta=bytes.get()+block*stride+entry_offset+n*6;
                lp::store(meta,successor,4);lp::store(meta+4,(1u<<used)-1,2);
                for(unsigned slot=0;slot<used;++slot) {
                    auto key=list[p*n+slot];
                    lp::store(entry(block,slot),std::uint64_t(key)|(std::uint64_t(value_for(key))<<20),6);
                    put_fp(block,slot,std::uint32_t(key_hash(key))&((1u<<B)-1));
                }
                block=successor;
            }
        }
        assert(next==blocks);
        entry_views.reserve(n);key_read.reserve(n);value_read.reserve(n);value_write.reserve(n);
        for(unsigned j=0;j<n;++j) {
            auto v=tp::view::bind(entry_layout,std::span(bytes.get(),bytes.size),blocks,stride,entry_offset+j*6);
            assert(v);entry_views.push_back(*v);
        }
        for(auto& v:entry_views) {
            key_read.push_back(*tp::bind_reader(key_plan,v));
            value_read.push_back(*tp::bind_reader(value_plan,v));
            value_write.push_back(*tp::bind_writer(value_write_plan,v));
        }
        if(separate || tiles==1) {
            byte* origin=separate?fingerprints.get():bytes.get();
            std::size_t extent=separate?fingerprints.size:bytes.size;
            std::size_t tile_stride=separate?B:stride;
            auto v=sp::view<F,byte>::attach(blocks*tiles*8,{{{std::span(origin,extent),tile_stride},{},{}}});
            assert(v);fp_view=*v;fp_read=sp::bind_decoder<std::uint32_t>(*fp_view);
        }
        initial.clear();initial.shrink_to_fit();
    }
    byte* entry(std::size_t block,unsigned slot) {return bytes.get()+block*stride+entry_offset+slot*6;}
    byte* fp(std::size_t block) {return separate?fingerprints.get()+block*fp_bytes:bytes.get()+block*stride;}
    void put_fp(std::size_t block,unsigned slot,std::uint32_t value) {
        byte* p=fp(block)+(slot/8)*B;
        constexpr unsigned R=B%8,W=B/8;
        if constexpr(W) p[slot%8]=byte(value>>R);
        for(unsigned r=0;r<R;++r) p[8*W+r]|=byte(((value>>r)&1)<<(slot%8));
    }
    std::uint32_t raw_fp(std::size_t block,unsigned slot) {
        byte* p=fp(block)+(slot/8)*B;
        constexpr unsigned R=B%8,W=B/8;
        std::uint32_t value=0;
        if constexpr(W)value=std::uint32_t(p[slot%8])<<R;
        for(unsigned r=0;r<R;++r)value|=std::uint32_t((p[8*W+r]>>(slot%8))&1)<<r;
        return value;
    }
    std::size_t footprint()const{return bytes.capacity()+fingerprints.capacity();}
    std::uint64_t value_packet(std::uint32_t v)const {
        return (v&15) | (std::uint64_t((v>>4)&255)<<8) |
            (std::uint64_t((v>>12)&255)<<16) | (std::uint64_t(v>>20)<<24);
    }
    template<bool Count=false>
    std::uint64_t lookup(std::uint32_t key,unsigned recipe,stats& counters,bool update=false,std::uint32_t salt=0) {
        std::size_t block=(key_hash(key)>>16)%(skew?std::max<std::size_t>(1,heads/8):heads);
        const auto wanted=std::uint32_t(key_hash(key))&((1u<<B)-1);
        std::array<std::uint32_t,16> decoded;
        std::array<ikea::owner_write,8> records;
        ikea::source_write_journal journal{records};
        while(block!=none) {
            if constexpr(Count)++counters.blocks;
            auto* meta=bytes.get()+block*stride+entry_offset+n*6;
            const auto metadata=recipe?metadata_read.get_unchecked(block):lp::load(meta,8);
            auto live=unsigned((metadata>>32)&65535);
            if(recipe==2) {assert(fp_read);fp_read->read_unchecked(block*tiles*8,n,decoded.data());}
            while(live) {
                unsigned j=std::countr_zero(live);live&=live-1;
                auto finger=recipe==2?decoded[j]:raw_fp(block,j);
                if(finger!=wanted)continue;
                if constexpr(Count)++counters.keys;
                const auto exact=recipe?key_read[j].get_unchecked(block):lp::load(entry(block,j),3)&0xfffff;
                if(exact!=key) {if constexpr(Count)++counters.false_keys;continue;}
                if(update) {
                    auto value=(value_for(key)^salt)&0xffffff;
                    if(recipe) value_write[j].set_unchecked(block,value_packet(value),journal);
                    else lp::store(entry(block,j),std::uint64_t(key)|(std::uint64_t(value)<<20),6);
                    return value;
                }
                if(recipe) {
                    auto p=value_read[j].get_unchecked(block);
                    return (p&15)|(((p>>8)&255)<<4)|(((p>>16)&255)<<12)|(((p>>24)&15)<<20);
                }
                return lp::load(entry(block,j),6)>>20;
            }
            block=metadata&0xffffffff;
        }
        return std::uint64_t{1}<<32;
    }
};

template<unsigned B>
void run(lp::options o,unsigned n,unsigned pad,bool split,unsigned occupancy,bool skewed) {
    table<B> t(n,pad,split,o.rows,occupancy,skewed);
    const std::string name="n"+std::to_string(n)+"b"+std::to_string(B)+"stride"+std::to_string(t.stride)+
        (split?"_split":"_combined")+"/load"+std::to_string(occupancy)+(skewed?"_skew":"_uniform");
    std::fprintf(stderr,"bucket %s keys=%zu heads=%zu blocks=%zu overflow=%zu encoded_allocation=%zu\n",
        name.c_str(),o.rows,t.heads,t.blocks,t.overflow,t.footprint());
    for(std::size_t block=0;block<t.blocks;++block)for(unsigned j=0;j<n;++j)
        if(t.fp_read)assert(t.raw_fp(block,j)==t.fp_read->get_unchecked(block*t.tiles*8+j));
    if(o.check && !split && n>8) {
        // The intended two-tile grouping is not representable by a constant
        // tile stride; binding each bucket also hits the aligned-origin rule.
        bool rejected=false;
        for(std::size_t block=1;block<std::min<std::size_t>(t.blocks,8);++block) {
            auto v=sp::view<typename table<B>::F,byte>::attach(n,{{{
                std::span(t.fp(block),t.fp_bytes),B},{},{}}});
            if(reinterpret_cast<std::uintptr_t>(t.fp(block))%64) {assert(!v);rejected=true;}
        }
        if(t.stride%64)assert(rejected);
    }
    std::vector<std::uint32_t> queries(o.queries);
    for(unsigned mode=0;mode<4;++mode) {
        std::uint64_t expected=0;
        for(std::size_t i=0;i<queries.size();++i) {
            auto key=std::uint32_t(lp::hash(i+o.seed)%o.rows);
            if(mode==1 || (mode==2 && (i&1)))key+=o.rows;
            queries[i]=key;expected+=key<o.rows?value_for(key):std::uint64_t{1}<<32;
        }
        for(unsigned recipe=0;recipe<3;++recipe) {
            if(recipe==2 && !t.fp_read)continue;
            stats accounting;std::uint64_t actual=0;
            for(auto key:queries)actual+=t.template lookup<true>(key,recipe,accounting);
            assert(actual==expected);
            if(o.check) {
                for(auto key:queries)if(key<o.rows) {
                    stats ignored;
                    assert(t.lookup(key,recipe,ignored,true,123)==(value_for(key)^123));
                    assert(t.lookup(key,recipe,ignored)==(value_for(key)^123));
                    t.lookup(key,recipe,ignored,true,0);
                }
                continue;
            }
            std::fprintf(stderr,"account %s mode=%u recipe=%u blocks=%zu keys=%zu false_keys=%zu\n",
                name.c_str(),mode,recipe,accounting.blocks,accounting.keys,accounting.false_keys);
            const std::array<const char*,4> mode_names{"hit","miss","mixed","update"};
            const std::array<const char*,3> recipe_names{"raw","rawfp_tuplepack","seriespack_tuplepack"};
            std::uint32_t epoch=0;
            lp::measure(name+"/"+mode_names[mode]+"/"+recipe_names[recipe],o,t.footprint(),accounting.blocks,[&]{
                std::uint64_t sum=0;stats ignored;
                for(auto key:queries)sum+=t.lookup(key,recipe,ignored,mode==3,epoch);
                ++epoch;return sum;
            });
            if(mode==3)for(auto key:queries){stats ignored;t.lookup(key,recipe,ignored,true,0);}
        }
    }
}

int main(int argc,char**argv) {
    auto o=lp::parse(argc,argv);if(o.check){o.rows=137;o.queries=193;}
    lp::header();
    for(unsigned load: {50u,90u})for(bool skew: {false,true}) {
        if(!o.check && skew && load==50)continue;
        run<7>(o,8,0,false,load,skew);run<8>(o,8,0,false,load,skew);
        run<7>(o,8,64,false,load,skew);run<7>(o,9,0,false,load,skew);
        run<7>(o,9,96,false,load,skew);run<7>(o,9,0,true,load,skew);
        run<8>(o,16,0,false,load,skew);run<8>(o,16,128,false,load,skew);
        run<12>(o,16,0,false,load,skew);run<8>(o,16,0,true,load,skew);
        run<12>(o,16,0,true,load,skew);
    }
    if(o.check)std::fprintf(stderr,"Bucket: exact hit/miss/overflow/update results, Local wire cross-checks and admission counterexample passed.\n");
}
