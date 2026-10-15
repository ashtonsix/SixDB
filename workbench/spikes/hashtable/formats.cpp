#include "promoted.h"
#include "ordinary_baseline.h"
#include <map>
#include <random>
#include <numeric>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>
#ifndef HT_FORMAT_ACCESS
#define HT_FORMAT_ACCESS 0
#endif
#ifndef HT_FORMAT_APPEND
#define HT_FORMAT_APPEND 0
#endif
#ifndef HT_FORMAT_BAGS
#define HT_FORMAT_BAGS 0
#endif
#ifndef HT_FORMAT_INLINE
#define HT_FORMAT_INLINE 0
#endif
#ifndef HT_FORMAT_K
#define HT_FORMAT_K 0
#endif
#ifndef HT_FORMAT_LOCAL_REDUCTION
#define HT_FORMAT_LOCAL_REDUCTION 0
#endif

namespace ht {
static_assert(HT_FORMAT_ACCESS+HT_FORMAT_APPEND+HT_FORMAT_BAGS+HT_FORMAT_INLINE<=1);
inline constexpr unsigned candidate_pairs=HT_FORMAT_INLINE?3:(HT_FORMAT_APPEND||HT_FORMAT_BAGS)?2:HT_FORMAT_ACCESS?4:7;
template<unsigned Arm> inline constexpr record_format selected_format=
    (HT_FORMAT_ACCESS||HT_FORMAT_APPEND||HT_FORMAT_BAGS||HT_FORMAT_INLINE)?record_format::exact:record_format(Arm/2);
template<unsigned Arm> inline constexpr record_access selected_access=
    HT_FORMAT_INLINE?record_access::both:HT_FORMAT_ACCESS && Arm<2*candidate_pairs?record_access(Arm/2):record_access::exact;
template<unsigned Arm,container_kind Kind> inline constexpr bool selected_append=
    Kind==container_kind::multimap && Arm<2*candidate_pairs && (HT_FORMAT_BAGS || HT_FORMAT_INLINE || (HT_FORMAT_APPEND && Arm/2==1));
template<unsigned Arm,container_kind Kind> using selected_bags=std::conditional_t<
    HT_FORMAT_BAGS && Arm/2==1,buffer_bags<8>,links<64,false,0,selected_append<Arm,Kind>>>;
template<unsigned Arm,container_kind Kind> inline constexpr unsigned selected_inline=
    HT_FORMAT_INLINE && Kind==container_kind::multimap && Arm>=2 && Arm<2*candidate_pairs?2:1;
template<unsigned K> word format_key(unsigned i){return word(i)*0xd6e8feb86659fd93ULL&mask(K);}
template<class Body> auto format_contents(const Body& body) {
    std::map<word,std::vector<word>> result;
    body.each([&](word k,word v){result[k].push_back(v);});
    for(auto& [k,v]:result)std::sort(v.begin(),v.end());return result;
}
template<unsigned K,container_kind Kind,record_format Format,bool Direct,record_access Access=record_access::exact,bool AdmittedAppend=false,
         class Bags=links<64,false,0,AdmittedAppend>,unsigned InlineValues=1> void format_check(oa::Pool& pool) {
    using Small=small_table<K,Kind,(Kind==container_kind::set?0:8),AdmittedAppend,Bags,8*InlineValues>;
    using Body=promoted_table<K,Kind,(Kind==container_kind::set?0:8),1,0,64,64,Format,Direct,Access,AdmittedAppend,Bags,8*InlineValues>;
    Small source(pool,19);for(unsigned i=0;i<8;++i)source.set_value(format_key<K>(i),i*13);
    auto table=Body::promote(source,pool,41,73);require(bool(table),"format promotion");
    std::map<word,std::vector<word>> expected;
    for(unsigned i=0;i<8;++i)expected[format_key<K>(i)]={Kind==container_kind::set?0:i*13};
    auto put=[&](unsigned i,word value) {
        auto key=format_key<K>(i);table->set_value(key,value);auto& v=expected[key];
        if constexpr(Kind==container_kind::multimap){v.push_back(value);std::sort(v.begin(),v.end());}
        else v={Kind==container_kind::set?0:value};
    };
    for(unsigned i=8;i<240;++i)put(i,mix(i));
    for(unsigned i=0;i<240;i+=3)for(unsigned j=0;j<5;++j)put(i,mix(i+j));
    require(format_contents(*table)==expected,"format complete records and bag refs");
    for(unsigned i=0;i<256;++i) {
        std::vector<word> got;auto key=format_key<K>(i);
        bool found=table->get(key,[&](word v){got.push_back(v);});std::sort(got.begin(),got.end());
        require(found==expected.contains(key),"format hit/miss");if(found)require(got==expected.at(key),"format point values");
    }
    // Erase primary, secondary and chain records, including MM bags, then reuse.
    for(unsigned i=0;i<240;i+=2){table->erase(format_key<K>(i));expected.erase(format_key<K>(i));}
    for(unsigned i=0;i<240;i+=2)put(i,~mix(i));
    table->validate();require(format_contents(*table)==expected,"format swap deletion and reuse");
    std::array<small_event,0> none;small_journal refused{none};
    require(!table->try_set(format_key<K>(250),17,refused),"format observer refusal");
    require(format_contents(*table)==expected,"format refusal preserves contents");
}

template<class F> double format_time(F&& f){auto a=clock_type::now();f();return std::chrono::duration<double,std::nano>(clock_type::now()-a).count();}
template<unsigned K,container_kind Kind,unsigned A,unsigned Arm> void format_trial(oa::Pool& pool,unsigned rep,unsigned fanout) {
    constexpr unsigned V=Kind==container_kind::set?0:8;
    constexpr unsigned B=A>3?A-4:0;
    constexpr auto Format=selected_format<Arm>;
    constexpr auto Access=selected_access<Arm>;
    constexpr bool AdmittedAppend=selected_append<Arm,Kind>;
    using Bags=selected_bags<Arm,Kind>;
    constexpr unsigned InlineValues=selected_inline<Arm,Kind>;
    constexpr unsigned Stored=Kind==container_kind::multimap?8*InlineValues:V;
    constexpr unsigned TagBits=Kind==container_kind::multimap?inline_bag<8,8*InlineValues>::tag_bits:1;
    using Reference=record_table<K,Kind,V,A,B,128,128,record_format::exact>;
    constexpr bool expanded=HT_FORMAT_INLINE && Kind==container_kind::multimap && Arm/2==2;
    constexpr unsigned P=expanded?((128+8*Reference::primary.shape.slots+63)/64)*64:128;
    constexpr unsigned Q=expanded?((128+8*Reference::secondary.shape.slots+63)/64)*64:128;
    using Candidate=promoted_table<K,Kind,V,A,B,P,Q,(Arm<2*candidate_pairs?Format:record_format::legacy_power_two),bool(Arm%2),Access,AdmittedAppend,Bags,8*InlineValues>;
    using Body=std::conditional_t<(Arm<2*candidate_pairs),Candidate,ordinary_abseil<K,Kind,Arm==2*candidate_pairs+1>>;
    // Keep the historical population fixed for the matched read/format study;
    // this is not the default bucket-capacity policy.
    using Control=record_table<K,Kind,V,A,B,128,128,record_format::legacy_power_two>;
    const unsigned n=std::min((Control::primary.shape.slots<<A)*7/8,K==8?128u:~0u);
    const unsigned rounds=std::max(16u,65536/n);
    std::vector<unsigned> order(n);std::iota(order.begin(),order.end(),0);std::mt19937 random(91);std::shuffle(order.begin(),order.end(),random);
    std::optional<Body> body;word sum=0;
    const auto construct=format_time([&]{
        if constexpr(Arm<2*candidate_pairs){small_table<K,Kind,V,AdmittedAppend,Bags,8*InlineValues> source(pool,19);body=Candidate::promote(source,pool,41,73);require(bool(body),"format construct");}
        else {body.emplace();body->reserve(n);}
    });
    const auto build=format_time([&]{for(auto i:order){
        const auto next=format_key<K>((i+1)%n);
        if constexpr(Kind==container_kind::multimap) {
            if(fanout) {
                word last=next;
                for(unsigned j=1;j<fanout;++j){const word v=mix(word(i)+(word(j)<<32));body->set_value(format_key<K>(i),v);last^=v;}
                body->set_value(format_key<K>(i),last);
            }else if(i%3==0){body->set_value(format_key<K>(i),mix(i));body->set_value(format_key<K>(i),mix(i)^next);}
            else body->set_value(format_key<K>(i),next);
        }else body->set_value(format_key<K>(i),next);
    }});
    const auto initial=body->footprint();
    std::array<word,3> population{};if constexpr(Arm<2*candidate_pairs)population=body->population();
    std::map<word,std::vector<word>> expected;
    for(unsigned i=0;i<n;++i){auto& v=expected[format_key<K>(i)];v={Kind==container_kind::set?0:format_key<K>((i+1)%n)};
        if constexpr(Kind==container_kind::multimap) {
            if(fanout) {
                word last=format_key<K>((i+1)%n);v.clear();
                for(unsigned j=1;j<fanout;++j){const word x=mix(word(i)+(word(j)<<32));v.push_back(x);last^=x;}v.push_back(last);
            }else if(i%3==0)v={mix(i),mix(i)^format_key<K>((i+1)%n)};
        }
        std::sort(v.begin(),v.end());}
    require(format_contents(*body)==expected,"timed format build oracle");
    for(unsigned i=0;i<2*n;++i) {
        std::vector<word> values;
        const bool found=body->get(format_key<K>(i),[&](word v){values.push_back(v);});
        std::sort(values.begin(),values.end());require(found==(i<n),"timed format point hit/miss oracle");
        if(found)require(values==expected.at(format_key<K>(i)),"timed format point values oracle");
        else require(values.empty(),"miss cannot consume values");
    }
    // Prove the full dependency graph independently of the timed loop. Bag
    // order is irrelevant: every bag's XOR names the next key in the cycle.
    if constexpr(Kind!=container_kind::set) {
        word key=0;
        for(unsigned i=0;i<n;++i) {
            require(key==format_key<K>(i),"serial graph visits every key exactly once");
            word next=0;require(body->get(key,[&](word v){next^=v;}),"serial graph all hits");key=next;
        }
        require(key==0,"serial graph closes after n steps");
    }
#if HT_FORMAT_LOCAL_REDUCTION
    // A loop-local result cannot alias the container, unlike an outlined
    // closure's borrowed accumulator. The all-tier operation and every value
    // consumer are otherwise unchanged, including the ordinary baselines.
    word cycle_sum=0;
    for(const auto& [key,values]:expected) {++cycle_sum;for(auto value:values)cycle_sum+=value;}
    cycle_sum*=rounds;
    auto reads=[&](bool misses) {
        word reduced=0;
        for(unsigned r=0;r<rounds;++r)for(auto i:order)
            reduced+=body->get(format_key<K>(i+(misses?n:0)),[&](word v){reduced+=v;});
        return reduced;
    };
    word read_result=reads(false);require(read_result==cycle_sum,"independent hit reduction oracle");sum+=read_result;
    const auto hit=format_time([&]{read_result=reads(false);});
    require(read_result==cycle_sum,"timed hit reduction oracle");sum+=read_result;
    read_result=reads(true);require(read_result==0,"independent miss reduction oracle");sum+=read_result;
    const auto miss=format_time([&]{read_result=reads(true);});
    require(read_result==0,"timed miss reduction oracle");sum+=read_result;
    auto dependent=[&] {
        word key=0,reduced=0;
        for(unsigned i=0;i<rounds*n;++i) {
            if constexpr(Kind==container_kind::set) {
                const bool found=body->get(format_key<K>(key),[](word){});
                key=(key+(found?1:7))%n;reduced+=found;
            }else {
                word next=0;reduced+=body->get(key,[&](word v){next^=v;reduced+=v;});key=next;
            }
        }
        return reduced;
    };
    read_result=dependent();require(read_result==cycle_sum,"dependent reduction oracle");sum+=read_result;
    const auto serial=format_time([&]{read_result=dependent();});
    require(read_result==cycle_sum,"timed dependent reduction oracle");sum+=read_result;
#else
    auto reads=[&](bool misses){for(unsigned r=0;r<rounds;++r)for(auto i:order)sum+=body->get(format_key<K>(i+(misses?n:0)),[&](word v){sum+=v;});};
    reads(false);const auto hit=format_time([&]{reads(false);});
    reads(true);const auto miss=format_time([&]{reads(true);});
    auto dependent=[&]{word key=0;for(unsigned i=0;i<rounds*n;++i){
        if constexpr(Kind==container_kind::set){const bool found=body->get(format_key<K>(key),[](word){});key=(key+(found?1:7))%n;sum+=found;}
        else {word next=0;sum+=body->get(key,[&](word v){next^=v;sum+=v;});key=next;}
    }};
    dependent();const auto serial=format_time(dependent);
#endif
    const auto update=format_time([&]{for(auto i:order)body->set_value(format_key<K>(i),mix(i));});
    const auto updated=body->footprint();
    for(unsigned i=0;i<n;++i){auto& v=expected[format_key<K>(i)];if constexpr(Kind==container_kind::multimap){v.push_back(mix(i));std::sort(v.begin(),v.end());}else v={Kind==container_kind::set?0:mix(i)};}
    require(format_contents(*body)==expected,"timed format update oracle");
    const auto erase=format_time([&]{for(unsigned i=0;i<n;i+=2)body->erase(format_key<K>(i));});
    for(unsigned i=0;i<n;i+=2)expected.erase(format_key<K>(i));
    require(format_contents(*body)==expected,"timed format erase oracle");
    const auto after=body->footprint();const auto release=format_time([&]{body.reset();});
    asm volatile(""::"r"(sum):"memory");
    auto emit=[&](const char* phase,double ns,unsigned ops,memory m){
        std::printf("%u,%u,%u,%u,%u,%u,%s,%u,%.3f,%llu,%llu,%llu,%u,%u,%llu,%llu,%llu,%u\n",Arm,K,unsigned(Kind),A,n,rep,phase,ops,ns,
            (unsigned long long)m.requested,(unsigned long long)m.backing,(unsigned long long)m.metadata,
            Arm<2*candidate_pairs?record_table<K,Kind,Stored,A,B,P,Q,(Arm<2*candidate_pairs?Format:record_format::legacy_power_two),bool(Arm%2),Access,TagBits>::primary.shape.slots:0,
            Arm<2*candidate_pairs?record_table<K,Kind,Stored,A,B,P,Q,(Arm<2*candidate_pairs?Format:record_format::legacy_power_two),bool(Arm%2),Access,TagBits>::secondary.shape.slots:0,
            (unsigned long long)population[0],(unsigned long long)population[1],(unsigned long long)population[2],fanout);
    };
    emit("construct",construct,1,initial);emit("build",build,n,initial);emit("hit",hit,rounds*n,initial);emit("miss",miss,rounds*n,initial);
    emit(Kind==container_kind::set?"serial_caller_cycle":"serial_value_cycle",serial,rounds*n,initial);
    emit("update",update,n,updated);emit("erase",erase,(n+1)/2,after);emit("release",release,1,after);
    emit("lifecycle",construct+build+update+erase+release,n,after);
}

template<unsigned K,container_kind Kind> void format_kind(oa::Pool& pool,bool checking,unsigned arm,unsigned rep,unsigned large,unsigned fanout) {
    lanes<2*candidate_pairs+2>([&](auto a){if(a!=arm)return;
        if(checking){if constexpr(a<2*candidate_pairs)format_check<K,Kind,selected_format<a>,bool(a%2),selected_access<a>,selected_append<a,Kind>,selected_bags<a,Kind>,selected_inline<a,Kind>>(pool);}
        else {if(large && K>8)format_trial<K,Kind,(K>8?10:6),a>(pool,rep,fanout);else format_trial<K,Kind,6,a>(pool,rep,fanout);}
    });
}
} // namespace ht
#if HT_FORMAT_K
#define HT_FORMAT_JOIN_(k,c) format_width_##k##_##c
#define HT_FORMAT_JOIN(k,c) HT_FORMAT_JOIN_(k,c)
void HT_FORMAT_JOIN(HT_FORMAT_K,HT_FORMAT_KIND)(ht::oa::Pool& pool,bool check,unsigned arm,unsigned rep,unsigned large,unsigned fanout){
    ht::format_kind<HT_FORMAT_K,ht::container_kind(HT_FORMAT_KIND)>(pool,check,arm,rep,large,fanout);
}
#else
#define HT_FORMAT_DECLARE_ONE(K,C) void format_width_##K##_##C(ht::oa::Pool&,bool,unsigned,unsigned,unsigned,unsigned);
#define HT_FORMAT_DECLARE(K) HT_FORMAT_DECLARE_ONE(K,0) HT_FORMAT_DECLARE_ONE(K,1) HT_FORMAT_DECLARE_ONE(K,2)
HT_FORMAT_DECLARE(8) HT_FORMAT_DECLARE(16) HT_FORMAT_DECLARE(24) HT_FORMAT_DECLARE(32)
HT_FORMAT_DECLARE(40) HT_FORMAT_DECLARE(48) HT_FORMAT_DECLARE(56) HT_FORMAT_DECLARE(64)
int main(int argc,char** argv){
    using namespace ht;require(argc>=2,"formats check | run K kind arm reps large");const bool check=std::string_view(argv[1])=="check";
    using Width=void(*)(oa::Pool&,bool,unsigned,unsigned,unsigned,unsigned);
#define HT_FORMAT_ROW(K) std::array<Width,3>{format_width_##K##_0,format_width_##K##_1,format_width_##K##_2}
    const std::array widths{HT_FORMAT_ROW(8),HT_FORMAT_ROW(16),HT_FORMAT_ROW(24),HT_FORMAT_ROW(32),HT_FORMAT_ROW(40),HT_FORMAT_ROW(48),HT_FORMAT_ROW(56),HT_FORMAT_ROW(64)};
    if(check){pool_owner owner;for(auto& row:widths)for(auto width:row)for(unsigned arm=0;arm<2*candidate_pairs;++arm)width(*owner.pool,true,arm,0,0,0);
        std::puts(HT_FORMAT_APPEND?"all-width/all-kind checked/admitted bag append, promotion, deletion, reuse, and refusal passed":HT_FORMAT_ACCESS?"all-width/all-kind bucket-read permissions, promotion, bags, deletion, reuse, and refusal passed":"all-width/all-kind format promotion, bags, deletion, reuse, and refusal passed");return 0;}
    require(argc==7 || (HT_FORMAT_INLINE && argc==8),"formats run K kind arm reps large [fanout]");const unsigned k=std::stoul(argv[2]),kind=std::stoul(argv[3]),arm=std::stoul(argv[4]),reps=std::stoul(argv[5]),large=std::stoul(argv[6]);
    const unsigned fanout=argc==8?std::stoul(argv[7]):0;require(fanout<=1024,"bounded fanout control");
    require(k>=8&&k<=64&&k%8==0&&kind<3&&arm<2*candidate_pairs+2&&reps>0&&large<2,"format arguments");
    std::puts("arm,K,kind,A,n,rep,phase,operations,ns,requested,owned_capacity,metadata,primary_slots,secondary_slots,build_primary_rows,build_secondary_rows,build_overflow_rows,fanout");
    // One contender/case per process avoids a different arm's Pool history.
    // Each repetition has a fresh Pool; allocator history is still recorded.
    for(unsigned rep=0;rep<reps;++rep){pool_owner owner;widths[k/8-1][kind](*owner.pool,false,arm,rep,large,fanout);}
}
#endif
