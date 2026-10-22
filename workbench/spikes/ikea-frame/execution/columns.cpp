// Cross-column representation/program experiment; no Frame interface/optimizer.
#include "columns_native.h"
#if FRAME_CROSS_DOMAINS
#include "columns_domains.h"
#endif
#include "../encoding/material.h"
#include <ikea/hashtable.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace {
using namespace frame_execution;
using namespace frame_columns;
namespace e = frame_encoding;
using Clock = std::chrono::steady_clock;
using Output = ikea::hashtable::table<64, 6, 16>;
#if FRAME_CROSS_DOMAINS
constexpr unsigned arm_count=13;
#else
constexpr unsigned arm_count=9;
#endif
void need(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
double elapsed(Clock::time_point a) { return std::chrono::duration<double,std::nano>(Clock::now()-a).count(); }
template<class F> void each(const Bits& bits, F&& f) {
    for (unsigned w=0;w<4;++w) for (auto rest=bits.words[w];rest;rest&=rest-1)
        f(w*64+unsigned(std::countr_zero(rest)));
}
Bits intersect(Bits a,const Bits& b) { for(unsigned w=0;w<4;++w)a.words[w]&=b.words[w];return a; }
Bits exclude(Bits a,const Bits& b) { for(unsigned w=0;w<4;++w)a.words[w]&=~b.words[w];return a; }
unsigned in_range(const Bits& bits,unsigned first,unsigned limit) {
    unsigned count=0;
    for(unsigned w=0;w<4;++w) {
        unsigned a=std::max(first,w*64),b=std::min(limit,(w+1)*64);
        if(a<b) { unsigned n=b-a;Word mask=n==64?~Word{}:((Word{1}<<n)-1)<<(a-w*64);count+=std::popcount(bits.words[w]&mask); }
    }
    return count;
}
struct Common {
    std::vector<Word> keys;
    std::vector<Bits> live,has_x,has_c;
    std::size_t bytes() const { return sizeof(*this)+keys.capacity()*8+(live.capacity()+has_x.capacity()+has_c.capacity())*sizeof(Bits); }
};
struct Independent { e::column c,x;std::vector<std::uint32_t> x_relative; };
struct Joint { e::column codes;std::vector<std::uint32_t> category,x;NativeCodes native; };
std::size_t column_capacity(const e::column& c) {
    return c.payload.capacity()+c.blocks.capacity()*sizeof(e::descriptor)+c.parts.capacity()*sizeof(e::dictionary_part)+c.coded.capacity()*8+c.binding_bytes();
}
std::vector<Word> decode(const e::column& c) {
    std::vector<Word> out(c.rows);
    for(unsigned b=0;b<c.blocks.size();++b)e::decode_block(c,b,out.data()+b*256);
    return out;
}
NativeCodes native_codes(std::span<const Word> values,std::size_t distinct) {
    NativeCodes out;
    if(distinct<=256)out.small.assign(values.begin(),values.end());
    else {need(distinct<=65536,"native index exceeds u16");out.large.assign(values.begin(),values.end());}
    return out;
}
void dictionary_ready(Independent& image) {
    if(image.x.parts.empty()||!image.x.parts[0].count)return;
    const auto p=e::dictionary_of(image.x,0);
    std::vector<Word> values(p.count);e::read_dictionary(p,0,p.count,values.data());
    image.x_relative.assign(values.begin(),values.end());
}
struct Data {
    std::vector<Common> common;
    std::array<std::vector<Independent>,2> independent;
    std::vector<Joint> joint;
    std::vector<NativeRows> native;
    std::array<double,5> construction_ns{};
#if FRAME_CROSS_DOMAINS
    std::array<std::vector<NativeX>,2> domain_x;
    std::array<double,2> domain_construction_ns{};
#endif
};
Data build(const Input& input) {
    Data data;data.common.reserve(input.parts.size());data.joint.reserve(input.parts.size());data.native.reserve(input.parts.size());
    for(auto& images:data.independent)images.reserve(input.parts.size());
    for(const auto& part:input.parts) {
        const auto n=(part.limit+255)/256*256;
        Common common;common.keys=part.dictionary;
        common.live.resize(n/256);common.has_x.resize(n/256);common.has_c.resize(n/256);
        std::vector<Word> c(n),x(n),codes(n);
        for(const auto& row:part.rows) {
            common.live[row.position/256].set(row.position%256);
            if(row.has_x) { common.has_x[row.position/256].set(row.position%256);x[row.position]=row.x; }
            if(row.code>=0) {common.has_c[row.position/256].set(row.position%256);c[row.position]=Word(row.code);}
        }
        for(unsigned arm=0;arm<2;++arm) {
            const auto start=Clock::now();
            Independent image;
            image.c=e::encode(c,{.bind_decoders=false});
            image.x=e::encode(x,{.dictionary=arm?e::dictionary_order::sorted:e::dictionary_order::none,.bind_decoders=false});
            dictionary_ready(image);
            need(!image.c.binding_bytes()&&!image.x.binding_bytes(),"unexpected bound decoder state");
            data.independent[arm].push_back(std::move(image));data.construction_ns[arm]+=elapsed(start);
        }
        {
            const auto start=Clock::now();Joint image;
            std::unordered_map<Word,unsigned> seen;
            for(const auto& row:part.rows) {
                const unsigned tag=row.code<0?unsigned(part.dictionary.size()):unsigned(row.code);
                const Word value=row.has_x?row.x:0;
                const Word pair=(Word(tag)<<32)|value;
                const auto [at,fresh]=seen.try_emplace(pair,unsigned(image.x.size()));
                if(fresh) {image.category.push_back(tag);image.x.push_back(std::uint32_t(value));}
                codes[row.position]=at->second;
            }
            image.codes=e::encode(codes,{.bind_decoders=false});
            data.construction_ns[2]+=elapsed(start);
            const auto native_start=Clock::now();
            image.native=native_codes(codes,image.x.size());
            data.construction_ns[4]+=elapsed(native_start);
            const auto rows_start=Clock::now();
            NativeRows native;native.category=native_codes(c,part.dictionary.size());native.x.assign(x.begin(),x.end());
            data.construction_ns[3]+=elapsed(rows_start);
#if FRAME_CROSS_DOMAINS
            for(unsigned kind=0;kind<2;++kind) {
                const auto domain_start=Clock::now();
                data.domain_x[kind].emplace_back(x,common.has_x,kind!=0);
                data.domain_construction_ns[kind]+=elapsed(domain_start);
            }
#endif
            data.joint.push_back(std::move(image));data.native.push_back(std::move(native));
        }
        data.common.push_back(std::move(common));
    }
    return data;
}
struct Plan { std::vector<Bits> selected;std::vector<std::uint8_t> allowed; };
std::vector<Plan> prepare(const Data& data,const Query& query) {
    std::vector<Plan> plans;plans.reserve(data.common.size());
    for(unsigned i=0;i<data.common.size();++i) {
        const auto& common=data.common[i];Plan plan;
        plan.allowed.resize(common.keys.size()+1);
        for(unsigned c=0;c<common.keys.size();++c)plan.allowed[c]=query.accepts(common.keys[c]);
        plan.allowed.back()=query.include_null;
        plan.selected.resize(common.live.size());
        if(!query.selection[i].any||!query.coarse[i].selected()){plans.push_back(std::move(plan));continue;}
        for(unsigned b=0;b<common.live.size();++b) {
            const auto present=intersect(common.live[b],common.has_x[b]);
            each(present,[&](unsigned p) {
                const unsigned row=b*256+p,block=row/query.coarse[i].grain;
                if(query.selection[i].contains(row)&&(query.coarse[i].all||std::binary_search(query.coarse[i].blocks.begin(),query.coarse[i].blocks.end(),block)))plan.selected[b].set(p);
            });
        }
        plans.push_back(std::move(plan));
    }
    return plans;
}
struct Scratch {
    std::vector<Total> categories;
    std::vector<std::uint32_t> pairs;
    std::size_t bytes() const{return sizeof(*this)+categories.capacity()*sizeof(Total)+pairs.capacity()*4;}
};
std::vector<Scratch> scratch(const Data& data,bool joint) {
    std::vector<Scratch> out;out.reserve(data.common.size());
    for(unsigned i=0;i<data.common.size();++i)out.push_back({std::vector<Total>(data.common[i].keys.size()+1),std::vector<std::uint32_t>(joint?data.joint[i].x.size():0)});
    return out;
}
void x_values(const Independent& image,unsigned b,Word* out) {
    const auto k=e::borrow(image.x,b);
    if(k.coded) {
        e::decode_stored(k,out);
        for(unsigned r=0;r<256;++r)out[r]=k.dictionary.min+image.x_relative[out[r]];
    } else e::decode_block(k,out);
}
Word x_constant(const Independent& image,unsigned b) {
    const auto k=e::borrow(image.x,b);
    return k.coded?k.dictionary.min+image.x_relative[k.d.base]:k.d.base;
}
Total reduce_x(const Independent& image,unsigned b,const Bits& mask,Word threshold) {
    const auto n=mask.count();if(!n||threshold>UINT32_MAX)return {};
    const auto k=e::borrow(image.x,b);
    if(!threshold) {
        const auto sum=k.coded?e::masked_coded_sum(k,mask.words,[&](Word code){return image.x_relative[code];}):e::masked_block_sum(k,mask.words);
        return {n,Word(sum)};
    }
    alignas(64)Word values[256];x_values(image,b,values);Total result;
    each(mask,[&](unsigned r){if(values[r]>=threshold){++result.count;result.sum+=values[r];}});
    return result;
}
void independent_group(const Independent& image,const Common& common,const Plan& plan,Word threshold,Scratch& work) {
    if(threshold>UINT32_MAX)return;
    const unsigned null_code=unsigned(common.keys.size());
    for(unsigned b=0;b<plan.selected.size();++b) {
        const auto& selected=plan.selected[b];if(!selected.count())continue;
        const auto& c=image.c.blocks[b];const auto& x=image.x.blocks[b];
        const auto normal=intersect(selected,common.has_c[b]),nulls=exclude(selected,common.has_c[b]);
        if(c.type==e::kind::constant) {
            if(plan.allowed[c.base]) {const auto r=reduce_x(image,b,normal,threshold);work.categories[c.base].count+=r.count;work.categories[c.base].sum+=r.sum;}
            if(plan.allowed[null_code]) {const auto r=reduce_x(image,b,nulls,threshold);work.categories[null_code].count+=r.count;work.categories[null_code].sum+=r.sum;}
            continue;
        }
        alignas(64)Word cs[256],xs[256];
        if(c.type==e::kind::runs&&(x.type==e::kind::runs||x.type==e::kind::constant)) {
            unsigned ce[256],xe[256],nc=0,nx=0;
            e::read_plane(image.c,b,0,c.count,cs);
            e::each_run(image.c,b,[&](unsigned rank,unsigned count){ce[rank]=(rank?ce[rank-1]:0)+count;++nc;});
            if(x.type==e::kind::constant){xs[0]=x_constant(image,b);xe[0]=256;nx=1;}
            else {
                const auto k=e::borrow(image.x,b);e::read_plane(k,0,x.count,xs);
                for(unsigned r=0;r<x.count;++r){xs[r]+=x.base;if(k.coded)xs[r]=k.dictionary.min+image.x_relative[xs[r]];}
                e::each_run(k,[&](unsigned rank,unsigned count){xe[rank]=(rank?xe[rank-1]:0)+count;++nx;});
            }
            unsigned ci=0,xi=0,first=0;
            while(ci<nc&&xi<nx) {
                const unsigned end=std::min(ce[ci],xe[xi]);const Word value=xs[xi];
                if(value>=threshold) {
                    const auto code=cs[ci]+c.base;
                    if(plan.allowed[code]){auto n=in_range(normal,first,end);work.categories[code].count+=n;work.categories[code].sum+=Word(n)*value;}
                    if(plan.allowed[null_code]){auto n=in_range(nulls,first,end);work.categories[null_code].count+=n;work.categories[null_code].sum+=Word(n)*value;}
                }
                first=end;if(ce[ci]==end)++ci;if(xe[xi]==end)++xi;
            }
            continue;
        }
        e::decode_block(image.c,b,cs);
        if(x.type==e::kind::constant) {
            const auto value=x_constant(image,b);if(value<threshold)continue;
            each(selected,[&](unsigned r){const auto code=common.has_c[b].test(r)?unsigned(cs[r]):null_code;if(plan.allowed[code]){++work.categories[code].count;work.categories[code].sum+=value;}});
        } else {
            x_values(image,b,xs);
            each(selected,[&](unsigned r){if(xs[r]<threshold)return;const auto code=common.has_c[b].test(r)?unsigned(cs[r]):null_code;if(plan.allowed[code]){++work.categories[code].count;work.categories[code].sum+=xs[r];}});
        }
    }
}
void joint_histogram(const Joint& image,const Plan& plan,Scratch& work) {
    for(unsigned b=0;b<plan.selected.size();++b) {
        const auto& mask=plan.selected[b];const auto count=mask.count();if(!count)continue;
        const auto& d=image.codes.blocks[b];
        if(d.type==e::kind::constant){work.pairs[d.base]+=count;continue;}
        alignas(64)Word codes[256];
        if(d.type==e::kind::runs) {
            e::read_plane(image.codes,b,0,d.count,codes);unsigned first=0;
            e::each_run(image.codes,b,[&](unsigned rank,unsigned n){work.pairs[d.base+codes[rank]]+=in_range(mask,first,first+n);first+=n;});
        } else {e::decode_block(image.codes,b,codes);each(mask,[&](unsigned r){++work.pairs[codes[r]];});}
    }
}
struct Result { std::optional<Output> output;Total nulls; };
Result group(const Data& data,const Query& query,const std::vector<Plan>& plan,std::vector<Scratch>& work,unsigned arm) {
    Result result;if(query.threshold>UINT32_MAX)return result;
    for(unsigned i=0;i<data.common.size();++i) {
        if(std::none_of(plan[i].allowed.begin(),plan[i].allowed.end(),[](auto a){return a!=0;}))continue;
        auto& w=work[i];std::fill(w.categories.begin(),w.categories.end(),Total{});
        const auto& common=data.common[i];
#if FRAME_CROSS_DOMAINS
        if(arm>=9)domain_group(data.native[i].category,data.domain_x[arm>=11][i],plan[i].selected,common.has_c,plan[i].allowed,query.threshold,w.categories,(arm%2)==0);
        else
#endif
        if(arm<=1)independent_group(data.independent[arm][i],common,plan[i],query.threshold,w);
        else if(arm==3||arm==5)native_group(data.native[i],plan[i].selected,common.has_c,plan[i].allowed,query.threshold,w.categories,arm==5);
        else {
            std::fill(w.pairs.begin(),w.pairs.end(),0);
            if(arm==2)joint_histogram(data.joint[i],plan[i],w);
            else native_histogram(data.joint[i].native,plan[i].selected,w.pairs,arm==6);
            const auto& j=data.joint[i];
            for(unsigned p=0;p<w.pairs.size();++p) if(w.pairs[p]&&j.x[p]>=query.threshold&&plan[i].allowed[j.category[p]]) {
                auto& out=w.categories[j.category[p]];out.count+=w.pairs[p];out.sum+=Word(w.pairs[p])*j.x[p];
            }
        }
        for(unsigned c=0;c<common.keys.size();++c)if(w.categories[c].count) {
            if(!result.output)result.output.emplace(713);
            result.output->modify_or_insert(common.keys[c]>>query.shift,{},[&](auto& bytes,bool){Total total;std::memcpy(&total,bytes.data(),16);total.count+=w.categories[c].count;total.sum+=w.categories[c].sum;std::memcpy(bytes.data(),&total,16);});
        }
        result.nulls.count+=w.categories.back().count;result.nulls.sum+=w.categories.back().sum;
    }
    return result;
}
Word joint_x(const Joint& image,const Plan& plan,Word threshold) {
    Word sum=0;
    for(unsigned b=0;b<plan.selected.size();++b) {
        const auto& mask=plan.selected[b];if(!mask.count())continue;
        const auto& d=image.codes.blocks[b];alignas(64)Word codes[256];
        if(d.type==e::kind::constant){const auto x=image.x[d.base];if(x>=threshold)sum+=Word(mask.count())*x;}
        else if(d.type==e::kind::runs){e::read_plane(image.codes,b,0,d.count,codes);unsigned first=0;e::each_run(image.codes,b,[&](unsigned rank,unsigned n){const auto x=image.x[d.base+codes[rank]];if(x>=threshold)sum+=Word(in_range(mask,first,first+n))*x;first+=n;});}
        else {e::decode_block(image.codes,b,codes);each(mask,[&](unsigned r){const auto x=image.x[codes[r]];if(x>=threshold)sum+=x;});}
    }
    return sum;
}
// A query-prepared forward map. Duplicate projected values are permitted by
// owner SUM; inverse-equality APIs are deliberately excluded. The frequency
// tag selects the unordered forward-map path, not a ranking/uniqueness proof.
struct Projection {
    std::vector<std::uint8_t> plane;
    e::dictionary_view dictionary{};
    e::code_sum sums;
    bool zero = true;
    Projection(std::span<const std::uint32_t> values,const e::column& codes,const Plan& plan,Word threshold,Word base = 0) {
        if(threshold>UINT32_MAX||std::none_of(plan.selected.begin(),plan.selected.end(),[](const auto& s){return s.count()!=0;}))return;
        std::vector<Word> weights;weights.reserve(values.size());Word maximum=0;
        for(auto relative:values){const Word x=base+relative;const Word weight=x>=threshold?x:0;weights.push_back(weight);maximum=std::max(maximum,weight);}
        if(!maximum)return;
        zero=false;const unsigned width=unsigned(std::bit_width(maximum));
        plane.resize(e::plane_bytes(width,weights.size()));e::pack(width,weights,plane.data());
        dictionary={0,unsigned(weights.size()),std::uint8_t(width),e::dictionary_order::frequency,
                    plane.data(),plane.size(),{codes.image,(Word{1}<<63)|threshold}};
    }
    Word sum(const Joint& image,const Plan& plan) {
        if(zero)return 0;Word total=0;
        for(unsigned b=0;b<plan.selected.size();++b)if(plan.selected[b].count()) {
            auto k=e::borrow(image.codes,b);k.coded=true;k.dictionary=dictionary;
            total+=Word(sums.masked(k,plan.selected[b].words));
        }
        return total;
    }
    std::size_t bytes() const{return sizeof(*this)+plane.capacity()+sums.values.capacity()*8;}
};
Word x_sum(const Data& data,const Query& query,const std::vector<Plan>& plan,unsigned arm,std::vector<Projection>& projections,std::vector<Projection>& independent_projections) {
    Word sum=0;if(query.threshold>UINT32_MAX)return 0;
    for(unsigned i=0;i<data.common.size();++i) {
#if FRAME_CROSS_DOMAINS
        if(arm>=9)sum+=domain_x_sum(data.domain_x[arm>=11][i],plan[i].selected,query.threshold);
        else
#endif
        if(arm<=1)for(unsigned b=0;b<plan[i].selected.size();++b) {
            const auto& mask=plan[i].selected[b];if(!mask.count())continue;
            const auto& image=data.independent[arm][i];auto k=e::borrow(image.x,b);
            if(arm==1&&query.threshold&&k.coded) {
                auto& projection=independent_projections[i];if(projection.zero)continue;
                k.dictionary=projection.dictionary;sum+=Word(projection.sums.masked(k,mask.words));
            } else sum+=reduce_x(image,b,mask,query.threshold).sum;
        }
        else if(arm==2)sum+=projections[i].sum(data.joint[i],plan[i]);
        else if(arm==7)sum+=joint_x(data.joint[i],plan[i],query.threshold);
        else if(arm==3||arm==5)sum+=native_x_sum(data.native[i],plan[i].selected,query.threshold);
        else sum+=native_projected_sum(data.joint[i].native,data.joint[i].x,plan[i].selected,query.threshold);
    }
    return sum;
}
using Normal=std::map<std::pair<bool,Word>,Total>;
Normal normal(const Result& result) {
    Normal out;if(result.nulls.count)out[{false,0}]=result.nulls;
    if(result.output)result.output->visit([&](Word key,const auto& bytes){Total t;std::memcpy(&t,bytes.data(),16);out[{true,key}]=t;});
    return out;
}
bool equal(const Normal& a,const Normal& b) {
    if(a.size()!=b.size())return false;auto x=a.begin(),y=b.begin();
    for(;x!=a.end();++x,++y)if(x->first!=y->first||x->second.count!=y->second.count||x->second.sum!=y->second.sum)return false;return true;
}
std::pair<Normal,Word> oracle(const Input& input,const Query& q) {
    Normal out;Word xsum=0;
    for(unsigned i=0;i<input.parts.size();++i)for(const auto& r:input.parts[i].rows) {
        if(!q.selection[i].any||!q.selection[i].contains(r.position)||(!q.coarse[i].all&&!std::binary_search(q.coarse[i].blocks.begin(),q.coarse[i].blocks.end(),r.position/q.coarse[i].grain))||!r.has_x||r.x<q.threshold)continue;
        xsum+=r.x;bool has_c=r.code>=0;Word key=has_c?input.parts[i].dictionary[r.code]:0;
        if(has_c?!q.accepts(key):!q.include_null)continue;
        auto& t=out[{has_c,has_c?key>>q.shift:0}];++t.count;t.sum+=r.x;
    }
    return {out,xsum};
}
Word digest(const Result& r) {
    Word sum=713^r.nulls.count^(r.nulls.sum*31337);
    if(r.output)r.output->visit([&](Word key,const auto& bytes){Total t;std::memcpy(&t,bytes.data(),16);sum+=(key*0x9e3779b97f4a7c15ULL)^(t.count*31337)^t.sum;});return sum;
}
volatile Word consumed;
const char* names[]={"independent-plain","independent-dictionary","shared-pair","native-pair-fused","native-shared-fused","native-pair-coalesced","native-shared-coalesced","shared-pair-decode-X","independent-plain-duplicate"
#if FRAME_CROSS_DOMAINS
    ,"native-lanes-flat","native-lanes-coalesced","native-global-FOR-flat","native-global-FOR-coalesced"
#endif
};
std::size_t state_bytes(const std::vector<Scratch>& work,const std::vector<Plan>& plan) {
    std::size_t n=0;for(const auto& w:work)n+=w.bytes();for(const auto& p:plan)n+=sizeof(p)+p.selected.capacity()*sizeof(Bits)+p.allowed.capacity();return n;
}
void material(const Data& d) {
    for(unsigned arm=0;arm<5;++arm) {
        std::size_t body=0,capacity=0,common=0,prepared=0,pairs=0;
        for(unsigned i=0;i<d.common.size();++i) {
            common+=d.common[i].bytes();const auto& j=d.joint[i];
            if(arm<=1){const auto& x=d.independent[arm][i];body+=x.c.total_bytes()+x.x.total_bytes();capacity+=sizeof(x)+column_capacity(x.c)+column_capacity(x.x);prepared+=x.x_relative.capacity()*4;}
            else if(arm==3){body+=d.native[i].category.small.size()+d.native[i].category.large.size()*2+d.native[i].x.size()*4;capacity+=sizeof(NativeRows)+d.native[i].bytes();}
            else {pairs+=j.x.size();const auto maps=j.category.capacity()*4+j.x.capacity()*4;body+=j.category.size()*4+j.x.size()*4;
                if(arm==2){body+=j.codes.total_bytes();capacity+=sizeof(e::column)+sizeof(std::vector<std::uint32_t>)*2+column_capacity(j.codes)+maps;}
                else {body+=j.native.small.size()+2*j.native.large.size();capacity+=sizeof(NativeCodes)+sizeof(std::vector<std::uint32_t>)*2+j.native.bytes()+maps;}}
        }
        std::cout<<"{\"type\":\"material\",\"arm\":"<<std::quoted(names[arm])<<",\"body_and_geometry_bytes\":"<<body<<",\"image_capacity_bytes\":"<<capacity<<",\"common_keys_and_masks_bytes\":"<<common<<",\"prepared_x_dictionary_bytes\":"<<prepared<<",\"joint_entries\":"<<pairs<<",\"construction_component_ns_single_observation\":"<<d.construction_ns[arm]<<"}\n";
    }
}
#if FRAME_CROSS_DOMAINS
void domain_material(const Data& d) {
    for(unsigned kind=0;kind<2;++kind) {
        std::size_t body=0,capacity=0,common=0;
        for(unsigned i=0;i<d.common.size();++i) {
            const auto& x=d.domain_x[kind][i];const auto& c=d.native[i].category;
            body+=c.small.size()+c.large.size()*2+(x.bytes8.size()+x.bytes16.size()*2+x.bytes32.size()*4);
            capacity+=sizeof(NativeCodes)+c.bytes()+x.bytes();common+=d.common[i].bytes();
            std::cout<<"{\"type\":\"native-domain\",\"arm\":"<<std::quoted(names[kind?11:9])<<",\"part\":"<<i<<",\"minimum\":"<<x.minimum<<",\"maximum\":"<<x.maximum<<",\"base\":"<<x.base<<",\"X_lane_bits\":"<<x.lane_bits<<",\"X_capacity_bytes\":"<<x.bytes()<<"}\n";
        }
        std::cout<<"{\"type\":\"material\",\"arm\":"<<std::quoted(names[kind?11:9])<<",\"body_and_geometry_bytes\":"<<body<<",\"image_capacity_bytes\":"<<capacity<<",\"common_keys_and_masks_bytes\":"<<common<<",\"prepared_x_dictionary_bytes\":0,\"joint_entries\":0,\"construction_component_ns_single_observation\":"<<d.domain_construction_ns[kind]<<"}\n";
    }
}
#endif
struct CountMemory:std::pmr::memory_resource {
    std::size_t live=0,peak=0;
    void* do_allocate(std::size_t n,std::size_t a)override{auto* p=std::pmr::new_delete_resource()->allocate(n,a);live+=n;peak=std::max(peak,live);return p;}
    void do_deallocate(void* p,std::size_t n,std::size_t a)override{live-=n;std::pmr::new_delete_resource()->deallocate(p,n,a);}
    bool do_is_equal(const std::pmr::memory_resource& o)const noexcept override{return this==&o;}
};
void repair(const Data& data,unsigned trials) {
    for(unsigned part=0;part<data.common.size();++part) {
        const auto& c=data.common[part];if(c.keys.size()<2)continue;
        const auto& j=data.joint[part];std::vector<unsigned> positions,candidates;
        for(unsigned b=0;b<c.live.size();++b)each(intersect(c.live[b],c.has_c[b]),[&](unsigned r){candidates.push_back(b*256+r);});
        const unsigned updates=unsigned(std::min<std::size_t>(64,candidates.size()));
        for(unsigned k=0;k<updates;++k)positions.push_back(candidates[k*candidates.size()/updates]);
        if(positions.empty())continue;
        CountMemory memory;std::pmr::unordered_map<Word,unsigned> reverse(&memory);
        const auto prep=Clock::now();reverse.reserve(j.x.size()+64);
        for(unsigned p=0;p<j.x.size();++p)reverse.emplace((Word(j.category[p])<<32)|j.x[p],p);
        const auto prep_ns=elapsed(prep);const auto index_bytes=memory.live;
        struct Stats { std::size_t new_pairs{},body{},buffer{},map_capacity{}; };
        const auto once=[&]<bool Verify>(unsigned arm) {
            auto code=decode(arm?j.codes:data.independent[0][part].c);
            auto cats=arm?j.category:std::vector<std::uint32_t>{};auto xs=arm?j.x:std::vector<std::uint32_t>{};
            std::vector<Word> inserted;
            for(auto row:positions) {
                const auto old=arm?j.category[code[row]]:unsigned(code[row]);const unsigned next=(old+1)%unsigned(c.keys.size());
                if(!arm)code[row]=next;
                else {
                    const auto x=j.x[code[row]];const Word key=(Word(next)<<32)|x;
                    const auto [at,fresh]=reverse.try_emplace(key,unsigned(xs.size()));
                    if(fresh){cats.push_back(next);xs.push_back(x);inserted.push_back(key);}
                    code[row]=at->second;
                }
            }
            auto next=e::encode(code,{.bind_decoders=false});
            consumed=next.payload.size()+next.blocks.size()+xs.size();
            const Stats stats{inserted.size(),next.total_bytes(),column_capacity(next),(cats.capacity()+xs.capacity())*4};
            if constexpr(Verify) {
                auto got=decode(next);const auto old_codes=decode(arm?j.codes:data.independent[0][part].c);
                for(unsigned r=0;r<got.size();++r) {
                    if(!c.live[r/256].test(r%256))continue;
                    const auto old_c=arm?j.category[old_codes[r]]:unsigned(old_codes[r]);
                    const auto want=std::binary_search(positions.begin(),positions.end(),r)?(old_c+1)%unsigned(c.keys.size()):old_c;
                    need((arm?cats[got[r]]:unsigned(got[r]))==want,"conditioning edit changed wrong C value");
                    if(arm)need(xs[got[r]]==j.x[old_codes[r]],"conditioning edit changed logical X");
                }
            }
            for(auto key:inserted)reverse.erase(key);
            return stats; // All private vectors/new body retire before timed call returns.
        };
        std::array<std::vector<double>,2> times;std::array<Stats,2> stats;
        for(unsigned arm=0;arm<2;++arm)(void)once.template operator()<true>(arm);
        for(unsigned t=0;t<std::max(1u,trials);++t)for(unsigned step=0;step<2;++step) {
            const auto arm=(t+step)%2;const auto begin=Clock::now();
            stats[arm]=once.template operator()<false>(arm);times[arm].push_back(elapsed(begin));
        }
        for(unsigned arm=0;arm<2;++arm) {
            (void)once.template operator()<true>(arm);
            std::cout<<"{\"type\":\"repair-construction\",\"part\":"<<part<<",\"arm\":"<<std::quoted(arm?"shared-append-map":"independent-C-only")<<",\"edits\":"<<positions.size()<<",\"new_pair_entries\":"<<stats[arm].new_pairs<<",\"new_code_body_geometry_bytes\":"<<stats[arm].body<<",\"new_code_buffer_capacity_bytes\":"<<stats[arm].buffer<<",\"new_projected_map_capacity_bytes\":"<<stats[arm].map_capacity<<",\"optional_reverse_index_resource_bytes\":"<<(arm?index_bytes:0)<<",\"optional_reverse_index_peak_resource_bytes\":"<<(arm?memory.peak:0)<<",\"reverse_index_prepare_ns_single_observation\":"<<(arm?prep_ns:0)<<",\"whole_part_repair_and_retire_ns\":[";
            for(unsigned t=0;t<times[arm].size();++t)std::cout<<(t?",":"")<<times[arm][t];std::cout<<"]}\n";
        }
    }
}
} // namespace
int main(int argc,char** argv) {
    try {
        const unsigned trials=argc>1?unsigned(std::stoul(argv[1])):0;
        need(argc<=2&&trials<=100,"usage: frame_execution_columns [trials<=100]");
        const auto input=read_input(std::cin);const auto data=build(input);material(data);
#if FRAME_CROSS_DOMAINS
        domain_material(data);
#endif
        unsigned checks=0;
        for(const auto& query:input.queries) {
            const auto begun=Clock::now();const auto plan=prepare(data,query);const auto prepare_ns=elapsed(begun);
            const auto [wanted,wanted_x]=oracle(input,query);
            const auto projection_start=Clock::now();std::vector<Projection> projections;projections.reserve(data.joint.size());
            for(unsigned i=0;i<data.joint.size();++i)projections.emplace_back(data.joint[i].x,data.joint[i].codes,plan[i],query.threshold);
            const auto projection_prepare_ns=elapsed(projection_start);
            const auto independent_projection_start=Clock::now();std::vector<Projection> independent_projections;independent_projections.reserve(data.joint.size());
            for(unsigned i=0;i<data.joint.size();++i) {
                const auto& image=data.independent[1][i];
                const Word base=image.x.parts.empty()?0:image.x.parts[0].min;
                independent_projections.emplace_back(image.x_relative,image.x,plan[i],query.threshold?query.threshold:Word(UINT32_MAX)+1,base);
            }
            const auto independent_projection_prepare_ns=elapsed(independent_projection_start);
            const auto independent_projection_first=Clock::now();
            need(x_sum(data,query,plan,1,projections,independent_projections)==wanted_x,"independent owner-projected SUM differs");
            const auto independent_projection_first_call_ns=elapsed(independent_projection_first);
            const auto projection_first=Clock::now();
            need(x_sum(data,query,plan,2,projections,independent_projections)==wanted_x,"owner-projected SUM differs");
            const auto projection_first_call_ns=elapsed(projection_first);
            std::array<std::vector<Scratch>,arm_count> states;
            std::array<std::array<std::vector<double>,2>,arm_count> times;
            std::array<std::array<unsigned,2>,arm_count> repeats{};
            std::array<Word,arm_count> digests{};
            for(unsigned arm=0;arm<arm_count;++arm) {
                const unsigned actual=arm==8?0:arm,group_arm=actual==7?2:actual;if(arm<7||arm>=9)states[arm]=scratch(data,actual==2||actual==4||actual==6);
                auto got=group(data,query,plan,states[group_arm],group_arm);
                need(equal(normal(got),wanted),"full grouped output differs from logical rows");
                need(x_sum(data,query,plan,actual,projections,independent_projections)==wanted_x,"isolated X differs from logical rows");
                digests[arm]=digest(got);checks+=2;
                for(unsigned op=0;op<2;++op) {
                    const auto start=Clock::now();
                    if(!op){auto value=group(data,query,plan,states[group_arm],group_arm);consumed=digest(value);}else consumed=x_sum(data,query,plan,actual,projections,independent_projections);
                    repeats[arm][op]=unsigned(std::clamp(1000000.0/std::max(1.0,elapsed(start)),1.0,128.0));
                }
            }
            // Equal batching also makes duplicate arms identical in repetition
            // count; the fastest calibration sets the common warm sample size.
            for(unsigned op=0;op<2;++op) {
                unsigned common=1;for(const auto& r:repeats)common=std::max(common,r[op]);
                for(auto& r:repeats)r[op]=common;
            }
            for(unsigned t=0;t<trials;++t)for(unsigned step=0;step<arm_count;++step) {
                const unsigned position=t/arm_count%2?arm_count-1-step:step;
                const unsigned treatment=position==0?0:position%2?(position+1)/2:arm_count-position/2;
                const unsigned arm=(treatment+t%arm_count)%arm_count,actual=arm==8?0:arm,group_arm=actual==7?2:actual;
                for(unsigned op=0;op<2;++op) {
                    const auto start=Clock::now();
                    for(unsigned k=0;k<repeats[arm][op];++k) {
                        if(!op){auto value=group(data,query,plan,states[group_arm],group_arm);consumed=digest(value);}
                        else consumed=x_sum(data,query,plan,actual,projections,independent_projections);
                    }
                    times[arm][op].push_back(elapsed(start)/repeats[arm][op]);
                }
            }
            for(unsigned arm=0;arm<arm_count;++arm) {
                const unsigned group_arm=(arm==7?2:arm==8?0:arm);
                auto got=group(data,query,plan,states[group_arm],group_arm);
                need(equal(normal(got),wanted)&&digest(got)==digests[arm],"timed work changed original logical outputs");
                for(unsigned op=0;op<2;++op) {
                    std::cout<<"{\"type\":\"query\",\"query\":"<<std::quoted(query.id)<<",\"arm\":"<<std::quoted(names[arm])<<",\"operation\":"<<std::quoted(op?"isolated-X-predicated-SUM":"grouped-COUNT-SUM")<<",\"plan_prepare_ns_single_observation\":"<<prepare_ns<<",\"plan_and_workspace_capacity_bytes\":"<<state_bytes(states[group_arm],plan)<<",\"owner_projection_prepare_ns_single_observation\":"<<(op?(arm==2?projection_prepare_ns:arm==1&&query.threshold?independent_projection_prepare_ns:0):0)<<",\"owner_projection_first_call_ns_single_observation\":"<<(op?(arm==2?projection_first_call_ns:arm==1&&query.threshold?independent_projection_first_call_ns:0):0)<<",\"owner_projection_extra_capacity_bytes\":"<<(op&&(arm==2||(arm==1&&query.threshold))?([&]{std::size_t n=0;for(const auto& p:arm==2?projections:independent_projections)n+=p.bytes();return n;})():0)<<",\"groups\":"<<wanted.size()<<",\"output_hash_owner_bytes\":"<<(got.output?got.output->bytes_used():0)<<",\"repeats_per_sample\":"<<repeats[arm][op]<<",\"whole_query_warm_ns\":[";
                    for(unsigned t=0;t<times[arm][op].size();++t)std::cout<<(t?",":"")<<times[arm][op][t];std::cout<<"]}\n";
                }
            }
        }
        repair(data,trials?6:1);
        std::cout<<"{\"type\":\"checked\",\"comparisons\":"<<checks<<",\"arm_order\":\"Williams first-order carryover balance at 2*arm_count trials; equal batching; exact duplicate independent arm\",\"timing_scope\":\"warmed complete queries; output create/consume/retire included, preparation separate; not point latency or interleaved throughput\"}\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
