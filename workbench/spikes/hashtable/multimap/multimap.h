#pragma once
#include "capacity-buffers.h"
#include "direct-buffers.h"
#ifndef WRITE_ARCH_INLINE_CAPACITY
#define WRITE_ARCH_INLINE_CAPACITY 0
#endif
#ifndef WRITE_ARCH_MM_OUTLINE
#define WRITE_ARCH_MM_OUTLINE 0
#endif

namespace write_arch {
template<unsigned W,unsigned b,unsigned V,bool WholeBytes,unsigned Inline=(V==8?2:1),
    buffer_representation Representation=buffer_representation::streampack_capacity,bool Direct=false,bool Coalesced=true,
    class Index=table<W,b,8+std::max(8u,Inline*V),WholeBytes,false,upsert_flow::split,V,true>,
    bool CachedCapacity=WRITE_ARCH_INLINE_CAPACITY> class multimap {
    static_assert(V==8||V==64);
    static_assert(Inline==0||Inline==(V==8?2:1));
    static_assert(!Direct||Representation==buffer_representation::streampack_capacity);
    static_assert(!Direct||Coalesced);
    static constexpr unsigned inline_count=Inline,inline_bytes=std::max(8u,inline_count*V),cell_bytes=8+inline_bytes;
    static constexpr word external=word{1}<<63;
    using index_type=Index;
    static_assert(std::is_same_v<typename index_type::value_type,ht::bytes<cell_bytes>>);
    index_type index_;
    friend struct multimap_probe;
    friend struct multimap_rebuild;
    template<class> friend class prepared_multimap;
    friend struct guarded_multimap_test_access;
    static constexpr unsigned key_width=W;
    using buffer_owner=std::conditional_t<Direct,direct_capacity_buffers<V>,capacity_buffers<V,Representation,Coalesced>>;
    buffer_owner buffers_;
    struct rebuilt_index_tag {};
    multimap(rebuilt_index_tag,index_type&& index):index_(std::move(index)){}
    static unsigned count(word meta){return unsigned(meta&65535);}
    static unsigned family(word meta){return unsigned((meta>>32)&255);}
    static bool is_external(word meta){if constexpr(!Inline)return true;else return meta&external;}
    static constexpr bool inline_capacity=CachedCapacity&&!Direct;
    static word metadata(unsigned n,unsigned which){
        word result=external|(word(which)<<32)|n;
        if constexpr(inline_capacity)result|=word(std::min(1u<<which,buffer_owner::max_values))<<16;
        return result;
    }
    unsigned reservation(word meta)const {
        if constexpr(inline_capacity)return unsigned((meta>>16)&65535);
        else return buffers_.capacity(family(meta));
    }
    byte* payload(word reference,word meta)const {
        if constexpr(inline_capacity)return buffers_.data_capacity(reference,reservation(meta));
        else return buffers_.data(reference,family(meta));
    }
    template<class Cell,class Consume> [[gnu::always_inline]] void consume_cell(Cell cell,Consume&& consume)const {
        const auto meta=cell.template read<0>();const auto n=count(meta);
        if constexpr(Inline)if(!is_external(meta)){
            consume(cell.template value<V,1>());
            if constexpr(inline_count==2)if(n==2)consume(cell.template value<V,1+V/8>());
            return;
        }
        const auto* data=payload(cell.template read<1>(),meta);
        if constexpr(V==64&&requires{typename std::remove_cvref_t<Consume>::valuewise_tag;}) {
#pragma clang loop vectorize(disable)
            for(unsigned i=0;i<n;++i){ht::bytes<V> value;std::memcpy(value.data(),data+std::size_t(i)*V,V);consume(value);}
        }else {
            for(unsigned i=0;i<n;++i){ht::bytes<V> value;std::memcpy(value.data(),data+std::size_t(i)*V,V);consume(value);}
        }
    }
    [[gnu::noinline]] void grow(byte* cell,word meta,const ht::bytes<V>& incoming) {
        const auto n=count(meta);ht::require(n<buffer_owner::max_values,"multimap maximum");
        const auto which=buffer_owner::class_for(n+1);
        const auto old_reference=is_external(meta)?ht::load<8>(cell+8):0;
        const auto* prefix=is_external(meta)?payload(old_reference,meta):cell+8;
        const auto reference=buffers_.create(which,{prefix,std::size_t(n)*V},incoming);
        ikea::no_coverage effects;
        tuple_word::write(cell+8,reference,effects);
        tuple_word::write(cell,metadata(n+1,which),effects);
        if(is_external(meta))buffers_.release(old_reference);
    }
  public:
    using value_type=ht::bytes<V>;
    using probe=typename index_type::probe;
    struct staged_probe {
        typename index_type::staged_probe index;
        word reference=0;const byte* payload=nullptr;unsigned count=0,geometry=0;byte phase=0;
    };
    static constexpr unsigned N=index_type::N;
    static constexpr auto bucket_count=index_type::bucket_count;
    explicit multimap(word seed):index_(seed){}
    std::size_t size()const{return index_.size();}
    std::size_t capacity()const{return bucket_count*N;}
    std::size_t bytes_used()const{return index_.bytes_used()+buffers_.bytes_used();}
    std::size_t spill_size()const{return index_.spill_size();}
    std::size_t live_buffers()const{return buffers_.live_buffers();}
    [[gnu::always_inline]] bool insert(word key,const value_type& value) {
        return insert_bytes(key,std::span<const byte,V>{value.data(),V});
    }
#if WRITE_ARCH_MM_OUTLINE
    [[gnu::noinline]]
#endif
    bool insert_bytes(word key,std::span<const byte,V> value) {
        value_type incoming;std::memcpy(incoming.data(),value.data(),V);
        return index_.edit_cell(key,[&](byte* cell,bool present) __attribute__((always_inline)) {
            ikea::no_coverage effects;
            if(!present){
                if constexpr(Inline){tuple_value<V,Coalesced>(cell+8,incoming,effects);tuple_word::write(cell,1,effects);}
                else {const auto reference=buffers_.create(0,{},incoming);
                    tuple_word::write(cell+8,reference,effects);tuple_word::write(cell,metadata(1,0),effects);}
                return;
            }
            const auto meta=ht::load<8>(cell);const auto n=count(meta);
            if(!is_external(meta)){
                if constexpr(Inline)if(n<inline_count){tuple_value<V,Coalesced>(cell+8+std::size_t(n)*V,incoming,effects);tuple_word::write(cell,n+1,effects);return;}
            }else if(n<reservation(meta)){
                const auto reference=ht::load<8>(cell+8);
                if constexpr(inline_capacity)buffers_.append_fitting_capacity(reference,reservation(meta),n,incoming);
                else buffers_.append_fitting(reference,family(meta),n,incoming);
                tuple_word::write(cell,meta+1,effects);return;
            }
            grow(cell,meta,incoming);
        });
    }
    bool erase(word key) {
        ht::require(!(key&~ht::mask<W>),"multimap key domain");
        return index_.erase_cell(key,[&](const byte* cell){if(is_external(ht::load<8>(cell)))buffers_.release(ht::load<8>(cell+8));});
    }
    template<class Consume> [[gnu::always_inline]] bool find(word key,Consume&& consume)const {
        return finish(prepare(key),key,std::forward<Consume>(consume));
    }
    [[gnu::always_inline]] probe prepare(word key)const{return index_.prepare(key);}
    template<bool Hint=true> [[gnu::always_inline]] void prefetch(const probe& p)const {index_.template prefetch<Hint>(p);}
    template<class Consume> [[gnu::always_inline]] bool finish(const probe& p,word key,Consume&& consume)const {
        return index_.finish_cell(p,key,[&](auto cell) __attribute__((always_inline)) {consume_cell(cell,consume);});
    }
    template<class Emit> void prepare_staged(staged_probe& state,word key,Emit&& emit)const {
        state={};index_.prepare_staged(state.index,key,emit);
    }
    template<class Consume,class Emit> bool advance(staged_probe& state,bool& found,Consume&& consume,Emit&& emit)const {
        if(state.phase==0){bool present=false;
            const auto done=index_.template advance_cell<ht::staged_values::dependent,16>(state.index,present,[&](auto cell) {
                const auto meta=cell.template read<0>();state.count=count(meta);
                if constexpr(inline_capacity)state.geometry=reservation(meta);
                else state.geometry=family(meta);
                if constexpr(Inline)if(!is_external(meta)){
                    state.payload=cell.address()+8;state.phase=2;emit(state.payload,std::size_t(state.count)*V);return;
                }
                state.reference=cell.template read<1>();
                if constexpr(Direct){state.payload=buffers_.data(state.reference,state.geometry);state.phase=2;
                    emit(state.payload,std::size_t(state.count)*V);
                }else {state.phase=1;buffers_.prepare_data(state.reference,emit);}
            },emit);
            if(done&&!present){found=false;return true;}
            return false;
        }
        if(state.phase==1){
            if constexpr(inline_capacity)state.payload=buffers_.data_capacity(state.reference,state.geometry);
            else state.payload=buffers_.data(state.reference,state.geometry);
            state.phase=2;
            emit(state.payload,std::size_t(state.count)*V);return false;
        }
        // Only the live prefix is consumed. This also respects the explicit
        // prefix-initialized diagnostic buffer's poisoned unused capacity.
        if constexpr(V==64&&requires{typename std::remove_cvref_t<Consume>::valuewise_tag;}) {
#pragma clang loop vectorize(disable)
            for(unsigned i=0;i<state.count;++i){value_type value;
                std::memcpy(value.data(),state.payload+std::size_t(i)*V,V);consume(value);
            }
        }else {
            for(unsigned i=0;i<state.count;++i){value_type value;
                std::memcpy(value.data(),state.payload+std::size_t(i)*V,V);consume(value);
            }
        }
        found=true;return true;
    }
    void validate()const {
        std::vector<typename buffer_owner::reference_witness> owners;
        std::vector<unsigned> capacities;
        index_.visit_cells([&](word,const byte* cell){const auto meta=ht::load<8>(cell);const auto n=count(meta);
            if(is_external(meta)){
                if constexpr(inline_capacity)capacities.push_back(reservation(meta));
                owners.push_back({ht::load<8>(cell+8),family(meta),n});
            }
            else ht::require(n&&n<=inline_count,"inline cell count");
        });
        buffers_.validate(owners);
        // Admit each reference and its actual block family before indexing the
        // family table with any cell-supplied geometry.
        if constexpr(inline_capacity)for(std::size_t i=0;i<owners.size();++i)
            ht::require(capacities[i]==buffers_.capacity(owners[i].which),"cached capacity matches source family");
    }
};
}
