#include <hwy/contrib/sort/vqsort.h>
#include <hwy/highway.h>
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <type_traits>
#include <vector>
unsigned sorts=0,payload_sorts=0;
bool nonempty_only=false;
void need(bool ok,const char* why){if(!ok){std::fprintf(stderr,"vqsort control: %s\n",why);std::exit(1);}}
std::uint64_t mix(std::uint64_t x){x+=0x9e3779b97f4a7c15ull;x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;x=(x^(x>>27))*0x94d049bb133111ebull;return x^(x>>31);}
constexpr std::size_t sizes[]={0,1,2,7,8,15,16,17,31,32,33,63,64,65,255,256,257,1023,1024,1025,16385,65537};
template<class T> void integer(){
 using U=std::make_unsigned_t<T>;
 for(auto n:sizes)for(unsigned shape=0;shape<7;++shape)for(bool descending:{false,true}){
  if(!n&&nonempty_only)continue;
  const T canary=std::bit_cast<T>(U(0x5a5a5a5a5a5a5a5aull));
  std::vector<T> storage(n+2,canary);T* values=storage.data()+1;
  for(std::size_t i=0;i<n;++i){
   if(shape==0)values[i]=std::numeric_limits<T>::min();
   else if(shape==1)values[i]=std::numeric_limits<T>::max();
   else if(shape==3)values[i]=T(i%7);
   else if(shape==6)values[i]=(i&1)?std::numeric_limits<T>::max():std::numeric_limits<T>::min();
   else values[i]=std::bit_cast<T>(U(mix(i+19)));
  }
  if(shape==4)std::sort(values,values+n);
  if(shape==5)std::sort(values,values+n,std::greater<T>{});
  std::map<T,std::size_t> counts;for(std::size_t i=0;i<n;++i)++counts[values[i]];
  std::vector<T> expected(values,values+n);
  if(descending){std::sort(expected.begin(),expected.end(),std::greater<T>{});hwy::VQSort(values,n,hwy::SortDescending{});}
  else {std::sort(expected.begin(),expected.end());hwy::VQSort(values,n,hwy::SortAscending{});}
  need(storage.front()==canary&&storage.back()==canary,"sort store canaries");
  need(std::equal(expected.begin(),expected.end(),values),"independent sorted-vector expectation");
  for(std::size_t i=0;i<n;++i){
   if(i)need(descending?values[i-1]>=values[i]:values[i-1]<=values[i],"monotonic order");
   auto at=counts.find(values[i]);need(at!=counts.end()&&at->second>0,"independent multiset");--at->second;
  }
  for(auto [value,count]:counts){(void)value;need(count==0,"no missing key");}
  ++sorts;
 }
}
template<class Pair> bool association(std::span<const Pair> original,std::span<const Pair> sorted,bool descending){
 if(original.size()!=sorted.size())return false;
 std::vector<bool> seen(original.size());
 for(std::size_t i=0;i<sorted.size();++i){
  auto id=sorted[i].value;
  if(id>=original.size()||seen[id]||original[id].key!=sorted[i].key)return false;
  if(i&&(descending?sorted[i-1].key<sorted[i].key:sorted[i-1].key>sorted[i].key))return false;
  seen[id]=true;
 }
 return true;
}
template<class Pair> void payload(){
 using K=decltype(Pair{}.key);
 for(auto n:sizes)for(unsigned shape=0;shape<6;++shape)for(bool descending:{false,true}){
  if(!n&&nonempty_only)continue;
  std::vector<Pair> original(n),sorted;
  for(std::size_t i=0;i<n;++i){
   original[i].value=decltype(Pair{}.value)(i);
   original[i].key=shape==0?K{0}:shape==1?std::numeric_limits<K>::max():shape==2?K(i%7):K(mix(i+31));
  }
  // Payloads must continue to name their original key after input permutation.
  if(shape==4||shape==5){
   std::sort(original.begin(),original.end(),[&](auto a,auto b){return shape==4?a.key<b.key:a.key>b.key;});
   for(std::size_t i=0;i<n;++i)original[i].value=decltype(Pair{}.value)(i);
  }
  sorted=original;
  Pair empty{};auto* data=n?sorted.data():&empty;
  if(descending)hwy::VQSort(data,n,hwy::SortDescending{});
  else hwy::VQSort(data,n,hwy::SortAscending{});
  need(association<Pair>(original,sorted,descending),"payload/key association and permutation");
  if(n>1){auto bad=sorted;bad[0].value=bad[1].value;need(!association<Pair>(original,bad,descending),"negative duplicate-payload control");}
  ++payload_sorts;
 }
}
int main(int argc,char**argv){
 if(argc==2&&std::string(argv[1])=="--zero"){
  std::uint16_t value=123;
  std::puts("entered direct VQSort uint16 zero-length call");std::fflush(stdout);
  hwy::VQSort(&value,0,hwy::SortAscending{});
  need(value==123,"empty-sort value unchanged");
  std::puts("returned direct VQSort uint16 zero-length call");return 0;
 }
 if(argc==2&&std::string(argv[1])=="--nonempty")nonempty_only=true;
 else need(argc==1,"known mode");
 integer<std::uint16_t>();integer<std::uint32_t>();integer<std::uint64_t>();
 integer<std::int16_t>();integer<std::int32_t>();integer<std::int64_t>();
 payload<hwy::K32V32>();payload<hwy::K64V64>();
 need(hwy::GetChosenTarget().IsInitialized(),"actual dynamic sort initialized dispatch");
 const auto index=hwy::GetChosenTarget().GetIndex();const char* selected="unresolved";unsigned matches=0;
 for(auto target:hwy::SupportedAndGeneratedTargets()){
  hwy::ChosenTarget candidate;candidate.Update(target);
  if(candidate.GetIndex()==index){selected=hwy::TargetName(target);++matches;}
 }
 need(matches==1,"unique selected target for matching compiled dispatch masks");
 std::printf("{\"peer\":\"Highway vqsort\",\"integer_sorts\":%u,\"payload_sorts\":%u,\"compiled_targets\":%lld,\"supported_targets\":%lld,\"selected_index\":%zu,\"selected_target\":\"%s\",\"stable_equal_keys\":false,\"pass\":true,\"timing_claim\":false}\n",sorts,payload_sorts,(long long)HWY_TARGETS,(long long)hwy::SupportedTargets(),index,selected);
}
