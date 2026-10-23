#pragma once
#include <ikea/hashtable/table.h>
#include <ikea/hashtable/author/table.h>
#include <ikea/streampack/read.h>
#include <ikea/streampack/write.h>
#include <optional>
#include "stream-write.h"
#include "compound-damage.h"

namespace ikea::hashtable {
namespace stream=ikea::streampack;
// Common storage only: each arena still checks its own owner and generation.
// The owner pointer also tags the enclosing multimap's one fresh destination.
struct prepared_stream_head {
    const void* owner;
    std::uint64_t generation;
    std::size_t block;
    stream::view destination;
};
struct bag_reservations {std::size_t singleton=0,small=0,large=0;};
// A move invalidates outstanding preparations at both object addresses. The
// moved-from owner may only be destroyed or assigned; no storage is retained.
struct preparation_generation {
    std::uint64_t value=0;
    preparation_generation()=default;
    preparation_generation(const preparation_generation&)=default;
    preparation_generation& operator=(const preparation_generation&)=default;
    preparation_generation(preparation_generation&& other)noexcept:value(other.value){++other.value;}
    preparation_generation& operator=(preparation_generation&& other)noexcept{++value;++other.value;return *this;}
    operator std::uint64_t()const{return value;}
    std::uint64_t operator++(){return ++value;}
};
// Arithmetic block IDs avoid a per-block pointer directory. Each immutable
// layout is shared across the arena; ID interpretation is scoped to this owner.
// Growth changes the arena base and retains every logical ID. Publication of
// a replacement owner must retain the old arena for old readers.
template<unsigned V,bool Singleton,unsigned Payload=Singleton?256*V:65536,class Storage=heap_storage_policy> class bag_arena {
    using allocation_context=typename Storage::context;
    using allocation=typename Storage::buffer;
    static_assert(V>0&&V<=64);
    static constexpr unsigned payload_bytes=Payload;
    static_assert(Singleton?Payload==256*V:Payload>=512&&Payload<=65536);
    using offset_type=std::conditional_t<Payload<=65535,std::uint16_t,std::uint32_t>;
    static constexpr unsigned directory_bytes_=32+(Singleton?0:256*sizeof(offset_type));
    static constexpr unsigned lengths_bytes_=Singleton?0:512;
    stream::layout format_=[] {
        stream::layout::options d{.family=stream::geometry::absolute_ends,.maximum=Singleton?V:65535,
            .byte_limit=payload_bytes,.fixed_reservation=Singleton?std::optional<std::uint16_t>(V):std::nullopt,
            .full=Singleton,.immortal=false};
        auto f=stream::layout::make(d);require(bool(f),"StreamPack layout");
        require(f->directory_bytes()==directory_bytes_&&f->lengths_bytes()==lengths_bytes_&&
            f->geometry_offset()==32&&f->used_offset()==0&&f->group_stride()==(Singleton?0:32)&&
            f->control_bytes()==2,"admitted Direct block recipe");return *f;
    }();
    static constexpr std::size_t payload_offset=(directory_bytes_+lengths_bytes_+2+63)&~std::size_t{63};
    allocation_context* memory_=nullptr;
    std::size_t construction_reserve_=0;
    allocation bytes_;
    // Cold owner metadata only. References resolve the payload offset directly;
    // lookup never follows this vector or an allocation-record directory.
    std::vector<std::size_t> block_offsets_;
    std::size_t used_end_=0;
    std::size_t retired_=0;
    preparation_generation head_generation_;
    std::size_t active_block_=0,recycle_cursor_=0;
    bool has_recycled_=false;
    // Existing physical ceilings, not the schema byte limit, bound reused blocks.
    std::size_t physical_capacity(std::size_t block)const {
        return block+1==block_offsets_.size()?payload_bytes:
            std::min<std::size_t>(payload_bytes,block_offsets_[block+1]-payload_offset-block_offsets_[block]);
    }
    bool entirely_retired(const stream::view& source)const {
        const auto count=source.size();
        const auto* tombs=reinterpret_cast<const byte*>(source.windows().directory.data());
        for(unsigned begin=0;begin<count;begin+=64){
            const auto bits=std::min(64u,count-begin);
            const word assigned=bits==64?~word{0}:(word{1}<<bits)-1;
            if((load<8>(tombs+begin/8)&assigned)!=assigned)return false;
        }
        return true; // An abandoned initialized empty block is reusable too.
    }
    bool recycle_block(unsigned reservation) {
        // Fresh construction has no reusable blocks. After a reset, retain
        // scanning even at retired_==0: an abandoned empty middle block can fit
        // a later request, and must not disappear from the reusable population.
        if(!retired_&&!has_recycled_)return false;
        // Bounded pressure scan: no allocation or new heap mutation on retire.
        // The cursor prevents a repeatedly unhelpful prefix from monopolizing work.
        const auto attempts=std::min<std::size_t>(64,block_offsets_.size());
        for(std::size_t i=0;i<attempts;++i){
            const auto block=recycle_cursor_;
            if(++recycle_cursor_==block_offsets_.size())recycle_cursor_=0;
            if(physical_capacity(block)<reservation)continue;
            auto destination=view(block);
            if(!entirely_retired(destination))continue;
            const auto reclaimed=extent(destination);
            require(retired_>=reclaimed,"retired block accounting");
            // Initialization belongs to private preparation. No prepared view or
            // staged reader from this mutable owner survives this mutation.
            ikea::no_effects effects;
            stream::initialize_unchecked(destination,effects);
            retired_-=reclaimed;active_block_=block;has_recycled_=true;return true;
        }
        return false;
    }
    [[gnu::always_inline]] void admit_read_recipe()const {
        const bool fixed=format_.fixed(),used=format_.has_used();
        const auto width=format_.fixed_width(),geometry=format_.geometry_offset(),
            used_offset=format_.used_offset(),stride=format_.group_stride();
        if constexpr(Singleton){__builtin_assume(fixed);__builtin_assume(!used);__builtin_assume(width==V);}
        else {__builtin_assume(!fixed);__builtin_assume(used);}
        __builtin_assume(geometry==32);__builtin_assume(used_offset==0);
        __builtin_assume(stride==(Singleton?0:32));
    }
    stream::storage<stream::byte> windows_at(std::size_t offset)const {
        auto* p=reinterpret_cast<stream::byte*>(bytes_.data+offset);
        const auto available=std::min<std::size_t>(payload_bytes,bytes_.size-offset);
        return {{p,available},{p-payload_offset,directory_bytes_},
            {p-payload_offset+directory_bytes_,lengths_bytes_},
            {p-payload_offset+directory_bytes_+lengths_bytes_,2}};
    }
    auto windows(std::size_t block)const{
        auto result=windows_at(block_offsets_[block]);
        result.payload=result.payload.first(std::min(result.payload.size(),physical_capacity(block)));
        return result;
    }
    auto view(std::size_t block)const{return stream::view::attach_unchecked(format_,windows(block));}
    unsigned extent(const stream::view& source)const {
        {
            admit_read_recipe();
            using Access=stream::author::access<stream::geometry::absolute_ends,
                std::conditional_t<Singleton,std::uint8_t,std::uint16_t>,offset_type,stream::byte>;
            return Access(source).start(source.size());
        }
    }
    auto bounded_view(std::size_t block)const {
        auto parts=windows(block);
        const auto end=block+1<block_offsets_.size()?block_offsets_[block+1]-payload_offset:used_end_;
        parts.payload=parts.payload.first(std::min<std::size_t>(payload_bytes,end-block_offsets_[block]));
        return stream::view::attach_unchecked(format_,parts);
    }
    // The ID names the payload's cache-line offset. Metadata is at fixed
    // negative offsets, so variable-size sealed blocks add no lookup hop.
    auto reference_view(word ref)const{return stream::view::attach_unchecked(format_,windows_at((ref>>8)*64));}
    // Keep the common capacity check visible without pulling allocator growth
    // and its exception paths into every prepared append.
    [[gnu::always_inline]] void ensure(std::size_t needed) {
        if(needed>bytes_.size)ensure_capacity(needed);
    }
    [[gnu::noinline]] void ensure_capacity(std::size_t needed) {
        if(needed>std::numeric_limits<std::size_t>::max()/2)throw std::length_error("StreamPack arena extent");
        // Expose at most 64 KiB of reusable construction slack, independently
        // of the (potentially much larger) virtual reservation. Seal contracts
        // the final logical extent once, rather than trimming every block.
        const auto exposed=(needed+65535)&~std::size_t{65535};
        const auto reserve=std::max({std::size_t{65536},std::bit_ceil(exposed),construction_reserve_});
        if(!bytes_.data)bytes_=allocation(exposed,memory_,true,reserve);
        else bytes_.grow(exposed,std::max(reserve,bytes_.reserved_bytes()));
    }
    void new_block(unsigned reservation) {
        if(used_end_>std::numeric_limits<std::size_t>::max()-payload_offset-63-reservation)
            throw std::length_error("StreamPack block extent");
        const auto offset=((used_end_+63)&~std::size_t{63})+payload_offset;
        if(block_offsets_.size()==block_offsets_.capacity())
            block_offsets_.reserve(std::max<std::size_t>(1,block_offsets_.size()*2));
        ensure(offset+reservation);
        auto admitted=stream::view::attach(format_,windows_at(offset));require(bool(admitted),"StreamPack block placement");
        ikea::no_effects effects;stream::initialize_unchecked(*admitted,effects);
        block_offsets_.push_back(offset);used_end_=offset;
        active_block_=block_offsets_.size()-1;
    }
    bag_arena(const bag_arena& source,allocation_context* memory):format_(source.format_),
        memory_(memory),construction_reserve_(source.construction_reserve_),
        bytes_(duplicate_buffer<Storage>(source.bytes_,memory)),block_offsets_(source.block_offsets_),
        used_end_(source.used_end_),retired_(source.retired_),head_generation_(source.head_generation_)
        ,active_block_(source.active_block_),recycle_cursor_(source.recycle_cursor_),has_recycled_(source.has_recycled_)
    {}
  public:
    bag_arena clone(allocation_context* memory=nullptr)const{return bag_arena(*this,memory);}
    static constexpr unsigned payload_limit=payload_bytes;
    explicit bag_arena(allocation_context* memory=nullptr,std::size_t reserve=0):memory_(memory),construction_reserve_(reserve){}
    using prepared_head=prepared_stream_head;
    bool valid(const prepared_head& head)const{return head.owner==this&&head.generation==head_generation_;}
    static bool admits_input(const stream::view& destination,std::span<const byte> input) {
        // Whole-operation source admission precedes every store. Per-chunk
        // checked calls cannot make a pair atomic when its second source aliases
        // metadata or controls written by the first chunk.
        const auto planes=destination.windows().planes();
        for(unsigned i=1;i<planes.size();++i)
            if(input_overlaps(input,planes[i].data(),planes[i].size()))return false;
        return !input_overlaps(input,&destination,sizeof(destination))&&
            !input_overlaps(input,&destination.format(),sizeof(stream::layout));
    }
    bool admits_replacement_input(word ref,std::span<const byte> input)const {
        const auto destination=reference_view(ref);return admits_input(destination,input);
    }
    bool disjoint_input(std::span<const byte> input)const {
        return !input_overlaps(input,bytes_.data,bytes_.size)&&!input_overlaps(input,this,sizeof(*this));
    }
    void stabilize_input(std::span<const byte>& input,std::vector<byte>& snapshot)const {
        if(disjoint_input(input))return;
        // Only payload aliases are admitted by this complete-operation entry.
        // Snapshot before prepare can grow/rebase the selected destination.
        const auto address=reinterpret_cast<std::uintptr_t>(input.data());
        for(std::size_t block=0;block<block_offsets_.size();++block){
            const auto source=bounded_view(block);const auto payload=source.windows().payload;
            const auto begin=reinterpret_cast<std::uintptr_t>(payload.data());
            if(address>=begin&&address-begin<=payload.size()&&input.size()<=payload.size()-(address-begin)){
                snapshot.assign(input.begin(),input.end());input=snapshot;return;
            }
        }
        require(false,"complete bag input aliases arena metadata");
    }
    word prepared_reference(const prepared_head& head)const {
        require(valid(head),"stale or foreign StreamPack head");
        const auto block_id=block_offsets_[head.block]/64;
        require(block_id<(word{1}<<53),"StreamPack reference width");
        return (block_id<<8)|head.destination.size();
    }
    template<class Emit> void append_damage(const prepared_head& head,unsigned reservation,Emit&& emit,unsigned count=1)const {
        require(valid(head),"stale StreamPack damage plan");
        const auto payload=block_offsets_[head.block],meta=payload-payload_offset;
        const auto n=head.destination.size(),begin=extent(head.destination);
        const write_arch::physical_region source{this,1,bytes_.data,bytes_.size};
        auto add=[&](std::size_t offset,std::size_t length){emit(write_arch::write_effect(source,offset,length));};
        add(payload+begin,reservation);
        if constexpr(!Singleton){
            add(meta+32+n*sizeof(offset_type),sizeof(offset_type));
            add(meta+directory_bytes_+(n/16)*32+(n%16?2*(n%16):0),n%16?2:32);
        }
        add(meta+n/8,(n+count-1)/8-n/8+1); // A tiny run can cross a tombstone byte.
        add(meta+directory_bytes_+lengths_bytes_,2);
    }
    template<class Emit> void retire_damage(word ref,Emit&& emit,unsigned count=1)const {
        const write_arch::physical_region source{this,1,bytes_.data,bytes_.size};
        const auto rank=unsigned(ref&255);
        emit(write_arch::write_effect(source,(ref>>8)*64-payload_offset+rank/8,(rank+count-1)/8-rank/8+1));
    }
    template<class Emit> void replace_damage(word ref,unsigned length,Emit&& emit,unsigned count=1)const {
        const auto payload=(ref>>8)*64,meta=payload-payload_offset;
        const write_arch::physical_region source{this,1,bytes_.data,bytes_.size};
        auto source_view=reference_view(ref);
        const auto begin=[&]{
            {admit_read_recipe();
                using Access=stream::author::access<stream::geometry::absolute_ends,std::conditional_t<Singleton,std::uint8_t,std::uint16_t>,offset_type,stream::byte>;
                return Access(source_view).start(unsigned(ref&255));
            }
        }();
        require(count&&((Singleton&&length==count*V)||(!Singleton&&count==1)),"replacement span recipe");
        emit(write_arch::write_effect(source,payload+begin,length));
        if constexpr(!Singleton)emit(write_arch::write_effect(source,meta+directory_bytes_+2*unsigned(ref&255),2));
    }
    ikea::bytes effect_storage(unsigned plane)const noexcept {return plane==1?ikea::bytes{bytes_.data,bytes_.size}:ikea::bytes{};}
    std::size_t bytes_used()const{return bytes_.capacity()+block_offsets_.capacity()*sizeof(std::size_t)+sizeof(*this);}
    storage_usage storage()const{return bytes_.usage();}
    std::size_t metadata_bytes()const{return block_offsets_.capacity()*sizeof(std::size_t)+sizeof(*this);}
    void retain_storage(typename Storage::retention& holds)const{bytes_.retain(holds);}
    typename Storage::trim_result relinquish_surplus(){
        ++head_generation_; // Previously prepared windows can include the tail.
        return bytes_.relinquish_tail(used_end_);
    }
    template<class Visit> void allocations(Visit&& visit)const {if(bytes_.size)visit(Singleton?"singleton_bags":Payload<65536?"small_variable_bags":"large_variable_bags",bytes_.data,bytes_.size);}
    std::size_t retired_bytes()const{return retired_;}
    // Any subsequent preparation or mutation invalidates this head. Growth
    // and all fallible work happen before the nonsuspending commit body.
    prepared_head prepare(unsigned length,unsigned reservation,unsigned slots=1) {
        require(length<=reservation&&reservation<=65535&&reservation<=payload_bytes,"bag reservation");
        require(slots&&slots<=256&&(Singleton||slots==1),"prepared StreamPack ranks");
        if constexpr(Singleton)require(length==slots*V&&reservation==slots*V,"complete fixed ranks");
        ++head_generation_;
        if(block_offsets_.empty())new_block(reservation);
        else {const auto dst=view(active_block_);const auto begin=extent(dst);
            if(dst.size()+slots>256||begin+reservation>physical_capacity(active_block_)){
                if(!recycle_block(reservation))new_block(reservation);
            }
        }
        const auto block=active_block_;
        ensure(block_offsets_[block]+extent(view(block))+reservation);
        return {this,head_generation_,block,view(block)};
    }
    template<class Effects> word append(prepared_head& head,std::span<const byte> input,unsigned reservation,Effects& effects) {
        require(head.owner==this&&head.generation==head_generation_,"stale or foreign StreamPack head");
        const auto block_id=block_offsets_[head.block]/64;
        require(block_id<(word{1}<<53),"StreamPack reference width");
        const auto rank=head.destination.size();
        {
            const auto begin=extent(head.destination);
            require(input.size()<=reservation&&reservation<=65535,"prepared StreamPack length");
            if constexpr(Singleton)require(input.size()==V&&reservation==V,"prepared fixed value");
            require(rank<256&&begin<=payload_bytes&&reservation<=payload_bytes-begin&&
                begin<=head.destination.windows().payload.size()&&reservation<=head.destination.windows().payload.size()-begin,"prepared StreamPack room");
            require(!stream::detail::input_alias(head.destination,std::as_bytes(input),effects),"prepared StreamPack source admission");
            if constexpr(std::same_as<Effects,ikea::journal>)
                require(stream::detail::room(effects,(reservation!=0)+(Singleton?0:2)+2),"prepared StreamPack effects");
            bag_write::writer<V,Singleton,offset_type> write(head.destination,rank,begin);
            write.append(std::as_bytes(input),reservation,effects);
            used_end_=std::max(used_end_,block_offsets_[head.block]+write.extent());
        }
        head.generation=++head_generation_;
        return (block_id<<8)|rank;
    }
    word append(prepared_head& head,std::span<const byte> input,unsigned reservation) {
        ikea::no_effects effects;return append(head,input,reservation,effects);
    }
    word append(std::span<const byte> input,unsigned reservation) {auto head=prepare(input.size(),reservation);return append(head,input,reservation);}
    template<class Effects> word append_run(prepared_head& head,std::span<const byte> input,Effects& effects) requires(Singleton) {
        require(valid(head)&&!input.empty()&&input.size()%V==0&&input.size()<=2*V,"prepared fixed run");
        const auto count=input.size()/V;
        const auto begin=extent(head.destination);
        require(head.destination.size()+count<=256&&begin+input.size()<=payload_bytes&&begin+input.size()<=head.destination.windows().payload.size(),"fixed run room");
        require(!stream::detail::input_alias(head.destination,std::as_bytes(input),effects),"fixed run source admission");
        if constexpr(std::same_as<Effects,ikea::journal>)require(stream::detail::room(effects,3*count),"fixed run effects admission");
        const auto first=prepared_reference(head);
        {
            bytes<2*V> copy;std::memcpy(copy.data(),input.data(),V);
            if(count==2)std::memcpy(copy.data()+V,input.data()+V,V);
            bag_write::writer<V,true,offset_type> write(head.destination,unsigned(first&255),begin);
            for(unsigned i=0;i<count;++i)write.append(std::as_bytes(std::span(copy).subspan(i*V,V)),V,effects);
            used_end_=std::max(used_end_,block_offsets_[head.block]+write.extent());head.generation=++head_generation_;
        }
        return first;
    }
    word append_run(prepared_head& head,std::span<const byte> input) requires(Singleton) {
        ikea::no_effects effects;return append_run(head,input,effects);
    }
    template<class Consume> [[gnu::always_inline]] void consume(word ref,Consume&& consume_value)const {
        const auto value=locate(ref);
        for(std::size_t i=0;i<value.size();i+=V){bytes<V> out;std::memcpy(out.data(),value.data()+i,V);consume_value(out);}
    }
    [[gnu::always_inline]] stream::bytes locate(word ref)const {
        // These immutable schema facts were established by this arena's
        // constructor, not inferred from stored bytes or from the query.
        // Retain the real authored StreamPack read while specializing away
        // runtime geometry branches (and the singleton's variable-length loop).
        admit_read_recipe();
        auto source=reference_view(ref);
        using Access=stream::author::access<stream::geometry::absolute_ends,
            std::conditional_t<Singleton,std::uint8_t,std::uint16_t>,offset_type,stream::byte>;
        const Access read(source);return read.value(unsigned(ref&255));
    }
    // The owner proves count adjacent live fixed ranks in this block, never
    // crossing rank256. This is a run of values, not one widened leaf value.
    [[gnu::always_inline]] stream::bytes locate_run(word ref,unsigned count)const requires(Singleton) {
        admit_read_recipe();auto source=reference_view(ref);
        const stream::author::access<stream::geometry::absolute_ends,std::uint8_t,offset_type,stream::byte> read(source);
        return source.windows().payload.subspan(read.start(unsigned(ref&255)),count*V);
    }
    template<class Emit> void metadata(word ref,Emit&& emit)const {
        admit_read_recipe();
        auto source=reference_view(ref);using Access=stream::author::access<stream::geometry::absolute_ends,
            std::conditional_t<Singleton,std::uint8_t,std::uint16_t>,offset_type,stream::byte>;
        const Access read(source);read.visit_value_metadata(unsigned(ref&255),[&](auto bytes){emit(reinterpret_cast<const byte*>(bytes.data()),bytes.size());});
    }
    std::vector<byte> copy(word ref)const {
        std::vector<byte> out;consume(ref,[&](const bytes<V>& value){out.insert(out.end(),value.begin(),value.end());});return out;
    }
    unsigned reserved(word ref)const {auto source=reference_view(ref);
        {admit_read_recipe();
            using Access=stream::author::access<stream::geometry::absolute_ends,std::conditional_t<Singleton,std::uint8_t,std::uint16_t>,offset_type,stream::byte>;
            return Access(source).reserved(unsigned(ref&255));
        }
    }
    void replace(word ref,std::span<const byte> value) {auto source=reference_view(ref);ikea::no_effects effects;
        {
            const auto rank=unsigned(ref&255);
            require(rank<source.size()&&source.live_unchecked(rank),"live replacement rank");
            admit_read_recipe();
            using Access=stream::author::access<stream::geometry::absolute_ends,std::conditional_t<Singleton,std::uint8_t,std::uint16_t>,offset_type,stream::byte>;
            const Access access(source);const auto begin=access.start(rank),reservation=access.reserved(rank);
            require(value.size()<=65535&&value.size()<=reservation,"replacement fits reservation");
            if constexpr(Singleton)require(value.size()==V,"complete fixed replacement");
            require(admits_input(source,value),"replacement source admission");
            bag_write::replace<Singleton>(source,rank,begin,std::as_bytes(value),effects);
        }
        ++head_generation_;
    }
    void retire(word ref) {auto source=reference_view(ref);
        if(source.live_unchecked(unsigned(ref&255)))retired_+=reserved(ref);
        ikea::no_effects effects;
        stream::retire_unchecked(source,unsigned(ref&255),effects);++head_generation_;}
    void validate()const {for(std::size_t b=0;b<block_offsets_.size();++b)require(bool(bounded_view(b).validate()),"StreamPack contents");}
    // Owner run validation compares its complete rank partition with the leaf's
    // independently recorded liveness. Sorting happens only in cold validation.
    void validate_partition(std::vector<word>& references)const requires(Singleton) {
        std::sort(references.begin(),references.end());
        require(std::adjacent_find(references.begin(),references.end())==references.end(),"overlapping fixed runs");
        std::size_t next=0;
        for(std::size_t b=0;b<block_offsets_.size();++b){
            const auto source=bounded_view(b);
            for(unsigned rank=0;rank<source.size();++rank)if(source.live_unchecked(rank)){
                const auto ref=((block_offsets_[b]/64)<<8)|rank;
                require(next<references.size()&&references[next++]==ref,"fixed run partition/liveness");
            }
        }
        require(next==references.size(),"unassigned fixed run reference");
    }
    void validate_reference(word ref)const {
        const auto offset=(ref>>8)*64;
        const auto block=std::lower_bound(block_offsets_.begin(),block_offsets_.end(),offset);
        require(block!=block_offsets_.end()&&*block==offset,"StreamPack block ID");
        const auto value=stream::value(bounded_view(block-block_offsets_.begin()),unsigned(ref&255));
        require(bool(value)&&!value->empty()&&value->size()%V==0,"live complete StreamPack bag");
    }
};
} // namespace ikea::hashtable
namespace ikea::hashtable {
template<unsigned W,unsigned b,unsigned V,unsigned E=W-b,search_kernel Kernel=search_kernel::generic,class Storage=heap_storage_policy> class multimap {
    template<unsigned,unsigned,unsigned,unsigned,search_kernel,class> friend class multimap;
    using allocation_context=typename Storage::context;
    using Index=table<W,b,8,E,Kernel,Storage>;
    static constexpr bool Tiny=V<=16;
    Index index_;
    bag_arena<V,true,256*V,Storage> singletons_;
    using small_arena=bag_arena<V,false,512*V,Storage>;
    small_arena bags_;
    bag_arena<V,false,65536,Storage> large_bags_;
    preparation_generation mutation_generation_;
    multimap(const multimap& source,allocation_context* memory):
        index_(source.index_.clone(memory)),singletons_(source.singletons_.clone(memory)),
        bags_(source.bags_.clone(memory)),large_bags_(source.large_bags_.clone(memory)),
        mutation_generation_(source.mutation_generation_){}
    struct rebuild_index_tag {};
    template<unsigned OldB,unsigned OldE,search_kernel OldFilter>
    multimap(rebuild_index_tag,const multimap<W,OldB,V,OldE,OldFilter,Storage>& source,
             word seed,allocation_context* memory):
        index_(seed,memory),singletons_(source.singletons_.clone(memory)),
        bags_(source.bags_.clone(memory)),large_bags_(source.large_bags_.clone(memory)),
        mutation_generation_(source.mutation_generation_) {
        // The arena representation and block/rank coordinates do not depend on
        // the hash seed or index geometry. Rebuild only the eight-byte references;
        // copied arenas retain dead blocks and reservation metadata too.
        source.index_.visit([&](word key,const bytes<8>& reference){index_.insert(key,reference);});
    }
    static constexpr word external=word{1}<<63;
    static constexpr word large=word{1}<<61;
    // Under !external, bit61 instead means a second adjacent
    // fixed rank. Arena IDs use only bits0..60, so this authoritative count uses no ID bits.
    static word tiny_id(word ref){if constexpr(Tiny)return ref&~large;else return ref;}
    static unsigned tiny_count(word ref){if constexpr(Tiny)return 1+unsigned(bool(ref&large));else return 1;}
    static word tiny_count_tag(unsigned length){if constexpr(Tiny)return length==2*V?large:0;else return 0;}
    auto tiny_values(word ref)const {return singletons_.locate_run(tiny_id(ref),tiny_count(ref));}
    static bytes<8> encode(word ref){bytes<8> out;store<8>(out.data(),ref);return out;}
    bool reference(word key,word& ref)const{return index_.find(key,[&](const auto& v){ref=load<8>(v.data());});}
    template<class Fn> [[gnu::always_inline]] decltype(auto) variable(word ref,Fn&& fn)const {
        if(ref&large)return fn(large_bags_,ref^(external|large));
        return fn(bags_,ref^external);
    }
    template<class Fn> decltype(auto) variable(word ref,Fn&& fn) {
        if(ref&large)return fn(large_bags_,ref^(external|large));
        return fn(bags_,ref^external);
    }
    template<class Consume> [[gnu::always_inline]] static void consume_values(stream::bytes raw,Consume&& c) {
        for(std::size_t i=0;i<raw.size();i+=V){bytes<V> out;std::memcpy(out.data(),raw.data()+i,V);c(out);}
    }
    template<class Consume> [[gnu::always_inline]] void consume(word ref,Consume&& c)const {
        if(ref&external){
            const auto raw=variable(ref,[](const auto& arena,word id){return arena.locate(id);});
            consume_values(raw,c);
        }else if constexpr(Tiny){
            // The run admission proves a contiguous interval in one block.
            // Resolve its first rank once; a singleton never reads its neighbour.
            const auto raw=tiny_values(ref);bytes<V> first;
            std::memcpy(first.data(),raw.data(),V);c(first);
            if(ref&large){bytes<V> second;std::memcpy(second.data(),raw.data()+V,V);c(second);}
        }else singletons_.consume(ref,c);
    }
    template<class Consume> [[gnu::always_inline]] void consume_at(word ref,typename Index::location,Consume&& c)const {
        consume(ref,c);
    }
    word first_reference(word,const bytes<V>& value) {
        return singletons_.append(value,V);
    }
    std::vector<byte> copy_reference(word ref)const {
        if(ref&external)return variable(ref,[](const auto& arena,word id){return arena.copy(id);});
        const auto raw=tiny_values(ref);std::vector<byte> out(raw.size());std::memcpy(out.data(),raw.data(),raw.size());return out;
    }
    void retire_reference(word ref) {
        if(ref&external)variable(ref,[](auto& arena,word id){arena.retire(id);});
        else for(unsigned i=0;i<tiny_count(ref);++i)singletons_.retire(tiny_id(ref)+i);
    }
    template<class Emit> void retirement_damage(word ref,Emit&& emit)const {
        if(ref&external)variable(ref,[&](const auto& arena,word id){arena.retire_damage(id,emit);});
        else singletons_.retire_damage(tiny_id(ref),emit,tiny_count(ref));
    }
  public:
    ikea::bytes effect_storage(const void* source,unsigned plane)const noexcept {
        if(source==this&&plane==unsigned(hashtable::plane::header))
            return {reinterpret_cast<const byte*>(this),sizeof(*this)};
        if(source==&index_)return index_.plane_storage(hashtable::plane(plane));
        if(source==&singletons_)return singletons_.effect_storage(plane);
        if(source==&bags_)return bags_.effect_storage(plane);
        if(source==&large_bags_)return large_bags_.effect_storage(plane);
        return {};
    }
    static constexpr unsigned K=Index::K,N=Index::N,B=Index::B,encoding=E;
    static constexpr bool bitmap=Index::bitmap;
    static constexpr std::size_t bucket_count=Index::bucket_count;
    using value_type=bytes<V>;
    class prepared_bag {
        friend class multimap;
        multimap* owner=nullptr;std::uint64_t generation=0;word key=0;
        unsigned length=0,reservation=0;
        std::optional<word> old_reference;
        bool reuse=false;
        typename Index::location primary{};
        decltype(author::table_access::prepare_write(std::declval<Index&>(),word{},std::declval<typename Index::value_type&>())) index;
        // Mutually exclusive fresh destination, including reuse's late-input
        // fallback. Its checked owner provenance is the arena discriminator.
        std::optional<prepared_stream_head> destination;
    };
  private:
    template<class Head,class Arena> static auto& destination_storage(Head& head,const Arena&) {
        return head.destination;
    }
    template<class Head,class Arena> static auto* destination_for(Head& head,const Arena& arena) {
        auto& destination=destination_storage(head,arena);
        return destination&&destination->owner==&arena?&*destination:nullptr;
    }
    word append_singleton(prepared_stream_head& destination,std::span<const byte> raw) {
        if constexpr(!Tiny)return singletons_.append(destination,raw,V);
        else return singletons_.append_run(destination,raw);
    }
  public:
    using storage_policy=Storage;
    struct staged_probe {typename Index::staged_probe index;word reference=0;stream::bytes value{};std::uint8_t phase=0;};
    multimap clone(allocation_context* memory=nullptr)const{return multimap(*this,memory);}
    template<unsigned OldB,unsigned OldE,search_kernel OldFilter>
    static multimap rebuild_index(const multimap<W,OldB,V,OldE,OldFilter,Storage>& source,
                                  word seed,allocation_context* memory=nullptr) {
        return multimap(rebuild_index_tag{},source,seed,memory);
    }
    explicit multimap(word seed,allocation_context* memory=nullptr,bag_reservations reserve={}):index_(seed,memory),singletons_(memory,reserve.singleton),bags_(memory,reserve.small),large_bags_(memory,reserve.large){}
    std::size_t capacity()const{return index_.capacity();}
    std::size_t size()const{return index_.size();}
    std::size_t spill_size()const{return index_.spill_size();}
    std::size_t primary_size()const{return index_.primary_size();}
    std::size_t bytes_used()const{
        auto size=sizeof(*this)+index_.bytes_used()-sizeof(index_)+singletons_.bytes_used()-sizeof(singletons_)
            +bags_.bytes_used()-sizeof(bags_)+large_bags_.bytes_used()-sizeof(large_bags_);
        return size;
    }
    std::size_t retired_bytes()const{return singletons_.retired_bytes()+bags_.retired_bytes()+large_bags_.retired_bytes();}
    storage_usage storage()const{auto result=index_.storage();result+=singletons_.storage();result+=bags_.storage();result+=large_bags_.storage();return result;}
    void retain_storage(typename Storage::retention& holds)const{index_.retain_storage(holds);singletons_.retain_storage(holds);bags_.retain_storage(holds);large_bags_.retain_storage(holds);}
    typename Storage::trim_result relinquish_surplus(){
        auto result=index_.relinquish_surplus();if(!result)return result;
        result=singletons_.relinquish_surplus();if(!result)return result;
        result=bags_.relinquish_surplus();if(!result)return result;
        result=large_bags_.relinquish_surplus();if(!result)return result;
        return {};
    }
    template<class Visit> void allocations(Visit&& visit)const {
        index_.allocations(visit);singletons_.allocations(visit);bags_.allocations(visit);large_bags_.allocations(visit);
    }
    auto prepare(word key)const{return index_.prepare(key);}
    template<class P> void prefetch(const P& p)const{index_.prefetch(p);}
    template<class Consume> [[gnu::always_inline]] bool find(word key,Consume&& c)const {
        return index_.find(key,[&](const auto& v,typename Index::location at) __attribute__((always_inline)){consume_at(load<8>(v.data()),at,c);});
    }
    template<staged_values Values=staged_values::dependent,class Emit>
    void prepare_staged(staged_probe& state,word key,Emit&& emit)const {
        state.phase=0;index_.template prepare_staged<Values>(state.index,key,emit);
    }
    template<staged_values Values=staged_values::dependent,class Consume,class Emit>
    [[gnu::always_inline]]
    bool advance(staged_probe& state,bool& found,Consume&& consume_value,Emit&& emit)const {
        if(state.phase==0){bool hit=false;
            if(!index_.template advance<Values>(state.index,hit,[&](const auto& v){state.reference=load<8>(v.data());},emit))return false;
            if(!hit){found=false;return true;}
            if(state.reference&external){variable(state.reference,[&](const auto& arena,word id){arena.metadata(id,emit);});state.phase=1;return false;}
            state.value=tiny_values(state.reference);state.phase=2;
            emit(reinterpret_cast<const byte*>(state.value.data()),state.value.size());return false;
        }
        if(state.phase==1){state.value=variable(state.reference,[](const auto& arena,word id){return arena.locate(id);});state.phase=2;
            emit(reinterpret_cast<const byte*>(state.value.data()),state.value.size());return false;}
        consume_values(state.value,consume_value);
        found=true;return true;
    }
    bool insert_bag(word key,std::span<const value_type> values) {
        return insert_bag_bytes(key,std::span(reinterpret_cast<const byte*>(values.data()),values.size_bytes()));
    }
    prepared_bag prepare_bag(word key,unsigned count,unsigned reserved_count=0) {
        return prepare_bag_impl(key,count,reserved_count,false);
    }
    // Prepare a complete replacement outside the nonsuspending commit. The
    // canonical key grant and exclusive/private owner must cover preparation;
    // allocation or relocation requires fresh physical source admission.
    prepared_bag prepare_replacement_bag(word key,unsigned count,unsigned reserved_count=0) {
        return prepare_bag_impl(key,count,reserved_count,true);
    }
  private:
    template<bool SourceAware=false> prepared_bag prepare_bag_impl(word key,unsigned count,unsigned reserved_count,bool replace,
            std::span<const byte>* input=nullptr,std::vector<byte>* snapshot=nullptr) {
        require(!(key&~mask<W>)&&count&&count<=65535/V,"prepared bag key/length");
        if(!reserved_count)reserved_count=count;
        require(reserved_count>=count&&reserved_count<=65535/V,"prepared bag reservation");
        // Capacity preparation may rehash before a later membership refusal.
        // Invalidate outstanding slot cursors before any such layout change.
        ++mutation_generation_;
        word old=0;
        typename Index::value_type previous{};
        const auto position=author::table_access::prepare_write(index_,key,previous);
        const bool present=position.present;if(present)old=load<8>(previous.data());
        require(present==replace,"prepared bag membership");
        prepared_bag head;head.owner=this;head.generation=mutation_generation_;head.key=key;
        head.index=position;
        if(present)head.old_reference=old;
        head.length=count*V;head.reservation=reserved_count*V;
        if(present){
            if(old&external){
                if(count>(Tiny?2u:1u)&&head.reservation<=variable(old,[](const auto& arena,word id){return arena.reserved(id);}))
                    head.reuse=true;
            }else if(count<=tiny_count(old))head.reuse=true;
        }

        // The complete operation already has its source. Resolve reuse before
        // touching a fresh tail. The late-input prepared API retains its
        // allocation-free fallback and unchanged accepted aliases.
        if constexpr(SourceAware){
            require(admit_prepared_input(head,*input,true),"complete bag source admission");
            if(use_replacement(head,*input))return head;
            if(count==1||(Tiny&&count==2))singletons_.stabilize_input(*input,*snapshot);
            else if(head.reservation<=small_arena::payload_limit)bags_.stabilize_input(*input,*snapshot);
            else large_bags_.stabilize_input(*input,*snapshot);
        }

        if(count==1||(Tiny&&count==2)){
            destination_storage(head,singletons_)=singletons_.prepare(head.length,head.length,count);
        }else if(head.reservation<=small_arena::payload_limit)destination_storage(head,bags_)=bags_.prepare(head.length,head.reservation);
        else destination_storage(head,large_bags_)=large_bags_.prepare(head.length,head.reservation);
        return head;
    }
    bool use_replacement(const prepared_bag& head,std::span<const byte> raw)const {
        if(!head.reuse)return false;
        const auto old=*head.old_reference;
        {
            // Ordinary external input is disjoint from every arena write and
            // control. Sources inside the arena retain the exact slow check.
            const bool separate=old&external?variable(old,[&](const auto& arena,word id){(void)id;return arena.disjoint_input(raw);}):singletons_.disjoint_input(raw);
            if(separate)return true;
        }
        const bool admitted=old&external?variable(old,[&](const auto& arena,word id){return arena.admits_replacement_input(id,raw);}):
            singletons_.admits_replacement_input(tiny_id(old),raw);
        if(!admitted)return false;
        bool overlap=false;replacement_damage(head,[&](ikea::journal::entry d){
            overlap|=input_overlaps(raw,effect_storage(d.source,d.plane).data()+d.offset,d.size);
        });
        return !overlap;
    }
    bool admit_prepared_input(const prepared_bag& head,std::span<const byte> raw,bool reuse)const {
        if(input_overlaps(raw,this,sizeof(*this))||input_overlaps(raw,&head,sizeof(head)))return false;
        if(reuse)return true; // Complete replacement source admission above.
        if(auto* destination=destination_for(head,singletons_))return singletons_.admits_input(destination->destination,raw);
        if(auto* destination=destination_for(head,bags_))return bags_.admits_input(destination->destination,raw);
        if(auto* destination=destination_for(head,large_bags_))return large_bags_.admits_input(destination->destination,raw);
        return false;
    }
    word reused_reference(const prepared_bag& head)const {
        const auto old=*head.old_reference;
        return old&external?old:tiny_id(old)|tiny_count_tag(head.length);
    }
    template<class Emit> void replacement_damage(const prepared_bag& head,Emit&& emit)const {
        const auto old=*head.old_reference;
        if(old&external)variable(old,[&](const auto& arena,word id){arena.replace_damage(id,head.length,emit);});
        else {const auto count=head.length/V;const auto id=tiny_id(old);
            singletons_.replace_damage(id,head.length,emit,count);
            if(count<tiny_count(old))singletons_.retire_damage(id+count,emit,tiny_count(old)-count);
        }
    }
    void replace_prepared(const prepared_bag& head,std::span<const byte> raw) {
        const auto old=*head.old_reference;
        if(old&external)variable(old,[&](auto& arena,word id){arena.replace(id,raw);});
        else {const auto count=head.length/V;const auto id=tiny_id(old);
            if(count==1)singletons_.replace(id,raw);
            else {bytes<2*V> copy;std::memcpy(copy.data(),raw.data(),2*V);
                for(unsigned i=0;i<count;++i)singletons_.replace(id+i,std::span(copy).subspan(i*V,V));}
            for(unsigned i=count;i<tiny_count(old);++i)singletons_.retire(id+i);
        }
    }
    bool publish_reference(const prepared_bag& head,word ref) {
        return author::table_access::insert_prepared(index_,head.key,head.index,encode(ref));
    }
  public:
    // Complete whole-bag replacement on private/exclusive storage. Payload
    // aliases remain supported; any necessary snapshot/allocation is charged
    // here before preparation, never hidden inside prepared commit.
    bool replace_bag_bytes(word key,std::span<const byte> raw) {
        require(!raw.empty()&&raw.size()%V==0&&raw.size()<=65535,"nonempty bounded bag");
        std::vector<byte> snapshot;
        auto head=prepare_bag_impl<true>(key,raw.size()/V,0,true,&raw,&snapshot);
        return insert_bytes(head,raw);
    }
    bool replace_bag(word key,std::span<const value_type> values) {
        return replace_bag_bytes(key,{reinterpret_cast<const byte*>(values.data()),values.size_bytes()});
    }
    bool insert(prepared_bag& head,std::span<const value_type> values) {
        return insert_bytes(head,std::span(reinterpret_cast<const byte*>(values.data()),values.size_bytes()));
    }
    bool insert_bytes(prepared_bag& head,std::span<const byte> raw) {
        require(head.owner==this&&head.generation==mutation_generation_,"stale or foreign prepared bag");
        require(raw.size()==head.length,"prepared bag payload length");
        const bool reuse=use_replacement(head,raw);
        require(admit_prepared_input(head,raw,reuse),"prepared bag source admission");
        if(reuse){
            const auto ref=reused_reference(head);replace_prepared(head,raw);
            if(ref!=*head.old_reference)publish_reference(head,ref);
            ++mutation_generation_;head.owner=nullptr;return false;
        }
        ++mutation_generation_;word ref;
        if(auto* destination=destination_for(head,singletons_))ref=append_singleton(*destination,raw)|tiny_count_tag(head.length);
        else if(auto* destination=destination_for(head,bags_))ref=external|bags_.append(*destination,raw,head.reservation);
        else if(auto* destination=destination_for(head,large_bags_))ref=external|large|large_bags_.append(*destination,raw,head.reservation);
        else {
            {require(false,"prepared bag has no destination");return false;}
        }
        const bool inserted=publish_reference(head,ref);
        if(head.old_reference)retire_reference(*head.old_reference);
        head.owner=nullptr;return inserted;
    }
    template<class Context,class Consume> lookup_status guarded_find(word key,Context& context,Consume&& consume_value)const {
        if((key&~mask<W>)||!context.enter(*this,key,access_intent::read))return lookup_status::denied;
        return find(key,std::forward<Consume>(consume_value))?lookup_status::found:lookup_status::absent;
    }
    template<class Context> mutation_status guarded_insert(prepared_bag& head,std::span<const value_type> values,Context& context) {
        return guarded_insert_bytes(head,std::span(reinterpret_cast<const byte*>(values.data()),values.size_bytes()),context);
    }
    template<class Context> mutation_status guarded_insert_bytes(prepared_bag& head,std::span<const byte> raw,Context& context) {
        if(head.owner!=this||head.generation!=mutation_generation_||raw.size()!=head.length||
            !context.enter(*this,head.key,access_intent::write))return mutation_status::denied;
        const bool reuse=use_replacement(head,raw);
        if(!admit_prepared_input(head,raw,reuse))return mutation_status::denied;
        compound_damage joined(context,*this,index_,raw);
        auto add=[&](ikea::journal::entry d){joined.add(d);};
        if(reuse){
            replacement_damage(head,add);const auto ref=reused_reference(head);
            const auto result=ref==*head.old_reference?
                (joined.prepare({})?mutation_status::updated:mutation_status::denied):
                index_.guarded_insert(head.key,encode(ref),joined);
            if(result!=mutation_status::updated)return result;
            replace_prepared(head,raw);++mutation_generation_;head.owner=nullptr;return result;
        }
        word ref=0;
        if(auto* destination=destination_for(head,singletons_)){
            if(!singletons_.valid(*destination))return mutation_status::denied;
            ref=singletons_.prepared_reference(*destination)|tiny_count_tag(head.length);
            singletons_.append_damage(*destination,head.length,add,head.length/V);
        }else if(auto* destination=destination_for(head,bags_)){
            if(!bags_.valid(*destination))return mutation_status::denied;
            ref=external|bags_.prepared_reference(*destination);bags_.append_damage(*destination,head.reservation,add);
        }else if(auto* destination=destination_for(head,large_bags_)){
            if(!large_bags_.valid(*destination))return mutation_status::denied;
            ref=external|large|large_bags_.prepared_reference(*destination);
            large_bags_.append_damage(*destination,head.reservation,add);
        }else return mutation_status::denied;
        if(head.old_reference)retirement_damage(*head.old_reference,add);
        const auto result=index_.guarded_insert(head.key,encode(ref),joined);
        if(result!=mutation_status::inserted&&result!=mutation_status::updated)return result;
        // Every issued store, including both arena and index counters, has
        // already been admitted and covered. Prepared room makes this infallible.
        if(auto* destination=destination_for(head,singletons_))append_singleton(*destination,raw);
        else if(auto* destination=destination_for(head,bags_))bags_.append(*destination,raw,head.reservation);
        else if(auto* destination=destination_for(head,large_bags_))large_bags_.append(*destination,raw,head.reservation);
        if(head.old_reference)retire_reference(*head.old_reference);
        ++mutation_generation_;head.owner=nullptr;return result;
    }
    template<class Context> mutation_status guarded_erase(word key,Context& context) {
        if((key&~mask<W>)||!context.enter(*this,key,access_intent::write))return mutation_status::denied;
        word ref;if(!reference(key,ref))return mutation_status::absent;
        compound_damage joined(context,*this,index_);
        retirement_damage(ref,[&](ikea::journal::entry d){joined.add(d);});
        const auto result=index_.guarded_erase(key,joined);
        if(result==mutation_status::erased){retire_reference(ref);++mutation_generation_;}
        return result;
    }
    bool insert_bag_bytes(word key,std::span<const byte> raw) {
        require(!raw.empty()&&raw.size()%V==0&&raw.size()<=65535,"nonempty bounded bag");
        auto head=prepare_bag(key,raw.size()/V);return insert_bytes(head,raw);
    }
    bool insert(word key,const value_type& value={}) {
        require(!(key&~mask<W>),"insert key outside W-bit domain");++mutation_generation_;
        word old;if(!reference(key,old)){index_.prepare_absent(key);const word ref=first_reference(key,value);return index_.insert(key,encode(ref));}
        auto previous=copy_reference(old);
        previous.insert(previous.end(),value.begin(),value.end());require(previous.size()<=65535,"bag maximum");
        if constexpr(Tiny)if(previous.size()==2*V){
            auto head=singletons_.prepare(2*V,2*V,2);
            const auto next=singletons_.append_run(head,previous)|large;
            index_.insert(key,encode(next));retire_reference(old);return false;
        }
        if((old&external)&&previous.size()<=variable(old,[](const auto& arena,word id){return arena.reserved(id);})){
            variable(old,[&](auto& arena,word id){arena.replace(id,previous);});return false;}
        const unsigned reservation=std::min<std::size_t>(65535,std::bit_ceil(previous.size()));
        const auto next=reservation<=small_arena::payload_limit?bags_.append(previous,reservation)|external:
            large_bags_.append(previous,reservation)|external|large;
        index_.insert(key,encode(next));
        retire_reference(old);return false;
    }
    bool erase(word key) {require(!(key&~mask<W>),"erase key outside W-bit domain");++mutation_generation_;word ref;if(!reference(key,ref))return false;index_.erase(key);
        retire_reference(ref);return true;}
    template<class Visit> void visit(Visit&& visitor)const {
        index_.visit([&](word k,const auto& ref){consume(load<8>(ref.data()),[&](const value_type& value){visitor(k,value);});});
    }
    template<class Visit> void visit_bags(Visit&& visitor)const {
        index_.visit([&](word key,const auto& value){const auto ref=load<8>(value.data());
            const auto raw=[&]{
                return ref&external?variable(ref,[](const auto& arena,word id){return arena.locate(id);}):tiny_values(ref);}();
            visitor(key,std::span(reinterpret_cast<const byte*>(raw.data()),raw.size()));
        });
    }
    void validate()const{
        index_.validate();singletons_.validate();bags_.validate();large_bags_.validate();
        std::vector<word> fixed_ranks;
        index_.visit([&](word,const auto& value){const auto ref=load<8>(value.data());
            if(ref&external)variable(ref,[](const auto& arena,word id){arena.validate_reference(id);});
            else {const auto id=tiny_id(ref);require((id&255)+tiny_count(ref)<=256,"tiny run block boundary");
                for(unsigned i=0;i<tiny_count(ref);++i){singletons_.validate_reference(id+i);fixed_ranks.push_back(id+i);}}});
        singletons_.validate_partition(fixed_ranks);
    }
};
} // namespace ikea::hashtable
