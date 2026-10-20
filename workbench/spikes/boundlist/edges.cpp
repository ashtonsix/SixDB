#include <ikea/boundlist/author/search.h>
#include "fused.h"
#include <array>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cassert>
using word=std::uint64_t;
word mix(word x){x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);}
namespace bl=ikea::boundlist;
template<class T> struct data {
 std::vector<T> values;std::array<T,1024> query;std::array<unsigned,1024> link;std::vector<word> children;
 using binding=decltype(bl::author::bind_search(bl::direct_source<T>{}));
 binding source{bl::direct_source<T>{nullptr,0}};
 data(unsigned n,unsigned run):values(n),children(n+1){
  for(unsigned i=0;i<n;++i)values[i]=T(100+i/run*3);
  for(unsigned i=0;i<=n;++i)children[i]=mix(i+713);
  source=bl::author::bind_search(bl::direct_source{values.data(),n});
  for(unsigned i=0;i<1024;++i){query[i]=T(99+mix(i+913)%(unsigned(values.back())-97));
   const unsigned rank=std::upper_bound(values.begin(),values.end(),query[i])-values.begin();
   link[i]=(i*713+1)-rank-unsigned(children[rank]);}
 }
};
template<unsigned C,unsigned M,class T> [[gnu::noinline]] word consume(const data<T>& f,unsigned rounds){
 std::array<unsigned,C> state{};for(unsigned i=0;i<C;++i)state[i]=i*127;
 for(unsigned step=0;step<rounds;++step){
  std::array<T,C> q;std::array<unsigned,C> ranks;
#pragma clang loop unroll(full)
  for(unsigned i=0;i<C;++i)q[i]=f.query[state[i]];
  if constexpr(M==2){std::array<bl::direct_source<T>,C> s;
#pragma clang loop unroll(full)
   for(unsigned i=0;i<C;++i)s[i]={f.values.data(),unsigned(f.values.size())};
   ranks=bl::author::upper_bounds(s,q);
  }else if constexpr(M==3){const auto s=[&]<std::size_t...I>(std::index_sequence<I...>){return std::array{((void)I,f.source)...};}(std::make_index_sequence<C>{});ranks=bl::author::upper_bounds(s,q);}
#pragma clang loop unroll(full)
  for(unsigned i=0;i<C;++i){unsigned r;
   if constexpr(M==0)r=bl::upper_bound(bl::direct_source{f.values.data(),unsigned(f.values.size())},q[i]);
   else if constexpr(M==1)r=bl::upper_bound(f.source,q[i]);
   else if constexpr(M==5)r=boundlist_fused::flat<false>(f.values.data(),unsigned(f.values.size()),q[i]);
   else if constexpr(M==4)r=std::upper_bound(f.values.begin(),f.values.end(),q[i])-f.values.begin();
   else r=ranks[i];
   state[i]=(r+f.link[state[i]]+unsigned(f.children[r]))&1023;
  }
 }
 word result=0;for(unsigned i=0;i<C;++i)result^=state[i]+i*713;return result;
}
volatile word observed;
template<class T,unsigned C>void panel(unsigned n,unsigned run){data<T> f(n,run);const auto expected=consume<C,4>(f,131072/C);
 for(unsigned rep=0;rep<5;++rep){auto one=[&]<unsigned M>(){observed=consume<C,M>(f,1024);const auto a=std::chrono::steady_clock::now();const auto result=consume<C,M>(f,131072/C);const auto b=std::chrono::steady_clock::now();assert(result==expected);observed=result;
 constexpr const char* names[]={"production_select_window","production_bound_search","production_batch","production_bound_batch","std_upper_bound","independent_fused_scan"};
 std::printf("%zu,%u,%u,%u,%s,%u,%.6f,%llu,%zu,%zu,%zu\n",sizeof(T)*8,n,run,C,names[M],rep,double(std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count())/131072,(unsigned long long)result,f.values.size()*sizeof(T),f.children.size()*sizeof(word),(M==1||M==3)?sizeof(f.source):0);};
 if(rep%2){one.template operator()<5>();one.template operator()<4>();one.template operator()<3>();one.template operator()<2>();one.template operator()<1>();one.template operator()<0>();}
 else{one.template operator()<0>();one.template operator()<1>();one.template operator()<2>();one.template operator()<3>();one.template operator()<4>();one.template operator()<5>();}}
}
int main(){std::puts("bits,count,run,chains,implementation,rep,ns,checksum,endpoint_bytes,child_bytes,binding_bytes");for(unsigned n:{4096,16384})for(unsigned r:{1,17,80}){panel<std::uint16_t,1>(n,r);panel<std::uint16_t,8>(n,r);panel<std::uint32_t,1>(n,r);panel<std::uint32_t,8>(n,r);panel<std::uint64_t,1>(n,r);panel<std::uint64_t,8>(n,r);}}
