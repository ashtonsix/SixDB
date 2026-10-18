#pragma once
#include "../multimap.h"
#include "index.h"
#include "pool.h"
#include <atomic>
#ifndef WRITE_ARCH_INPLACE_HEAD
#define WRITE_ARCH_INPLACE_HEAD 1
#endif
#ifndef WRITE_ARCH_DEFER_DAMAGE
#define WRITE_ARCH_DEFER_DAMAGE 1
#endif
#ifndef WRITE_ARCH_SCALAR_IDENTITY
#define WRITE_ARCH_SCALAR_IDENTITY 1
#endif
#ifndef WRITE_ARCH_FITTING_PLAN
#define WRITE_ARCH_FITTING_PLAN 0
#endif
#ifndef WRITE_ARCH_OBSERVED_TYPED
#define WRITE_ARCH_OBSERVED_TYPED 0
#endif
#if WRITE_ARCH_OBSERVED_TYPED
static_assert(WRITE_ARCH_GUARDED_TYPED,"observed typed commit requires the typed immediate writer");
#endif

namespace write_arch {
// Private exclusive heap adapter, not a concurrent/MVCC owner. Adoption requires
// that all earlier mutable and retained borrows have ended. Ownership stays here
// while a head exists; callers get copied values, never mutable owner access.
// Context enters separately at prepare and commit. It must retain its grant for
// each call and must not reenter or invalidate owner storage, saved prefix or
// plan state. prepare(writes, retirements) resolves physical
// identity and preflights preservation; before() publishes it synchronously.
// Descriptor spans are valid only for this commit call, including refusal.
// A context must copy retained descriptors and resolve their source identity
// synchronously; raw source pointers cannot outlive the affected allocation.
template<class MM> class prepared_multimap {
    friend struct guarded_multimap_test_access;
  public:
    enum class status {ready,denied,needs_capacity,busy,full};
  private:
    using Index=typename MM::index_type;
    using Pool=typename MM::buffer_owner;
    static constexpr unsigned V=sizeof(typename MM::value_type);
    static_assert(std::is_same_v<Pool,capacity_buffers<V>>);
#if WRITE_ARCH_SCALAR_IDENTITY
    // Process-local authority, never a serialized handle. Allocation happens
    // once per adoption; copying/destroying a token does not touch shared state.
    // Saturate rather than wrap: exhaustion must not revive an old token.
    inline static std::atomic<word> next_identity_{1};
    static word allocate_identity(){
        word candidate=next_identity_.load(std::memory_order_relaxed);
        for(;;){
            ht::require(candidate!=~word{0},"prepared MM identity exhausted");
            if(next_identity_.compare_exchange_weak(candidate,candidate+1,
                std::memory_order_relaxed,std::memory_order_relaxed))return candidate;
        }
    }
    using identity_handle=word;
#else
    struct identity {};
    using identity_handle=std::shared_ptr<const identity>;
#endif
    // Counts start at zero and can only advance by one after this bound check.
    // Equality is sufficient: no accepted append can produce an over-bound count.
    struct damage_list {
        std::array<ht::mutation_damage,24> writes;
        std::array<ht::mutation_source,3> retired;
        unsigned write_count=0,retired_count=0;
        damage_list()noexcept{}
        damage_list(const damage_list&)=delete;
        damage_list& operator=(const damage_list&)=delete;
        [[gnu::always_inline]] void add(ht::mutation_damage write){if(write_count==writes.size())throw std::runtime_error("prepared MM damage bound");writes[write_count++]=write;}
        [[gnu::always_inline]] void retire(ht::mutation_source source){if(retired_count==retired.size())throw std::runtime_error("prepared MM retirement bound");retired[retired_count++]=source;}
    };
    struct head {
        guarded_index_access::selection<Index> index;
        std::optional<guarded_pool_access::reservation<Pool>> reservation;
        word meta=0,reference=0;
        unsigned count=0;
#if !WRITE_ARCH_DEFER_DAMAGE
#if WRITE_ARCH_INPLACE_HEAD
        std::array<ht::mutation_damage,24> writes;
        std::array<ht::mutation_source,3> retired;
#else
        std::array<ht::mutation_damage,24> writes{};
        std::array<ht::mutation_source,3> retired{};
#endif
        unsigned write_count=0,retired_count=0;
        [[gnu::always_inline]] void add(ht::mutation_damage write){if(write_count==writes.size())throw std::runtime_error("prepared MM damage bound");writes[write_count++]=write;}
        [[gnu::always_inline]] void retire(ht::mutation_source source){if(retired_count==retired.size())throw std::runtime_error("prepared MM retirement bound");retired[retired_count++]=source;}
#endif
#if WRITE_ARCH_INPLACE_HEAD
        head()noexcept{}
        head(const head&)=delete;
        head& operator=(const head&)=delete;
        head(head&&)=delete;
        head& operator=(head&&)=delete;
#endif
        bool external=false;
    };
    // Fitting appends own no detached pool resources. Keep their plan free of
    // the optional reservation and its exception-cleanup state.
    struct fitting_head {
        guarded_index_access::selection<Index> index;
        word meta=0,reference=0;
        unsigned count=0;
        bool external=false;
    };
#if WRITE_ARCH_GUARDED_TYPED
    static_assert(WRITE_ARCH_FITTING_PLAN&&WRITE_ARCH_DEFER_DAMAGE,
        "typed existing-primary experiment uses the fitting leaf and commit-time effects");
    struct primary_fitting_head {
        guarded_index_access::existing_primary<Index> index;
        word meta=0,reference=0;
        unsigned count=0;
        bool external=false;
    };
    struct primary_growing_head:primary_fitting_head {
        std::optional<guarded_pool_access::reservation<Pool>> reservation;
    };
#if WRITE_ARCH_POOL_BORROWED
    struct primary_borrowed_growing_head:primary_fitting_head {
        guarded_pool_access::borrowed_reservation<Pool> reservation;
    };
#endif
#endif
    template<class Plan> static constexpr bool reservation_plan=std::is_same_v<Plan,head>
#if WRITE_ARCH_GUARDED_TYPED
        ||std::is_same_v<Plan,primary_growing_head>
#if WRITE_ARCH_POOL_BORROWED
        ||std::is_same_v<Plan,primary_borrowed_growing_head>
#endif
#endif
        ;
    template<class Plan> static bool reserved(const Plan& plan)noexcept {
        if constexpr(reservation_plan<Plan>)return bool(plan.reservation);
        else return false;
    }
#if WRITE_ARCH_SCALAR_IDENTITY
    const identity_handle identity_=allocate_identity();
#else
    identity_handle identity_=std::make_shared<identity>();
#endif
    std::unique_ptr<MM> owner_;
    std::optional<head> pending_;
    word revision_=0;
    word index_epoch_=0,pool_epoch_=0;
    bool busy_=false;
    struct scope {bool& busy;explicit scope(bool& flag):busy(flag){busy=true;}~scope(){busy=false;}};
    // Source qualification belongs to commit after reacquiring the grant.
    // The exclusive adapter keeps the selected cells and reservations stable;
    // no persistent damage descriptor or input pointer must outlive admission.
    template<class Plan,class List> [[gnu::always_inline]] void describe(const Plan& plan,List& list)const {
        auto add=[&](auto d) __attribute__((always_inline)) {list.add(d);};
        auto retire=[&](auto source) __attribute__((always_inline)) {list.retire(source);};
        if constexpr(reservation_plan<Plan>) {
            if(plan.reservation){
                guarded_pool_access::damage(*plan.reservation,owner_.get(),add);
                guarded_pool_access::directory_retirement(*plan.reservation,owner_.get(),retire);
                if(plan.external)guarded_pool_access::release_damage(owner_->buffers_,plan.reference,owner_.get(),add,retire);
            }
        }
        if(!reserved(plan)&&plan.external)guarded_pool_access::append_damage(owner_->buffers_,plan.reference,plan.count,owner_.get(),add);
        guarded_index_access::damage(owner_->index_,plan.index,owner_.get(),add);
        if(!plan.index.present||reserved(plan))
            add(ht::mutation_damage{{owner_.get(),ht::owner_part::header,reinterpret_cast<const byte*>(owner_.get()),sizeof(MM)},{0,0,sizeof(MM)}});
    }
    [[gnu::always_inline]] status finish_discovery(head& next){
        if(next.index.needs_capacity)return status::needs_capacity;
        if(next.index.present){
            next.meta=ht::load<8>(next.index.cell);next.count=MM::count(next.meta);next.external=MM::is_external(next.meta);
            if(next.count==Pool::max_values)return status::full;
            if(next.external)next.reference=ht::load<8>(next.index.cell+8);
        }
        const auto room=next.external?owner_->reservation(next.meta):MM::inline_count;
        if(next.count==room){
            const auto* prefix=next.external?owner_->payload(next.reference,next.meta):next.index.cell+8;
            next.reservation.emplace(guarded_pool_access::prepare(owner_->buffers_,Pool::class_for(next.count+1),
                {prefix,std::size_t(next.count)*V}));
            if(next.external){
                ht::require(next.reservation->which!=MM::family(next.meta),"growing prepared bag changes family before retirement");
            }
        }
#if !WRITE_ARCH_DEFER_DAMAGE
        describe(next,next);
#endif
        return status::ready;
    }
    [[gnu::always_inline]] status discover(head& next,word key){
        next.index=guarded_index_access::prepare(owner_->index_,key);return finish_discovery(next);
    }
    template<class Plan,class Context> [[gnu::always_inline]] ht::mutation_status finish(Plan& plan,std::span<const byte,V> input,Context& context){
        // Structural epochs never wrap into acceptance of an old observation.
        if((!plan.index.present&&index_epoch_==~word{0})||(reserved(plan)&&pool_epoch_==~word{0}))
            return ht::mutation_status::denied;
        typename MM::value_type incoming;std::memcpy(incoming.data(),input.data(),V);
#if WRITE_ARCH_DEFER_DAMAGE
        damage_list effects;describe(plan,effects);
#else
        auto& effects=plan;
#endif
        const auto writes=std::span<const ht::mutation_damage>(effects.writes.data(),effects.write_count);
        const auto retired=std::span<const ht::mutation_source>(effects.retired.data(),effects.retired_count);
        if(!context.prepare(writes,retired))return ht::mutation_status::denied;
        static_assert(noexcept(context.before(writes[0].source,writes[0].span)));
        for(const auto& d:writes)context.before(d.source,d.span);
        guarded_index_access::commit(owner_->index_,plan.index,[&](byte* cell) __attribute__((always_inline)) noexcept {
            ikea::no_coverage effects;
            if constexpr(reservation_plan<Plan>) {
                if(plan.reservation){
                    const auto reference=guarded_pool_access::commit<V,true>(*plan.reservation,incoming);
                    tuple_word::write(cell+8,reference,effects);
                    tuple_word::write(cell,MM::metadata(plan.count+1,plan.reservation->which),effects);
                    if(plan.external)owner_->buffers_.release(plan.reference);
                    return;
                }
            }
            if(plan.external){
                owner_->buffers_.append_fitting(plan.reference,MM::family(plan.meta),plan.count,incoming);
                tuple_word::write(cell,plan.meta+1,effects);
            }else {
                tuple_value<V>(cell+8+std::size_t(plan.count)*V,incoming,effects);
                tuple_word::write(cell,plan.count+1,effects);
            }
        });
        if(!plan.index.present)++index_epoch_;
        if(reserved(plan))++pool_epoch_;
        return plan.index.present?ht::mutation_status::updated:ht::mutation_status::inserted;
    }
    template<class Context,class Inspect> [[gnu::always_inline]] ht::mutation_status finish_general(
        guarded_index_access::selection<Index> selection,std::span<const byte,V> input,Context& context,Inspect&& inspect){
        head plan;plan.index=selection;
        const auto discovered=finish_discovery(plan);
        if(discovered==status::needs_capacity)return ht::mutation_status::needs_capacity;
        if(discovered!=status::ready)return ht::mutation_status::denied;
        inspect(plan);return finish(plan,input,context);
    }
    template<class Context,class Inspect> [[gnu::noinline]] ht::mutation_status finish_growing(
        guarded_index_access::selection<Index> selection,std::span<const byte,V> input,Context& context,Inspect&& inspect){
        return finish_general(selection,input,context,std::forward<Inspect>(inspect));
    }
    template<class Context,class Inspect> [[gnu::always_inline]] ht::mutation_status finish_selected(
        guarded_index_access::selection<Index> selection,std::span<const byte,V> input,Context& context,Inspect&& inspect){
#if WRITE_ARCH_FITTING_PLAN
        static_assert(WRITE_ARCH_DEFER_DAMAGE,"light fitting plans construct damage at commit");
        fitting_head next;next.index=selection;
        if(next.index.needs_capacity)return ht::mutation_status::needs_capacity;
        if(next.index.present){
            next.meta=ht::load<8>(next.index.cell);next.count=MM::count(next.meta);next.external=MM::is_external(next.meta);
            if(next.count==Pool::max_values)return ht::mutation_status::denied;
            if(next.external)next.reference=ht::load<8>(next.index.cell+8);
        }
        const auto room=next.external?owner_->reservation(next.meta):MM::inline_count;
        if(next.count<room){inspect(next);return finish(next,input,context);}
        return finish_growing(selection,input,context,std::forward<Inspect>(inspect));
#else
        return finish_general(selection,input,context,std::forward<Inspect>(inspect));
#endif
    }
#if WRITE_ARCH_GUARDED_TYPED
#if WRITE_ARCH_POOL_BORROWED
    template<class Context> [[gnu::noinline]] ht::mutation_status finish_primary_allocating(
#else
    template<class Context> [[gnu::noinline]] ht::mutation_status finish_primary_growing(
#endif
            guarded_index_access::existing_primary<Index> selected,word meta,word reference,
            std::span<const byte,V> input,Context& context){
        primary_growing_head next;next.index=selected;next.meta=meta;next.reference=reference;
        next.count=MM::count(meta);next.external=MM::is_external(meta);
        const auto* prefix=next.external?owner_->payload(reference,meta):selected.cell+8;
        next.reservation.emplace(guarded_pool_access::prepare(owner_->buffers_,Pool::class_for(next.count+1),
            {prefix,std::size_t(next.count)*V}));
        if(next.external)ht::require(next.reservation->which!=MM::family(meta),
            "growing prepared bag changes family before retirement");
        return finish(next,input,context);
    }
#if WRITE_ARCH_POOL_BORROWED
    template<class Context> [[gnu::noinline]] ht::mutation_status finish_primary_growing(
            guarded_index_access::existing_primary<Index> selected,word meta,word reference,
            std::span<const byte,V> input,Context& context){
        primary_borrowed_growing_head next;
        next.index=selected;next.meta=meta;next.reference=reference;
        next.count=MM::count(meta);next.external=MM::is_external(meta);
        const auto* prefix=next.external?owner_->payload(reference,meta):selected.cell+8;
        if(!guarded_pool_access::prepare_borrowed(owner_->buffers_,Pool::class_for(next.count+1),
                {prefix,std::size_t(next.count)*V},next.reservation))
            return finish_primary_allocating(selected,meta,reference,input,context);
        if(next.external)ht::require(next.reservation.which!=MM::family(meta),
            "growing prepared bag changes family before retirement");
        return finish(next,input,context);
    }
#endif
#if WRITE_ARCH_OBSERVED_TYPED
    // Current metadata/reference are loaded under the observed call's grant.
    // Feed the same fitting and growing commit kernels without changing the
    // immediate caller's already measured inlining boundary.
    template<class Context> [[gnu::always_inline]] ht::mutation_status finish_primary_loaded(
            guarded_index_access::existing_primary<Index> selected,word meta,word reference,
            std::span<const byte,V> input,Context& context){
        const auto count=MM::count(meta);
        if(count==Pool::max_values)return ht::mutation_status::denied;
        const bool external=MM::is_external(meta);
        const auto room=external?owner_->reservation(meta):MM::inline_count;
        if(count==room)return finish_primary_growing(selected,meta,reference,input,context);
        primary_fitting_head next{selected,meta,reference,count,external};
        return finish(next,input,context);
    }
#endif
    template<class Context> [[gnu::always_inline]] ht::mutation_status finish_primary_existing(
            guarded_index_access::existing_primary<Index> selected,std::span<const byte,V> input,Context& context){
        const auto meta=ht::load<8>(selected.cell);const auto count=MM::count(meta);
        if(count==Pool::max_values)return ht::mutation_status::denied;
        const bool external=MM::is_external(meta);
        const auto reference=external?ht::load<8>(selected.cell+8):0;
        const auto room=external?owner_->reservation(meta):MM::inline_count;
        if(count==room)return finish_primary_growing(selected,meta,reference,input,context);
        primary_fitting_head next{selected,meta,reference,count,external};
        return finish(next,input,context);
    }
    template<class Context> [[gnu::noinline]] ht::mutation_status append_general_resolved(
            word key,std::span<const byte,V> input,Context& context){
        // The first primary probe failed. Complete duplicate/capacity discovery
        // stays in the existing resolver; its repeated probe is fully charged.
        return finish_selected(guarded_index_access::prepare_complete(owner_->index_,key),
            input,context,[](const auto&)noexcept{});
    }
#endif
  public:
    class token {
        friend class prepared_multimap;
        identity_handle identity_{};
        word revision_=0;
        token(identity_handle owner,word revision):identity_(std::move(owner)),revision_(revision){}
      public: token()=default;
    };
    struct preparation {status result;token prepared;};
    // Obtain identity before moving the caller's owner. The rvalue reference
    // binds that owner without consuming it if allocation fails.
    explicit prepared_multimap(std::unique_ptr<MM>&& owner):owner_(std::move(owner)){ht::require(bool(owner_),"prepared MM owns its material");}
    prepared_multimap(const prepared_multimap&)=delete;
    prepared_multimap& operator=(const prepared_multimap&)=delete;
    prepared_multimap(prepared_multimap&&)=delete;
    prepared_multimap& operator=(prepared_multimap&&)=delete;
    bool accepts(const token& t)const noexcept {
        return !busy_&&owner_&&pending_&&t.identity_==identity_&&t.revision_==revision_;
    }
    bool cancel(const token& t)noexcept {
        if(!accepts(t))return false;
        pending_.reset();return true;
    }
    // This invalidates any pending head before handing ownership to a rebuild
    // or another descriptor. A new adapter has a new identity even at the same
    // address. Identity values are never reused during this process lifetime.
    std::unique_ptr<MM> release_owner(){
        ht::require(!busy_,"prepared MM is not reentrant");pending_.reset();return std::move(owner_);
    }
    template<class Context> preparation prepare(word key,Context& context){
        if(busy_||pending_)return {status::busy,{}};
        if(!owner_||(key&~ht::mask<MM::key_width>))return {status::denied,{}};
        scope active(busy_);
        if(!context.enter(*owner_,key,ht::access_intent::write))return {status::denied,{}};
        ht::require(revision_!=~word{0},"prepared MM revision exhausted");
#if WRITE_ARCH_INPLACE_HEAD
        pending_.emplace();
        struct rollback {std::optional<head>& pending;bool keep=false;~rollback(){if(!keep)pending.reset();}} guard{pending_};
        auto& next=*pending_;
#else
        head next;
#endif
        const auto discovered=discover(next,key);
        if(discovered!=status::ready)return {discovered,{}};
#if WRITE_ARCH_INPLACE_HEAD
        guard.keep=true;
#else
        pending_.emplace(std::move(next));
#endif
        ++revision_;
        return {status::ready,token(identity_,revision_)};
    }
    template<class Context> ht::mutation_status commit(const token& t,std::span<const byte,V> input,Context& context){
        if(!accepts(t))return ht::mutation_status::denied;
        scope active(busy_);auto& plan=*pending_;
        if(!context.enter(*owner_,plan.index.key,ht::access_intent::write))return ht::mutation_status::denied;
        const auto result=finish(plan,input,context);
        if(result!=ht::mutation_status::denied)pending_.reset();
        return result;
    }
    // One admitted operation: no suspended head, token, or revision publication.
    // A pre-existing promised head still excludes other mutations. Local plans
    // own only detached resources until effects are accepted.
    template<class Context> [[gnu::always_inline]] ht::mutation_status append(word key,std::span<const byte,V> input,Context& context){
        if(busy_||pending_||!owner_||(key&~ht::mask<MM::key_width>))return ht::mutation_status::denied;
        scope active(busy_);
        if(!context.enter(*owner_,key,ht::access_intent::write))return ht::mutation_status::denied;
#if WRITE_ARCH_GUARDED_TYPED
        if(auto selected=guarded_index_access::primary_existing(owner_->index_,key);selected.cell)
            return finish_primary_existing(selected,input,context);
        return append_general_resolved(key,input,context);
#else
        return finish_selected(guarded_index_access::prepare(owner_->index_,key),input,context,[](const auto&)noexcept{});
#endif
    }
    // Plain lookahead control: no head or pointer survives the protected hint.
    // Later append re-enters and resolves current storage independently.
    template<class Context,class Emit> [[gnu::always_inline]] bool prefetch_append(word key,Context& context,Emit&& emit)noexcept {
        static_assert(noexcept(context.enter(*owner_,key,ht::access_intent::write)));
        static_assert(noexcept(emit(static_cast<const byte*>(nullptr),std::size_t{})));
        if(busy_||pending_||!owner_||(key&~ht::mask<MM::key_width>))return false;
        scope active(busy_);
        if(!context.enter(*owner_,key,ht::access_intent::write))return false;
        const auto probe=owner_->index_.prepare(key);emit(probe.primary,Index::bucket_bytes);return true;
    }
#include "observed.inc"
    template<class Consume> bool find(word key,Consume&& consume){
        ht::require(!busy_&&owner_,"prepared MM read owner");scope active(busy_);
        return owner_->find(key,std::forward<Consume>(consume));
    }
    std::size_t size()const {ht::require(bool(owner_),"prepared MM owner");return owner_->size();}
};
}
