#include "corpus_sum.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace cc=calico_control;
using u64=uint64_t;using u128=cc::u128;
namespace fs=std::filesystem;
static u64 checks=0,accepted=0,refused=0,sum_calls=0,synthetic=0,corpus_accepted=0,corpus_refused=0;
static std::string context;
void need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(context+": "+why);}
std::string decimal(u128 n){if(!n)return "0";std::string s;while(n){s.push_back(char('0'+n%10));n/=10;}std::reverse(s.begin(),s.end());return s;}
u64 maximum(unsigned width){return width==8?UINT64_MAX:(u64{1}<<(8*width))-1;}
u128 oracle(std::span<const u64> values){u128 sum=0;for(auto value:values)sum+=u128(value);return sum;}
const char* name(frame::LayoutId l){return l==frame::LayoutId::lane256?"lane256":l==frame::LayoutId::byteplane?"byteplane":"fastlanes";}
const std::array<cc::sum_program,3> programs{{{frame::LayoutId::lane256,256},{frame::LayoutId::byteplane,256},{frame::LayoutId::fastlanes,1024}}};
bool covers(const std::vector<cc::range>& ranges,const void* p,std::size_t n){
 auto at=reinterpret_cast<uintptr_t>(p);if(!n)return true;
 for(const auto& r:ranges){auto begin=reinterpret_cast<uintptr_t>(r.data);if(at>=begin&&at-begin<=r.bytes&&n<=r.bytes-(at-begin))return true;}return false;
}
void validate(cc::sum_candidate& candidate,std::span<const u64> values,cc::sum_program p){
 need(bool(candidate)&&candidate.refusal==cc::sum_refusal::none,"applicable candidate owns its image");++accepted;
 auto& owner=*candidate.owner;const auto expected=oracle(values);
 const auto& prep=owner.preparation();
 need(prep.rows==values.size()&&prep.complete_frames==values.size()/p.frame_rows&&prep.tail_rows==values.size()%p.frame_rows,"honest natural complete/tail grain");
 need(owner.image().view().z==p.frame_rows,"actual source grain");
 need(owner.image().binding().plan==owner.image().view().plan,"actual retained bound plan");
 for(unsigned repeat=0;repeat<5;++repeat){u128 got=0;need(owner.sum(got)&&got==expected,"actual exact original-value SUM");++sum_calls;}
 need(owner.image().view().frame_sum_cache.empty(),"no prepared aggregate answers");
 for(std::size_t i=0;i<values.size();i+=std::max<std::size_t>(1,values.size()/17)){
  u64 got=0;need(owner.image().view().point(0,i,got)&&got==values[i],"source working map round trip");
 }
 for(const auto& patch:owner.image().view().main.patches)need(patch.empty(),"actual exact-cover noescape image");
 const auto retained=owner.retained_ranges(),query=owner.sum_ranges();
 for(const auto& r:retained)need(r.data&&r.bytes&&r.capacity>=r.bytes,"retained extent/capacity");
 for(std::size_t i=0;i<retained.size();++i)for(std::size_t j=i+1;j<retained.size();++j){
  auto a=reinterpret_cast<uintptr_t>(retained[i].data),b=reinterpret_cast<uintptr_t>(retained[j].data);
  need(a+retained[i].bytes<=b||b+retained[j].bytes<=a,"retained charges do not overlap");
 }
 for(const auto& r:query)need(covers(retained,r.data,r.bytes),"query ranges belong to charged retained owner");
}
void check_refusal(std::span<const u64> values,uint8_t width,cc::sum_program p,cc::sum_refusal why){
 auto candidate=cc::prepare_sum(values,width,p);need(!candidate&&candidate.refusal==why&&!candidate.detail.empty(),"explicit candidate refusal");++refused;
}
void fixtures(){
 for(auto p:programs)for(uint8_t width:{1,2,4,8})for(unsigned pattern=0;pattern<3;++pattern)
 for(std::size_t n:{std::size_t{0},std::size_t{1},std::size_t(p.frame_rows-1),std::size_t(p.frame_rows),std::size_t(p.frame_rows+1),std::size_t(3*p.frame_rows+17),std::size_t(8*p.frame_rows)}){
  context=std::string(name(p.layout))+"/width"+std::to_string(width)+"/pattern"+std::to_string(pattern)+"/rows"+std::to_string(n);++synthetic;
  std::vector<u64> values(n);const auto limit=maximum(width);
  for(std::size_t i=0;i<n;++i){
   if(pattern==0)values[i]=limit;
   else if(pattern==1)values[i]=i%2?limit:0;
   else {const u64 base=width==8?(u64{1}<<63):width==4?1'000'000:0;values[i]=base+((i/p.frame_rows*1024+(i%p.frame_rows&((u64{1}<<((i/p.frame_rows%4)*4))-1)))&std::min<u64>(limit,65535));}
  }
  if(p.layout==frame::LayoutId::byteplane&&pattern==1&&width>2&&n>=p.frame_rows){check_refusal(values,width,p,cc::sum_refusal::layout_width);continue;}
  auto c=cc::prepare_sum(values,width,p);validate(c,values,p);
  const auto expected=oracle(values);std::fill(values.begin(),values.end(),0);
  u128 got=0;need(c.owner->sum(got)&&got==expected,"owner does not borrow original input");++sum_calls;
 }
 for(auto p:programs)for(std::size_t n:{std::size_t{1},std::size_t(p.frame_rows-1),std::size_t(p.frame_rows+1),std::size_t(2*p.frame_rows+17)}){
  context=std::string(name(p.layout))+"/u64-boundaries/rows"+std::to_string(n);++synthetic;
  const std::array<u64,6> edges{0,UINT64_MAX,(u64{1}<<63)-1,u64{1}<<63,1,UINT64_MAX-1};
  std::vector<u64> values(n);for(std::size_t i=0;i<n;++i)values[i]=edges[i%edges.size()];
  if(p.layout==frame::LayoutId::byteplane&&n>=p.frame_rows)check_refusal(values,8,p,cc::sum_refusal::layout_width);
  else {auto c=cc::prepare_sum(values,8,p);validate(c,values,p);}
  context+="/near-max-nonzero-base";++synthetic;
  for(std::size_t i=0;i<n;++i)values[i]=UINT64_MAX-1023+(i%256);
  auto c=cc::prepare_sum(values,8,p);validate(c,values,p);
  if(n>=p.frame_rows)need(c.owner->image().binding().type->boxes[0].offset==INT64_MAX-1023,"actual nonzero signed working base");
 }
 const auto p=programs[0];
 for(unsigned count:{1,2,16,17,256,257,300}){
  context="palette/frames"+std::to_string(count);++synthetic;
  std::vector<u64> values(std::size_t(count)*p.frame_rows);
  for(std::size_t i=0;i<values.size();++i)values[i]=100+1000*(count==300?0:i/p.frame_rows);
  if(count==257){check_refusal(values,8,p,cc::sum_refusal::palette);continue;}
  auto c=cc::prepare_sum(values,8,p);validate(c,values,p);
  const auto distinct=count==300?1:count;const auto k=distinct==1?1:distinct<=16?16:256;
  need(c.owner->preparation().symbols==k&&c.owner->preparation().distinct_boxes==distinct,"actual source palette boundary and exact deduplication");
 }
 context="program-domain-refusals";const std::array<u64,1> one{256};
 check_refusal(one,3,p,cc::sum_refusal::field_width);check_refusal(one,1,p,cc::sum_refusal::value_width);
 check_refusal(one,8,{frame::LayoutId::bytepack,256},cc::sum_refusal::program);
 check_refusal(one,8,{frame::LayoutId::fastlanes,256},cc::sum_refusal::grain);
 check_refusal(one,8,{frame::LayoutId::lane256,1024},cc::sum_refusal::grain);
 check_refusal(one,8,{frame::LayoutId::byteplane,8},cc::sum_refusal::grain);
 for(uint8_t w:{2,4}){std::array<u64,1> too_wide{maximum(w)+1};check_refusal(too_wide,w,p,cc::sum_refusal::value_width);}
}
void write(const fs::path& path,std::span<const uint8_t> bytes){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());need(bool(out),"write actual encoded artifact");}
void corpus(const fs::path& inputs,const fs::path& output){
 for(const char* column:{"OS","UserID","URLHash"}){
  std::ifstream in(inputs/(std::string(column)+".u64"),std::ios::binary|std::ios::ate);need(bool(in),"existing corpus open");
  const auto bytes=std::size_t(in.tellg());need(bytes%sizeof(u64)==0,"stored u64 extent");const auto available=bytes/2048;need(available>=8,"eight source blocks exist");
  std::array<std::size_t,8> ids;std::vector<u64> values(2048);
  for(std::size_t i=0;i<8;++i){ids[i]=i*available/8;in.seekg(ids[i]*2048);in.read(reinterpret_cast<char*>(values.data()+i*256),2048);need(bool(in),"original stratified source block");}
  for(auto p:programs){
   context=std::string(column)+"/"+name(p.layout);auto c=cc::prepare_sum(values,8,p);
   std::cout<<"{\"event\":\"corpus\",\"column\":\""<<column<<"\",\"program\":\""<<name(p.layout)<<"\",\"physical_width\":8,\"frame_rows\":"<<p.frame_rows<<",\"source_block_ids\":[";
   for(std::size_t i=0;i<8;++i){if(i)std::cout<<',';std::cout<<ids[i];}
   std::cout<<"],\"applicable\":"<<(c?"true":"false")<<",\"refusal\":\""<<cc::refusal_name(c.refusal)<<"\"";
   if(!c){need(p.layout==frame::LayoutId::byteplane&&std::string(column)!="OS"&&c.refusal==cc::sum_refusal::layout_width,"expected real source program inapplicability");++corpus_refused;std::cout<<"}\n";continue;}
   validate(c,values,p);++corpus_accepted;const auto& prep=c.owner->preparation();
   const auto& image=c.owner->image();const auto& type=*image.binding().type;
   auto wire=frame::serialize(type);const auto stem=std::string(column)+"-"+name(p.layout);
   write(output/(stem+".type"),wire);write(output/(stem+".meta"),image.metadata());write(output/(stem+".body"),image.body());
   std::size_t logical=0,capacity=0,shared_logical=0,shared_capacity=0,body_capacity=0;
   for(const auto& r:c.owner->retained_ranges()){
    logical+=r.bytes;capacity+=r.capacity;
    if(r.role.starts_with("type.")||r.role.starts_with("binding.")||r.role.starts_with("sum.")||r.role=="field.byte-offsets"){shared_logical+=r.bytes;shared_capacity+=r.capacity;}
    if(r.role=="container.body-evidence-main-tail")body_capacity=r.capacity;
   }
   // Component headers are embedded in owned headers already counted above.
   // This conceptual split does not imply separate physical cache lines or
   // actual sharing in this fixture. No sharing machinery is introduced.
   const std::size_t shared_headers=sizeof(frame::Type)+sizeof(engine::typebound_leaf_plan)+sizeof(engine::typebound_leaf_binding)+sizeof(std::vector<uint16_t>)+sizeof(std::vector<uint8_t>)+sizeof(cc::sum_program);
   shared_logical+=shared_headers;shared_capacity+=shared_headers;
   need(logical>=shared_logical&&capacity>=shared_capacity,"shareable/instance account partitions actual known ranges");
   std::cout<<",\"rows\":"<<prep.rows<<",\"complete_frames\":"<<prep.complete_frames<<",\"tail_rows\":"<<prep.tail_rows<<",\"expected_sum\":\""<<decimal(oracle(values))<<"\",\"symbols\":"<<prep.symbols<<",\"distinct_boxes\":"<<prep.distinct_boxes<<",\"max_residual_bits\":"<<unsigned(prep.max_residual_bits)<<",\"boxes\":[";
   for(std::size_t i=0;i<type.boxes.size();++i){if(i)std::cout<<',';std::cout<<"{\"base\":\""<<type.boxes[i].offset<<"\",\"bits\":"<<unsigned(type.boxes[i].range_log2)<<'}';}
   std::cout<<"],\"selected_symbols\":[";
   for(std::size_t i=0;i<image.view().main.n_frames;++i){if(i)std::cout<<',';std::cout<<unsigned(image.view().main.symbol(i));}
   std::cout<<"],\"binding_field_id\":"<<image.binding().field_ids[0]<<",\"binding_width\":"<<unsigned(image.binding().widths[0])<<",\"binding_generation\":"<<image.binding().artifact_generation<<",\"type_wire_bytes\":"<<wire.size()<<",\"metadata_bytes\":"<<image.metadata().size()<<",\"body_bytes\":"<<image.body().size()<<",\"body_capacity_bytes\":"<<body_capacity<<",\"known_retained_logical_bytes\":"<<logical<<",\"known_retained_capacity_bytes\":"<<capacity<<",\"shareable_known_logical_bytes\":"<<shared_logical<<",\"shareable_known_capacity_bytes\":"<<shared_capacity<<",\"instance_known_logical_bytes\":"<<logical-shared_logical<<",\"instance_known_capacity_bytes\":"<<capacity-shared_capacity<<",\"shareable_private_codec_element_bytes\":"<<prep.private_codec_element_bytes<<",\"fit_box_capacity_bytes\":"<<prep.fit_box_capacity_bytes<<",\"wire_staging_bytes\":"<<prep.wire_staging_bytes<<",\"artifact_stem\":\""<<stem<<"\",\"actual_sharing\":false,\"aggregate_cache\":false}\n";
  }
 }
}
int main(int argc,char**argv){try{
 if(argc!=3)throw std::invalid_argument("usage: frame_calico_corpus_check INPUT_DIRECTORY OUTPUT_DIRECTORY");
 const fs::path output=argv[2];fs::create_directories(output);fixtures();corpus(argv[1],output);
 need(corpus_accepted==7&&corpus_refused==2,"all real candidates retained including inapplicable byteplane");
 std::cout<<"{\"event\":\"summary\",\"synthetic_cases\":"<<synthetic<<",\"accepted\":"<<accepted<<",\"refusals\":"<<refused<<",\"sum_calls\":"<<sum_calls<<",\"checks\":"<<checks<<",\"corpus_accepted\":"<<corpus_accepted<<",\"corpus_refused\":"<<corpus_refused<<",\"pass\":true,\"timing_claim\":false}\n";
 }catch(const std::exception& e){std::cerr<<"Calico corpus SUM failure: "<<e.what()<<'\n';return 2;}}
