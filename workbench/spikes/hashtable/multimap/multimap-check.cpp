#include "multimap.h"
#include <iostream>
#include <random>
#include <unordered_map>

namespace write_arch {
// Test-only physical witness. No production borrowing API or lifetime promise.
struct multimap_probe {
    template<class Owner> static const byte* first(Owner& owner,word key) {
        const byte* result=nullptr;
        const bool found=owner.index_.find_cell(key,[&](auto cell){
            const auto* p=cell.source+cell.row*Owner::cell_bytes;
            const auto meta=ht::load<8>(p);
            result=Owner::is_external(meta)?owner.buffers_.data(ht::load<8>(p+8),Owner::family(meta)):p+8;
        });
        ht::require(found&&result,"alias witness live value");return result;
    }
};
}

template<unsigned W,unsigned V,bool Whole,unsigned Inline=(V==8?2:1),
    write_arch::buffer_representation Representation=write_arch::buffer_representation::streampack_capacity,bool Direct=false,bool Coalesced=true> void check() {
    using namespace write_arch;
    using Value=ht::bytes<V>;
    multimap<W,10,V,Whole,Inline,Representation,Direct,Coalesced> owner(731);
    std::unordered_map<word,std::vector<Value>> expected;
    auto payload=[](unsigned ordinal){Value value{};for(unsigned i=0;i<V;++i)value[i]=byte((ordinal*131+i*17)^(ordinal>>3));return value;};
    auto verify=[&]{
        ht::require(owner.size()==expected.size(),"mutable multimap key count");
        for(const auto& [k,values]:expected){unsigned count=0;
            const auto found=owner.find(k,[&](const Value& got){ht::require(count<values.size()&&got==values[count++],"mutable multimap complete ordered values");});
            ht::require(found&&count==values.size(),"mutable multimap multiplicity");
            typename decltype(owner)::staged_probe state{};bool staged_hit=false;count=0;unsigned steps=0;
            auto emit=[](const byte* p,std::size_t n){ht::require(p&&n,"multimap stage request");};
            owner.prepare_staged(state,k,emit);
            while(!owner.advance(state,staged_hit,[&](const Value& got){
                ht::require(count<values.size()&&got==values[count++],"staged multimap ordered value");
            },emit))ht::require(++steps<10000,"staged multimap termination");
            ht::require(staged_hit&&count==values.size(),"staged multimap multiplicity");
        }
        owner.validate();
    };
    auto append=[&](word k,const Value& value){const bool fresh=!expected.contains(k);ht::require(owner.insert(k,value)==fresh,"mutable multimap insert result");expected[k].push_back(value);};
    ht::permutation<W,W-10> route{731};
    const auto alias_key=route.recover(12,1777);append(alias_key,payload(1777));
    for(unsigned count=1;count<17;++count){
        const auto expected_value=expected.at(alias_key).front();
        const auto* source=multimap_probe::first(owner,alias_key);
        ht::require(!owner.insert_bytes(alias_key,std::span<const byte,V>{source,V}),"self-source append remains existing");
        expected.at(alias_key).push_back(expected_value);verify();
    }
    ht::require(owner.erase(alias_key),"alias fixture release");expected.erase(alias_key);verify();
    // Different primary/Swiss slot coordinates and StreamPack ranks; exceed a
    // complete 256-buffer block and retain holes through reuse.
    for(unsigned i=0;i<700;++i){const auto k=route.recover(i%3,i/3);for(unsigned j=0;j<9;++j)append(k,payload(i*65537+j/2));}
    verify();
    for(unsigned i=0;i<700;i+=2){const auto k=route.recover(i%3,i/3);ht::require(owner.erase(k),"mutable multimap erase");expected.erase(k);}
    verify();
    for(unsigned i=0;i<700;i+=2){const auto k=route.recover(i%3,i/3);for(unsigned j=0;j<9;++j)append(k,payload(i*65537+j+17));}
    verify();
    std::mt19937_64 rng(9137);
    for(unsigned step=0;step<12000;++step){const auto k=route.recover(rng()%8,rng()%500);
        if(rng()%4==0)ht::require(owner.erase(k)==bool(expected.erase(k)),"random multimap erase");
        else append(k,payload(step%11));
        if(step%397==0)verify();
    }
    verify();
    for(const auto& [k,values]:expected)ht::require(owner.erase(k),"drain multimap");expected.clear();
    ht::require(!owner.size()&&!owner.live_buffers(),"all external buffers released");owner.validate();
    // A bounded concurrent population must not accumulate descriptor history.
    std::size_t drained_bytes=0;
    for(unsigned cycle=0;cycle<5000;++cycle){const auto k=route.recover(cycle%8,cycle%500);
        for(unsigned j=0;j<9;++j)append(k,payload(cycle*137+j));
        if(cycle%251==0)verify();
        ht::require(owner.erase(k),"repeated family drain");expected.erase(k);
        if(cycle==10)drained_bytes=owner.bytes_used();
        if(cycle>10)ht::require(owner.bytes_used()==drained_bytes,"bounded owner metadata after drain");
    }
    owner.validate();const auto empty_bytes=owner.bytes_used();
    const word large_key=route.recover(11,123);
    for(unsigned i=0;i<capacity_buffers<V>::max_values;++i)append(large_key,payload(i));
    verify();const auto bytes_before=owner.bytes_used();bool refused=false;
    ht::require(bytes_before-empty_bytes<2*capacity_buffers<V>::max_values*V+4096,"sparse maximum bag allocated capacity");
    try{owner.insert(large_key,payload(17));}catch(const std::runtime_error&){refused=true;}
    ht::require(refused&&owner.bytes_used()==bytes_before,"maximum refuses without allocation");verify();
    ht::require(owner.erase(large_key)&&!owner.live_buffers(),"maximum bag released");owner.validate();
    for(unsigned q=0;q<1000;++q){typename decltype(owner)::staged_probe state{};bool found=true;unsigned steps=0;
        auto emit=[](const byte* p,std::size_t n){ht::require(p&&n,"absent stage request");};
        owner.prepare_staged(state,route.recover(q%3,q/3),emit);
        while(!owner.advance(state,found,[](const Value&){ht::require(false,"absent multimap consumed value");},emit))
            ht::require(++steps<10000,"absent multimap termination");
        ht::require(!found,"staged multimap absent");
    }
    std::cout<<"multimap_check,ok,W,"<<W<<",V,"<<V<<",whole,"<<Whole<<",inline,"<<Inline<<",representation,"<<unsigned(Representation)<<",direct,"<<Direct<<",coalesced,"<<Coalesced<<'\n';
}
int main() {
    try {
        check<24,8,false>();check<24,8,true>();check<24,64,false>();check<24,64,true>();
        check<40,8,false>();check<40,8,true>();check<40,64,false>();check<40,64,true>();
        check<24,64,false,0>();check<24,64,true,0>();check<40,64,false,0>();check<40,64,true,0>();
        check<24,64,true,0,write_arch::buffer_representation::mutable_prefix_diagnostic>();
        check<40,64,true,0,write_arch::buffer_representation::mutable_prefix_diagnostic>();
        constexpr auto full=write_arch::buffer_representation::streampack_capacity;
        check<24,8,true,2,full,true>();check<40,8,true,2,full,true>();
        check<24,64,true,1,full,true>();check<40,64,true,1,full,true>();
        check<24,64,true,0,full,true>();check<40,64,true,0,full,true>();
        check<24,64,true,1,full,false,false>();check<40,64,true,1,full,false,false>();
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
