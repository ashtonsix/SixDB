#pragma once
#include "recipe_view.h"
#include "hash_packet.h"
#include "links.h"
#ifndef HT_BUCKET_SENTINEL
#define HT_BUCKET_SENTINEL 0
#endif
#ifndef HT_RECORD_CONSUME
#define HT_RECORD_CONSUME 0
#endif
#ifndef HT_HIERARCHY_PACKET_COMPACT
#define HT_HIERARCHY_PACKET_COMPACT 0
#endif
#ifndef HT_HIERARCHY_PACKET_HASH
#define HT_HIERARCHY_PACKET_HASH 0
#endif
#ifndef HT_RECORD_READ_WINDOWS
#define HT_RECORD_READ_WINDOWS 0
#endif
static_assert(HT_RECORD_READ_WINDOWS>=0 && HT_RECORD_READ_WINDOWS<=2);
static_assert(HT_HIERARCHY_PACKET_COMPACT==0 || HT_HIERARCHY_PACKET_COMPACT==1);
static_assert(HT_HIERARCHY_PACKET_HASH>=0 && HT_HIERARCHY_PACKET_HASH<=2);

namespace ht {
using existing_record_result=std::optional<std::expected<mutation_delta,mutation_error>>;
struct skip_existing_record_edit {
    template<class Edit> existing_record_result operator()(Edit&)const {return {};}
};
struct no_record_effects {
    void bucket(const void*,const oa::Allocation&,std::size_t,std::size_t,bool)const noexcept {}
    void operator()(const link_event&)const noexcept {}
};
// Geometry control for the integrated transition experiment. Unlike the old
// word-map table, values are opaque bytes plus the explicit MM tag throughout
// both bucket tiers and key-overflow links. MM bag ownership is separate.
template<unsigned K,container_kind Kind,unsigned ValueBytes,unsigned A=6,unsigned B=2,
         unsigned PrimaryBytes=128,unsigned SecondaryBytes=128,
         record_format Format=record_format::exact,bool Direct=false,
         record_access Access=record_access::exact,unsigned TagBits=1> class record_table {
    template<class Table> friend struct consolidation_access;
    static_assert(A<K && B<K);
    using primary_list=list_view<K,Kind,ValueBytes,A,TagBits>;
    using secondary_list=list_view<K,Kind,ValueBytes,B,TagBits>;
    static_assert(primary_list::payload_bytes<=64); // A multimap tag adds one encoded byte.
    using chains=links<K,false,primary_list::row_bytes>;
    static constexpr bool legacy=Format==record_format::legacy_power_two && !Direct && Access==record_access::exact;
    static_assert(!HT_RECORD_READ_WINDOWS || !legacy,"selective hints require the admitted field recipe");
    using ls=layout_solver::candidate;
    template<class View> static constexpr auto make_layout(unsigned slots,unsigned bytes,bool secondary) {
        namespace l=layout_solver;
        return l::make({.fingerprint_bits=View::hash_type::F,.suffix_bits=View::hash_type::C,
                .value_bytes=ValueBytes,.kind=Kind,.where=secondary?l::tier::secondary:l::tier::primary,
                .target=record_platform,.count_bytes=1,.descendant_bytes=4,
                .head_bytes=secondary?4u:0u,.chain_reference_bytes=4,.allocation_alignment=64,.tag_bits=TagBits},
                {.slots=slots,.unit_bytes=bytes,.fp=Format==record_format::padded?l::fingerprint::byte_tiles:l::fingerprint::exact_bytes,
                 .suffix_encoding=l::suffix::bytes,.fields=Format==record_format::rows?l::body::rows:l::body::planes,
                 .tag=Kind!=container_kind::multimap?l::tags::none:
                    Format==record_format::tag_plane?l::tags::byte_plane:
                    (Format==record_format::tag_bits||Format==record_format::aligned)?l::tags::bit_plane:l::tags::row_byte,.selector_lanes=16,
                 .value_alignment=Kind!=container_kind::set && Format==record_format::aligned?8u:1u});
    }
    template<class View> static consteval ls solve(unsigned bytes,bool secondary) {
        for(unsigned slots=32;slots;--slots) {
            if constexpr(Format==record_format::legacy_power_two)if(!std::has_single_bit(slots))continue;
            if(auto result=make_layout<View>(slots,bytes,secondary))return *result;
        }
        throw "no admitted record bucket";
    }
  public:
    using record=typename primary_list::record;
    static constexpr auto primary=solve<primary_list>(PrimaryBytes,false);
    static constexpr auto secondary=solve<secondary_list>(SecondaryBytes,true);
    static constexpr unsigned key_bits=K,primary_bits=A,secondary_bits=B;
    // Cold experimental admission. The same solver owns both the minimum
    // extent for a requested slot count and the final fitting bucket recipe.
    // Padding may admit additional slots; callers inspect primary/secondary.
    static consteval unsigned unit_for_slots(unsigned slots,bool secondary,unsigned quantum) {
        if(!slots || slots>32 || !std::has_single_bit(quantum) || quantum>64)throw "slot extent admission";
        const auto plan=secondary?make_layout<secondary_list>(slots,4096,true):make_layout<primary_list>(slots,4096,false);
        if(!plan)throw "slot extent layout";
        return unsigned((plan->used_bytes+quantum-1)/quantum*quantum);
    }
  private:
    static constexpr bool sentinel=HT_BUCKET_SENTINEL && !legacy;
    using primary_view=std::conditional_t<legacy,primary_list,recipe_view<K,Kind,ValueBytes,A,primary,Direct,Access,sentinel>>;
    using secondary_view=std::conditional_t<legacy,secondary_list,recipe_view<K,Kind,ValueBytes,B,secondary,Direct,Access,sentinel>>;
    oa::Allocation p_,q_;
    typename primary_view::hash_type hp_;
    typename secondary_view::hash_type hq_;
    chains overflow_;
    template<class View,const ls& Plan> struct bound_bucket {
        byte* base;
        View view()const {
            if constexpr(legacy)return {base+Plan.header_bytes,Plan.shape.slots};else return {base};
        }
        unsigned count()const {return get<1>(base);}
        word descendants()const {return get<4>(base+1);}
        void descendants(word n)const {put<4>(base+1,n);}
        std::uint32_t head()const {static_assert(Plan.spec.where==layout_solver::tier::secondary);return get<4>(base+5);}
        void head(std::uint32_t n)const {put<4>(base+5,n);}
        template<bool Read,class Consume> int find(byte fp,word suffix,Consume&& consume)const {
            auto query=[&](unsigned n) {
                if constexpr(Read && !legacy)return view().find_read(fp,suffix,n,consume);
                else {
                    const int rank=view().find(fp,suffix,n);
                    if constexpr(Read)if(rank>=0)consume(view().read(unsigned(rank)));
                    return rank;
                }
            };
            // The physical fingerprint plane is eight bits even when the
            // logical domain is narrower. Only a real 255 query needs count.
            if constexpr(sentinel) {
                if constexpr(View::hash_type::F<8)return query(Plan.shape.slots);
                else if(fp!=255)return query(Plan.shape.slots);
            }
            return query(count());
        }
        record read(unsigned rank)const {return view().read(rank);}
        byte tag(unsigned rank)const {
            static_assert(Kind==container_kind::multimap);
            if constexpr(legacy)return get<1>(view().payload_at(rank)+View::payload_bytes);
            else {
                constexpr auto tag=Plan.get(layout_solver::field_id::tag);
                const auto shift=(tag.offset_bits+rank*tag.stride_bits)%8;
                return (get<1,Direct>(view().template at<layout_solver::field_id::tag>(rank))>>shift)&mask(tag.logical_bits);
            }
        }
        template<unsigned Bytes> void patch(unsigned rank,unsigned offset,std::span<const byte,Bytes> bytes,
                                           std::optional<byte> next_tag)const noexcept {
            patch_bytes<Bytes>(view().payload_at(rank)+offset,bytes);
            if constexpr(Kind==container_kind::multimap)if(next_tag) {
                if constexpr(legacy)put<1>(view().payload_at(rank)+View::payload_bytes,*next_tag);
                else {
                    constexpr auto tag=Plan.get(layout_solver::field_id::tag);
                    auto* p=view().template at<layout_solver::field_id::tag>(rank);
                    if constexpr(tag.width_bits<8) {
                        const auto shift=(tag.offset_bits+rank*tag.stride_bits)%8;
                        constexpr auto bits=mask(tag.logical_bits);
                        put<1,Direct>(p,(get<1,Direct>(p)&~(bits<<shift))|(word(*next_tag&bits)<<shift));
                    }else put<1,Direct>(p,*next_tag&mask(tag.logical_bits));
                }
            }
        }
        void write(unsigned rank,const record& r)const {ikea::no_coverage e;view().write(rank,r,e);}
        void append(byte fp,word suffix,const record& r)const {
            auto v=view();const auto n=count();
            if constexpr(legacy){put_fingerprint(v.data,n,fp);put<View::suffix_bytes>(v.suffix_at(n),suffix);}
            else {ikea::no_coverage e;v.key(n,fp,suffix,e);}
            write(n,r);put<1>(base,n+1);
        }
        void erase(unsigned rank)const {ikea::no_coverage e;require(bool(view().erase(rank,count(),e)),"record bucket erase");put<1>(base,count()-1);}
    };
    auto p(word i)const {return bound_bucket<primary_view,primary>{reinterpret_cast<byte*>(p_.address)+i*PrimaryBytes};}
    auto q(word i)const {return bound_bucket<secondary_view,secondary>{reinterpret_cast<byte*>(q_.address)+i*SecondaryBytes};}
    static auto encoded(const record& r) {
        std::array<byte,primary_view::row_bytes> out{};
        std::copy(r.payload.begin(),r.payload.end(),out.begin());
        if constexpr(Kind==container_kind::multimap)out[primary_view::payload_bytes]=r.tag;
        return out;
    }
    static record decoded(std::span<const byte,primary_view::row_bytes> row) {
        record r;std::copy_n(row.data(),primary_view::payload_bytes,r.payload.data());
        if constexpr(Kind==container_kind::multimap)r.tag=row[primary_view::payload_bytes];return r;
    }
    template<unsigned Bytes,bool DirectStore=Direct> static void patch_bytes(byte* destination,std::span<const byte,Bytes> bytes)noexcept {
        lanes<Bytes/8>([&](auto i){word v;std::memcpy(&v,bytes.data()+8*i,8);put<8,DirectStore>(destination+8*i,v);});
        if constexpr(Bytes%8){word v=0;std::memcpy(&v,bytes.data()+Bytes/8*8,Bytes%8);put<Bytes%8,DirectStore>(destination+Bytes/8*8,v);}
    }
    struct location {unsigned tier=0,rank=0;word index=0;typename chains::position link{};};
    template<class Key> [[gnu::always_inline]] static word full_key(const Key& key) {
        if constexpr(std::is_invocable_v<Key>)return key();else return key;
    }
    template<bool Read,class Key,class SecondaryHash,class Consume> [[gnu::always_inline]] location find_hashed(
            const Key& key,typename primary_view::hash_type::parts h,SecondaryHash&& secondary_hash,Consume&& consume)const {
        auto b=p(h.bucket);const auto rank=b.template find<Read>(h.fingerprint,h.suffix,consume);
        if(rank>=0)return {1,unsigned(rank),h.bucket,{}};
        if(!b.descendants())return {};
        auto h2=secondary_hash();auto s=q(h2.bucket);const auto found=s.template find<Read>(h2.fingerprint,h2.suffix,consume);
        if(found>=0)return {2,unsigned(found),h2.bucket,{}};
        if(s.descendants())if(auto link=overflow_.find(s.head(),full_key(key))) {
            if constexpr(Read)consume(decoded(overflow_.payload(link)));
            return {3,0,h2.bucket,link};
        }
        return {};
    }
    [[gnu::always_inline]] location find(word key,typename primary_view::hash_type::parts h)const {
        return find_hashed<false>(key,h,[&]{return hq_.split(key);},[](const record&){});
    }
    record read(location at)const {
        if(at.tier==1)return p(at.index).read(at.rank);
        if(at.tier==2)return q(at.index).read(at.rank);
        return decoded(overflow_.payload(at.link));
    }
    template<class Effects> void before_p(word index,Effects& effects)const noexcept {
        effects.bucket(this,p_,index*PrimaryBytes,PrimaryBytes,false);
    }
    template<class Effects> void before_q(word index,Effects& effects)const noexcept {
        effects.bucket(this,q_,index*SecondaryBytes,SecondaryBytes,true);
    }
    template<class Effects> void write(location at,const record& value,Effects& effects) {
        if constexpr(primary_view::row_bytes!=0) {
            if(at.tier==1){before_p(at.index,effects);p(at.index).write(at.rank,value);}
            else if(at.tier==2){before_q(at.index,effects);q(at.index).write(at.rank,value);}
            else {overflow_.observe_payload(at.link,effects);const auto row=encoded(value);
                std::memcpy(overflow_.payload(at.link).data(),row.data(),row.size());}
        }
    }
    // Scoped, nonreentrant existing-row continuation. It must not escape or
    // mutate/relocate the index. A declined callback has made no writes, events
    // or owner changes. A handled refusal may retain independent bag capacity,
    // but cannot change reachable values, this row or caller cardinality. A
    // successful callback makes at most one patch and returns keys==0.
    // Patch bytes must fit the payload and remain stable for this
    // infallible commit; all validation precedes the first observer event.
    // Parent bucket/block damage is published before any packed field changes.
    template<class Effects> class record_edit {
        record_table& owner_;location at_;Effects& effects_;
      public:
        record_edit(record_table& owner,location at,Effects& effects):owner_(owner),at_(at),effects_(effects){}
        byte tag()const {
            if(at_.tier==1)return owner_.p(at_.index).tag(at_.rank);
            if(at_.tier==2)return owner_.q(at_.index).tag(at_.rank);
            return get<1>(owner_.overflow_.payload(at_.link).data()+primary_view::payload_bytes);
        }
        // Return owned bytes: no index or bag binding escapes the read.
        template<unsigned Bytes> std::array<byte,Bytes> prefix()const {
            static_assert(Bytes<=primary_view::payload_bytes);
            const byte* data=at_.tier==1?owner_.p(at_.index).view().payload_at(at_.rank):
                at_.tier==2?owner_.q(at_.index).view().payload_at(at_.rank):
                owner_.overflow_.payload(at_.link).data();
            std::array<byte,Bytes> bytes;
            lanes<Bytes/8>([&](auto i){auto v=get<8,Direct>(data+8*i);std::memcpy(bytes.data()+8*i,&v,8);});
            if constexpr(Bytes%8) {auto v=get<Bytes%8,Direct>(data+(Bytes/8)*8);
                std::memcpy(bytes.data()+(Bytes/8)*8,&v,Bytes%8);}
            return bytes;
        }
        template<unsigned Bytes> void patch(unsigned offset,std::span<const byte,Bytes> bytes,
                                           std::optional<byte> tag={})noexcept {
            static_assert(Bytes<=primary_view::payload_bytes);
            if constexpr(Bytes || Kind==container_kind::multimap) {
                if(at_.tier==1){owner_.before_p(at_.index,effects_);owner_.p(at_.index).template patch<Bytes>(at_.rank,offset,bytes,tag);}
                else if(at_.tier==2){owner_.before_q(at_.index,effects_);owner_.q(at_.index).template patch<Bytes>(at_.rank,offset,bytes,tag);}
                else {
                    owner_.overflow_.observe_payload(at_.link,effects_);
                    auto* p=owner_.overflow_.payload(at_.link).data();patch_bytes<Bytes,false>(p+offset,bytes);
                    if constexpr(Kind==container_kind::multimap)if(tag)put<1>(p+primary_view::payload_bytes,*tag);
                }
            }
        }
    };
  public:
    // Scoped immutable reader. The root, hash interpretation, overflow directory
    // and all allocations stay alive and unchanged until the entire cohort ends.
    // No per-hop Hold or ownership is implied by copying this binding.
    class reader {
        friend class record_table;
        const record_table* owner_;
        byte* pbase_;byte* qbase_;
        typename primary_view::hash_type hp_;
        typename secondary_view::hash_type hq_;
        const chains* overflow_;
        explicit reader(const record_table& t):owner_(&t),pbase_(reinterpret_cast<byte*>(t.p_.address)),
            qbase_(reinterpret_cast<byte*>(t.q_.address)),hp_(t.hp_),hq_(t.hq_),overflow_(&t.overflow_) {}
        template<class Bucket,class Consume> [[gnu::always_inline]] static bool consume_bucket(
                const Bucket& b,byte fp,word suffix,Consume&& consume) {
            const int rank=b.template find<bool(HT_RECORD_CONSUME)>(fp,suffix,consume);
            if(rank<0)return false;
            if constexpr(!HT_RECORD_CONSUME)consume(b.read(unsigned(rank)));
            return true;
        }
      public:
        enum class stage:byte {primary,secondary,chain_directory,chain_payload,primary_value,secondary_value};
        struct probe {
            // link also carries the located rank in a value stage. The entire
            // root/bucket interpretation remains immutable for this cohort.
            byte* row=nullptr;word suffix=0;std::uint32_t link=0;
            byte fingerprint=0;stage next=stage::primary;
        };
        static bool cold_path(const probe& p) {return p.next==stage::chain_directory || p.next==stage::chain_payload;}
        template<class Consume> [[gnu::always_inline]] bool get_record(word key,Consume&& consume)const {
            return owner_->get_record(key,std::forward<Consume>(consume));
        }
      private:
        template<class View,const ls& Plan,class Emit> [[gnu::always_inline]] static void hint_bucket(byte* row,Emit&& emit) {
            if constexpr(HT_RECORD_READ_WINDOWS)emit(row,View::lookup_bytes);
            else emit(row,Plan.shape.unit_bytes);
        }
        template<const ls& Plan,class Emit> [[gnu::always_inline]] static void hint_value(byte* row,unsigned rank,Emit&& emit) {
            using id=layout_solver::field_id;
            const auto value=*layout_solver::field_window(Plan,id::values,rank);
            if constexpr(Kind==container_kind::multimap) {
                const auto tag=*layout_solver::field_window(Plan,id::tag,rank);
                if(tag.offset<=value.offset+value.bytes && value.offset<=tag.offset+tag.bytes) {
                    const auto first=std::min(value.offset,tag.offset),last=std::max(value.offset+value.bytes,tag.offset+tag.bytes);
                    emit(row+first,last-first);
                }else {emit(row+value.offset,value.bytes);emit(row+tag.offset,tag.bytes);}
            }else emit(row+value.offset,value.bytes);
        }
        template<class View,const ls& Plan,class Consume,class Emit>
        [[gnu::always_inline]] static bool locate_bucket(probe& at,const bound_bucket<View,Plan>& b,
                stage value_stage,Consume&& consume,Emit&& emit) {
            if constexpr(HT_RECORD_READ_WINDOWS==2 && Kind!=container_kind::set) {
                const int rank=b.template find<false>(at.fingerprint,at.suffix,[](const record&){});
                if(rank<0)return false;
                at.link=unsigned(rank);at.next=value_stage;
                hint_value<Plan>(at.row,unsigned(rank),emit);return true;
            }else return consume_bucket(b,at.fingerprint,at.suffix,consume);
        }
      public:
        template<class Emit> [[gnu::always_inline]] void prepare_read(probe& at,word key,Emit&& emit)const {
            const auto h=hp_.split(key);at.row=pbase_+h.bucket*PrimaryBytes;
            at.suffix=h.suffix;at.fingerprint=h.fingerprint;at.next=stage::primary;
            hint_bucket<primary_view,primary>(at.row,emit);
        }
        template<bool Resolve=true,class Consume,class Emit> [[gnu::always_inline]] bool advance_read(
                probe& at,word key,bool& found,Consume&& consume,Emit&& emit)const {
            if constexpr(HT_RECORD_READ_WINDOWS==2 && Kind!=container_kind::set) {
                if(at.next==stage::primary_value) {
                    consume(bound_bucket<primary_view,primary>{at.row}.read(at.link));found=true;return true;
                }
                if(at.next==stage::secondary_value) {
                    consume(bound_bucket<secondary_view,secondary>{at.row}.read(at.link));found=true;return true;
                }
            }
            if(at.next==stage::primary) {
                bound_bucket<primary_view,primary> b{at.row};
                if(locate_bucket(at,b,stage::primary_value,consume,emit)) {
                    if constexpr(HT_RECORD_READ_WINDOWS==2 && Kind!=container_kind::set)return false;
                    found=true;return true;
                }
                if(!b.descendants()){found=false;return true;}
                const auto h=hq_.split(key);at.row=qbase_+h.bucket*SecondaryBytes;
                at.suffix=h.suffix;at.fingerprint=h.fingerprint;at.next=stage::secondary;
                hint_bucket<secondary_view,secondary>(at.row,emit);return false;
            }
            if(at.next==stage::secondary) {
                bound_bucket<secondary_view,secondary> b{at.row};
                if(locate_bucket(at,b,stage::secondary_value,consume,emit)) {
                    if constexpr(HT_RECORD_READ_WINDOWS==2 && Kind!=container_kind::set)return false;
                    found=true;return true;
                }
                if(!b.descendants()){found=false;return true;}
                at.link=b.head();at.next=stage::chain_directory;
                const auto w=overflow_->directory_window(at.link);emit(w.data(),w.size());return false;
            }
            if(at.next==stage::chain_directory) {
                if constexpr(Resolve) {
                    const auto w=overflow_->payload_window(at.link);at.row=const_cast<byte*>(w.data());
                    at.next=stage::chain_payload;emit(w.data(),w.size());return false;
                } else __builtin_unreachable();
            }
            // Key-overflow decoding can materialize a wide record. Keep that
            // rare path out of a ready primary/secondary Ring callback's frame.
            if constexpr(!Resolve)__builtin_unreachable();
            constexpr unsigned stride=K/8+primary_view::row_bytes;
            const unsigned count=ht::get<2>(at.row+4);
            for(unsigned i=0;i<count;++i) {
                const auto* row=at.row+6+std::size_t(i)*stride;
                if(ht::get<K/8>(row)==key) {
                    consume(decoded(std::span<const byte,primary_view::row_bytes>(row+K/8,primary_view::row_bytes)));
                    found=true;return true;
                }
            }
            at.link=ht::get<4>(at.row);if(!at.link){found=false;return true;}
            at.next=stage::chain_directory;const auto w=overflow_->directory_window(at.link);
            emit(w.data(),w.size());return false;
        }
    };
    [[gnu::always_inline]] reader bind_read()const {return reader(*this);}
    explicit record_table(oa::Pool& pool,word primary_seed,word secondary_seed):hp_(primary_seed),hq_(secondary_seed),overflow_(pool) {
        admit_identity<1>();admit_identity<4>(); // Header proofs do not depend on payload/suffix widths.
        primary_view::prepare(primary.shape.slots);secondary_view::prepare(secondary.shape.slots);
        // The bound list body must match every solver field, not just capacity.
        auto proof=[]<class View>(const ls& plan) {
            using f=layout_solver::field_id;
            require(plan.get(f::fingerprints).offset_bits==plan.header_bytes*8 &&
                plan.get(f::suffixes).offset_bits==(plan.header_bytes+plan.shape.slots)*8 &&
                plan.get(f::values).offset_bits==(plan.header_bytes+plan.shape.slots*(1+View::suffix_bytes))*8 &&
                plan.get(f::values).stride_bits==View::row_bytes*8,"record bucket field binding");
        };
        if constexpr(legacy){proof.template operator()<primary_view>(primary);proof.template operator()<secondary_view>(secondary);}
        auto p=oa::allocate(pool,{.bytes=(word(1)<<A)*PrimaryBytes,.alignment=64});if(!p)throw std::bad_alloc{};
        auto q=oa::allocate(pool,{.bytes=(word(1)<<B)*SecondaryBytes,.alignment=64});if(!q)throw std::bad_alloc{};
        p_=std::move(*p);q_=std::move(*q);std::memset(p_.address,0,p_.size);std::memset(q_.address,0,q_.size);
        if constexpr(sentinel) {
            for(word i=0;i<(word(1)<<A);++i)this->p(i).view().initialize_empty();
            for(word i=0;i<(word(1)<<B);++i)this->q(i).view().initialize_empty();
        }
    }
    record_table(record_table&&)noexcept=default;
    record_table& operator=(record_table&&)noexcept=default;
  private:
    template<class SecondaryHash,class Consume> [[gnu::always_inline]] bool get_hashed(
            word key,typename primary_view::hash_type::parts h,SecondaryHash&& secondary_hash,Consume&& consume)const {
        if constexpr(HT_RECORD_CONSUME)return find_hashed<true>(key,h,secondary_hash,consume).tier!=0;
        else {auto at=find_hashed<false>(key,h,secondary_hash,[](const record&){});
            if(!at.tier)return false;consume(read(at));return true;}
    }
    // Only immutable hash facts survive between lanes. Counts, locations, bags
    // and allocation bindings are resolved afresh for every operation. The
    // Controls compare SIMD in both tiers, SIMD primary with scalar secondary,
    // and per-lane scalar hashes. This changes preparation, not stored format.
    template<unsigned N,bool Mutable> class packet_query {
        using owner_type=std::conditional_t<Mutable,record_table,const record_table>;
        owner_type& owner_;std::span<const word,N> keys_;
        static constexpr bool compact=HT_HIERARCHY_PACKET_COMPACT;
        static constexpr unsigned hash_mode=HT_HIERARCHY_PACKET_HASH;
        struct empty_state {};
        using primary_packet=std::conditional_t<compact,compact_hash_packet<N,K,A>,hash_packet<N>>;
        using secondary_packet=std::conditional_t<compact,compact_hash_packet<N,K,B>,hash_packet<N>>;
        [[no_unique_address]] std::conditional_t<hash_mode==2,empty_state,primary_packet> primary_;
        [[no_unique_address]] std::conditional_t<hash_mode==0,std::optional<secondary_packet>,empty_state> secondary_;
        auto primary_hash(unsigned lane)const {
            if constexpr(hash_mode==2)return owner_.hp_.split(keys_[lane]);
            else return packet_parts<K,A>(primary_,lane);
        }
        auto secondary_hash(unsigned lane) {
            if constexpr(hash_mode==0) {
                if(!secondary_)secondary_=split_packet<N,compact>(owner_.hq_,keys_);
                return packet_parts<K,B>(*secondary_,lane);
            }else return owner_.hq_.split(keys_[lane]);
        }
      public:
        packet_query(owner_type& owner,std::span<const word,N> keys):owner_(owner),keys_(keys),primary_([&]{
            if constexpr(hash_mode==2)return empty_state{};
            else return split_packet<N,compact>(owner.hp_,keys);
        }()){}
        template<class Consume> [[gnu::always_inline]] bool get(unsigned lane,Consume&& consume) {
            return owner_.get_hashed(keys_[lane],primary_hash(lane),[&]{return secondary_hash(lane);},consume);
        }
        template<class Prepare,class Effects,class Existing>
        [[gnu::always_inline]] auto mutate(unsigned lane,Prepare&& prepare,Effects& effects,Existing&& existing) requires(Mutable) {
            // A miss followed by insertion can need the secondary hash twice.
            // Match the scalar entry point's cache without retaining any lookup
            // result or mutable bucket state between lanes.
            std::optional<typename secondary_view::hash_type::parts> secondary;
            return owner_.mutate_hashed(keys_[lane],primary_hash(lane),[&]{
                if constexpr(hash_mode==0)return secondary_hash(lane);
                else {if(!secondary)secondary=secondary_hash(lane);return *secondary;}
            },std::forward<Prepare>(prepare),effects,std::forward<Existing>(existing));
        }
    };
  public:
    template<class Consume> [[gnu::always_inline]] bool get_record(word key,Consume&& consume)const {
        return get_hashed(key,hp_.split(key),[&]{return hq_.split(key);},consume);
    }
    /// Scoped admission for N initialized keys, including inactive lanes. The
    /// callback must not escape this packet, relocate/reseed the index, suspend,
    /// or change its key input. Ordinary lane mutations preserve hash geometry.
    template<unsigned N,class Consume> [[gnu::always_inline]] auto with_packet(std::span<const word,N> keys,Consume&& consume) {
        packet_query<N,true> packet(*this,keys);return consume(packet);
    }
    template<unsigned N,class Consume> [[gnu::always_inline]] auto with_packet(std::span<const word,N> keys,Consume&& consume)const {
        packet_query<N,false> packet(*this,keys);return consume(packet);
    }
    // One bucket publication replaces the individual packing writes. Chain
    // observers retain owner/block identity and structural outcomes. Scratch
    // callers erase the observer completely; it is not an undo mechanism.
    static constexpr unsigned mutation_event_bound=chains::reservation_event_bound+2;
    static constexpr unsigned erase_event_bound=chains::erase_event_bound+2;
  private:
    template<class Key,class SecondaryHash,class Prepare,class Effects,class Existing>
    [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    mutate_hashed(const Key& key,typename primary_view::hash_type::parts h,SecondaryHash&& secondary_hash,
                  Prepare&& prepare,Effects& effects,Existing&& existing) {
        if(!effects_room(effects,mutation_event_bound))return std::unexpected(mutation_error::effects);
        auto at=find_hashed<false>(key,h,secondary_hash,[](const record&){});std::optional<record> old;
        if(at.tier) {
            record_edit<Effects> edit(*this,at,effects);
            if(auto done=existing(edit))return *done;
            old=read(at);
        }
        auto b=p(h.bucket);typename secondary_view::hash_type::parts h2{};
        std::optional<typename chains::template reservation<Effects>> reserved;
        std::uint32_t next_head=0;
        if(!at.tier && b.count()==primary.shape.slots) {
            h2=secondary_hash();auto s=q(h2.bucket);
            if(s.count()==secondary.shape.slots) {
                reserved=overflow_.reserve_record(s.head(),effects);
                if(!reserved)return std::unexpected(mutation_error::capacity);
                next_head=reserved->head_after();
            }
        }
        auto next=prepare(old);if(!next)return std::unexpected(mutation_error::preparation);
        if constexpr(requires {next->commit();}) {static_assert(noexcept(next->commit()));next->commit();}
        if(at.tier)write(at,*next,effects);
        else if(b.count()<primary.shape.slots){before_p(h.bucket,effects);b.append(h.fingerprint,h.suffix,*next);}
        else {
            auto s=q(h2.bucket);
            before_q(h2.bucket,effects);before_p(h.bucket,effects);
            if(reserved) {const auto row=encoded(*next);reserved->commit_record(full_key(key),row);s.head(next_head);s.descendants(s.descendants()+1);}
            else s.append(h2.fingerprint,h2.suffix,*next);
            b.descendants(b.descendants()+1);
        }
        return mutation_delta{!at.tier,next->value_delta};
    }
  public:
    template<class Prepare,class Effects,class Existing=skip_existing_record_edit>
    [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    mutate(word key,Prepare&& prepare,Effects& effects,Existing existing={}) {
        std::optional<typename secondary_view::hash_type::parts> secondary;
        return mutate_hashed(key,hp_.split(key),[&]{
            if(!secondary)secondary=hq_.split(key);return *secondary;
        },std::forward<Prepare>(prepare),effects,existing);
    }
    template<class Prepare> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    mutate(word key,Prepare&& prepare) {no_record_effects effects;return mutate(key,std::forward<Prepare>(prepare),effects);}
    bool insert_record(word key,const record& record) {
        auto r=mutate(key,[&](const auto& old)->std::optional<typename record_table::record>{
            require(!old,"unique transition source");return record;
        });return bool(r);
    }
    /// Private construction from a proven unique stream. The caller owns the
    /// absence proof; this skips an otherwise redundant full duplicate search.
    /// No observer is needed before this private index becomes discoverable.
    bool insert_unique_record(word key,const record& value) {
        const auto h=hp_.split(key);auto b=p(h.bucket);
        if(b.count()<primary.shape.slots){b.append(h.fingerprint,h.suffix,value);return true;}
        const auto h2=hq_.split(key);auto s=q(h2.bucket);
        if(s.count()<secondary.shape.slots)s.append(h2.fingerprint,h2.suffix,value);
        else {
            no_link_observer observer;auto reserved=overflow_.reserve_record(s.head(),observer);
            if(!reserved)return false;
            const auto row=encoded(value);reserved->commit_record(key,row);
            s.head(reserved->head_after());s.descendants(s.descendants()+1);
        }
        b.descendants(b.descendants()+1);return true;
    }
    template<class Effects,class Prepare> std::expected<std::optional<record>,mutation_error>
    erase(word key,Effects& effects,Prepare&& prepare) {
        auto h=hp_.split(key);auto at=find(key,h);if(!at.tier)return std::optional<record>{};
        if(!effects_room(effects,erase_event_bound))return std::unexpected(mutation_error::effects);
        auto old=read(at);
        if(!prepare(old))return std::unexpected(mutation_error::effects);
        if(at.tier==1){before_p(at.index,effects);p(at.index).erase(at.rank);}
        else {
            auto b=p(h.bucket);auto s=q(at.index);
            before_p(h.bucket,effects);before_q(at.index,effects);
            if(at.tier==2)s.erase(at.rank);
            else {auto head=s.head();overflow_.erase(head,at.link,effects);s.head(head);s.descendants(s.descendants()-1);}
            b.descendants(b.descendants()-1);
        }
        return old;
    }
    template<class Effects> auto erase(word key,Effects& effects) {return erase(key,effects,[](const record&){return true;});}
    std::optional<record> erase(word key) {no_record_effects e;auto r=erase(key,e);require(bool(r),"record erase");return *r;}
    template<class Visit> void each_record(Visit&& visit)const {
        for(word i=0;i<(word(1)<<A);++i) {auto b=p(i);auto v=b.view();for(unsigned rank=0;rank<b.count();++rank)
            visit(hp_.invert({i,get<primary_view::suffix_bytes>(v.suffix_at(rank)),v.fingerprint(rank)}),v.read(rank));}
        for(word i=0;i<(word(1)<<B);++i) {
            auto b=q(i);auto v=b.view();for(unsigned rank=0;rank<b.count();++rank)
                visit(hq_.invert({i,get<secondary_view::suffix_bytes>(v.suffix_at(rank)),v.fingerprint(rank)}),v.read(rank));
            overflow_.each_record(b.head(),[&](word key,auto row){visit(key,decoded(row));});
        }
    }
    struct scan_cursor {
        word bucket=0;
        unsigned rank=0,tier=0;
        std::uint32_t link=0;
        bool entered=false;
    };
    /// The owner and its interpretation stay immutable through the entire scan.
    /// A cursor retains no storage. Each bucket/link entry and each attempted row
    /// costs one work unit, including empty buckets. Refusal leaves that row
    /// unconsumed; a callback may only return false before committing its row.
    template<class Visit> record_scan_result scan(scan_cursor& at,unsigned budget,Visit&& visit)const {
        record_scan_result result;
        [[clang::always_inline]]result=scan_keys(at,budget,[&](auto h){return hp_.invert(h);},
            [&](auto h){return hq_.invert(h);},[](word key){return key;},visit);
        return result;
    }
  private:
    // One traversal, with key interpretation supplied by the composition.
    // Matching-layout consolidation keeps primary facts encoded until needed.
    template<class PrimaryKey,class SecondaryKey,class RawKey,class Visit>
    record_scan_result scan_keys(scan_cursor& at,unsigned budget,PrimaryKey&& primary_key,
            SecondaryKey&& secondary_key,RawKey&& raw_key,Visit&& visit)const {
        record_scan_result result{record_scan_state::yielded};
        for(;;) {
            if(at.tier==0 && at.bucket==(word(1)<<A)) {at.tier=1;at.bucket=0;}
            if(at.tier==1 && at.bucket==(word(1)<<B)) {result.state=record_scan_state::done;return result;}
            if(at.tier==2 && !at.link) {at.tier=1;++at.bucket;continue;}
            if(result.work==budget)return result;
            auto rows=[&](unsigned count,auto&& read) {
                if(!at.entered){++result.work;at.entered=true;}
                while(at.rank<count && result.work<budget) {
                    ++result.work;
                    if(!read(at.rank)){result.state=record_scan_state::refused;return false;}
                    ++at.rank;++result.records;
                }
                return true;
            };
            if(at.tier==0) {
                auto b=p(at.bucket);auto v=b.view();const auto count=b.count();
                if(!rows(count,[&](unsigned rank) {
                    return visit(primary_key(typename primary_view::hash_type::parts{
                        at.bucket,get<primary_view::suffix_bytes>(v.suffix_at(rank)),v.fingerprint(rank)}),v.read(rank));
                }))return result;
                if(at.rank==count){++at.bucket;at.rank=0;at.entered=false;}
            }else if(at.tier==1) {
                auto b=q(at.bucket);auto v=b.view();const auto count=b.count();
                if(!rows(count,[&](unsigned rank) {
                    return visit(secondary_key(typename secondary_view::hash_type::parts{
                        at.bucket,get<secondary_view::suffix_bytes>(v.suffix_at(rank)),v.fingerprint(rank)}),v.read(rank));
                }))return result;
                if(at.rank==count){at.tier=2;at.link=b.head();at.rank=0;at.entered=false;}
            }else {
                // Bind once per contiguous link visit, never once per row.
                const auto* data=overflow_.data(at.link);const auto count=get<2>(data+4);
                if(!rows(count,[&](unsigned rank) {
                    const auto* row=data+6+rank*(K/8+primary_view::row_bytes);
                    return visit(raw_key(get<K/8>(row)),decoded(std::span<const byte,primary_view::row_bytes>(row+K/8,primary_view::row_bytes)));
                }))return result;
                if(at.rank==count){at.link=get<4>(data);at.rank=0;at.entered=false;}
            }
        }
    }
  public:
    template<class Visit> void read_storage(Visit&& visit)const {
        visit(std::span<const byte>(reinterpret_cast<const byte*>(this),sizeof(*this)));
        visit(std::span<const byte>(reinterpret_cast<const byte*>(p_.address),p_.size));
        visit(std::span<const byte>(reinterpret_cast<const byte*>(q_.address),q_.size));
        visit(overflow_.directory_storage());
        overflow_.allocations([&](const oa::Allocation& a){visit(std::span<const byte>(reinterpret_cast<const byte*>(a.address),a.size));});
    }
    memory footprint()const {
        auto m=overflow_.footprint();m.requested+=p_.size+q_.size;m.backing+=oa::capacity(p_)+oa::capacity(q_);
        m.metadata+=sizeof(*this)-sizeof(overflow_);return m;
    }
    std::array<word,3> population()const {
        std::array<word,3> out{};
        for(word i=0;i<(word(1)<<A);++i)out[0]+=p(i).count();
        for(word i=0;i<(word(1)<<B);++i){auto s=q(i);out[1]+=s.count();out[2]+=s.descendants();}
        return out;
    }
    void validate()const {
        overflow_.validate();word downstream=0;
        auto inactive=[](auto bucket,unsigned capacity) {
            if constexpr(sentinel)for(unsigned rank=bucket.count();rank<capacity;++rank)
                require(bucket.view().fingerprint(rank)==255,"inactive fingerprint invariant");
        };
        for(word i=0;i<(word(1)<<A);++i){auto b=p(i);require(b.count()<=primary.shape.slots,"primary count");downstream+=b.descendants();inactive(b,primary.shape.slots);}
        for(word i=0;i<(word(1)<<B);++i){auto b=q(i);require(b.count()<=secondary.shape.slots,"secondary count");inactive(b,secondary.shape.slots);}
        const auto counts=population();require(downstream==counts[1]+counts[2],"downstream accounting");
    }
};
} // namespace ht
