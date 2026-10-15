#include "aggregate.h"

namespace ht {
double elapsed(clock_type::time_point begin) {
    return std::chrono::duration<double,std::nano>(clock_type::now()-begin).count();
}
struct pair_input {
    std::array<std::vector<record>,2> rows;
    std::vector<word> keys;
    std::unordered_map<word,word> expected;
    unsigned local,overlap;
    pair_input(unsigned n,unsigned percent):local(n),overlap(percent) {
        unsigned shared=n*percent/100,total=n*2-shared;
        keys=make_input<40>(total,82452).keys;
        std::mt19937_64 random(314159);
        for(unsigned side=0;side<2;++side) {
            for(unsigned i=0;i<n;++i) {
                auto k=keys[side?n-shared+i:i];
                auto v=i%17==0?~word(0):i%19==0?0:random();
                rows[side].push_back({k,v});expected[k]+=v;
            }
            std::shuffle(rows[side].begin(),rows[side].end(),random);
        }
        std::shuffle(keys.begin(),keys.end(),random);
    }
};
struct copy_result {word copied=0;bool complete=true;};
template<bool Unique,class Destination,class Source>
[[gnu::noinline]] copy_result migrate(Destination& destination,const Source& source) {
    copy_result result;
    source.each([&](word k,word v) __attribute__((always_inline)) {
        if(!result.complete)return;
        if constexpr(Unique)result.complete=destination.insert_absent(k,v);
        else {
            auto d=destination.upsert(k,v,add{});
            require(d!=0,"initially empty migration destination");result.complete=d>0;
        }
        result.copied+=result.complete;
    });
    return result;
}
template<class Map> [[gnu::noinline]] word combine_source(Map& destination,const Map& source) {
    word added=0;
    source.each([&](word k,word v) __attribute__((always_inline)) {
        auto d=destination.upsert(k,v,add{});require(d>=0,"baseline merge");added+=d;
    });
    return added;
}
template<class Map> void verify(const Map& map,const std::unordered_map<word,word>& expected) {
    map.validate();std::unordered_map<word,word> actual;
    map.each([&](word k,word v){require(actual.emplace(k,v).second,"transition uniqueness");});
    require(actual==expected,"transition independent oracle");
    for(auto [k,v]:expected) {word got=~v;require(map.get_value(k,got)&&got==v,"transition lookup");}
}
struct sample {
    bool grown=false;
    double setup=0,build=0,prepare=0,migrate=0,old_retire=0,merge=0,source_retire=0,read=0,release=0;
    word before=0,staged=0,after=0,retained=0,owned=0,metadata=0,baseline_peak=0;
    word primary=0,secondary=0,chain=0,migrated=0,added=0;
    void print(const pair_input& in,const char* arm,unsigned rep)const {
        std::printf("%s,%u,%u,%zu,%u,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
            arm,in.overlap,in.local,in.keys.size(),rep,unsigned(grown),setup,build,prepare,migrate,old_retire,
            merge,source_retire,read,release,(unsigned long long)before,(unsigned long long)staged,
            (unsigned long long)after,(unsigned long long)retained,(unsigned long long)owned,
            (unsigned long long)metadata,(unsigned long long)baseline_peak,(unsigned long long)primary,
            (unsigned long long)secondary,(unsigned long long)chain,(unsigned long long)migrated,(unsigned long long)added);
        std::fflush(stdout);
    }
};
template<class Map> void inspect_and_read(Map& map,const pair_input& input,sample& out) {
    verify(map,input.expected);
    auto mem=map.footprint();out.retained=mem.requested;out.owned=mem.backing;out.metadata=mem.metadata;
    if constexpr(requires{map.distribution();}) {
        auto d=map.distribution();out.primary=d.primary;out.secondary=d.secondary;out.chain=d.chain;
    }
    word expected=0;for(auto [k,v]:input.expected)expected+=v;
    require(consume_values(map,input.keys)==expected,"transition read warmup");
    auto begin=clock_type::now();auto sum=consume_values(map,input.keys);out.read=elapsed(begin);
    require(sum==expected,"transition timed checksum");
}
template<unsigned A,bool Unique> void candidate_trial(
        oa::Pool& pool,const pair_input& input,const char* arm,unsigned rep) {
    using small=table<40,A,A-4,128,128>;
    using large=table<40,A+1,A-3,128,128>;
    sample out;
    // Exact union size is a supplied policy input in this diagnostic. The same
    // estimate is available to the baseline; neither map computes it secretly.
    out.grown=input.keys.size()>small::primary_buckets*small::primary.slots*7/8;
    std::optional<small> receiver,source;std::optional<large> replacement;
    auto begin=clock_type::now();
    receiver.emplace(pool,mix(851+rep*4),mix(852+rep*4));
    source.emplace(pool,mix(851+rep*4),mix(852+rep*4));out.setup=elapsed(begin);
    begin=clock_type::now();
    require(accumulate(*receiver,input.rows[0])==input.local,"receiver local occupancy");
    require(accumulate(*source,input.rows[1])==input.local,"source local occupancy");out.build=elapsed(begin);
    out.before=receiver->footprint().requested+source->footprint().requested;
    if(out.grown) {
        begin=clock_type::now();
        // Deterministic experiment inputs stand for independently supplied
        // fresh entropy; this is not an entropy generator or security claim.
        replacement.emplace(pool,mix(12471+rep*4),mix(12472+rep*4));out.prepare=elapsed(begin);
        begin=clock_type::now();auto copied=migrate<Unique>(*replacement,*receiver);out.migrate=elapsed(begin);
        require(copied.complete&&copied.copied==input.local,"receiver migration admission");out.migrated=copied.copied;
        out.staged=out.before+replacement->footprint().requested;
        // Commit the complete unique copy before retiring its old owner. The
        // following merge has prefix/cursor semantics, not transactional merge.
        begin=clock_type::now();receiver.reset();out.old_retire=elapsed(begin);
        typename large::merge_cursor cursor;
        begin=clock_type::now();auto result=replacement->merge_reencoded(*source,cursor,add{});out.merge=elapsed(begin);
        require(result.complete&&result.processed==input.local,"cross-geometry merge admission");out.added=result.added;
        out.after=replacement->footprint().requested+source->footprint().requested;
    } else {
        out.staged=out.before;typename small::merge_cursor cursor;
        begin=clock_type::now();auto result=receiver->merge_from(*source,cursor,add{});out.merge=elapsed(begin);
        require(result.complete&&result.processed==input.local,"unchanged geometry merge");out.added=result.added;
        out.after=receiver->footprint().requested+source->footprint().requested;
    }
    require(out.added+input.local==input.keys.size(),"transition occupancy delta");
    begin=clock_type::now();source.reset();out.source_retire=elapsed(begin);
    if(out.grown)inspect_and_read(*replacement,input,out);else inspect_and_read(*receiver,input,out);
    begin=clock_type::now();replacement.reset();receiver.reset();out.release=elapsed(begin);out.print(input,arm,rep);
}
template<bool Reserve> void baseline_trial(oa::Pool& pool,const pair_input& input,const char* arm,unsigned rep) {
    sample out;std::optional<abseil<40>> receiver,source;auto begin=clock_type::now();
    receiver.emplace(pool,0,0);source.emplace(pool,0,0);
    receiver->reserve(input.local);source->reserve(input.local);out.setup=elapsed(begin);
    begin=clock_type::now();
    require(accumulate(*receiver,input.rows[0])==input.local,"baseline receiver occupancy");
    require(accumulate(*source,input.rows[1])==input.local,"baseline source occupancy");out.build=elapsed(begin);
    out.before=receiver->footprint().requested+source->footprint().requested;
    auto capacity=receiver->map.capacity();receiver->counters.peak_bytes=receiver->counters.current_bytes;
    if constexpr(Reserve) {
        begin=clock_type::now();receiver->reserve(input.keys.size());out.prepare=elapsed(begin);
    }
    out.staged=receiver->footprint().requested+source->footprint().requested;
    begin=clock_type::now();out.added=combine_source(*receiver,*source);out.merge=elapsed(begin);
    require(out.added+input.local==input.keys.size(),"baseline merge delta");
    out.grown=capacity!=receiver->map.capacity();
    out.after=receiver->footprint().requested+source->footprint().requested;
    out.baseline_peak=receiver->counters.peak_bytes+source->counters.current_bytes;
    begin=clock_type::now();source.reset();out.source_retire=elapsed(begin);
    inspect_and_read(*receiver,input,out);
    begin=clock_type::now();receiver.reset();out.release=elapsed(begin);out.print(input,arm,rep);
}
template<unsigned K,unsigned A> void transition_check(oa::Pool& pool) {
    using small=table<K,A,0,128,64>;using large=table<K,A+1,1,128,128>;
    small old(pool,17,43),source(pool,17,43);large dest(pool,91,104),control(pool,91,104);
    std::vector<word> keys{0,mask(K)};std::mt19937_64 random(9135);
    while(keys.size()<220) {auto k=random()&mask(K);if(std::find(keys.begin(),keys.end(),k)==keys.end())keys.push_back(k);}
    std::unordered_map<word,word> old_values,source_values,expected;
    for(unsigned i=0;i<180;++i) {auto v=mix(i);require(old.set(keys[i],v)==1,"transition check old");old_values[keys[i]]=v;}
    for(unsigned i=0;i<180;i+=7) {require(old.erase(keys[i]),"transition vacancy");old_values.erase(keys[i]);}
    expected=old_values;
    for(unsigned i=40;i<220;++i) {auto v=~mix(i);require(source.set(keys[i],v)==1,"transition check source");source_values[keys[i]]=v;expected[keys[i]]+=v;}
    auto copied=migrate<true>(dest,old),copied_control=migrate<false>(control,old);
    require(copied.complete&&copied.copied==old_values.size()&&copied_control.complete,"transition unique copy");
    verify(dest,old_values);verify(control,old_values);verify(old,old_values);
    typename large::merge_cursor cursor;
    word combines=0;
    auto result=dest.merge_reencoded(source,cursor,[&](word x,word y)noexcept {++combines;return x+y;});
    auto done=dest.merge_reencoded(source,cursor,[&](word x,word y)noexcept {++combines;return x+y;});
    require(result.complete&&result.processed==source_values.size()&&done.complete&&!done.processed,"cross geometry cursor");
    require(result.added+old_values.size()==expected.size()&&combines==source_values.size()-result.added,"cross geometry combination once");
    verify(dest,expected);verify(source,source_values);verify(old,old_values);
    for(unsigned i=0;i<keys.size();i+=3)require(dest.erase(keys[i])==(expected.erase(keys[i])!=0),"post-transition erase");
    verify(dest,expected);
}
void transition_refusal() {
    pool_owner owner(1u<<20);using small=table<40,2,0,64,64>;using large=table<40,3,1,64,64>;
    small source(*owner.pool,13,17);std::unordered_map<word,word> expected;
    for(word i=0;i<1200;++i) {require(source.set(i,mix(i))==1,"refusal source");expected[i]=mix(i);}
    std::optional<large> replacement;replacement.emplace(*owner.pool,89,101);
    std::vector<oa::Allocation> pressure;
    while(auto a=oa::allocate(*owner.pool,{.bytes=4096,.alignment=64}))pressure.push_back(std::move(*a));
    auto result=migrate<true>(*replacement,source);
    require(!result.complete&&result.copied>0&&result.copied<expected.size(),"migration partial refusal");
    verify(source,expected);replacement->validate();replacement.reset();
    bool refused=false;
    try {table<40,13,9,128,128> impossible(*owner.pool,1,2);}catch(const std::bad_alloc&) {refused=true;}
    require(refused,"replacement constructor refusal");verify(source,expected);
    pressure.clear();auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*owner.pool,0).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"transition trim");orbital::worker::poll(owner.lane,1024);
    }
    replacement.emplace(*owner.pool,89,101);result=migrate<true>(*replacement,source);
    require(result.complete&&result.copied==expected.size(),"migration retry from unchanged source");verify(*replacement,expected);
    // Re-encoded merge refusal after combining duplicates; retry must resume
    // at the refused source entry and must not double any earlier combination.
    small target(*owner.pool,441,771);std::unordered_map<word,word> target_values;
    for(word i=0;i<300;++i) {require(target.set(i,7)==1,"merge refusal target");target_values[i]=7;}
    while(auto a=oa::allocate(*owner.pool,{.bytes=4096,.alignment=64}))pressure.push_back(std::move(*a));
    typename small::merge_cursor cursor;word calls=0,processed=0,added=0;
    auto combine=[&](word x,word y)noexcept {++calls;return x+y;};
    auto part=target.merge_reencoded(*replacement,cursor,combine);processed+=part.processed;added+=part.added;
    require(!part.complete&&processed>0,"cross geometry merge refusal");
    auto saved=cursor;auto again=target.merge_reencoded(*replacement,cursor,combine);
    require(!again.complete&&!again.processed&&cursor.index==saved.index&&cursor.slot==saved.slot&&cursor.phase==saved.phase&&cursor.link==saved.link,"merge refused cursor stable");
    pressure.clear();
    while(oa::trim_cold(*owner.pool,0).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"merge retry trim");orbital::worker::poll(owner.lane,1024);
    }
    auto final=target.merge_reencoded(*replacement,cursor,combine);processed+=final.processed;added+=final.added;
    require(final.complete&&processed==1200&&added==900&&calls==300,"merge resumes without repeated combination");
    for(auto [k,v]:expected)target_values[k]+=v;
    verify(target,target_values);verify(*replacement,expected);verify(source,expected);
}
template<unsigned A> void campaign(oa::Pool& pool,unsigned local,unsigned reps) {
    for(auto overlap:{0u,50u,90u}) {
        pair_input input(local,overlap);
        for(unsigned rep=0;rep<reps;++rep)for(unsigned j=0;j<4;++j)switch((rep+j)%4) {
            case 0:baseline_trial<false>(pool,input,"abseil_natural",rep);break;
            case 1:baseline_trial<true>(pool,input,"abseil_reserve",rep);break;
            case 2:candidate_trial<A,false>(pool,input,"rebuild",rep);break;
            case 3:candidate_trial<A,true>(pool,input,"unique",rep);break;
        }
    }
}
} // namespace ht
int main(int argc,char** argv) {
    using namespace ht;auto mode=argc>1?std::string_view(argv[1]):"check";
    auto reps=argc>2?std::stoul(argv[2]):9;require(reps>0,"transition repeats");
    std::puts("arm,overlap,local,union,rep,grown,setup_ns,build_ns,prepare_ns,migrate_ns,old_retire_ns,merge_ns,source_retire_ns,read_ns,release_ns,before_requested,staged_requested,after_requested,retained_requested,retained_owned,metadata,baseline_peak_requested,primary,secondary,chain,migrated,added");
    if(mode=="check") {
        transition_refusal();pool_owner pool;
        transition_check<8,3>(*pool.pool);transition_check<16,7>(*pool.pool);transition_check<16,8>(*pool.pool);
        transition_check<24,3>(*pool.pool);transition_check<32,3>(*pool.pool);transition_check<40,7>(*pool.pool);
        transition_check<48,3>(*pool.pool);transition_check<56,3>(*pool.pool);transition_check<64,3>(*pool.pool);
    } else {
        pool_owner pool;
        if(mode=="hot")campaign<7>(*pool.pool,700,reps);
        else {require(mode=="large","transition mode");campaign<13>(*pool.pool,55000,reps);}
    }
}
