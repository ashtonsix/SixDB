#include "control.h"
#include <frame/aggregate.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace cc=calico_control;
using cc::i128;using cc::u128;
using i64=int64_t;using u64=uint64_t;
static u64 checks=0,cases=0,candidate=0,sum_calls=0,mask_calls=0,patch_values=0,leaf_wrapped=0,symbol_bits=0;
static std::string context,surface="all";
static long selected_case=-1;
static bool safe_ranges=false;
static u64 bounded_range_omissions=0,guarded_calls=0,refusals=0;
void need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(context+": "+why);}
u64 mix(u64 x){x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;return (x^(x>>27))*0x94d049bb133111ebULL;}
bool enter(const std::string& name){const auto id=candidate++;if(selected_case>=0&&u64(selected_case)!=id)return false;context=name;std::fprintf(stderr,"CASE %llu %s\n",(unsigned long long)id,name.c_str());++cases;return true;}
const char* label(frame::LayoutId l){switch(l){case frame::LayoutId::bytepack:return "bytepack";case frame::LayoutId::byteplane:return "byteplane";case frame::LayoutId::lane256:return "lane256";default:return "fastlanes";}}
std::vector<unsigned> widths(frame::LayoutId l){if(l==frame::LayoutId::bytepack)return {0,1,3,5,7};if(l==frame::LayoutId::byteplane)return {0,1,7,8,9,12,16};return {0,1,7,8,9,16,17,24,31,32,33,40,56,63,64};}
u64 bitmask(unsigned k){return k==64?~u64{0}:(u64{1}<<k)-1;}
i64 base_for(unsigned k,unsigned field){if(k==64)return INT64_MIN;if(k==63)return field?0:INT64_MIN;if(k>32)return field?(i64{1}<<40):-(i64{1}<<60);return field?123456789:-1000000;}
frame::Type raw_type(frame::LayoutId layout,unsigned z,unsigned k){
 frame::TypeBuilder b(1,layout,frame::Align::word8);b.array(z,2).universe(INT64_MIN,INT64_MAX).universe(INT64_MIN,INT64_MAX);
 b.symbol(0,{{base_for(k,0),uint8_t(k)},{base_for(k,1),uint8_t(k)}});return b.build();
}
template<class T>std::vector<std::span<const T>> spans(const std::vector<std::vector<T>>& v){std::vector<std::span<const T>> out;for(const auto& f:v)out.emplace_back(f);return out;}
void ranges_ok(const std::vector<cc::range>& ranges){for(const auto& r:ranges)need(r.data&&r.bytes>0&&r.capacity>=r.bytes,"conditioning range extent");}
// This separate read-envelope fixture puts the last admitted byte directly
// before a protected page. It does not change the callable owner's buffers.
class guarded_bytes {
 void* allocation_=nullptr;std::size_t extent_=0;uint8_t* data_=nullptr;
public:
 guarded_bytes(const void* source,std::size_t n){
  if(!n)return;const auto page=std::size_t(sysconf(_SC_PAGESIZE));
  extent_=((n+page-1)/page+1)*page;
  allocation_=mmap(nullptr,extent_,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  if(allocation_==MAP_FAILED)throw std::runtime_error("guard mmap");
  auto* end=static_cast<uint8_t*>(allocation_)+extent_-page;
  if(mprotect(end,page,PROT_NONE))throw std::runtime_error("guard mprotect");
  data_=end-n;std::memcpy(data_,source,n);
 }
 ~guarded_bytes(){if(allocation_&&allocation_!=MAP_FAILED)munmap(allocation_,extent_);}
 guarded_bytes(const guarded_bytes&)=delete;
 const uint8_t* data()const{return data_;}
};
bool covers(const std::vector<cc::range>& ranges,const void* data,std::size_t bytes){
 if(!bytes)return true;const auto at=reinterpret_cast<uintptr_t>(data);
 for(const auto&r:ranges){auto start=reinterpret_cast<uintptr_t>(r.data);if(at>=start&&at-start<=r.bytes&&bytes<=r.bytes-(at-start))return true;}return false;
}
void same_ranges(const std::vector<cc::range>& a,const std::vector<cc::range>& b){
 need(a.size()==b.size(),"stable conditioning range count");
 for(std::size_t i=0;i<a.size();++i)need(a[i].data==b[i].data&&a[i].bytes==b[i].bytes&&a[i].capacity==b[i].capacity&&a[i].role==b[i].role,"same query/conditioning owner after reuse");
}
void raw_cases(){
 for(auto layout:{frame::LayoutId::bytepack,frame::LayoutId::byteplane,frame::LayoutId::lane256,frame::LayoutId::fastlanes})
 for(auto k:widths(layout))for(unsigned nf:{0,1,3,65})for(unsigned pattern=0;pattern<4;++pattern){
  const unsigned z=layout==frame::LayoutId::fastlanes?1024:256;
  if(!enter(std::string("raw_")+label(layout)+"_k"+std::to_string(k)+"_frames"+std::to_string(nf)+"_patch"+std::to_string(pattern)))continue;
  const std::size_t n=std::size_t(nf)*z;std::vector<std::vector<i64>> v(2,std::vector<i64>(n));
  for(unsigned f=0;f<2;++f)for(std::size_t i=0;i<n;++i){
   u64 r=i%z==0?0:i%z==1?bitmask(k):mix(i+31*f+19)&bitmask(k);
   v[f][i]=i64(i128(base_for(k,f))+i128(r));
   bool patched=pattern==3||(pattern==2&&i%2==0)||(pattern==1&&(i==0||i==n/2||i+1==n));
   if(patched)v[f][i]=base_for(k,f)==INT64_MIN?INT64_MAX:INT64_MIN;
  }
  const auto borrowed=spans(v);cc::signed_image image(raw_type(layout,z,k),borrowed);
  guarded_bytes guarded_symbols(image.stream().symbol_data(),image.stream().symbol_size());
  guarded_bytes guarded_body(image.stream().body_data(),image.stream().body_size());
  auto guarded=image.stream();guarded.borrowed_symbols=guarded_symbols.data();
  guarded.borrowed_body=guarded_body.data();guarded.borrowed_body_size=image.stream().body_size();
  for(const auto& p:guarded.patches)need(p.capacity()==p.size(),"exact patch allocation in guarded fixture");
  for(unsigned f=0;f<2;++f){
   const auto ranges=image.sum_ranges(f);ranges_ok(ranges);
   need(covers(ranges,&image,sizeof(image))&&covers(ranges,image.stream().symbol_data(),image.stream().symbol_size())&&covers(ranges,image.stream().body_data(),image.stream().body_size())&&covers(ranges,image.stream().patches[f].data(),image.stream().patches[f].size()*sizeof(frame::RawPatch))&&covers(ranges,&image.plan(f),sizeof(image.plan(f)))&&covers(ranges,image.plan(f).sym.data(),image.plan(f).sym.size()*sizeof(engine::raw_farray_sum_plan::symbol)),"signed SUM actual input range identity");
   i128 expected=0;for(i64 x:v[f])expected+=i128(x);
   for(unsigned repeat=0;repeat<3;++repeat){i128 got=0;need(image.sum(f,got)&&got==expected,"bound exact signed SUM");++sum_calls;}
   i128 got=0;need(engine::raw_farray_sum(image.codec(),image.stream(),f,got)&&got==expected,"checked signed SUM");++sum_calls;
   need(engine::raw_farray_sum_trusted(image.plan(f),guarded,got)&&got==expected,"guarded exact signed SUM");++guarded_calls;
   patch_values+=image.stream().patches[f].size();
   for(auto [lo,hi]:std::array<std::pair<i64,i64>,4>{{{INT64_MIN,INT64_MAX},{base_for(k,f),base_for(k,f)},{INT64_MIN,INT64_MIN},{INT64_MAX,INT64_MAX}}}){
    if(safe_ranges&&k>=63&&lo==hi&&(lo==INT64_MIN||lo==INT64_MAX)&&lo!=base_for(k,f)){++bounded_range_omissions;continue;}
    std::vector<uint8_t> mask;need(image.range_mask(f,lo,hi,mask),"signed range call");need(mask.size()==(n+7)/8,"signed mask extent");
    for(std::size_t i=0;i<n;++i)need(bool(mask[i/8]>>(i%8)&1)==(v[f][i]>=lo&&v[f][i]<=hi),"signed range bit oracle");++mask_calls;
    std::vector<uint8_t> guarded_mask;need(image.codec().range_mask_trusted(guarded,f,lo,hi,guarded_mask)&&guarded_mask==mask,"guarded signed predicate read extent");++guarded_calls;
   }
   if(nf){
    for(u64 frame:{u64{0},u64{nf-1}}){i128 want=0;for(unsigned j=0;j<z;++j)want+=v[f][frame*z+j];
     need(engine::raw_farray_sum_frame_trusted(image.plan(f),image.stream(),frame,got)&&got==want,"direct complete-frame SUM");
     // Same canonical image with only the derived offset table omitted.
     auto checkpoints=image.stream();checkpoints.offset.clear();
     need(engine::raw_farray_sum_frame_trusted(image.plan(f),checkpoints,frame,got)&&got==want,"checkpoint complete-frame SUM");
    }
    auto corrupt=image.stream();corrupt.checkpoint[0]++;
    need(!engine::raw_farray_sum(image.codec(),corrupt,f,got),"ordinary malformed checkpoint refusal");++refusals;
   }
   same_ranges(ranges,image.sum_ranges(f));
  }
 }
}
void multi_raw(){
 if(!enter("raw_multisymbol_mixed_widths_patches"))return;
 frame::TypeBuilder b(16,frame::LayoutId::lane256,frame::Align::word8);b.array(256,1).universe(INT64_MIN,INT64_MAX);
 std::array<i64,5> base{-100,-1000,100000,i64{1}<<30,-(i64{1}<<40)};std::array<unsigned,5> bits{0,8,16,24,40};
 for(unsigned s=0;s<5;++s)b.symbol(0,{{base[s],uint8_t(bits[s])}});
 for(unsigned s=5;s<15;++s)b.symbol(0,{{i64(s)<<48,56}});b.symbol(0,{{INT64_MIN,64}});
 std::vector<std::vector<i64>> v(1,std::vector<i64>(65*256));
 for(std::size_t i=0;i<v[0].size();++i){auto s=(i/256)%5;v[0][i]=base[s]+i64(mix(i+9)&bitmask(bits[s]));}
 v[0][256+19]=-500;const auto borrowed=spans(v);cc::signed_image image(b.build(),borrowed);
 i128 expected=0;for(auto x:v[0])expected+=x;i128 got=0;need(image.sum(0,got)&&got==expected,"multisymbol complete SUM");++sum_calls;
 for(auto symbol:image.stream().symbols)symbol_bits|=u64{1}<<symbol;
 need(std::popcount(symbol_bits)>=5,"distinct selected symbols");need(!image.stream().patches[0].empty(),"real selected escape stream");
}
frame::Type unsigned_type(frame::LayoutId layout,unsigned z,const std::array<unsigned,4>& ks){
 frame::TypeBuilder b(1,layout,frame::Align::word8);b.array(z,4);
 for(unsigned f=0;f<4;++f)b.universe(f==3?INT64_MIN:0,f==0?255:f==1?65535:f==2?i64(UINT32_MAX):INT64_MAX);
 b.symbol(0,{{0,uint8_t(ks[0])},{0,uint8_t(ks[1])},{0,uint8_t(ks[2])},{INT64_MIN,uint8_t(ks[3])}});return b.build();
}
void unsigned_cases(){
 const std::array<uint8_t,4> physical{1,2,4,8};
 for(auto layout:{frame::LayoutId::bytepack,frame::LayoutId::byteplane,frame::LayoutId::lane256,frame::LayoutId::fastlanes}){
  const unsigned z=layout==frame::LayoutId::fastlanes?1024:256;
  const std::array<unsigned,4> ks=layout==frame::LayoutId::bytepack?std::array<unsigned,4>{3,5,7,7}:layout==frame::LayoutId::byteplane?std::array<unsigned,4>{8,12,16,16}:std::array<unsigned,4>{8,16,32,64};
  for(std::size_t n:{std::size_t{0},std::size_t{1},std::size_t{z-1},std::size_t{z},std::size_t{z+1},std::size_t{3*z+17},std::size_t{65*z+1}}){
   if(!enter(std::string("container_")+label(layout)+"_rows"+std::to_string(n)))continue;
   std::vector<std::vector<u64>> v(4,std::vector<u64>(n));
   for(unsigned f=0;f<4;++f)for(std::size_t i=0;i<n;++i)v[f][i]=i%z==0?0:i%z==1?bitmask(ks[f]):mix(i+17*f)&bitmask(ks[f]);
   // u64 field order encoding maps logical zero to INT64_MIN. Narrow boxes
   // still cover exactly these unsigned residuals; no local codec algebra.
   const auto borrowed=spans(v);cc::unsigned_image image(unsigned_type(layout,z,ks),physical,borrowed);
   need(image.view().main.symbols.empty()&&image.view().main.body.empty()&&image.view().exact_min.empty()&&image.view().exact_max.empty()&&image.view().tail.empty(),"actual borrowed container, no decoded copies");
   const auto body=image.body(),meta=image.metadata();guarded_bytes guarded_body(body.data(),body.size());
   engine::typebound_leaf_view guarded(image.binding());
   need(engine::typebound_leaf_load_borrowed_trusted(image.binding(),meta.data(),meta.size(),guarded_body.data(),body.size(),n,guarded)==0,"guarded actual container admission");
   for(unsigned f=0;f<4;++f){
    const auto ranges=image.sum_ranges(f);ranges_ok(ranges);
    need(covers(ranges,body.data(),body.size())&&covers(ranges,meta.data(),meta.size())&&covers(ranges,image.view().main.symbol_data(),image.view().main.symbol_size())&&covers(ranges,image.view().main.body_data(),image.view().main.body_size())&&covers(ranges,image.view().borrowed_tail,std::size_t(image.view().tail_n)*15),"container SUM uses conditioned original bytes");
    u128 want=0;for(auto x:v[f])want+=x;u128 got=0;
    for(unsigned repeat=0;repeat<3;++repeat){need(image.sum(f,got)&&got==want,"container full+literal-tail exact SUM");++sum_calls;}
    need(image.view().frame_sum_cache.empty(),"no per-frame result cache");
    need(guarded.sum(f,got)&&got==want,"guarded exact container SUM");++guarded_calls;
    for(u64 q:{u64{0},bitmask(ks[f])/2,bitmask(ks[f])}){
     if(safe_ranges&&physical[f]==8&&ks[f]==64&&q==~u64{0}){++bounded_range_omissions;continue;}
     std::vector<uint8_t> mask;need(image.range_mask(f,q,physical[f]==8?~u64{0}:bitmask(8*physical[f]),mask),"container range call");
     need(mask.size()==(n+7)/8,"container mask extent");for(std::size_t i=0;i<n;++i)need(bool(mask[i/8]>>(i%8)&1)==(v[f][i]>=q),"container range bit oracle");++mask_calls;
     if(n%8&&!mask.empty())need((mask.back()>>(n%8))==0,"partial-tail mask padding");
     std::vector<uint8_t> guarded_mask;need(guarded.range_mask(f,q,physical[f]==8?~u64{0}:bitmask(8*physical[f]),guarded_mask)&&guarded_mask==mask,"guarded container predicate read extent");++guarded_calls;
    }
    for(std::size_t i=0;i<n;i+=std::max<std::size_t>(1,n/17)){u64 value=0;need(image.view().point(f,i,value)&&value==v[f][i],"whole/tail point oracle");}
   same_ranges(ranges,image.sum_ranges(f));
   }
   engine::typebound_leaf_view wrong(image.binding());
   need(engine::typebound_leaf_load(image.binding(),meta.data(),meta.size(),body.data(),body.size(),n+1,wrong)!=0,"container row-count mismatch refusal");++refusals;
  }
 }
}
template<class T>void leaf_cases(){
 for(std::size_t n:{0,1,7,8,15,16,17,255,256,257,1023,1024,1025})for(unsigned active=0;active<4;++active)for(bool patched:{false,true})for(i64 base:{i64{-100000},INT64_MAX-17,INT64_MIN}){
  if(!enter("leaf_u"+std::to_string(sizeof(T)*8)+"_n"+std::to_string(n)+"_mask"+std::to_string(active)+"_patch"+std::to_string(patched)+"_base"+std::to_string(base)))continue;
  std::vector<T> residual(n);for(std::size_t i=0;i<n;++i)residual[i]=T(mix(i+71));T empty=0;const auto* data=n?residual.data():&empty;
  frame::Mask mask(n);for(std::size_t i=0;i<n;++i)if(active==1||(active==3&&i%3==1))mask.set(i);
  std::vector<frame::aggregate::Patch> patches;
  if(patched&&n){for(u64 idx:{u64{0},u64(n/2),u64(n-1)})if(std::none_of(patches.begin(),patches.end(),[&](auto p){return p.idx==idx;}))patches.push_back({idx,idx%2?INT64_MAX:INT64_MIN});}
  i128 exact=0;u64 expected=0;
  for(std::size_t i=0;i<n;++i){if(active==2||(active==3&&i%3!=1))continue;i64 value=std::bit_cast<i64>(u64(base)+u64(residual[i]));for(auto p:patches)if(p.idx==i)value=p.val;exact+=i128(value);expected+=u64(value);}
  const auto got=frame::aggregate::sum<T>(data,n,base,active?&mask:nullptr,patches.data(),patches.size());
  guarded_bytes guarded_residual(data,n*sizeof(T));
  if(n){const auto guarded=frame::aggregate::sum<T>(reinterpret_cast<const T*>(guarded_residual.data()),n,base,active?&mask:nullptr,patches.data(),patches.size());need(std::bit_cast<u64>(guarded)==expected,"guarded dense leaf read extent");++guarded_calls;}
  need(std::bit_cast<u64>(got)==expected,"leaf explicit modulo-2^64 law");
  if(exact<INT64_MIN||exact>INT64_MAX){need(i128(got)!=exact,"modulo leaf cannot establish exact wide SUM");++leaf_wrapped;}
  ++sum_calls;
 }
}
template<class F>void refuses(F&& operation,const char* why){bool refused=false;try{operation();}catch(const std::invalid_argument&){refused=true;}need(refused,why);++refusals;}
void admission_cases(){
 if(!enter("explicit_owner_admission_refusals"))return;
 std::vector<std::vector<i64>> signed_fields(2,std::vector<i64>(1));auto ss=spans(signed_fields);
 refuses([&]{cc::signed_image x(raw_type(frame::LayoutId::lane256,256,8),ss);},"signed partial domain refused");
 signed_fields[0].resize(256);signed_fields[1].resize(512);ss=spans(signed_fields);
 refuses([&]{cc::signed_image x(raw_type(frame::LayoutId::lane256,256,8),ss);},"signed unequal domains refused");
 signed_fields[1].resize(256);ss=spans(signed_fields);auto bad=raw_type(frame::LayoutId::lane256,256,8);bad.perm_id[0]=1;
 refuses([&]{cc::signed_image x(bad,ss);},"nonidentity permutation refused");
 std::array<unsigned,4> ks{8,16,32,64};auto t=unsigned_type(frame::LayoutId::lane256,256,ks);
 std::vector<std::vector<u64>> values(4,std::vector<u64>(256));auto us=spans(values);
 std::array<uint8_t,4> widths{3,2,4,8};
 refuses([&]{cc::unsigned_image x(t,widths,us);},"unsupported unsigned width refused");
 widths[0]=1;values[0][0]=256;us=spans(values);
 refuses([&]{cc::unsigned_image x(t,widths,us);},"value outside physical width refused");
 values[0][0]=255;us=spans(values);t=unsigned_type(frame::LayoutId::lane256,256,{0,16,32,64});
 refuses([&]{cc::unsigned_image x(t,widths,us);},"Type-bound v1 actual escape refusal");
}
int main(int argc,char**argv){try{
 if(argc>1)surface=argv[1];if(argc>2)selected_case=std::stol(argv[2]);
 safe_ranges=surface=="all-safe-ranges";if(safe_ranges)surface="all";
 if(surface=="all"||surface=="raw"){raw_cases();multi_raw();}
 if(surface=="all"||surface=="unsigned")unsigned_cases();
 if(surface=="all"||surface=="leaf"){leaf_cases<uint8_t>();leaf_cases<uint16_t>();leaf_cases<uint32_t>();}
 if(surface=="all"||surface=="admission")admission_cases();
 need(cases>0,"selected cases exist");
 std::printf("{\"surface\":\"%s\",\"cases\":%llu,\"checks\":%llu,\"sum_calls\":%llu,\"range_calls\":%llu,\"raw_patch_values\":%llu,\"leaf_wide_counterexamples\":%llu,\"multisymbol_bits\":%llu,\"bounded_range_endpoints\":%s,\"bounded_range_omissions\":%llu,\"guarded_calls\":%llu,\"refusals\":%llu,\"pass\":true,\"timing_claim\":false}\n",surface.c_str(),(unsigned long long)cases,(unsigned long long)checks,(unsigned long long)sum_calls,(unsigned long long)mask_calls,(unsigned long long)patch_values,(unsigned long long)leaf_wrapped,(unsigned long long)symbol_bits,safe_ranges?"true":"false",(unsigned long long)bounded_range_omissions,(unsigned long long)guarded_calls,(unsigned long long)refusals);
 return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"Calico control failure: %s\n",e.what());return 2;}}
