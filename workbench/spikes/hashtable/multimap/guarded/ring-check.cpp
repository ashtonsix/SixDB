// Actual Ring integration: stable observations, synchronous qualified hints,
// and fallible commits only after returning from the native dispatch region.
#define WRITE_ARCH_MM_CHECK_NO_MAIN
#include "multimap-check.cpp"
#include <loom/ring.h>
#include <loom/stream.h>
#include <thread>

// Keep the fault-injecting allocator's pairs consistent when Orbital uses
// nothrow allocation. Without these forms ASan's operator-new interceptor can
// allocate memory that the test's replacement delete then sends to free().
void* operator new(std::size_t n,const std::nothrow_t&)noexcept {
    try{return ::operator new(n);}catch(...){return nullptr;}
}
void* operator new[](std::size_t n,const std::nothrow_t&)noexcept {
    try{return ::operator new[](n);}catch(...){return nullptr;}
}
void operator delete(void* p,const std::nothrow_t&)noexcept{::operator delete(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{::operator delete[](p);}

namespace {
using Ring=loom::Ring<void*>;
template<unsigned V> struct ring_case {
    using MM=write_arch::multimap<16,10,V,false>;
    using Session=write_arch::prepared_multimap<MM>;
    using Head=typename Session::observation;
    using State=typename Session::observation_status;
    struct Lane {
        Session& session;MM& owner;
        const mm_observation<MM>* current=nullptr;
        loom::DemandClass demand;
        unsigned grants=0;std::size_t hints=0,enters=0;
    };
    struct Grant {
        Lane& lane;bool allow,held=false;
        bool enter(const MM& owner,word,ht::access_intent intent)noexcept {
            ++lane.enters;
            if(!allow)return false;
            ht::require(!held&&!lane.grants&&&owner==&lane.owner&&intent==ht::access_intent::write,"one native owner grant");
            held=true;++lane.grants;return true;
        }
        ~Grant(){if(held)--lane.grants;}
    };
    struct Frame {
        Lane* lane=nullptr;Head head;word key=0;
        bool started=false,ready=false,denied=false,committed=false,allow=true;
        unsigned refresh=0,visits=0;
    };
    static void hint(Lane& lane,const byte* pointer,std::size_t size,Ring* ring)noexcept {
        ht::require(lane.grants==1&&lane.current,"hint inside current owner grant");
        const auto address=reinterpret_cast<std::uintptr_t>(pointer);
        const bool live=std::any_of(lane.current->regions.begin(),lane.current->regions.end(),[&](const auto& r){
            const auto base=reinterpret_cast<std::uintptr_t>(r.source.base);
            return address>=base&&address-base<=r.source.extent&&size&&size<=r.source.extent-(address-base);
        });
        ht::require(live,"native hint stays within current physical source");++lane.hints;
        if(ring)ring->prefetch(lane.demand,pointer);
    }
    static LOOM_NATIVE_CC void observe(Ring::Cursor ring,Ring::Slot* slot,void* opaque){
        auto& f=*static_cast<Frame*>(opaque);auto& lane=*f.lane;
        {
            Grant grant{lane,f.allow};
            auto emit=[&](const byte* p,std::size_t n)noexcept{hint(lane,p,n,ring.operator->());};
            const auto p=f.started?lane.session.advance_observation(f.head,grant,emit):
                lane.session.begin_observation(f.key,f.head,grant,emit);
            f.started|=p.status!=State::denied;f.refresh|=p.refresh;++f.visits;
            f.ready=p.status==State::ready;f.denied=p.status==State::denied;
        }
        ht::require(!lane.grants,"grant ends before native tail transfer");
        // Denial is delivered to the owner boundary, never an unbounded retry.
        if(f.ready||f.denied)[[clang::musttail]] return Ring::finish(ring,slot,opaque);
        [[clang::musttail]] return Ring::yield_unchanged(ring,slot,opaque);
    }
    struct Independent {
        std::array<unsigned,73> input{};
        std::array<word,64> values{};
        loom::IndependentStream<unsigned,4,8> stream;
        loom::DemandClass demand;word sum=0;unsigned visits=0;
        explicit Independent(loom::DemandClass d):stream(input),demand(d){
            for(unsigned i=0;i<input.size();++i)input[i]=(17*i+3)%64;
            for(unsigned i=0;i<values.size();++i)values[i]=73*i+19;
        }
    };
    static LOOM_NATIVE_CC void independent(Ring::Cursor ring,Ring::Slot* slot,void* opaque){
        auto& f=*static_cast<Independent*>(opaque);auto sum=f.sum;
        const bool done=f.stream.advance([&](unsigned k)noexcept{ring->prefetch(f.demand,&f.values[k]);},
            [&](unsigned k)noexcept{sum+=f.values[k];});
        f.sum=sum;++f.visits;
        if(done)[[clang::musttail]] return Ring::finish(ring,slot,opaque);
        [[clang::musttail]] return Ring::yield_unchanged(ring,slot,opaque);
    }
    static void run(){
        auto original=std::make_unique<MM>(31);
        std::map<word,std::vector<ht::bytes<V>>> expected;
        // More than two full 256-rank families: after the earlier retirement
        // leaves a reusable directory slot, filling that slot alone need not
        // relocate the directory. Force growth past the existing capacity.
        constexpr word keys=1024;
        for(word k=0;k<keys;++k)for(unsigned n=0;n<(k==0?4u:k==1?9u:1u);++n){
            auto v=value<V>(k*100+n);original->insert(k,v);expected[k].push_back(v);
        }
        Session session(std::move(original));auto& owner=access::owner(session);
        const auto demand=loom::bind_demand({loom::MemoryPattern::irregular,1},1);
        ht::require(bool(demand),"one-byte qualified hint class");Lane lane{session,owner,nullptr,*demand};
        std::array<Frame,5> frames;
        // Prime all heads before admission, including those later held in overflow.
        for(unsigned i=0;i<frames.size();++i){auto& f=frames[i];f.lane=&lane;f.key=i;
            for(unsigned step=0;step<100;++step){mm_observation snapshot(owner);lane.current=&snapshot;
                State state;
                {Grant grant{lane,true};auto emit=[&](const byte* p,std::size_t n)noexcept{hint(lane,p,n,nullptr);};
                    failure_probe::after=0;failure_probe::hit=false;
                    const auto p=f.started?session.advance_observation(f.head,grant,emit):session.begin_observation(f.key,f.head,grant,emit);
                    failure_probe::after=-1;ht::require(!failure_probe::hit,"priming allocates nothing");state=p.status;f.started=true;}
                lane.current=nullptr;snapshot.verify(true);if(state==State::ready)break;
                ht::require(step<99&&state==State::pending,"priming completes");
            }
        }
        loom::Worker worker;ht::require(bool(loom::adopt(worker)),"Ring worker adoption");
        auto arena=orbital::allocation::open_arena(8*1024*1024);ht::require(bool(arena),"Ring arena");
        ht::require(bool(loom::configure(worker,*arena,0)),"Ring worker configuration");
        Ring ring;ht::require(bool(ring.configure(worker,{2,4})),"bounded Ring configuration");
        Independent other(*demand);
        for(auto& f:frames)ht::require(ring.push(observe,{&f}),"head admission");
        ht::require(ring.push(independent,{&other})&&!ring.push(independent,{&other}),"overflow and admission refusal");
        const auto before_hints=lane.hints;
        struct FixtureGrant {
            bool enter(const MM&,word,ht::access_intent)const noexcept{return true;}
            bool prepare(std::span<const ht::mutation_damage>,std::span<const ht::mutation_source>)const noexcept{return true;}
            void before(ht::mutation_source,ikea::byte_write)const noexcept{}
        };
        // Fixture churn runs while no native frame is executing. Its complete
        // ordered result is checked below; byte-by-byte publication checks are
        // retained for actual observed commits and in multimap-check.cpp.
        auto append=[&](word k,unsigned salt){auto v=value<V>(salt);FixtureGrant c;
            ht::require(session.append(k,std::span<const byte,V>(v.data(),V),c)==ht::mutation_status::updated,"boundary mutation");
            expected[k].push_back(v);
        };
        const auto retired=access::bag(owner,0).second;append(0,100001);
        ht::require(access::bag(owner,0).second!=retired,"pending head payload replaced");
        const auto old_directory=write_arch::guarded_pool_test_access::directory(access::pool(owner));
        for(word k=5;k<keys&&write_arch::guarded_pool_test_access::directory(access::pool(owner))==old_directory;++k)
            for(unsigned n=1;n<3;++n)append(k,unsigned(100100+k*4+n));
        ht::require(write_arch::guarded_pool_test_access::directory(access::pool(owner))!=old_directory,"pending head directory relocated");
        ht::require(session.cancel_observation(frames[4].head),"logical overflow cancellation");
        ht::require(ring.release_if([&](const Ring::Arguments& args)noexcept{return std::get<0>(args)==&frames[4];})==1,"physical overflow cleanup");
        // This counter covers authored Emit calls only. Automatic admission/
        // promotion hints are excluded by dependency-free push calls above.
        ht::require(lane.hints==before_hints,"no authored hints during admission and cleanup");
        frames[4].started=false;ht::require(ring.push(observe,{&frames[4]}),"reuse only after physical cleanup");
        frames[1].allow=false;unsigned denied=0;
        for(unsigned steps=0;ring.pending()&&steps<1000;++steps){
            {mm_observation snapshot(owner);lane.current=&snapshot;failure_probe::after=0;failure_probe::hit=false;
                ring.spin({1});failure_probe::after=-1;ht::require(!failure_probe::hit&&!lane.grants,"native region allocation-free and grant-free on return");
                lane.current=nullptr;snapshot.verify(true);}
            for(auto& f:frames){
                if(f.denied){++denied;ht::require(!f.allow,"explicit refusal delivered once");
                    f.denied=false;f.allow=true;ht::require(ring.push(observe,{&f}),"owner retries after physical retirement");}
                if(f.ready&&!f.committed){context c(owner,f.key);auto v=value<V>(200000+unsigned(f.key));
                    auto result=session.commit_observed(f.head,std::span<const byte,V>(v.data(),V),c);
                    ht::require(result.status==ht::mutation_status::updated,"fallible commit at returned boundary");c.finish();
                    f.refresh|=result.refresh;f.committed=true;expected[f.key].push_back(v);}
            }
        }
        ht::require(!ring.pending()&&denied==1&&ring.counters().promoted>=4,"bounded drain and overflow promotion");
        for(auto& f:frames)ht::require(f.committed&&f.visits,"each accepted head commits");
        ht::require(frames[0].refresh&(Session::pool_changed|Session::content_changed),"active refresh after retirement");
        ht::require(frames[2].refresh&Session::pool_changed,"overflow observation refreshes after promotion");
        word sum=0;for(auto k:other.input)sum+=other.values[k];
        ht::require(other.stream.empty()&&other.sum==sum&&other.visits==19,"independent work survives interleaving");
        for(const auto& [k,bag]:expected){std::vector<ht::bytes<V>> actual;
            ht::require(session.find(k,[&](auto v){actual.push_back(v);})&&actual==bag,"native complete ordered bags");}
        owner.validate();ht::require(bool(ring.close()),"Ring closes after physical drain");
        for(;;){auto p=loom::shutdown(worker);ht::require(p.kind()!=orbital::Progress::Kind::failed,"worker shutdown");
            if(p.kind()==orbital::Progress::Kind::done)break;loom::poll(worker,{64,0});std::this_thread::yield();}
        loom::relinquish(worker);
        std::printf("OBSERVED_RING_PASS,%u,%zu,%zu\n",V,lane.hints,lane.enters);
    }
};
}
int main(){ring_case<8>::run();ring_case<64>::run();}
