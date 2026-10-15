#pragma once
#include "table_storage.h"
#include "table_hash.h"
#include "table_chains.h"

namespace hashtable_spike::tiered {
enum class outcome {inserted,replaced,erased,missing,grow_required};
struct mutation {outcome result;int distinct_delta;};

// Complete single-writer consumer. Captured copies share immutable storage;
// mutation admits all writable buckets/links before the first logical store.
// Seed changes and geometry changes build a separate table and publish it only
// after complete reinsertion. Borrowed addresses cannot cross any mutation.
template<bucket_plan P,bucket_plan Q,sharing Storage,bool Quotient,bool Direct=false,bool Promote=false>
class table {
    static_assert(P.key_bits==Q.key_bits&&P.value_bits==Q.value_bits);
    static_assert(P.reference_bits==0&&Q.reference_bits==32);
    static constexpr unsigned K=P.key_bits,V=P.value_bits;
    using Hash1=std::conditional_t<Quotient,quotient_hash<K,P.address_bits,P.search!=key_search::exact_raw>,permutation<K,false>>;
    using Hash2=std::conditional_t<Quotient,quotient_hash<K,Q.address_bits,Q.search!=key_search::exact_raw>,permutation<K,false>>;
    static_assert(Quotient||(P.search!=key_search::exact_raw&&Q.search!=key_search::exact_raw));
    using Primary=bucket_storage<P,Storage>;
    using Secondary=bucket_storage<Q,Storage>;
    inline static const bucket_binding<P> primary_binding{};
    inline static const bucket_binding<Q> secondary_binding{};
    Primary primary_;
    Secondary secondary_;
    chain_store<K,V,Direct> chains_;
    Hash1 hash1_;
    Hash2 hash2_;
    std::size_t size_{};
    table(oa::Pool& pool,Primary p,Secondary q,word first_seed,word second_seed)
        :primary_(std::move(p)),secondary_(std::move(q)),chains_(pool),hash1_(first_seed),hash2_(second_seed){}
    static unsigned pi(word h){if constexpr(P.address_bits==0)return 0;else return unsigned(h>>P.remainder_bits());}
    static unsigned qi(word h){if constexpr(Q.address_bits==0)return 0;else return unsigned(h>>Q.remainder_bits());}
    bucket<P,Direct> primary(unsigned i)const{return {primary_.at(i),primary_binding};}
    bucket<Q,Direct> secondary(unsigned i)const{return {secondary_.at(i),secondary_binding};}
    struct located {
        unsigned tier{},bucket{},rank{};
        std::uint32_t link=no_link,previous=no_link;
    };
    located downstream(word key,word h2)const {
        const auto index=qi(h2);const auto q=secondary(index);
        const auto found=q.find(h2);if(found>=0)return {1,index,unsigned(found)};
        if(!q.descendants())return {3};
        auto previous=no_link;
        for(auto ref=std::uint32_t(q.reference());ref!=no_link;) {
            const auto link=chains_.read(ref);
            for(unsigned i=0;i<link.size();++i)if(link.key(i)==key)return {2,index,i,ref,previous};
            previous=ref;ref=link.next();
        }
        return {3};
    }
    orbital::Result<void> prepare_primary_secondary(unsigned p,unsigned q) {
        auto first=primary_.prepare(p);if(!first)return first;
        return secondary_.prepare(q);
    }
    orbital::Result<void> prepare_removal(const located& location) {
        if(location.tier==1)return {};
        auto ready=chains_.prepare(location.link);if(!ready)return ready;
        if(chains_.read(location.link).size()==1&&location.previous!=no_link)
            return chains_.prepare(location.previous);
        return {};
    }
    void remove_downstream(const located& location) {
        if(location.tier==1){secondary(location.bucket).erase(location.rank);return;}
        chains_.erase(location.link,location.rank);
        auto q=secondary(location.bucket);q.descendants(q.descendants()-1);
        if(chains_.read(location.link).size()==0) {
            const auto next=chains_.read(location.link).next();
            if(location.previous==no_link)q.reference(next);
            else chains_.next(location.previous,next);
            chains_.retire(location.link);
        }
    }
    template<class Recipe>orbital::Result<mutation> promote(unsigned pindex,word hash,Recipe&& recipe,const located& location) {
        auto ready=prepare_primary_secondary(pindex,location.bucket);if(!ready)return std::unexpected(ready.error());
        ready=prepare_removal(location);if(!ready)return std::unexpected(ready.error());
        // All allocation/ownership admission precedes this move. The existing
        // downstream lookup supplies the source: no reverse index or search of
        // unrelated secondary buckets is needed after a primary deletion.
        auto p=primary(pindex);p.append_with(hash,[&](auto destination) {
            if(location.tier==1)destination.assign(secondary(location.bucket).read_value(location.rank));
            else destination.assign(chains_.read(location.link).read_value(location.rank));
            recipe(destination,false);
        });p.descendants(p.descendants()-1);
        remove_downstream(location);return mutation{outcome::replaced,0};
    }
  public:
    static constexpr auto primary_plan=P,secondary_plan=Q;
    table(const table&)=default;
    table& operator=(const table&)=default;
    table(table&&)noexcept=default;
    table& operator=(table&&)noexcept=default;
    static orbital::Result<table> make(oa::Pool& pool,word primary_seed,word secondary_seed) {
        auto p=Primary::make(pool,primary_binding);if(!p)return std::unexpected(p.error());
        auto q=Secondary::make(pool,secondary_binding);if(!q)return std::unexpected(q.error());
        return table(pool,std::move(*p),std::move(*q),primary_seed,secondary_seed);
    }
    std::size_t size()const{return size_;}
    template<class Consume>[[gnu::always_inline]] bool visit(word key,Consume&& consume)const {
        const auto h=hash1_(key);const auto p=primary(pi(h));const auto found=p.find(h);
        if(found>=0){consume(p.read_value(unsigned(found)));return true;}
        if(!p.descendants())return false;
        const auto h2=hash2_(key);const auto location=downstream(key,h2);
        if(location.tier==1){consume(secondary(location.bucket).read_value(location.rank));return true;}
        if(location.tier==2){consume(chains_.read(location.link).read_value(location.rank));return true;}
        return false;
    }
    [[gnu::always_inline]] bool get(word key,word& out)const requires(V<=64) {
        return visit(key,[&](auto value){out=value.read();});
    }
    enum class read_stage {primary,secondary,chain_directory,chain_body,chain_payload};
    struct read_probe {
        word key{},hash{};
        const byte* data{};
        typename chain_store<K,V,Direct>::read_probe chain{};
        read_stage stage=read_stage::primary;
    };
    // This first scheduling comparison deliberately fixes flat ownership. A
    // chunk directory needs its own stages; at() must not hide those loads here.
    template<class Emit>[[gnu::always_inline]] void prepare_read(read_probe& probe,word key,Emit&& emit)const {
        static_assert(Storage==sharing::flat);
        const auto h=hash1_(key);const auto* p=primary_.at(pi(h));emit(p,P.bytes);
        // Only fields consumed by the new phase need writing. Clearing cold
        // chain state here added stores to every primary hit in the first build.
        probe.key=key;probe.hash=h;probe.data=p;probe.stage=read_stage::primary;
    }
    template<bool IncludeBody=true,class Consume,class Emit>[[gnu::always_inline]] bool advance_visit(read_probe& p,Consume&& consume,bool& found,Emit&& emit)const {
        switch(p.stage) {
        case read_stage::primary: {
            const bucket<P,Direct> b{const_cast<byte*>(p.data),primary_binding};
            const auto at=b.find(p.hash);
            if(at>=0){consume(b.read_value(unsigned(at)));found=true;return true;}
            if(!b.descendants()){found=false;return true;}
            p.hash=hash2_(p.key);p.data=secondary_.at(qi(p.hash));p.stage=read_stage::secondary;
            emit(p.data,Q.bytes);return false;
        }
        case read_stage::secondary: {
            const bucket<Q,Direct> b{const_cast<byte*>(p.data),secondary_binding};
            const auto at=b.find(p.hash);
            if(at>=0){consume(b.read_value(unsigned(at)));found=true;return true;}
            if(!b.descendants()){found=false;return true;}
            p.chain=chains_.prepare_read(std::uint32_t(b.reference()),emit);
            p.stage=read_stage::chain_directory;return false;
        }
        case read_stage::chain_directory:
            chains_.read_directory(p.chain,emit);p.stage=read_stage::chain_body;return false;
        case read_stage::chain_body:
            if constexpr(IncludeBody){chains_.read_body(p.chain,emit);p.stage=read_stage::chain_payload;return false;}
            else __builtin_unreachable(); // Caller tail-transferred this stage.
        case read_stage::chain_payload: {
            const auto link=p.chain.payload;
            for(unsigned i=0;i<link.size();++i)if(link.key(i)==p.key){consume(link.read_value(i));found=true;return true;}
            const auto ref=link.next();if(ref==no_link){found=false;return true;}
            p.chain=chains_.prepare_read(ref,emit);p.stage=read_stage::chain_directory;return false;
        }
        }
        __builtin_unreachable();
    }
    template<bool IncludeBody=true,class Emit>[[gnu::always_inline]] bool advance_read(read_probe& p,word& out,bool& found,Emit&& emit)const requires(V<=64) {
        return advance_visit<IncludeBody>(p,[&](auto value){out=value.read();},found,std::forward<Emit>(emit));
    }
  private:
    template<bool KnownAbsent,class Recipe>[[gnu::always_inline]] orbital::Result<mutation> set_impl(word key,Recipe&& recipe) {
        require((key&~mask(K))==0);
        const auto h=hash1_(key);const auto pindex=pi(h);auto p=primary(pindex);
        const auto found=KnownAbsent?-1:p.find(h);
        if(found>=0) {
            auto ready=primary_.prepare(pindex);if(!ready)return std::unexpected(ready.error());
            recipe(primary(pindex).write_value(unsigned(found)),false);return mutation{outcome::replaced,0};
        }
        return set_after_primary<KnownAbsent>(key,h,pindex,std::forward<Recipe>(recipe));
    }
    // Keep the primary-hit recipe fusible without dragging every allocation and
    // overflow path into its caller. This continuation already knows the primary
    // miss; it must not repeat hashing/search merely to reach the shared tail.
    template<bool KnownAbsent,class Recipe>[[gnu::noinline]] orbital::Result<mutation> set_after_primary(word key,word h,unsigned pindex,Recipe&& recipe) {
        auto p=primary(pindex);
        word h2{};
        if(!KnownAbsent&&p.descendants()) {
            h2=hash2_(key);const auto location=downstream(key,h2);
            if constexpr(Promote)if(location.tier<3&&p.size()<P.slots)return promote(pindex,h,recipe,location);
            if(location.tier==1) {
                auto ready=secondary_.prepare(location.bucket);if(!ready)return std::unexpected(ready.error());
                recipe(secondary(location.bucket).write_value(location.rank),false);return mutation{outcome::replaced,0};
            }
            if(location.tier==2) {
                auto ready=chains_.prepare(location.link);if(!ready)return std::unexpected(ready.error());
                chains_.value_with(location.link,location.rank,[&](auto value){recipe(value,false);});return mutation{outcome::replaced,0};
            }
        }
        if(p.size()<P.slots) {
            auto ready=primary_.prepare(pindex);if(!ready)return std::unexpected(ready.error());
            primary(pindex).append_with(h,[&](auto value){recipe(value,true);});++size_;return mutation{outcome::inserted,1};
        }
        if(p.descendants()==mask(P.descendants_bits))return mutation{outcome::grow_required,0};
        if(KnownAbsent||!p.descendants())h2=hash2_(key);
        const auto qindex=qi(h2);auto q=secondary(qindex);
        if(q.size()<Q.slots) {
            auto ready=prepare_primary_secondary(pindex,qindex);if(!ready)return std::unexpected(ready.error());
            p.data=primary(pindex).data;q.data=secondary(qindex).data;
            q.append_with(h2,[&](auto value){recipe(value,true);});p.descendants(p.descendants()+1);++size_;
            return mutation{outcome::inserted,1};
        }
        if(q.descendants()==mask(Q.descendants_bits))return mutation{outcome::grow_required,0};
        auto ready=prepare_primary_secondary(pindex,qindex);if(!ready)return std::unexpected(ready.error());
        q.data=secondary(qindex).data;
        auto tail=no_link;auto available=no_link;
        if(q.descendants())for(auto ref=std::uint32_t(q.reference());ref!=no_link;) {
            const auto link=chains_.read(ref);
            if(link.size()<link.capacity){available=ref;break;}
            tail=ref;ref=link.next();
        }
        if(available!=no_link) {
            ready=chains_.prepare(available);if(!ready)return std::unexpected(ready.error());
        } else {
            if(tail!=no_link){ready=chains_.prepare(tail);if(!ready)return std::unexpected(ready.error());}
            const auto family=tail==no_link?0:chains_.next_family(chains_.family(tail));
            auto allocated=chains_.allocate(family);if(!allocated)return std::unexpected(allocated.error());
            available=*allocated;
            // No fallible work remains. Rebind after any StreamPack relocation.
            if(tail!=no_link)chains_.next(tail,available);
            else secondary(qindex).reference(available);
        }
        chains_.append_with(available,key,[&](auto value){recipe(value,true);});
        p.data=primary(pindex).data;q.data=secondary(qindex).data;
        p.descendants(p.descendants()+1);q.descendants(q.descendants()+1);++size_;
        return mutation{outcome::inserted,1};
    }
  public:
    orbital::Result<mutation> set(word key,word value) requires(V<=64) {
        require((value&~mask(V))==0);
        return set_impl<false>(key,[&](auto destination,bool){destination.assign(value);});
    }
    // One actual record-update recipe: infallible, non-reentrant, and initializing
    // every value field on insertion. Ownership preparation precedes invocation;
    // it must not retain the accessor, allocate, throw, or mutate another entry.
    template<class Recipe>[[gnu::always_inline]] orbital::Result<mutation> update_or_insert(word key,Recipe&& recipe) {
        return set_impl<false>(key,std::forward<Recipe>(recipe));
    }
    // Rebuilding an actual table supplies a known-distinct range. This is the
    // scalar construction control; it removes duplicate discovery, not hashing,
    // capacity checks, allocation, counters or primitive stores. It is not yet
    // a bucket-partitioned/SIMD bulk builder.
    template<class Range>static orbital::Result<table> build_distinct(oa::Pool& pool,word seed,word other_seed,const Range& source) {
        auto result=make(pool,seed,other_seed);if(!result)return result;
        orbital::Result<void> status;
        auto insert=[&](word key,auto value) {
            if(!status)return;
            auto r=result->template set_impl<true>(key,[&](auto destination,bool){destination.assign(value);});
            if(!r)status=std::unexpected(r.error());
            else if(r->result!=outcome::inserted)status=std::unexpected(orbital::Error::capacity);
        };
        if constexpr(requires{source.visit_entries(insert);})source.visit_entries(insert);
        else source.each(insert);
        if(!status)return std::unexpected(status.error());return result;
    }
    orbital::Result<mutation> erase(word key) {
        const auto h=hash1_(key);const auto pindex=pi(h);auto p=primary(pindex);
        const auto found=p.find(h);
        if(found>=0) {
            auto ready=primary_.prepare(pindex);if(!ready)return std::unexpected(ready.error());
            primary(pindex).erase(unsigned(found));--size_;return mutation{outcome::erased,-1};
        }
        if(!p.descendants())return mutation{outcome::missing,0};
        const auto h2=hash2_(key);const auto location=downstream(key,h2);
        if(location.tier==3)return mutation{outcome::missing,0};
        auto ready=prepare_primary_secondary(pindex,location.bucket);if(!ready)return std::unexpected(ready.error());
        ready=prepare_removal(location);if(!ready)return std::unexpected(ready.error());
        remove_downstream(location);
        p.data=primary(pindex).data;p.descendants(p.descendants()-1);--size_;
        return mutation{outcome::erased,-1};
    }
    orbital::Result<std::size_t> clear() {
        if(!size_)return std::size_t{0};
        // All fallible ownership preparation precedes the first logical store.
        // A failed preparation may leave equal private copies, never a prefix
        // of cleared buckets. The owner publishes the returned removed count.
        for(unsigned i=0;i<(1u<<P.address_bits);++i) {
            auto ready=primary_.prepare(i);if(!ready)return std::unexpected(ready.error());
        }
        for(unsigned i=0;i<(1u<<Q.address_bits);++i) {
            auto ready=secondary_.prepare(i);if(!ready)return std::unexpected(ready.error());
        }
        for(unsigned i=0;i<(1u<<P.address_bits);++i)primary(i).clear();
        for(unsigned i=0;i<(1u<<Q.address_bits);++i)secondary(i).clear();
        chains_.clear();return std::exchange(size_,0);
    }
    template<class F>void visit_entries(F&& f)const {
        for(unsigned i=0;i<(1u<<P.address_bits);++i) {
            const auto b=primary(i);for(unsigned j=0;j<b.size();++j)f(hash1_.invert(b.hash(i,j)),b.read_value(j));
        }
        for(unsigned i=0;i<(1u<<Q.address_bits);++i) {
            const auto b=secondary(i);for(unsigned j=0;j<b.size();++j)f(hash2_.invert(b.hash(i,j)),b.read_value(j));
            if(b.descendants())for(auto ref=std::uint32_t(b.reference());ref!=no_link;) {
                const auto link=chains_.read(ref);for(unsigned j=0;j<link.size();++j)f(link.key(j),link.read_value(j));
                ref=link.next();
            }
        }
    }
    template<class F>void each(F&& f)const requires(V<=64) {
        visit_entries([&](word key,auto value){f(key,value.read());});
    }
    struct population {std::size_t primary{},secondary{},chain{};};
    population counts()const {
        population out;
        for(unsigned i=0;i<(1u<<P.address_bits);++i)out.primary+=primary(i).size();
        for(unsigned i=0;i<(1u<<Q.address_bits);++i){out.secondary+=secondary(i).size();out.chain+=secondary(i).descendants();}
        return out;
    }
    template<class F>void allocations(F&& f)const {primary_.allocations(f);secondary_.allocations(f);chains_.allocations(f);}
    auto chain_memory()const{return chains_.memory();}
    template<class F>void metadata(F&& f)const {primary_.metadata(f);secondary_.metadata(f);chains_.metadata(f);}
    template<class F>static void preparation(F&& f) {
        f(&primary_binding,sizeof(primary_binding));f(&secondary_binding,sizeof(secondary_binding));chain_store<K,V,Direct>::preparation(f);
    }
    static constexpr std::size_t shared_controls=sizeof(primary_binding)+sizeof(secondary_binding);
    static constexpr std::size_t static_zero_image_bytes=chain_store<K,V,Direct>::static_zero_image_bytes;
    void audit()const {
        std::vector<std::size_t> primary_desc(std::size_t{1}<<P.address_bits);
        for(unsigned i=0;i<(1u<<Q.address_bits);++i) {
            const auto b=secondary(i);
            for(unsigned j=0;j<b.size();++j)++primary_desc[pi(hash1_(hash2_.invert(b.hash(i,j))))];
            std::size_t downstream_count=0;
            if(b.descendants())for(auto ref=std::uint32_t(b.reference());ref!=no_link;) {
                const auto link=chains_.read(ref);require(link.size()>0&&link.size()<=link.capacity);
                for(unsigned j=0;j<link.size();++j) {
                    const auto key=link.key(j);require(qi(hash2_(key))==i);++primary_desc[pi(hash1_(key))];++downstream_count;
                }
                ref=link.next();
            }
            require(downstream_count==b.descendants());
        }
        for(unsigned i=0;i<(1u<<P.address_bits);++i)require(primary(i).descendants()==primary_desc[i]);
        const auto c=counts();require(c.primary+c.secondary+c.chain==size_);chains_.audit();
    }
};
} // namespace hashtable_spike::tiered
