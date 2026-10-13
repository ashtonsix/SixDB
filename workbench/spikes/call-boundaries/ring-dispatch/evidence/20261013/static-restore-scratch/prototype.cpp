#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <tuple>
using U=std::uint64_t;
using V=U __attribute__((vector_size(16)));
#define CC __attribute__((preserve_none))
#define VALUES V a,V b,V c,V d,V e,V f,V g,V h
#define PASS a,b,c,d,e,f,g,h
struct Ring;struct Slot;
using Fn=CC void(*)(Ring*,Slot*,U,U,VALUES);
using Args=std::tuple<U,U,V,V,V,V,V,V,V,V>;
struct Slot{Fn dispatch;Args args;U id,result;};
struct Ring{std::array<Slot,8> slots;unsigned next{},live{8};};
CC void metadata(Ring*,Slot*,U,U,VALUES);
CC void payload(Ring*,Slot*,U,U,VALUES);
#if MODE
// Wrapper's physical signature matches the target; the target is inlined.
template<Fn Target> CC void restored(Ring* r,Slot* s,U,U,VALUES){
  [[clang::always_inline]] return Target(r,s,std::get<0>(s->args),std::get<1>(s->args),std::get<2>(s->args),std::get<3>(s->args),std::get<4>(s->args),std::get<5>(s->args),std::get<6>(s->args),std::get<7>(s->args),std::get<8>(s->args),std::get<9>(s->args));
}
constexpr Fn meta=restored<metadata>,data=restored<payload>;
#else
constexpr Fn meta=metadata,data=payload;
#endif
[[gnu::always_inline]] CC void resume(Ring*r,Slot*,U remaining,U sum,VALUES){
 if(!r->live)return;
 Slot*s=&r->slots[r->next];r->next=(r->next+1)%8;
 while(std::get<0>(s->args)==0){s=&r->slots[r->next];r->next=(r->next+1)%8;}
#if MODE==0
 [[clang::musttail]] return s->dispatch(r,s,std::get<0>(s->args),std::get<1>(s->args),std::get<2>(s->args),std::get<3>(s->args),std::get<4>(s->args),std::get<5>(s->args),std::get<6>(s->args),std::get<7>(s->args),std::get<8>(s->args),std::get<9>(s->args));
#elif MODE==1
 [[clang::musttail]] return s->dispatch(r,s,remaining,sum,PASS);
#elif MODE==2
 [[clang::musttail]] return s->dispatch(r,s,0,0,V{},V{},V{},V{},V{},V{},V{},V{});
#else
 static_assert(__has_builtin(__builtin_nondeterministic_value));
 [[clang::musttail]] return s->dispatch(r,s,__builtin_nondeterministic_value(U{}),__builtin_nondeterministic_value(U{}),__builtin_nondeterministic_value(V{}),__builtin_nondeterministic_value(V{}),__builtin_nondeterministic_value(V{}),__builtin_nondeterministic_value(V{}),__builtin_nondeterministic_value(V{}),__builtin_nondeterministic_value(V{}),__builtin_nondeterministic_value(V{}),__builtin_nondeterministic_value(V{}));
#endif
}
template<unsigned... Changed> [[gnu::always_inline]] CC void changed(Ring*r,Slot*s,U remaining,U sum,VALUES){
 Args values{remaining,sum,PASS};((std::get<Changed>(s->args)=std::get<Changed>(values)),...);
 [[clang::musttail]] return resume(r,s,remaining,sum,PASS);
}
CC void metadata(Ring*r,Slot*s,U remaining,U sum,VALUES){
 sum+=s->id+13;s->dispatch=data;
#if MODE
 [[clang::musttail]] return changed<1>(r,s,remaining,sum,PASS);
#else
 [[clang::musttail]] return changed<0,1,2,3,4,5,6,7,8,9>(r,s,remaining,sum,PASS);
#endif
}
CC void payload(Ring*r,Slot*s,U remaining,U sum,VALUES){
 V delta{s->id+remaining,sum^remaining};a+=delta;b+=delta;c+=delta;d+=delta;e+=delta;f+=delta;g+=delta;h+=delta;
 if(!--remaining){
  s->args=Args{remaining,sum,PASS};s->result=sum+a[0]+a[1]+b[0]+b[1]+c[0]+c[1]+d[0]+d[1]+e[0]+e[1]+f[0]+f[1]+g[0]+g[1]+h[0]+h[1];--r->live;
  [[clang::musttail]] return resume(r,s,remaining,sum,PASS);
 }
 s->dispatch=meta;
 [[clang::musttail]] return changed<0,1,2,3,4,5,6,7,8,9>(r,s,remaining,sum,PASS);
}
int main(){
 Ring r;U expected=0;std::array<U,8> gold_sum{};std::array<std::array<std::array<U,2>,8>,8> gold_vectors{};
 for(U i=0;i<8;++i){
  auto&s=r.slots[i];s.id=i;s.dispatch=meta;U rem=7,sum=i*5;std::array<std::array<U,2>,8> v{};
  for(U j=0;j<8;++j)v[j]={i*31+j*7,i*41+j*11};
  s.args=Args{rem,sum,V{v[0][0],v[0][1]},V{v[1][0],v[1][1]},V{v[2][0],v[2][1]},V{v[3][0],v[3][1]},V{v[4][0],v[4][1]},V{v[5][0],v[5][1]},V{v[6][0],v[6][1]},V{v[7][0],v[7][1]}};
  while(rem){sum+=i+13;for(auto&x:v){x[0]+=i+rem;x[1]+=sum^rem;}--rem;}
  gold_sum[i]=sum;gold_vectors[i]=v;expected+=sum;for(auto x:v)expected+=x[0]+x[1];
 }
 resume(&r,nullptr,0,0,V{},V{},V{},V{},V{},V{},V{},V{});
 U actual=0;for(auto&s:r.slots){
  actual+=s.result;
  if(std::get<0>(s.args)!=0 || std::get<1>(s.args)!=gold_sum[s.id])std::abort();
  [&]<std::size_t... I>(std::index_sequence<I...>){
    if(!((std::get<I+2>(s.args)[0]==gold_vectors[s.id][I][0] && std::get<I+2>(s.args)[1]==gold_vectors[s.id][I][1])&&...))std::abort();
  }(std::make_index_sequence<8>{});
 }if(actual!=expected)std::abort();
 std::printf("mode=%d checksum=%llu expected=%llu slot_bytes=%zu\n",MODE,(unsigned long long)actual,(unsigned long long)expected,sizeof(Slot));
}
