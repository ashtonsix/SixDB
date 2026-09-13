#include "../native.h"
#include <ikea/tuplepack.h>
#include <array>
#include <optional>
#include <span>

namespace lp=layout_probe;
namespace tp=ikea::tuplepack;
using lp::byte;

// Fixed control bytes contain length and a 32-bit tail offset. Every string is
// 16 bytes in this first fixture. Variable prefix collisions, not arena policy,
// are the controlled data distribution here.
struct candidate {
    std::string name;
    unsigned fixed, prefix, stride;
    bool side=false, plane=false;
};
struct record {
    std::array<byte,16> text;
    std::uint64_t scalar;
    byte rare;
};
struct storage {
    candidate c;
    lp::buffer core, tails, side, prefixes;
    std::size_t rows;
    unsigned core_width;
    tp::layout layout;
    tp::view view;
    tp::reader<8> scalar_plan;
    tp::read_operation<8,byte> scalar_read;
    tp::writer<8> scalar_write_plan;
    tp::mutation_operation<8> scalar_write;

    static tp::layout format(unsigned bytes) {
        std::vector<tp::code> codes;
        for (unsigned i=0; i<bytes; ++i) codes.push_back({byte(i),0,8});
        auto f=tp::layout::make(bytes,codes); assert(f); return *f;
    }
    storage(candidate cfg, const std::vector<record>& data)
        : c(cfg), core(data.size()*c.stride), tails(data.size()*(16-c.prefix)),
          side(c.side?data.size():1), prefixes(c.plane?data.size()*c.prefix:1), rows(data.size()),
          core_width(std::min<unsigned>(64,c.plane?c.fixed:c.fixed+c.prefix)),
          layout(format(core_width)),
          view(*tp::view::bind(layout,std::span(core.get(),core.size),rows,c.stride,0)),
          scalar_plan(*tp::reader<8>::make(layout,std::array<byte,8>{0,1,2,3,4,5,6,7})),
          scalar_read(*tp::bind_reader(scalar_plan,view)),
          scalar_write_plan(*tp::writer<8>::make(layout,std::array<byte,8>{0,1,2,3,4,5,6,7})),
          scalar_write(*tp::bind_writer(scalar_write_plan,view)) {
        assert(tails.size < (std::uint64_t{1}<<32));
        for (std::size_t r=0;r<rows;++r) {
            byte* p=core.get()+r*c.stride;
            lp::store(p,data[r].scalar,8);
            lp::store(p+8,r*(16-c.prefix),4); p[12]=16;
            for (unsigned j=13;j<c.fixed;++j) p[j]=byte(lp::hash(r*67+j));
            if(c.side) side.get()[r]=data[r].rare; else p[c.fixed-1]=data[r].rare;
            std::memcpy(prefix_at(r),data[r].text.data(),c.prefix);
            std::memcpy(tails.get()+r*(16-c.prefix),data[r].text.data()+c.prefix,16-c.prefix);
        }
    }
    byte* prefix_at(std::size_t r) {
        return c.plane?prefixes.get()+r*c.prefix:core.get()+r*c.stride+c.fixed;
    }
    const byte* prefix_at(std::size_t r) const {
        return c.plane?prefixes.get()+r*c.prefix:core.get()+r*c.stride+c.fixed;
    }
    const byte* tail_at(std::size_t r) const {
        return tails.get()+lp::load(core.get()+r*c.stride+8,4);
    }
    byte rare(std::size_t r) const {return c.side?side.get()[r]:core.get()[r*c.stride+c.fixed-1];}
    std::size_t footprint() const {return core.capacity()+tails.capacity()+side.capacity()+prefixes.capacity();}
};

struct query {std::size_t row; std::array<byte,16> text;};
[[gnu::noinline]] std::uint64_t equal(storage& s, const std::vector<query>& queries) {
    std::uint64_t sum=0;
    for (const auto& q:queries) {
        if(std::memcmp(s.prefix_at(q.row),q.text.data(),s.c.prefix)!=0) continue;
        sum+=std::memcmp(s.tail_at(q.row),q.text.data()+s.c.prefix,16-s.c.prefix)==0;
    }
    return sum;
}
[[gnu::noinline]] std::uint64_t full(storage& s, const std::vector<std::size_t>& ids) {
    std::uint64_t sum=0;
    for(auto r:ids) {
        const byte* p=s.prefix_at(r); const byte* t=s.tail_at(r);
        for(unsigned j=0;j<16;++j) sum+=std::uint64_t(j+1)*(j<s.c.prefix?p[j]:t[j-s.c.prefix]);
        sum+=s.rare(r);
    }
    return sum;
}
[[gnu::noinline]] std::uint64_t scalars(storage& s,const std::vector<std::size_t>& ids,bool ikea) {
    std::uint64_t sum=0;
    for(auto r:ids) sum+=ikea?s.scalar_read.get_unchecked(r):lp::load(s.core.get()+r*s.c.stride,8);
    return sum;
}
[[gnu::noinline]] std::uint64_t all_fields(storage& s,const std::vector<std::size_t>& ids) {
    std::uint64_t sum=0;
    for(auto r:ids) {
        const auto* row=s.core.get()+r*s.c.stride;
        sum+=lp::load(row,8)+row[12]+s.rare(r);
        for(unsigned j=13;j<s.c.fixed-(s.c.side?0:1);++j)sum+=std::uint64_t(j+1)*row[j];
        const auto* p=s.prefix_at(r);const auto* t=s.tail_at(r);
        for(unsigned j=0;j<16;++j)sum+=std::uint64_t(j+1)*(j<s.c.prefix?p[j]:t[j-s.c.prefix]);
    }
    return sum;
}
[[gnu::noinline]] std::uint64_t updates(storage& s,const std::vector<std::size_t>& ids,bool ordinary,std::uint64_t salt=0) {
    std::array<ikea::owner_write,8> records;
    ikea::source_write_journal journal{records};
    std::uint64_t sum=0;
    for(auto r:ids) {
        const auto x=lp::hash(r)^salt;
        if(ordinary) {journal.used=0; s.scalar_write.set_unchecked(r,x,journal); sum+=journal.used;}
        else {lp::store(s.core.get()+r*s.c.stride,x,8); sum+=1;}
    }
    return sum;
}

int main(int argc,char** argv) {
    auto o=lp::parse(argc,argv);
    if(o.check){o.rows=97;o.queries=193;}
    auto ids=lp::row_ids(o);
    std::vector<record> data(o.rows);
    for(std::size_t r=0;r<o.rows;++r) {
        data[r].scalar=lp::hash(r);data[r].rare=byte(lp::hash(r+89));
        for(unsigned j=0;j<16;++j) data[r].text[j]=byte(lp::hash(r*31+j+o.seed));
    }
    lp::header();
    const std::array<candidate,8> choices{{
        {"A63",55,8,63},{"B64prefix9",55,9,64},{"C64pad",55,8,64},
        {"A_prefix_plane",55,8,55,false,true},
        {"D65",57,8,65},{"E64prefix7",57,7,64},
        {"F64side",56,8,64,true},{"D_prefix_plane",57,8,57,false,true}}};
    std::uint64_t expected_full=0,expected_scalar=0;
    for(auto r:ids) {
        for(unsigned j=0;j<16;++j) expected_full+=std::uint64_t(j+1)*data[r].text[j];
        expected_full+=data[r].rare; expected_scalar+=data[r].scalar;
    }
    // Rotate candidate order by seed; the runner uses independent seeds.
    for(unsigned ci=0;ci<choices.size();++ci) {
        auto c=choices[(ci+o.seed)%choices.size()];
        storage s(c,data);
        assert(full(s,ids)==expected_full);
        assert(scalars(s,ids,false)==expected_scalar && scalars(s,ids,true)==expected_scalar);
        auto expected_all=expected_full+expected_scalar+16*ids.size();
        for(auto r:ids)for(unsigned j=13;j<c.fixed+(c.side?1:0)-1;++j)
            expected_all+=std::uint64_t(j+1)*byte(lp::hash(r*67+j));
        assert(all_fields(s,ids)==expected_all);
        if(!o.check) {
            lp::measure(c.name+"/scalar_raw",o,s.footprint(),0,[&]{return scalars(s,ids,false);});
            lp::measure(c.name+"/scalar_tuplepack",o,s.footprint(),0,[&]{return scalars(s,ids,true);});
            lp::measure(c.name+"/full_string_rare",o,s.footprint(),ids.size(),[&]{return full(s,ids);});
            lp::measure(c.name+"/all_fields",o,s.footprint(),ids.size(),[&]{return all_fields(s,ids);});
        }
        for(unsigned mismatch: {0u,7u,8u,15u,16u,17u}) {
            std::vector<query> queries;
            std::size_t visits=0;
            std::uint64_t expected=0;
            for(auto r:ids) {
                query q{r,data[r].text};
                unsigned actual_mismatch=mismatch;
                if(mismatch==17)actual_mismatch=std::array<unsigned,8>{16,0,7,7,7,7,8,15}[queries.size()%8];
                if(actual_mismatch<16) q.text[actual_mismatch]^=1;
                else ++expected;
                visits+=actual_mismatch>=c.prefix;
                queries.push_back(q);
            }
            assert(equal(s,queries)==expected);
            if(!o.check) lp::measure(c.name+"/equality_mismatch"+std::to_string(mismatch),o,
                s.footprint(),visits,[&]{return equal(s,queries);});
        }
        auto before=full(s,ids);
        assert(updates(s,ids,true,123)==ids.size());
        assert(full(s,ids)==before);
        for(auto r:ids) assert(s.scalar_read.get_unchecked(r)==(lp::hash(r)^123));
        assert(updates(s,ids,true)==ids.size());
        assert(scalars(s,ids,true)==expected_scalar);
        if(!o.check) {
            std::uint64_t epoch=0;
            lp::measure(c.name+"/update_raw",o,s.footprint(),0,[&]{return updates(s,ids,false,++epoch);});
            lp::measure(c.name+"/update_tuplepack_effects",o,s.footprint(),0,[&]{return updates(s,ids,true,++epoch);});
        }
    }
    if(o.check) std::fprintf(stderr,"Prefix: all eight layouts reconstruct exactly; six equality cases, all-field/scalar projections and preserving writes passed.\n");
}
