#pragma once
// Included after the complete-operation benchmark's owner definitions.
namespace mm_study {
struct lifetime_input {
    static constexpr unsigned background=16384,cycles=2048,bag_count=513,bag_values=65;
    std::vector<word> keys;
    std::vector<Value> initial,large;
    std::vector<std::vector<Value>> bags;
    lifetime_input():keys(background+bag_count+1),initial(background),large(65535/V),bags(bag_count) {
        for(unsigned i=0;i<keys.size();++i)keys[i]=key(i,32452867);
        for(unsigned i=0;i<background;++i)initial[i]=payload(keys[i],0);
        for(unsigned j=0;j<large.size();++j)large[j]=payload(keys.back(),j);
        for(unsigned i=0;i<bag_count;++i)for(unsigned j=0;j<bag_values;++j)bags[i].push_back(payload(keys[background+i],j));
    }
};
template<class Owner> [[gnu::noinline]] void lifetime_run(const char* name,const lifetime_input& in,unsigned rep) {
    using clock=std::chrono::steady_clock;using I=lifetime_input;
    auto elapsed=[](auto start){return std::chrono::duration<double,std::nano>(clock::now()-start).count();};
    std::unique_ptr<Owner> owner;
    auto report=[&](const char* phase,double ns,std::size_t operations){
        std::cout<<name<<','<<phase<<','<<V<<','<<rep<<','<<operations<<','<<ns<<','<<ns/operations<<','<<(owner?bytes(*owner):0)<<'\n';
    };
    auto verify=[&](word k,std::span<const Value> expected){unsigned count=0;
        const auto hit=find(*owner,k,[&](const Value& got){ht::require(count<expected.size()&&got==expected[count++],"lifetime ordered values");});
        ht::require(hit&&count==expected.size(),"lifetime multiplicity");
    };
    auto background=[&]{for(unsigned i=0;i<I::background;++i)verify(in.keys[i],{&in.initial[i],1});};
    auto start=clock::now();
    if constexpr(std::is_same_v<Owner,Abseil>){owner=std::make_unique<Owner>();owner->reserve(I::background+I::bag_count);}
    else owner=std::make_unique<Owner>(bits(32452867));
    for(unsigned i=0;i<I::background;++i)append(*owner,in.keys[i],in.initial[i]);
    report("build_background",elapsed(start),I::background);background();
    unsigned erased=0;start=clock::now();
    for(unsigned cycle=0;cycle<I::cycles;++cycle){
        for(unsigned j=0;j<9;++j)append(*owner,in.keys.back(),in.large[j]);
        erased+=owner->erase(Key(in.keys.back()));
    }
    report("drain_refill",elapsed(start),I::cycles*10);
    ht::require(erased==I::cycles&&owner->size()==I::background&&!find(*owner,in.keys.back(),[](const Value&){}),"lifetime drained population");background();
    start=clock::now();for(const auto& value:in.large)append(*owner,in.keys.back(),value);
    report("sparse_maximum",elapsed(start),in.large.size());verify(in.keys.back(),in.large);
    start=clock::now();const auto released=owner->erase(Key(in.keys.back()));
    report("sparse_release",elapsed(start),1);ht::require(released,"sparse maximum release");
    start=clock::now();for(unsigned i=0;i<I::bag_count;++i)for(const auto& v:in.bags[i])append(*owner,in.keys[I::background+i],v);
    report("family_fill",elapsed(start),I::bag_count*I::bag_values);
    for(unsigned i=0;i<I::bag_count;++i)verify(in.keys[I::background+i],in.bags[i]);
    erased=0;start=clock::now();for(unsigned i=0;i<I::bag_count;++i)if(i%256)erased+=owner->erase(Key(in.keys[I::background+i]));
    report("family_erase",elapsed(start),I::bag_count-3);
    ht::require(erased==I::bag_count-3&&owner->size()==I::background+3,"family survivors");
    for(unsigned i=0;i<I::bag_count;++i){if(i%256)ht::require(!find(*owner,in.keys[I::background+i],[](const Value&){}),"family removed key");
        else verify(in.keys[I::background+i],in.bags[i]);}
    background();if constexpr(!std::is_same_v<Owner,Abseil>)owner->validate();
    start=clock::now();owner.reset();report("destroy",elapsed(start),I::background+3);
}
inline int lifetimes(unsigned reps,std::string_view only="",unsigned rep_base=0) {
    check_owner(only);
    ht::require(reps>0,"lifetime repetitions");const lifetime_input in;
    std::cout<<"implementation,phase,V,rep,operations,total_ns,ns,bytes\n";
    for(unsigned trial=0;trial<reps;++trial){const auto rep=rep_base+trial;
      for(unsigned i=0;i<owner_count;++i){const auto which=(i+rep)%owner_count;
      if(!only.empty()&&owner_names[which]!=only)continue;
      switch(which){
#if WRITE_ARCH_W == 24 && !WRITE_ARCH_MM_FOCUSED
      case 0:lifetime_run<Maintained>("maintained",in,rep);break;
      case 1:lifetime_run<Prior>("prior_v74",in,rep);break;
#endif
      case 2:lifetime_run<Packed>("packed_inline",in,rep);break;
#if WRITE_ARCH_W == 24 && !WRITE_ARCH_MM_FOCUSED
      case 3:lifetime_run<Whole>("whole_inline",in,rep);break;
#endif
      case 4:lifetime_run<Abseil>("abseil",in,rep);break;
#if WRITE_ARCH_W == 24 && !WRITE_ARCH_MM_FOCUSED
      case 5:lifetime_run<WholeDirect>("whole_direct",in,rep);break;
      case 6:if constexpr(V==64)lifetime_run<PackedCompact>("packed_compact",in,rep);break;
      case 7:if constexpr(V==64)lifetime_run<WholeCompact>("whole_compact",in,rep);break;
      case 8:if constexpr(V==64)lifetime_run<WholePrefix>("whole_prefix_diagnostic",in,rep);break;
      case 9:if constexpr(V==64)lifetime_run<CompactDirect>("compact_direct",in,rep);break;
      case 10:if constexpr(V==64)lifetime_run<WholeLegacy>("whole_legacy_writer",in,rep);break;
#endif
    }}}
    return 0;
}
}
