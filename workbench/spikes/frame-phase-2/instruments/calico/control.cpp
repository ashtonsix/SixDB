#include "control.h"
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <type_traits>

namespace calico_control {
namespace {
template<class T> void append(std::vector<range>& out, const std::vector<T>& v, const char* role) {
    if (!v.empty()) out.push_back({v.data(), v.size()*sizeof(T), v.capacity()*sizeof(T), role});
}
void main_ranges(std::vector<range>& out, const frame::RawStream& s, uint32_t field) {
    if (s.symbol_size()) out.push_back({s.symbol_data(), s.symbol_size(), s.symbols.capacity(), "main.symbols"});
    if (s.body_size()) out.push_back({s.body_data(), s.body_size(), s.body.capacity(), "main.body"});
    if (field < s.patches.size()) append(out, s.patches[field], "field.patches");
    if (!s.patches.empty()) out.push_back({s.patches.data(), s.patches.size()*sizeof(s.patches[0]), s.patches.capacity()*sizeof(s.patches[0]), "patch.headers"});
}
} // namespace

signed_image::signed_image(frame::Type type, std::span<const std::span<const int64_t>> fields)
    : type_(std::move(type)), codec_(type_) {
    if (!codec_.valid() || fields.size()!=codec_.nfields() || fields.empty())
        throw std::invalid_argument("RawCodec Type/field count");
    const auto n=fields[0].size();
    if (n%codec_.z()) throw std::invalid_argument("RawCodec requires complete frames");
    std::vector<const int64_t*> pointers;
    for (const auto field:fields) {
        if (field.size()!=n) throw std::invalid_argument("RawCodec unequal row domains");
        pointers.push_back(field.data());
    }
    if (!codec_.encode(pointers.data(), n/codec_.z(), stream_) || !codec_.validate(stream_))
        throw std::invalid_argument("RawCodec encode/validation refused");
    plans_.resize(fields.size());
    for (uint32_t i=0;i<fields.size();++i)
        if (!engine::raw_farray_sum_bind(codec_,i,plans_[i])) throw std::invalid_argument("RawCodec SUM bind");
}
bool signed_image::sum(uint32_t field, i128& out) const {
    return field<plans_.size() && engine::raw_farray_sum_trusted(plans_[field],stream_,out);
}
bool signed_image::range_mask(uint32_t field,int64_t lo,int64_t hi,std::vector<uint8_t>& out)const {
    return codec_.range_mask_trusted(stream_,field,lo,hi,out);
}
std::vector<range> signed_image::sum_ranges(uint32_t field)const {
    if(field>=plans_.size())throw std::invalid_argument("field");
    std::vector<range> out;
    out.push_back({this,sizeof(*this),sizeof(*this),"owner.headers"});
    main_ranges(out,stream_,field);
    out.push_back({&plans_[field],sizeof(plans_[field]),sizeof(plans_[field]),"sum.plan-header"});
    append(out,plans_[field].sym,"sum.symbol-plan");
    return out;
}

unsigned_image::unsigned_image(frame::Type type,std::span<const uint8_t> widths,
                              std::span<const std::span<const uint64_t>> fields)
    :type_(std::move(type)),ids_(widths.size()),widths_(widths.begin(),widths.end()),
     plan_(type_),binding_{&type_,ids_.data(),widths_.data(),static_cast<uint16_t>(widths.size()),0,&plan_},view_(binding_) {
    if(widths.empty()||widths.size()!=fields.size()||widths.size()>=engine::kTypeboundContainerBit)
        throw std::invalid_argument("Type-bound fields");
    std::iota(ids_.begin(),ids_.end(),uint16_t{1});
    if(!binding_.valid())throw std::invalid_argument("Type-bound binding");
    struct column{uint16_t id;uint8_t width;};
    const auto n=fields[0].size();
    std::vector<column> columns;
    std::vector<std::vector<uint8_t>> wire(fields.size());
    std::vector<const uint8_t*> pointers;
    for(std::size_t f=0;f<fields.size();++f){
        if(fields[f].size()!=n)throw std::invalid_argument("Type-bound unequal row domains");
        columns.push_back({ids_[f],widths_[f]});
        // The store API requires non-null input pointers even for zero rows.
        wire[f].resize(std::max<std::size_t>(1,n*widths_[f]));
        for(std::size_t i=0;i<n;++i){
            const auto value=fields[f][i];
            if(widths_[f]<8 && value>=(uint64_t{1}<<(8*widths_[f])))throw std::invalid_argument("unsigned value outside declared width");
            for(unsigned b=0;b<widths_[f];++b)wire[f][i*widths_[f]+b]=static_cast<uint8_t>(value>>(8*b));
        }
        pointers.push_back(wire[f].data());
    }
    if(engine::typebound_leaf_store(binding_,columns.data(),columns.size(),pointers.data(),n,meta_,body_))
        throw std::invalid_argument("Type-bound exact-cover store refused");
    // Re-prove the actual writer-produced afterimage once, then pin the same
    // immutable bytes for the trusted borrowed query view. No cached answers.
    engine::typebound_leaf_view checked(binding_);
    if(engine::typebound_leaf_load(binding_,meta_.data(),meta_.size(),body_.data(),body_.size(),n,checked) ||
       engine::typebound_leaf_load_borrowed_trusted(binding_,meta_.data(),meta_.size(),body_.data(),body_.size(),n,view_))
        throw std::invalid_argument("Type-bound image admission refused");
}
bool unsigned_image::sum(uint32_t field,u128& out)const{return view_.sum(field,out);}
bool unsigned_image::range_mask(uint32_t field,uint64_t lo,uint64_t hi,std::vector<uint8_t>& out)const{return view_.range_mask(field,lo,hi,out);}
std::vector<range> unsigned_image::sum_ranges(uint32_t field)const {
    if(field>=widths_.size())throw std::invalid_argument("field");
    std::vector<range> out{{this,sizeof(*this),sizeof(*this),"owner.headers"}};
    // A whole immutable container is a sufficient conditioning span, including
    // evidence and literal tail. It is not a claim of minimal SUM traffic.
    append(out,meta_,"container.metadata");append(out,body_,"container.body-evidence-main-tail");
    append(out,widths_,"field.widths");
    append(out,view_.field_byte_at,"field.byte-offsets");
    if(!view_.main.patches.empty())out.push_back({view_.main.patches.data(),view_.main.patches.size()*sizeof(view_.main.patches[0]),view_.main.patches.capacity()*sizeof(view_.main.patches[0]),"patch.headers"});
    out.push_back({&plan_.sum_plan[field],sizeof(plan_.sum_plan[field]),sizeof(plan_.sum_plan[field]),"sum.plan-header"});
    append(out,plan_.sum_plan[field].sym,"sum.symbol-plan");
    return out;
}
std::vector<range> unsigned_image::retained_ranges()const {
    std::vector<range> out{{this,sizeof(*this),sizeof(*this),"owner.headers"}};
    append(out,type_.z,"type.grains");append(out,type_.w,"type.field-counts");
    append(out,type_.uni_lo,"type.universe-lo");append(out,type_.uni_hi,"type.universe-hi");
    append(out,type_.perm_id,"type.permutations");append(out,type_.boxes,"type.boxes");
    append(out,ids_,"binding.field-ids");append(out,widths_,"binding.widths");
    append(out,meta_,"container.metadata");append(out,body_,"container.body-evidence-main-tail");
    append(out,plan_.sum_plan,"sum.plan-headers");
    for(const auto& p:plan_.sum_plan)append(out,p.sym,"sum.symbol-plan");
    append(out,view_.main.symbols,"main.owned-symbols");append(out,view_.main.body,"main.owned-body");
    append(out,view_.main.checkpoint,"main.checkpoints");append(out,view_.main.offset,"main.offsets");
    append(out,view_.main.patches,"patch.headers");
    for(const auto& p:view_.main.patches)append(out,p,"field.patches");
    append(out,view_.exact_min,"evidence.owned-min");append(out,view_.exact_max,"evidence.owned-max");
    append(out,view_.tail,"tail.headers");for(const auto& t:view_.tail)append(out,t,"tail.owned-values");
    append(out,view_.field_byte_at,"field.byte-offsets");
    append(out,view_.frame_sum_cache,"cache.frame-sum-headers");
    for(const auto& c:view_.frame_sum_cache)append(out,c,"cache.frame-sums");
    append(out,view_.window_min_cache,"cache.window-min");append(out,view_.window_max_cache,"cache.window-max");
    append(out,view_.window_extreme_ready,"cache.window-extreme-ready");
    // RawCodec's private vector headers are in owner.headers. Their element
    // bytes are a separate source-derived charge; their capacities are opaque.
    return out;
}
} // namespace calico_control
