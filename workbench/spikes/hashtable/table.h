#pragma once
#include "hash.h"
#include "layout.h"
#include "links.h"
namespace ht {
template<recipe P,bool Direct> struct bucket {
    static_assert(P.slots>0 && P.slots<=32);
    byte* data;
    unsigned count()const {
        auto n=unsigned(get<1,Direct>(data));
        __builtin_assume(n<=P.slots); // owner-established occupied-prefix invariant
        return n;
    }
    word descendants()const {return get<4,Direct>(data+1);}
    void descendants(word n) {put<4,Direct>(data+1,n);}
    std::uint32_t head()const {static_assert(P.header==9);return get<4,Direct>(data+5);}
    void head(std::uint32_t n) {static_assert(P.header==9);put<4,Direct>(data+5,n);}
    byte fingerprint(unsigned slot)const {return data[P.fp+slot];}
    word suffix(unsigned slot)const {return get<P.suffix_bytes,Direct>(data+P.suffix+slot*P.suffix_bytes);}
    word value(unsigned slot)const {return get<P.value_bytes,Direct>(data+P.values+slot*P.value_bytes);}
    void value(unsigned slot,word v) {put<P.value_bytes,Direct>(data+P.values+slot*P.value_bytes,v);}
    [[gnu::always_inline]] int find(byte fp,word suffix)const {
        unsigned n=count();
        for(unsigned group=0;group<P.slots;group+=16) {
            constexpr unsigned first_bytes=P.slots<=8?8:16;
            word bits;
            if constexpr(P.slots<=16) bits=fingerprint_matches<first_bytes,Direct>(data+P.fp,fp,n);
            else if(group==0) bits=fingerprint_matches<16,Direct>(data+P.fp,fp,std::min(n,16u));
            else bits=fingerprint_matches<(P.slots<=24?8:16),Direct>(data+P.fp+16,fp,n>16?n-16:0);
            while(bits) {
                auto slot=group+take_match(bits);
                if(this->suffix(slot)==suffix) return int(slot);
            }
        }
        return -1;
    }
    void append(byte fp,word suffix,word value) {
        auto n=count(); put_fingerprint<Direct>(data+P.fp,n,fp);
        put<P.suffix_bytes,Direct>(data+P.suffix+n*P.suffix_bytes,suffix);
        this->value(n,value); put<1,Direct>(data,n+1);
    }
    void erase(unsigned slot) {
        auto last=count()-1;
        if(slot!=last) {
            put_fingerprint<Direct>(data+P.fp,slot,fingerprint(last));
            put<P.suffix_bytes,Direct>(data+P.suffix+slot*P.suffix_bytes,suffix(last));
            value(slot,value(last));
        }
        put<1,Direct>(data,last);
    }
};
// Fixed geometry is the first executable comparison, not the eventual owner API.
// Constructor establishes scratch mutation authority; no per-operation refcount.
// Total distinct occupancy is intentionally absent: return deltas to the caller.
template<unsigned K,unsigned A,unsigned B,unsigned PrimaryUnit,unsigned SecondaryUnit,bool Direct=false,
         bool Filter=false,unsigned ValueBytes=8>
class table {
    template<unsigned,unsigned,unsigned,unsigned,unsigned,bool,bool,unsigned> friend class table;
    static_assert(ValueBytes<=8);
  public:
    using primary_hash=permutation<K,A>; using secondary_hash=permutation<K,B>;
    static constexpr auto primary=solve(primary_hash::C,ValueBytes,PrimaryUnit,false);
    static constexpr auto secondary=solve(secondary_hash::C,ValueBytes,SecondaryUnit,true);
    static constexpr unsigned key_bits=K, primary_bits=A, secondary_bits=B, value_bytes=ValueBytes;
    static constexpr word primary_buckets=word(1)<<A, secondary_buckets=word(1)<<B;
    static_assert(!Filter || primary.values+primary.slots*primary.value_bytes<=primary.unit-4,
                  "this filter recipe requires four owned padding bytes; never change slot capacity silently");
  private:
    oa::Allocation p_,q_;
    primary_hash hp_; secondary_hash hq_;
    links<K,Direct,ValueBytes> overflow_;
    bucket<primary,Direct> p(word i)const {return {reinterpret_cast<byte*>(p_.address)+i*primary.unit};}
    bucket<secondary,Direct> q(word i)const {return {reinterpret_cast<byte*>(q_.address)+i*secondary.unit};}
    [[gnu::always_inline]] static bool may_descend(bucket<primary,Direct> b,byte fp) {
        if(!b.descendants()) return false;
        if constexpr(Filter) return get<4,Direct>(b.data+primary.unit-4)&(word(1)<<(fp&31));
        else return true;
    }
    struct location {unsigned tier=0,slot=0;word index=0; typename links<K,Direct,ValueBytes>::position link{};};
    [[gnu::always_inline]] location downstream(word key)const {
        auto h=hq_.split(key); auto b=q(h.bucket); auto s=b.find(h.fingerprint,h.suffix);
        if(s>=0) return {2,unsigned(s),h.bucket,{}};
        if(b.descendants()) {
            auto at=overflow_.find(b.head(),key); if(at) return {3,0,h.bucket,at};
        }
        return {};
    }
    template<class Transform> [[gnu::always_inline]] int upsert_miss(
            typename primary_hash::parts h,word key,word value,Transform&& combined) {
        auto b=p(h.bucket);
        if(may_descend(b,h.fingerprint)) {
            auto at=downstream(key);
            if(at.tier==2) {auto c=q(at.index);c.value(at.slot,combined(c.value(at.slot)));return 0;}
            if(at.tier==3) {overflow_.update(at.link,combined);return 0;}
        }
        return insert_new(h,key,value);
    }
    // Global absence is established by lookup, or by copying unique entries
    // from an immutable source into an initially empty replacement.
    [[gnu::always_inline]] int insert_new(typename primary_hash::parts h,word key,word value) {
        auto b=p(h.bucket);
        if(b.count()<primary.slots) {b.append(h.fingerprint,h.suffix,value);return 1;}
        auto h2=hq_.split(key); auto c=q(h2.bucket);
        if(c.count()<secondary.slots) c.append(h2.fingerprint,h2.suffix,value);
        else {
            auto head=c.head(); if(!overflow_.append(head,key,value)) return -1;
            c.head(head); c.descendants(c.descendants()+1);
        }
        b.descendants(b.descendants()+1);
        if constexpr(Filter) {
            auto* hint=b.data+primary.unit-4;
            put<4,Direct>(hint,get<4,Direct>(hint)|(word(1)<<(h.fingerprint&31)));
        }
        return 1;
    }
    // The bound consumer materializes continuation state only on a primary
    // miss. Keep this experimental boundary distinct from the ordinary body.
    template<class Combine> [[gnu::noinline]] int continue_upsert(
            typename primary_hash::parts h,word key,word value,Combine&& combine) {
        auto combined=[&](word old) __attribute__((always_inline)) {
            word result;[[clang::always_inline]] result=combine(old,value);return result;
        };
        return upsert_miss(h,key,value,combined);
    }
  public:
    // Exclusive scratch cohort over stable primary/secondary owners and seeds.
    // Reallocation/reseeding ends the cohort; no borrowed slot escapes an update.
    // Overflow growth uses the owning table and rebinds its StreamPack accesses.
    // The immediate combiner is infallible and cannot reenter/mutate this owner.
    class mutator {
        friend class table;
        primary_hash hash_;
        byte* base_;
        table* owner_;
        explicit mutator(table& t):hash_(t.hp_),base_(reinterpret_cast<byte*>(t.p_.address)),owner_(&t) {}
      public:
        template<class Combine> [[gnu::always_inline]] int upsert(word key,word value,Combine&& combine)const {
            static_assert(noexcept(combine(word{},word{})));
            auto h=hash_.split(key);bucket<primary,Direct> b{base_+h.bucket*primary.unit};
            auto slot=b.find(h.fingerprint,h.suffix);
            if(slot>=0) {
                word updated;[[clang::always_inline]] updated=combine(b.value(slot),value);
                b.value(slot,updated);return 0;
            }
            return owner_->continue_upsert(h,key,value,std::forward<Combine>(combine));
        }
    };
    [[gnu::always_inline]] mutator bind_write() {return mutator(*this);}
    // Borrowed immutable cohort. The owner must remain stable: mutation,
    // relocation and publication end this borrow. No ownership or counters are
    // duplicated; copying immutable hash/base facts keeps them outside a batch.
    class reader {
        friend class table;
        byte* pbase_; byte* qbase_;
        primary_hash hp_; secondary_hash hq_;
        const links<K,Direct,ValueBytes>* overflow_;
        reader(const table& t):pbase_(reinterpret_cast<byte*>(t.p_.address)),
            qbase_(reinterpret_cast<byte*>(t.q_.address)),hp_(t.hp_),hq_(t.hq_),overflow_(&t.overflow_) {}
        bucket<primary,Direct> p(word i)const {return {pbase_+i*primary.unit};}
        bucket<secondary,Direct> q(word i)const {return {qbase_+i*secondary.unit};}
      public:
        enum class stage:byte {primary,secondary,chain_directory,chain_payload};
        struct probe {
            byte* row=nullptr;
            word suffix=0;
            std::uint32_t link=0;
            byte fingerprint=0;
            stage next=stage::primary;
        };
        template<class Emit> [[gnu::always_inline]] void prepare_read(probe& at,word key,Emit&& emit)const {
            auto h=hp_.split(key);
            at.row=p(h.bucket).data;at.suffix=h.suffix;at.fingerprint=h.fingerprint;at.next=stage::primary;
            emit(at.row,primary.unit);
        }
        // All pointees stay immutable and qualified through the read cohort.
        // The key stays with the caller, avoiding a second copy in staged state.
        // Return true on completion; a miss leaves value unchanged.
        template<bool Resolve=true,class Emit>
        [[gnu::always_inline]] bool advance_read(probe& at,word key,word& value,bool& found,Emit&& emit)const {
            if(at.next==stage::primary) {
                bucket<primary,Direct> b{at.row};auto s=b.find(at.fingerprint,at.suffix);
                if(s>=0) {value=b.value(s);found=true;return true;}
                if(!may_descend(b,at.fingerprint)) {found=false;return true;}
                auto h=hq_.split(key);
                at.row=q(h.bucket).data;at.suffix=h.suffix;at.fingerprint=h.fingerprint;at.next=stage::secondary;
                emit(at.row,secondary.unit);return false;
            }
            if(at.next==stage::secondary) {
                bucket<secondary,Direct> b{at.row};auto s=b.find(at.fingerprint,at.suffix);
                if(s>=0) {value=b.value(s);found=true;return true;}
                if(!b.descendants()) {found=false;return true;}
                at.link=b.head();at.next=stage::chain_directory;
                auto w=overflow_->directory_window(at.link);emit(w.data(),w.size());return false;
            }
            if(at.next==stage::chain_directory) {
                if constexpr(Resolve) {
                    auto w=overflow_->payload_window(at.link);
                    at.row=const_cast<byte*>(w.data());at.next=stage::chain_payload;
                    emit(w.data(),w.size());return false;
                } else __builtin_unreachable(); // caller tail-transfers this stage to its cold resolver
            }
            if(links<K,Direct,ValueBytes>::read_payload(at.row,key,value)) {found=true;return true;}
            at.link=get<4,Direct>(at.row);
            if(!at.link) {found=false;return true;}
            at.next=stage::chain_directory;
            auto w=overflow_->directory_window(at.link);emit(w.data(),w.size());return false;
        }
        [[gnu::always_inline]] bool get_value(word key,word& value)const {
            auto h=hp_.split(key);auto b=p(h.bucket);auto s=b.find(h.fingerprint,h.suffix);
            if(s>=0) {value=b.value(s);return true;}
            if(!may_descend(b,h.fingerprint)) return false;
            auto h2=hq_.split(key);auto c=q(h2.bucket);s=c.find(h2.fingerprint,h2.suffix);
            if(s>=0) {value=c.value(s);return true;}
            if(c.descendants()) {
                auto at=overflow_->find(c.head(),key);
                if(at) {value=overflow_->value(at);return true;}
            }
            return false;
        }
        // Stage independent requests through the same stored hierarchy. Hashes
        // are scalar here; fingerprint comparisons use the explicit ISA body.
        // This is a packet-scheduling control, not a SIMD-hashing result.
        template<unsigned N> [[gnu::always_inline]] unsigned getN(const word* keys,word* values)const {
            static_assert(N==4 || N==8);
            std::array<typename primary_hash::parts,N> first;
            std::array<typename secondary_hash::parts,N> second;
            unsigned found=0,secondary_pending=0,chain_pending=0;
            lanes<N>([&](auto i) {first[i]=hp_.split(keys[i]);});
            lanes<N>([&](auto i) {
                auto h=first[i];auto b=p(h.bucket);auto s=b.find(h.fingerprint,h.suffix);
                if(s>=0) {values[i]=b.value(s);found|=1u<<i;}
                else if(may_descend(b,h.fingerprint)) secondary_pending|=1u<<i;
            });
            if(!secondary_pending) return found;
            lanes<N>([&](auto i) {if(secondary_pending&(1u<<i)) second[i]=hq_.split(keys[i]);});
            lanes<N>([&](auto i) {
                if(secondary_pending&(1u<<i)) {
                    auto h=second[i];auto c=q(h.bucket);auto s=c.find(h.fingerprint,h.suffix);
                    if(s>=0) {values[i]=c.value(s);found|=1u<<i;}
                    else if(c.descendants()) chain_pending|=1u<<i;
                }
            });
            lanes<N>([&](auto i) {
                if(chain_pending&(1u<<i)) {
                    auto at=overflow_->find(q(second[i].bucket).head(),keys[i]);
                    if(at) {values[i]=overflow_->value(at);found|=1u<<i;}
                }
            });
            return found;
        }
    };
    [[gnu::always_inline]] reader bind_read()const {return reader(*this);}
    template<class Visit> void allocations(Visit&& visit)const {
        visit(p_);visit(q_);overflow_.allocations(visit);
    }
    static void prepare() {
        links<K,Direct,ValueBytes>::prepare();
        if constexpr(!Direct) {
            admit_identity<1>();admit_identity<4>();admit_identity<ValueBytes>();
            admit_identity<primary.suffix_bytes>();admit_identity<secondary.suffix_bytes>();
        }
    }
    static word preparation_bytes() {
        std::array<bool,9> used{};
        for(auto b:{1u,2u,4u,ValueBytes,K/8,primary.suffix_bytes,secondary.suffix_bytes}) used[b]=true;
        auto fields=Direct?0:std::count(used.begin()+1,used.end(),true)*sizeof(field<1>);
        return fields+links<K,Direct,ValueBytes>::preparation_bytes();
    }
    table(oa::Pool& pool,word seed1,word seed2):hp_(seed1),hq_(seed2),overflow_(pool) {
        prepare();
        auto a=oa::allocate(pool,{.bytes=primary_buckets*primary.unit,.alignment=64,.initialization=oa::Initialization::zero});
        if(!a) throw std::bad_alloc(); p_=std::move(*a);
        auto b=oa::allocate(pool,{.bytes=secondary_buckets*secondary.unit,.alignment=64,.initialization=oa::Initialization::zero});
        if(!b) throw std::bad_alloc(); q_=std::move(*b);
    }
    table(const table&)=delete;
    [[gnu::always_inline]] bool get_value(word key,word& value)const {
        return bind_read().get_value(key,value);
    }
    // One search. Combine existing values infallibly; on absence insert incoming
    // without calling the combiner. -1 refuses this key, 0 updates, +1 inserts.
    // Already-completed operations remain committed if a later operation refuses.
    template<class Combine> [[gnu::always_inline]] int upsert(word key,word value,Combine&& combine) {
        static_assert(noexcept(combine(word{},word{})));
        auto combined=[&](word old) __attribute__((always_inline)) {
            word result;[[clang::always_inline]] result=combine(old,value);return result;
        };
        auto h=hp_.split(key); auto b=p(h.bucket); auto s=b.find(h.fingerprint,h.suffix);
        if(s>=0) {b.value(s,combined(b.value(s)));return 0;}
        return upsert_miss(h,key,value,combined);
    }
    // Source and destination have identical geometry/seeds but distinct owners.
    // The source stays immutable through completion (including retries). Cursor
    // names the next unconsumed entry; a refused insertion never advances it.
    // It belongs to this owner pair and combiner, which cannot reenter/mutate
    // either owner. Returned added/processed counts are per-call deltas.
    // Secondary/link entries initially reconstruct keys. References never move.
    struct merge_cursor {word index=0;unsigned slot=0,phase=0;std::uint32_t link=0;};
    struct merge_result {word added=0,processed=0;bool complete=false;};
    template<class Combine> [[gnu::noinline]] merge_result merge_from(
            const table& source,merge_cursor& at,Combine&& combine) {
        static_assert(noexcept(combine(word{},word{})));
        require(this!=&source && hp_==source.hp_ && hq_==source.hq_,"merge identity");
        merge_result result;
        while(at.phase<3) {
            int delta;
            if(at.phase==0) {
                if(at.index==primary_buckets) {at={.phase=1};continue;}
                auto src=source.p(at.index);
                if(at.slot==src.count()) {++at.index;at.slot=0;continue;}
                auto fp=src.fingerprint(at.slot);auto suffix=src.suffix(at.slot),value=src.value(at.slot);
                auto dst=p(at.index);auto slot=dst.find(fp,suffix);
                auto combined=[&](word old) __attribute__((always_inline)) {
                    word v;[[clang::always_inline]] v=combine(old,value);return v;
                };
                if(slot>=0) {dst.value(slot,combined(dst.value(slot)));delta=0;}
                else if(!may_descend(dst,fp) && dst.count()<primary.slots) {
                    dst.append(fp,suffix,value);delta=1;
                } else {
                    typename primary_hash::parts h{at.index,suffix,fp};
                    delta=upsert_miss(h,hp_.invert(h),value,combined);
                }
            } else if(at.phase==1) {
                if(at.index==secondary_buckets) {at.phase=3;break;}
                auto src=source.q(at.index);
                if(at.slot==src.count()) {at.phase=2;at.link=src.head();at.slot=0;continue;}
                auto key=hq_.invert({at.index,src.suffix(at.slot),src.fingerprint(at.slot)});
                delta=upsert(key,src.value(at.slot),combine);
            } else {
                if(!at.link) {++at.index;at.slot=0;at.phase=1;continue;}
                if(at.slot==source.overflow_.count(at.link)) {
                    at.link=source.overflow_.next(at.link);at.slot=0;continue;
                }
                typename links<K,Direct,ValueBytes>::position pos{at.link,at.slot};
                delta=upsert(source.overflow_.key(pos),source.overflow_.value(pos),combine);
            }
            if(delta<0) return result;
            ++at.slot;++result.processed;result.added+=delta;
        }
        result.complete=true;return result;
    }
    // Geometry/entropy may differ. Reconstruct through the source permutation
    // and insert through this owner's permutation; no encoded ref transfers.
    // Like matching-layout merge, refusal leaves a committed prefix and the
    // cursor on its next unconsumed entry. Source remains immutable on retry.
    template<class Source,class Combine> [[gnu::noinline]] merge_result merge_reencoded(
            const Source& source,merge_cursor& at,Combine&& combine) {
        static_assert(Source::key_bits==K && Source::value_bytes==ValueBytes && noexcept(combine(word{},word{})));
        require(static_cast<const void*>(this)!=static_cast<const void*>(&source),"distinct merge owners");
        merge_result result;
        while(at.phase<3) {
            word key,value;
            if(at.phase==0) {
                if(at.index==Source::primary_buckets) {at={.phase=1};continue;}
                auto src=source.p(at.index);
                if(at.slot==src.count()) {++at.index;at.slot=0;continue;}
                key=source.hp_.invert({at.index,src.suffix(at.slot),src.fingerprint(at.slot)});
                value=src.value(at.slot);
            } else if(at.phase==1) {
                if(at.index==Source::secondary_buckets) {at.phase=3;break;}
                auto src=source.q(at.index);
                if(at.slot==src.count()) {at.phase=2;at.link=src.head();at.slot=0;continue;}
                key=source.hq_.invert({at.index,src.suffix(at.slot),src.fingerprint(at.slot)});
                value=src.value(at.slot);
            } else {
                if(!at.link) {++at.index;at.slot=0;at.phase=1;continue;}
                if(at.slot==source.overflow_.count(at.link)) {
                    at.link=source.overflow_.next(at.link);at.slot=0;continue;
                }
                typename decltype(source.overflow_)::position pos{at.link,at.slot};
                key=source.overflow_.key(pos);value=source.overflow_.value(pos);
            }
            auto delta=upsert(key,value,combine);
            if(delta<0) return result;
            ++at.slot;++result.processed;result.added+=delta;
        }
        result.complete=true;return result;
    }
    [[gnu::always_inline]] int set(word key,word value) {
        return upsert(key,value,[](word,word incoming)noexcept {return incoming;});
    }
    // Scratch construction capability: caller proves global absence. This
    // skips duplicate search, not hashing, storage admission or reachability
    // accounting. On refusal no key is published; existing entries remain.
    [[gnu::always_inline]] bool insert_absent(word key,word value) {
        return insert_new(hp_.split(key),key,value)>0;
    }
    [[gnu::always_inline]] bool erase(word key) {
        auto h=hp_.split(key); auto b=p(h.bucket); auto s=b.find(h.fingerprint,h.suffix);
        if(s>=0) {b.erase(s);return true;}
        if(!may_descend(b,h.fingerprint)) return false;
        auto at=downstream(key); if(!at.tier) return false;
        auto c=q(at.index);
        if(at.tier==2) c.erase(at.slot);
        else {auto head=c.head();overflow_.erase(head,at.link);c.head(head);c.descendants(c.descendants()-1);}
        auto remaining=b.descendants()-1;b.descendants(remaining);
        // Monotone summary between empty-downstream epochs: stale bits may
        // cause extra work, never a false negative. Churn must charge saturation.
        if constexpr(Filter) if(!remaining) put<4,Direct>(b.data+primary.unit-4,0);
        return true;
    }
    template<class Visit> void each(Visit&& visit)const {
        for(word i=0;i<primary_buckets;++i) {
            auto b=p(i); for(unsigned j=0,n=b.count();j<n;++j)
                visit(hp_.invert({i,b.suffix(j),b.fingerprint(j)}),b.value(j));
        }
        for(word i=0;i<secondary_buckets;++i) {
            auto b=q(i); for(unsigned j=0,n=b.count();j<n;++j)
                visit(hq_.invert({i,b.suffix(j),b.fingerprint(j)}),b.value(j));
            overflow_.each(b.head(),visit);
        }
    }
    struct routing {word primary=0,secondary=0,chain=0;};
    routing distribution()const {
        routing r;
        for(word i=0;i<primary_buckets;++i) r.primary+=p(i).count();
        for(word i=0;i<secondary_buckets;++i) {r.secondary+=q(i).count();r.chain+=q(i).descendants();}
        return r;
    }
    std::span<const byte> directory_storage()const {return overflow_.directory_storage();}
    std::array<word,3> filter_stats(std::span<const word> absent)const requires Filter {
        std::vector<word> needed(primary_buckets);
        each([&](word key,word) {
            auto h=hp_.split(key);auto b=p(h.bucket);
            if(b.find(h.fingerprint,h.suffix)<0) needed[h.bucket]|=word(1)<<(h.fingerprint&31);
        });
        std::array<word,3> result{}; // set bits, stale bits, avoided downstream visits
        for(word i=0;i<primary_buckets;++i) {
            auto bits=get<4,Direct>(p(i).data+primary.unit-4);
            result[0]+=std::popcount(bits);result[1]+=std::popcount(bits&~needed[i]);
        }
        for(auto key:absent) {
            auto h=hp_.split(key);auto b=p(h.bucket);
            result[2]+=b.descendants()&&!may_descend(b,h.fingerprint);
        }
        return result;
    }
    memory footprint()const {
        auto m=overflow_.footprint();
        m.requested+=p_.size+q_.size; m.backing+=oa::capacity(p_)+oa::capacity(q_);
        m.metadata+=sizeof(*this)-sizeof(overflow_); return m;
    }
    void validate()const {
        std::vector<word> pc(primary_buckets),qc(secondary_buckets),filters(primary_buckets);
        each([&](word key,word) {
            auto a=hp_.split(key);auto b=p(a.bucket);
            if(b.find(a.fingerprint,a.suffix)>=0) return;
            ++pc[a.bucket];filters[a.bucket]|=word(1)<<(a.fingerprint&31);
            auto c=hq_.split(key);auto d=q(c.bucket);
            if(d.find(c.fingerprint,c.suffix)<0) ++qc[c.bucket];
        });
        for(word i=0;i<primary_buckets;++i) {
            require(pc[i]==p(i).descendants(),"primary descendants");
            if constexpr(Filter) {
                auto bits=get<4,Direct>(p(i).data+primary.unit-4);
                require((bits&filters[i])==filters[i],"filter has no false negative");
                if(!pc[i]) require(bits==0,"empty descendant summary");
            }
        }
        for(word i=0;i<secondary_buckets;++i) require(qc[i]==q(i).descendants(),"secondary descendants");
        overflow_.validate();
    }
};
} // namespace ht
