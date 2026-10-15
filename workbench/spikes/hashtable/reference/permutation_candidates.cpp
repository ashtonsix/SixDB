// Ephemeral analytical candidate screen. No production contracts or security claims.
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string_view>
#include <unordered_set>
#include <vector>
using word=std::uint64_t;
constexpr word mask(unsigned n){return n==64?~word{}:(word{1}<<n)-1;}
constexpr word mix(word x){x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);}
constexpr word odd_inverse(word x){word r=x;for(unsigned i=0;i<6;++i)r*=2-x*r;return r;}
void check(bool v){if(!v)std::abort();}
enum Family{strong,right_m_right,right_m_left,pcg,hybrid_hypothesis};
constexpr const char* names[]{"strong","right_m_right","right_m_left","pcg_all_widths_diagnostic","pcg_standard_strong_other_hypothesis_rejected"};
template<unsigned W> [[gnu::always_inline]] word undo_right(word v,unsigned s){for(unsigned d=s;d<W;d*=2)v^=v>>d;return v&mask(W);}
template<unsigned W> [[gnu::always_inline]] word undo_left(word v,unsigned s){for(unsigned d=s;d<W;d*=2)v^=v<<d;return v&mask(W);}
template<unsigned W,Family F> struct permutation{
 static constexpr bool Standard=W==8||W==16||W==32||W==64;
 static constexpr bool IsPcg=F==pcg||(F==hybrid_hypothesis&&Standard);
 static constexpr bool IsStrong=F==strong||(F==hybrid_hypothesis&&!Standard);
 static constexpr unsigned control=W>=64?5:W>=32?4:W>=16?3:2,finish=(2*W+2)/3;
 static constexpr word C=12605985483714917081ULL&mask(W);
 word seed,m,im;
 explicit permutation(word entropy):seed(entropy&mask(W)),m((mix(entropy^0x6d756c7469706c79ULL)|1)&mask(W)),im(odd_inverse(m)&mask(W)){}
 [[gnu::always_inline]] word operator()(word key)const{
  word z=(key^seed)&mask(W);
  if constexpr(IsStrong){z^=z>>(W*30/64);z=(z*0xbf58476d1ce4e5b9ULL)&mask(W);z^=z>>(W*27/64);z=(z*0x94d049bb133111ebULL)&mask(W);return z^(z>>(W*31/64));}
  else if constexpr(IsPcg){z^=z>>((z>>(W-control))+control);z=(z*C)&mask(W);return z^(z>>finish);}
  else{z^=z>>(W/2);z=(z*m)&mask(W);if constexpr(F==right_m_left)return (z^(z<<(W/2)))&mask(W);else return z^(z>>(W/2));}
 }
 [[gnu::always_inline]] word inverse(word z)const{
  if constexpr(IsStrong){z=undo_right<W>(z,W*31/64);z=(z*odd_inverse(0x94d049bb133111ebULL))&mask(W);z=undo_right<W>(z,W*27/64);z=(z*odd_inverse(0xbf58476d1ce4e5b9ULL))&mask(W);return undo_right<W>(z,W*30/64)^seed;}
  else if constexpr(IsPcg){z=undo_right<W>(z,finish);z=(z*odd_inverse(C))&mask(W);const unsigned s=(z>>(W-control))+control;return undo_right<W>(z,s)^seed;}
  else{if constexpr(F==right_m_left)z=undo_left<W>(z,W/2);else z=undo_right<W>(z,W/2);z=(z*im)&mask(W);return undo_right<W>(z,W/2)^seed;}
 }
};
template<unsigned W,Family F>void verify(){
 for(unsigned s=0;s<4;++s){permutation<W,F>p(mix(371+s));std::mt19937_64 rng(173+s);const unsigned count=W<=16?1u<<W:65536;
  for(unsigned i=0;i<count;++i){word x=W<=16?i:rng()&mask(W);word y=p(x);check((y&~mask(W))==0&&p.inverse(y)==x&&p(p.inverse(x))==x);}
 }
}
word reverse(word x,unsigned w){word out=0;for(unsigned b=0;b<w;++b){out=(out<<1)|(x&1);x>>=1;}return out;}
constexpr const char* shapes[]{"uniform","sequential","stride","fixed_high_prefix","bit_reverse","gray","inverse_multiplier_stride","pcg_zero_seed_bucket_preimage","strong_zero_seed_bucket_preimage","pcg_zero_seed_four_buckets","strong_zero_seed_four_buckets","pcg_zero_seed_balanced_low_remainder","strong_zero_seed_balanced_low_remainder","stride8_fixed_low","stride16_fixed_low"};
template<unsigned W>std::vector<word>input(std::size_t n,unsigned shape){
 std::vector<word>out;out.reserve(n);const unsigned used=std::bit_width(n-1);
 constexpr unsigned A=std::min(10u,W/2-2),R=W-A;
 if(shape==0){std::mt19937_64 rng(0x6b657973+W);std::unordered_set<word>seen;while(out.size()<n){word k=rng()&mask(W);if(seen.insert(k).second)out.push_back(k);}}
 else for(word i=0;i<n;++i){word v=0;
  switch(shape){
   case 1:v=i;break;
   case 2:v=i<<(W-used);break;
   case 3:v=i|((mix(W)&mask(W-used))<<used);break;
   case 4:v=reverse(i,W);break;
   case 5:v=i^(i>>1);break;
   case 6:v=(i*odd_inverse(permutation<W,pcg>::C))&mask(W);break;
   case 7:v=permutation<W,pcg>(0).inverse(i);break;
   case 8:v=permutation<W,strong>(0).inverse(i);break;
   case 9:case 10:{word y=((i&3)<<R)|(i>>2);v=shape==9?permutation<W,pcg>(0).inverse(y):permutation<W,strong>(0).inverse(y);break;}
   case 11:case 12:{const unsigned low=std::min(8u,R-(used-A));word y=((i&mask(A))<<R)|((i>>A)<<low);v=shape==11?permutation<W,pcg>(0).inverse(y):permutation<W,strong>(0).inverse(y);break;}
   case 13:case 14:{const unsigned low=std::min(shape==13?8u:16u,W-used);v=(i<<low)|(mix(W)&mask(low));break;}
  }
  check(v<=mask(W));out.push_back(v);
 }
 check(std::unordered_set<word>(out.begin(),out.end()).size()==n);
 return out;
}
template<unsigned W,Family F>void quality(unsigned seeds){
 constexpr unsigned A=std::min(10u,W/2-2),B=A>3?A-3:1;
 const std::size_t n=(std::size_t{1}<<A)*16*19/20;
 for(unsigned shape=0;shape<std::size(shapes);++shape){const auto keys=input<W>(n,shape);
  for(unsigned s=0;s<seeds;++s){std::mt19937_64 rng(0x73656564+s);const word e=rng();permutation<W,F>p(e),q(mix(e^0x7365636f6e646172ULL));
   std::vector<unsigned>counts(1u<<A),down(1u<<B);unsigned spill=0,chain=0;
   for(word key:keys){unsigned b=p(key)>>(W-A);if(counts[b]++<16)continue;++spill;unsigned qb=q(key)>>(W-B);if(down[qb]++>=16)++chain;}
   std::printf("%u,%s,%s,%u,%llu,%zu,%u,%u,%u,%u,%u,%u\n",W,names[F],shapes[shape],s,(unsigned long long)e,n,A,B,spill,chain,*std::max_element(counts.begin(),counts.end()),*std::max_element(down.begin(),down.end()));
  }
 }
}
template<unsigned W,Family F,bool Inverse,unsigned L>[[gnu::noinline]]word kernel(const permutation<W,F>&incoming,std::size_t n,word initial){
 const auto p=incoming;auto step=[&](word x){if constexpr(Inverse)return p.inverse(x);else return p(x);};
 word a=initial&mask(W),b=(initial+1)&mask(W),c=(initial+2)&mask(W),d=(initial+3)&mask(W),e=(initial+4)&mask(W),f=(initial+5)&mask(W),g=(initial+6)&mask(W),h=(initial+7)&mask(W);
 for(std::size_t i=0;i<n;i+=L){
  a=step(a);
  if constexpr(L>=4){b=step(b);c=step(c);d=step(d);}
  if constexpr(L>=8){e=step(e);f=step(f);g=step(g);h=step(h);}
  if constexpr(L==1)asm volatile("":"+r"(a));
  if constexpr(L==4)asm volatile("":"+r"(a),"+r"(b),"+r"(c),"+r"(d));
  if constexpr(L==8)asm volatile("":"+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f),"+r"(g),"+r"(h));
 }
 if constexpr(L==1)return a;else if constexpr(L==4)return a^b^c^d;else return a^b^c^d^e^f^g^h;
}
template<unsigned W,Family F,bool Inverse,unsigned L>void timing(unsigned repetitions){
 for(unsigned r=0;r<repetitions;++r){permutation<W,F>p(mix(9381+r));auto warm=kernel<W,F,Inverse,L>(p,16384,731);asm volatile(""::"r"(warm));
  constexpr std::size_t n=1u<<21;const auto start=std::chrono::steady_clock::now();auto result=kernel<W,F,Inverse,L>(p,n,731);asm volatile("":"+r"(result)::"memory");const auto stop=std::chrono::steady_clock::now();
  double ns=std::chrono::duration<double,std::nano>(stop-start).count()/n;
  std::printf("%u,%s,%s,%u,%u,%zu,%.6f,%llu\n",W,names[F],Inverse?"inverse":"forward",L,r,n,ns,(unsigned long long)result);
 }
}
template<unsigned W,Family F>void run(std::string_view mode,unsigned repetitions){
 verify<W,F>();if(mode=="quality")quality<W,F>(repetitions);
 if(mode=="timing"){timing<W,F,false,1>(repetitions);timing<W,F,false,4>(repetitions);timing<W,F,false,8>(repetitions);timing<W,F,true,1>(repetitions);timing<W,F,true,4>(repetitions);timing<W,F,true,8>(repetitions);}
}
template<unsigned W>void width(std::string_view mode,unsigned r){run<W,strong>(mode,r);run<W,right_m_right>(mode,r);run<W,right_m_left>(mode,r);run<W,pcg>(mode,r);run<W,hybrid_hypothesis>(mode,r);}
int main(int argc,char**argv){check(argc==3);std::string_view mode(argv[1]);unsigned reps=std::strtoul(argv[2],nullptr,10);check(reps&&(mode=="check"||mode=="quality"||mode=="timing"));
 if(mode=="quality")std::puts("width,family,shape,seed_index,entropy,n,primary_bits,secondary_bits,spilled,chained,primary_max,secondary_max");
 if(mode=="timing")std::puts("width,family,direction,independent_chains,rep,operations,ns_per_operation,checksum");
 width<8>(mode,reps);width<16>(mode,reps);width<24>(mode,reps);width<32>(mode,reps);width<40>(mode,reps);width<48>(mode,reps);width<56>(mode,reps);width<64>(mode,reps);
 std::fprintf(stderr,"PASS: all widths, all five families, bidirectional inverse identities; exhaustive8/16,65536 samples wider,4seeds.\n");
}
