#include "corpus_sum.h"
#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>

namespace calico_control {
const char* refusal_name(sum_refusal r) {
    switch(r) {
    case sum_refusal::none:return "none";
    case sum_refusal::field_width:return "field-width";
    case sum_refusal::value_width:return "value-outside-field-width";
    case sum_refusal::program:return "program-not-admitted";
    case sum_refusal::grain:return "grain-not-admitted";
    case sum_refusal::layout_width:return "layout-residual-width";
    case sum_refusal::palette:return "more-than-256-exact-boxes";
    case sum_refusal::size:return "container-size";
    default:return "upstream-refused";
    }
}
prepared_sum::prepared_sum(std::unique_ptr<unsigned_image> image, sum_preparation p)
    :image_(std::move(image)),preparation_(p) {
    if(!image_)throw std::invalid_argument("prepared SUM requires its actual image");
}
bool prepared_sum::sum(u128& out)const { return image_->sum(0,out); }
std::vector<range> prepared_sum::sum_ranges()const {
    auto out=image_->sum_ranges(0);
    out.push_back({this,sizeof(*this),sizeof(*this),"prepared.headers"});return out;
}
std::vector<range> prepared_sum::retained_ranges()const {
    auto out=image_->retained_ranges();
    out.push_back({this,sizeof(*this),sizeof(*this),"prepared.headers"});return out;
}
sum_candidate prepare_sum(std::span<const uint64_t> values,uint8_t width,sum_program program) {
    auto refuse=[](sum_refusal r,const char* detail) {
        return sum_candidate{nullptr,r,detail};
    };
    if(!(width==1||width==2||width==4||width==8))
        return refuse(sum_refusal::field_width,"Actual unsigned Engine widths are 1,2,4,8 bytes");
    uint32_t z=0;
    switch(program.layout) {
    case frame::LayoutId::lane256:case frame::LayoutId::byteplane:z=256;break;
    case frame::LayoutId::fastlanes:z=1024;break;
    default:return refuse(sum_refusal::program,"This control exposes lane256, byteplane and FastLanes only");
    }
    if(program.frame_rows!=z)
        return refuse(sum_refusal::grain,"This control admits lane256/byteplane at 256 and FastLanes at 1024");
    const auto n=values.size(),complete=n/z;
    // Conservative bound for these grains: eight payload bytes/row, one
    // symbol plus at most two eight-byte evidence values per source frame.
    // The actual load API takes uint32 byte lengths. Never truncate them.
    if(complete>UINT32_MAX||u128(n)*8+u128(complete+1)*17>UINT32_MAX)
        return refuse(sum_refusal::size,"Actual container byte lengths must fit uint32");
    if(width<8) {
        const auto limit=(uint64_t{1}<<(8*width))-1;
        for(auto value:values)if(value>limit)
            return refuse(sum_refusal::value_width,"Source value exceeds its declared unsigned physical width");
    }
    std::vector<frame::FieldBox> boxes;
    boxes.reserve(std::min<std::size_t>(complete,256));
    uint8_t max_bits=0;
    for(std::size_t f=0;f<complete;++f) {
        auto lo=INT64_MAX,hi=INT64_MIN;
        for(std::size_t i=f*z;i<(f+1)*z;++i) {
            const auto encoded=engine::typebound_detail::order_encode(values[i],width);
            lo=std::min(lo,encoded);hi=std::max(hi,encoded);
        }
        // A signed working-domain span can reach UINT64_MAX, never 2^64.
        const auto delta=uint64_t(i128(hi)-i128(lo));
        const auto bits=uint8_t(std::bit_width(delta));
        if(program.layout==frame::LayoutId::byteplane&&bits>16)
            return refuse(sum_refusal::layout_width,"A complete source frame needs more than byteplane's 16 residual bits");
        max_bits=std::max(max_bits,bits);
        const frame::FieldBox box{lo,bits};
        if(std::none_of(boxes.begin(),boxes.end(),[&](const auto& b){return b.offset==box.offset&&b.range_log2==box.range_log2;})) {
            if(boxes.size()==256)
                return refuse(sum_refusal::palette,"No exact source palette larger than 256 is available; boxes are not merged");
            boxes.push_back(box);
        }
    }
    const auto distinct=uint16_t(boxes.size());
    // Empty/literal-only images need a valid Type but no fitted main box.
    if(boxes.empty())boxes.push_back({0,0});
    const uint16_t symbols=boxes.size()==1?1:boxes.size()<=16?16:256;
    frame::TypeBuilder builder(symbols,program.layout,frame::Align::word8);
    builder.array(z,1).universe(width==8?INT64_MIN:0,
        width==8?INT64_MAX:int64_t((uint64_t{1}<<(8*width))-1));
    for(uint16_t s=0;s<symbols;++s)
        builder.symbol(0,{boxes[s<boxes.size()?s:0]}); // duplicate padding, no merged boxes
    auto type=builder.build();
    sum_preparation p{program};
    p.rows=n;p.complete_frames=complete;p.tail_rows=n%z;
    p.distinct_boxes=distinct;p.symbols=symbols;p.physical_width=width;p.max_residual_bits=max_bits;
    p.type_wire_bytes=frame::serialize(type).size();
    p.fit_box_capacity_bytes=boxes.capacity()*sizeof(frame::FieldBox);
    p.wire_staging_bytes=std::max<std::size_t>(1,n*width);
    p.private_codec_element_bytes=std::size_t(symbols)*2*sizeof(std::size_t);
    if(symbols==16)p.private_codec_element_bytes+=16+2*16*sizeof(int64_t);
    std::array<uint8_t,1> widths{width};
    std::array<std::span<const uint64_t>,1> fields{values};
    try {
        auto image=std::make_unique<unsigned_image>(std::move(type),widths,fields);
        return {std::make_unique<prepared_sum>(std::move(image),p),sum_refusal::none,{}};
    }catch(const std::invalid_argument& e) {
        return {nullptr,sum_refusal::upstream,e.what()};
    }
}
} // namespace calico_control
