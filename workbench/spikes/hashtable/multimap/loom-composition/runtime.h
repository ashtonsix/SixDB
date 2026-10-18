#pragma once

// Original typed control, extracted from frozen A's scheduled.h SHA256
// 842fe29ce5bf6b9093ed5a45006bd11dc0d1ee966d5b0adb2b88372215d548c9
// in capture 4dda02876e0962278680e42df620eedf3923aa02f40df0095f51896f54316e85.
// The callback's historical visits increment remains paid. Common callbacks
// without that counter are net-operation comparisons, not isolated ABI costs.
// Heat/bound_batch, private allocation storage and experimental IBS macros are
// absent. This caller owns stable heap tables through returned drain/discard;
// it makes no Descriptor/Pool retention claim.
// Source runtime SHA256 a0f727a05338a9e71e1e3436c3fd16b87c7148e8a495469ad06beebab33b13d9.

#include "reader.h"
#include <loom/hardware.h>
#include <loom/ring.h>
#include <orbital/allocation.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <utility>
#include <sched.h>

namespace study {
template<loom::Hint Hint,bool FixedLine=false> struct hint_emitter {
    std::size_t page,line;
    [[gnu::always_inline]] void operator()(const byte* p,std::size_t n)const {
        if(!n)return;
        constexpr std::size_t native_line=64;
        const auto stride=FixedLine?native_line:line;
        loom::prefetch<Hint>(p,page,stride);
        const auto begin=reinterpret_cast<std::uintptr_t>(p),end=begin+n;
        if constexpr(FixedLine)if(n<=native_line){
            const auto last=end-1;
            if((begin^last)&~std::uintptr_t(native_line-1))
                loom::prefetch<Hint>(p+n-1,page,native_line);
            return;
        }
        for(auto at=(begin&~std::uintptr_t(stride-1))+stride;at<end;at+=stride)
            loom::prefetch<Hint>(reinterpret_cast<const byte*>(at),page,stride);
    }
};
template<class Read> struct ring_job {
    typename Read::staged_probe probe;
    word current=0,left=0,sum=0,hits=0,edge=0,visits=0;
};
template<class Read> struct ring_context {Read read;const input* source;std::size_t page,line;};
template<class Read> using Schedule=typename loom::Ring<ring_job<Read>*>::template WithShared<const ring_context<Read>*>;
template<class Read,loom::Hint Hint,bool DirectKeys,bool Independent=false,bool FixedLine=false>
[[gnu::noinline]] LOOM_NATIVE_CC void visit(typename Schedule<Read>::Cursor ring,typename Schedule<Read>::Slot* slot,
    const ring_context<Read>* context,ring_job<Read>* job) {
    const auto& in=*context->source;const hint_emitter<Hint,FixedLine> emit{context->page,context->line};bool found=false;++job->visits;
    const bool done=context->read.advance(job->probe,found,write_arch::reduction_study::select<Independent&&Multi&&V==64,ReductionOwner,Read>([&](const auto& value) __attribute__((always_inline)){
        const auto d=digest(value);job->sum+=d;if constexpr(!Independent)job->edge^=d;
    }),emit);
    if(done){job->hits+=found;
        if constexpr(Independent)job->current+=independent_width;
        else if constexpr(DirectKeys)job->current=job->edge;
        else if constexpr(V>=4)job->current=found?job->edge:in.next[job->current];
        else job->current=in.narrow_edges[job->current][found]^job->edge;
        job->edge=0;
        if(!--job->left)[[clang::musttail]] return Schedule<Read>::finish(ring,slot,context,job);
        const auto k=Independent?in.request_keys[job->current]:DirectKeys?job->current:key(job->current,in.seed);
        context->read.prepare_staged(job->probe,k,emit);
    }
    [[clang::musttail]] return Schedule<Read>::yield_unchanged(ring,slot,context,job);
}
template<class Read> class ring_runner {
    ring_context<Read> context_;
    Schedule<Read> ring_;
    template<loom::Hint Hint,bool DirectKeys,bool Independent=false,bool FixedLine=false> result execute() {
        constexpr unsigned C=Independent?independent_width:32;std::array<ring_job<Read>,C> jobs;
        const auto& in=*context_.source;const hint_emitter<Hint,FixedLine> emit{context_.page,context_.line};
        const auto requests=Independent?in.request_keys.size():in.requests;
        for(unsigned j=0;j<std::min<std::size_t>(C,requests);++j){auto& job=jobs[j];word k;
            if constexpr(Independent){job.current=j;job.left=(requests-1-j)/C+1;k=in.request_keys[j];}
            else {const auto rank=in.order[std::size_t(j)*in.requests/C];
                job.current=DirectKeys?in.keys[rank]:rank;job.left=(std::size_t(j+1)*in.requests/C)-(std::size_t(j)*in.requests/C);
                k=DirectKeys?job.current:key(job.current,in.seed);}
            context_.read.prepare_staged(job.probe,k,emit);
            require(bool(ring_.push(visit<Read,Hint,DirectKeys,Independent,FixedLine>,{&job})),"Loom admission");
        }
        while(ring_.pending())ring_.spin({1024});
        result out;for(const auto& job:jobs){out.sum+=job.sum;out.hits+=job.hits;if constexpr(!Independent)out.terminal^=job.current;}return out;
    }
  public:
    ring_runner(loom::Worker& worker,Read read,const input& in):context_{read,&in,worker.hints.profile.geometry.page_bytes,worker.hints.profile.geometry.line_bytes}{
        require(bool(ring_.configure(worker,{independent_width,0},{&context_})),"Loom configuration");
    }
    ring_runner(const ring_runner&)=delete;
    ring_runner& operator=(const ring_runner&)=delete;
    ~ring_runner(){if(!ring_.close())std::abort();}
    std::size_t bytes_used(bool independent=false)const{return ring_.storage_bytes()+(independent?independent_width:32)*sizeof(ring_job<Read>)+sizeof(context_);}
    // Frozen A uses dynamic hint geometry (FixedLine=false). An explicitly
    // qualified strong 64-byte control can specialize the same exact emitter;
    // it must not silently change only one reader or one matched pair side.
    template<loom::Hint Hint,bool FixedLine=false> result run_independent(){
        // Native runtime admission qualifies hardware line64 once. Explicit
        // functional-only checks use the same bounded 64-byte address shape on
        // other reported geometry, without changing that hardware observation.
        return execute<Hint,false,true,FixedLine>();
    }
    template<loom::Hint Hint> result run(){if constexpr(V>=8)if(!context_.source->miss)return execute<Hint,true>();return execute<Hint,false>();}
};

// Stable Worker binding, configured once outside timing. Hardware calibration
// is disabled for this experiment; hints use the actual captured CPU profile.
struct runtime {
    loom::Worker worker;
    cpu_set_t prior_affinity{};
    explicit runtime(bool require_native_line=true){
        require(sched_getaffinity(0,sizeof(prior_affinity),&prior_affinity)==0,"capture caller affinity");
        const auto cpu=sched_getcpu();require(cpu>=0,"current CPU");
        require(bool(loom::adopt(worker,cpu)),"Loom worker adoption");
        try{
            auto hardware=loom::discover_hardware(cpu);require(bool(hardware),"Loom hardware discovery");
            loom::HardwareOptions options;options.calibrate=false;loom::HardwareService service(options);
            auto profile=service.request(std::move(*hardware));require(bool(profile),"Loom hardware profile");
            auto arena=orbital::allocation::open_arena(16ULL<<20);require(bool(arena),"Loom arena");
            require(bool(loom::configure(worker,std::move(*arena),0,*profile)),"Loom worker configuration");
            require(!require_native_line||worker.hints.profile.geometry.line_bytes==64,
                "this fixed native comparison requires a qualified 64-byte line");
        }catch(...){stop();throw;}
    }
    runtime(const runtime&)=delete;
    runtime& operator=(const runtime&)=delete;
    void stop(){
        const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        for(;;){const auto state=loom::shutdown(worker);
            if(state.kind()==orbital::Progress::Kind::done)break;
            if(state.kind()==orbital::Progress::Kind::failed||std::chrono::steady_clock::now()>limit)std::abort();
            loom::poll(worker,{1024,0});
        }
        loom::relinquish(worker);
        if(sched_setaffinity(0,sizeof(prior_affinity),&prior_affinity)!=0)std::abort();
    }
    ~runtime(){stop();}
};

} // namespace study
