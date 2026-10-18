// Members of prepared_multimap. These heads cache observations, never empty
// slots or pool ranks. Each head and its inputs stay alive until every physical
// borrower has retired or cleanup has returned. Logical cancellation or commit
// alone does not end that borrow. Other Ring work may remain after per-head
// cleanup; the adapter/owner must outlive all its borrowers. Each call requires the same serialized, non-reentrant owner lane;
// epochs alone are not concurrent lifetime protection. Input storage belongs
// to the caller and must survive logical completion and physical cleanup.
// Invalidation currently covers appends only. Erase, clear, replacement or
// internal rebuild would require explicit structural/content invalidation.
    enum class observation_status {denied,pending,ready};
    enum observation_refresh : unsigned {index_changed=1,pool_changed=2,content_changed=4};
    struct observation_progress {observation_status status;unsigned refresh=0;};
    struct observed_commit {ht::mutation_status status;unsigned refresh=0;};
    class observation {
        friend class prepared_multimap;
        enum class phase {inactive,index,pool,ready};
        identity_handle identity_{};
        typename Index::staged_probe probe_{};
        guarded_index_access::selection<Index> selected_;
        word key_=0,index_epoch_=0,pool_epoch_=0,meta_=0,reference_=0;
        phase phase_=phase::inactive;
      public:
        observation()=default;
        observation(const observation&)=delete;
        observation& operator=(const observation&)=delete;
        observation(observation&&)=delete;
        observation& operator=(observation&&)=delete;
    };
  private:
    bool accepts_observation(const observation& h)const noexcept {
        return !busy_&&!pending_&&owner_&&h.identity_==identity_&&h.phase_!=observation::phase::inactive;
    }
    template<class Emit> void restart_observation(observation& h,Emit& emit)noexcept {
        h.index_epoch_=index_epoch_;h.pool_epoch_=pool_epoch_;
        h.selected_.present=false;h.selected_.cell=nullptr;
        h.phase_=observation::phase::index;
        owner_->index_.prepare_staged(h.probe_,h.key_,emit);
    }
    // Called only while the saved cell is qualified by the index epoch.
    template<class Emit> void observe_bag(observation& h,word meta,word reference,Emit& emit)noexcept {
        h.meta_=meta;h.reference_=reference;h.pool_epoch_=pool_epoch_;
        if(MM::is_external(meta)){
            h.phase_=observation::phase::pool;
            owner_->buffers_.prepare_data(reference,emit);
        }else {
            const auto n=MM::count(meta);h.phase_=observation::phase::ready;
            if(n<MM::inline_count)emit(h.selected_.cell+8+std::size_t(n)*V,V);
            else emit(h.selected_.cell+8,std::min(std::size_t(n)*V,std::size_t(64)));
        }
    }
  public:
    bool cancel_observation(observation& h)noexcept {
        if(!accepts_observation(h))return false;
        h.phase_=observation::phase::inactive;return true;
    }
    // Context.enter and Emit must be bounded, nonblocking and allocation-free;
    // noexcept alone does not establish those properties. Emit issues its hint
    // synchronously, inside this protected call. Do
    // not save these addresses in a Ring automatic MemoryDependency: promotion
    // can issue that hint after a mutation, outside the epoch check.
    template<class Context,class Emit>
    observation_progress begin_observation(word key,observation& h,Context& context,Emit&& emit)noexcept {
        static_assert(noexcept(context.enter(*owner_,key,ht::access_intent::write)));
        static_assert(noexcept(emit(static_cast<const byte*>(nullptr),std::size_t{})));
        if(busy_||pending_||!owner_||h.phase_!=observation::phase::inactive||(key&~ht::mask<MM::key_width>))return {observation_status::denied};
        scope active(busy_);
        if(!context.enter(*owner_,key,ht::access_intent::write))return {observation_status::denied};
        h.identity_=identity_;h.key_=key;restart_observation(h,emit);
        return {observation_status::pending};
    }
    template<class Context,class Emit>
    observation_progress advance_observation(observation& h,Context& context,Emit&& emit)noexcept {
        static_assert(noexcept(context.enter(*owner_,h.key_,ht::access_intent::write)));
        static_assert(noexcept(emit(static_cast<const byte*>(nullptr),std::size_t{})));
        if(!accepts_observation(h))return {observation_status::denied};
        scope active(busy_);
        if(!context.enter(*owner_,h.key_,ht::access_intent::write))return {observation_status::denied};
        if(h.index_epoch_!=index_epoch_){
            const auto reason=index_changed|(h.pool_epoch_!=pool_epoch_?pool_changed:0u);
            restart_observation(h,emit);return {observation_status::pending,reason};
        }
        if(h.phase_==observation::phase::index){
            bool present=false;
            const auto done=owner_->index_.template advance_cell<ht::staged_values::dependent,16>(h.probe_,present,[&](auto cell)noexcept {
                h.selected_=guarded_index_access::observed_existing(owner_->index_,h.key_,h.probe_.first.hash,cell);
                const auto meta=cell.template read<0>();
                observe_bag(h,meta,MM::is_external(meta)?cell.template read<1>():0,emit);
            },emit);
            if(done&&!present){h.phase_=observation::phase::ready;return {observation_status::ready};}
            return {observation_status::pending};
        }
        if(h.selected_.present){
            const auto meta=ht::load<8>(h.selected_.cell);
            const auto reference=MM::is_external(meta)?ht::load<8>(h.selected_.cell+8):0;
            const unsigned reason=(h.meta_!=meta||h.reference_!=reference?content_changed:0u)|
                (h.pool_epoch_!=pool_epoch_?pool_changed:0u);
            if(reason){observe_bag(h,meta,reference,emit);return {observation_status::pending,reason};}
            if(h.phase_==observation::phase::pool){
                const auto n=MM::count(meta);const auto room=owner_->reservation(meta);
                const auto* payload=owner_->payload(reference,meta);
                if(n<room)emit(payload+std::size_t(n)*V,V);
                else emit(payload,std::min(std::size_t(n)*V,std::size_t(64)));
                h.phase_=observation::phase::ready;return {observation_status::pending};
            }
        }
        return {observation_status::ready};
    }
    // Run outside the native memory callback: allocation/preservation may fail.
    // The caller serializes repeated-key input order. A ready native frame is
    // only a commit candidate; it is not a completed append or a reservation.
    template<class Context>
    observed_commit commit_observed(observation& h,std::span<const byte,V> input,Context& context){
        if(!accepts_observation(h)||h.phase_!=observation::phase::ready)return {ht::mutation_status::denied};
        scope active(busy_);
        if(!context.enter(*owner_,h.key_,ht::access_intent::write))return {ht::mutation_status::denied};
        const bool moved=h.index_epoch_!=index_epoch_;
        unsigned reason=(moved?index_changed:0u)|(h.pool_epoch_!=pool_epoch_?pool_changed:0u);
#if WRITE_ARCH_OBSERVED_TYPED
        // The saved cell is usable only after the grant and index-epoch check.
        // No saved pool pointer or old metadata qualifies this write: reload
        // the current bag, then select any growing reservation synchronously.
        if(!moved&&h.selected_.present&&h.selected_.row!=Index::N){
            const auto meta=ht::load<8>(h.selected_.cell);
            const auto reference=MM::is_external(meta)?ht::load<8>(h.selected_.cell+8):0;
            // Preserve the general path's refresh reporting on maximum refusal.
            if(MM::count(meta)!=Pool::max_values&&(h.meta_!=meta||h.reference_!=reference))reason|=content_changed;
            const auto selected=guarded_index_access::primary_observed(owner_->index_,h.selected_);
            const auto result=finish_primary_loaded(selected,meta,reference,input,context);
            if(result==ht::mutation_status::updated)h.phase_=observation::phase::inactive;
            return {result,reason};
        }
#endif
        const auto selected=!moved&&h.selected_.present?h.selected_:
            guarded_index_access::prepare(owner_->index_,h.key_,h.probe_.first.hash);
        const auto result=finish_selected(selected,input,context,[&](const auto& plan)noexcept {
            if(!moved&&h.selected_.present&&(h.meta_!=plan.meta||h.reference_!=plan.reference))reason|=content_changed;
        });
        if(result==ht::mutation_status::updated||result==ht::mutation_status::inserted)h.phase_=observation::phase::inactive;
        return {result,reason};
    }
