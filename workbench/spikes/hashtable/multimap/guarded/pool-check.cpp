#include "pool.h"
#include <iostream>
#include <map>
#include <functional>

namespace failure_probe {thread_local int after=-1;thread_local bool hit=false;}
void* operator new(std::size_t n){
    if(failure_probe::after==0){failure_probe::after=-1;failure_probe::hit=true;throw std::bad_alloc();}
    if(failure_probe::after>0)--failure_probe::after;
    if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();
}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t)noexcept{::operator delete(p);}

namespace write_arch {
struct guarded_pool_test_access {
    template<class Pool> static const void* directory(const Pool& pool){return pool.blocks_.data();}
    template<class Pool> static const byte* directory_slot(const Pool& pool,word ref){
        return reinterpret_cast<const byte*>(&pool.blocks_[ref>>8].payload);
    }
    // The physical source may include unassigned storage and object padding.
    // Observe defined fields semantically and only initialized payload bytes.
    template<unsigned V,buffer_representation R,bool C,class Field,class Bytes>
    static void inspect(const capacity_buffers<V,R,C>& pool,Field&& field,Bytes&& bytes){
        using Pool=capacity_buffers<V,R,C>;
        auto scalar=[&](const auto& value){field(&value,sizeof(value),[p=&value]{return std::vector<word>{word(*p)};});};
        field(&pool.blocks_,sizeof(pool.blocks_),[&pool]{return std::vector<word>{word(pool.blocks_.data()),pool.blocks_.size(),pool.blocks_.capacity()};});
        scalar(pool.first_free_);scalar(pool.assigned_bytes_);scalar(pool.live_buffers_);
        for(const auto& f:pool.families_){scalar(f.first);scalar(f.capacity);scalar(f.ranks);}
        for(const auto& slot:pool.blocks_){
            field(&slot.owner,sizeof(slot.owner),[&slot]{return std::vector<word>{word(slot.owner.get())};});
            field(&slot.payload,sizeof(slot.payload),[&slot]{return std::vector<word>{word(slot.payload)};});scalar(slot.next_free);
            if(!slot.owner)continue;
            const auto& b=*slot.owner;
            field(&b.storage,sizeof(b.storage),[&b]{return std::vector<word>{word(b.storage.data),b.storage.size,b.storage.capacity()};});
            bytes(b.returned.data(),b.returned.size());scalar(b.returned_count);scalar(b.live);scalar(b.which);
            scalar(b.previous);scalar(b.next);scalar(b.listed);
            bytes(b.storage.data,2);
            const auto initialized=std::size_t(Pool::assigned(b))*pool.families_[b.which].capacity*V;
            bytes(b.storage.data+64,initialized);
        }
    }
};
}

namespace {
namespace ht=ikea::hashtable;
using write_arch::guarded_pool_access;
using ht::byte;using ht::word;
struct observation {
    struct region {ht::mutation_source source;std::vector<byte> old,covered,known;bool retired=false;};
    struct field {std::size_t region,offset,size;std::function<std::vector<word>()> get;std::vector<word> old;};
    std::vector<region> regions;
    std::vector<field> fields;
    std::pair<std::size_t,std::size_t> locate(const void* ptr,std::size_t size){
        const auto address=reinterpret_cast<std::uintptr_t>(ptr);
        for(std::size_t i=0;i<regions.size();++i){const auto& s=regions[i].source;const auto base=reinterpret_cast<std::uintptr_t>(s.base);
            if(address>=base&&address-base<=s.extent&&size<=s.extent-(address-base))return {i,address-base};}
        throw std::runtime_error("initialized field outside physical sources");
    }
    template<class Pool> explicit observation(const Pool& pool,const void* owner=nullptr,const byte* base=nullptr,std::size_t extent=0){
        if(!owner){owner=&pool;base=reinterpret_cast<const byte*>(&pool);extent=sizeof(pool);}
        auto add=[&](ht::mutation_source s){regions.push_back({s,std::vector<byte>(s.extent),std::vector<byte>(s.extent),std::vector<byte>(s.extent)});};
        add({owner,ht::owner_part::header,base,extent});
        guarded_pool_access::sources(pool,owner,add);
        write_arch::guarded_pool_test_access::inspect(pool,
            [&](const void* p,std::size_t size,auto get){auto [r,offset]=locate(p,size);auto old=get();fields.push_back({r,offset,size,std::move(get),std::move(old)});},
            [&](const byte* p,std::size_t size){if(!size)return;auto [r,offset]=locate(p,size);auto& target=regions[r];
                std::copy_n(p,size,target.old.begin()+offset);std::fill_n(target.known.begin()+offset,size,1);});
    }
    void before(const ht::mutation_damage& d){
        auto it=std::find_if(regions.begin(),regions.end(),[&](const auto& r){return r.source.owner==d.source.owner&&r.source.part==d.source.part&&r.source.base==d.source.base&&r.source.extent==d.source.extent;});
        ht::require(it!=regions.end()&&!d.span.plane&&d.span.size&&d.span.offset<=it->old.size()&&d.span.size<=it->old.size()-d.span.offset,"prepared pool source bounds");
        verify(true);
        std::fill_n(it->covered.begin()+d.span.offset,d.span.size,1);
    }
    void verify(bool unchanged=false)const{
        for(const auto& r:regions)for(std::size_t i=0;i<r.old.size();++i)
            if(r.known[i])ht::require((!r.retired&&r.old[i]==r.source.base[i])||(!unchanged&&r.covered[i]),"uncovered prepared pool store");
        for(const auto& f:fields)if(regions[f.region].retired||f.old!=f.get()){
            ht::require(!unchanged,"pool preparation changed metadata");
            const auto& coverage=regions[f.region].covered;
            ht::require(std::all_of(coverage.begin()+f.offset,coverage.begin()+f.offset+f.size,[](byte b){return b!=0;}),"uncovered pool metadata field");
        }
    }
    void retire(ht::mutation_source source){
        auto it=std::find_if(regions.begin(),regions.end(),[&](const auto& r){return r.source.owner==source.owner&&r.source.part==source.part&&r.source.base==source.base&&r.source.extent==source.extent;});
        ht::require(it!=regions.end(),"retirement source identity");it->retired=true;
    }
    template<class Pool> void verify_retirements(const Pool& pool,const void* owner=nullptr)const {
        if(!owner)owner=&pool;
        guarded_pool_access::sources(pool,owner,[&](auto source){for(const auto& r:regions)if(r.retired)
            ht::require(r.source.base!=source.base||r.source.extent!=source.extent,"declared retirement still belongs to live pool");});
    }
};
template<unsigned V> ht::bytes<V> value(unsigned index){
    ht::bytes<V> v;for(unsigned i=0;i<V;++i)v[i]=byte(index*131u+i*47u+(index>>(i%7)));return v;
}
template<unsigned V> void check(){
    using Pool=write_arch::capacity_buffers<V>;
    Pool pool;std::map<word,std::pair<unsigned,std::vector<byte>>> expected;
    {
        observation unchanged(pool);bool refused=false;
        // First new allocates the block object; aligned payload allocation is
        // separate. Fail the following descriptor-directory allocation.
        failure_probe::after=1;failure_probe::hit=false;
        try{auto failed=guarded_pool_access::prepare(pool,0,{});(void)failed;}catch(const std::bad_alloc&){refused=true;}
        failure_probe::after=-1;
        ht::require(refused&&failure_probe::hit,"detached preparation allocation failure");unchanged.verify(true);
    }
    auto verify=[&]{
        std::vector<typename Pool::reference_witness> witnesses;
        for(const auto& [ref,entry]:expected){
            witnesses.push_back({ref,entry.first,unsigned(entry.second.size()/V)});
            ht::require(std::equal(entry.second.begin(),entry.second.end(),pool.data(ref,entry.first)),"prepared pool complete payload");
        }
        pool.validate(witnesses);
    };
    auto release=[&](word ref){
        observation before(pool);std::vector<ht::mutation_damage> writes;
        std::vector<ht::mutation_source> retired;
        writes.push_back({{&pool,ht::owner_part::header,reinterpret_cast<const byte*>(&pool),sizeof(pool)},{0,0,sizeof(pool)}});
        guarded_pool_access::release_damage(pool,ref,&pool,[&](auto d){writes.push_back(d);},[&](auto s){retired.push_back(s);});
        before.verify(true);for(auto d:writes)before.before(d);for(auto s:retired)before.retire(s);
        pool.release(ref);before.verify();before.verify_retirements(pool);expected.erase(ref);verify();
    };
    auto append=[&](unsigned n,unsigned salt){
        const auto which=Pool::class_for(n);std::vector<byte> prefix((n-1)*V);
        for(unsigned i=0;i+1<n;++i){auto v=value<V>(i+salt);std::copy(v.begin(),v.end(),prefix.begin()+i*V);}
        auto incoming=value<V>(n-1+salt);auto full=prefix;full.insert(full.end(),incoming.begin(),incoming.end());
        observation before(pool);
        {auto canceled=guarded_pool_access::prepare(pool,which,prefix);before.verify(true);}
        auto commit=[&](auto& plan){
        before.verify(true);std::vector<ht::mutation_damage> writes;
        writes.push_back({{&pool,ht::owner_part::header,reinterpret_cast<const byte*>(&pool),sizeof(pool)},{0,0,sizeof(pool)}});
        guarded_pool_access::damage(plan,&pool,[&](auto d){writes.push_back(d);});
        for(auto d:writes)before.before(d);
        const auto result=guarded_pool_access::commit<V,true>(plan,incoming);
        const auto* payload=plan.destination->storage.data+64+plan.rank*plan.bytes;
        if(!plan.reused)for(std::size_t i=full.size();i<plan.bytes;++i)ht::require(payload[i]==0,"new assigned slack initialized");
        before.verify();expected.emplace(result,std::pair{which,std::move(full)});verify();return result;
        };
#if WRITE_ARCH_POOL_BORROWED
        guarded_pool_access::borrowed_reservation<Pool> borrowed;
        failure_probe::after=0;failure_probe::hit=false;
        const auto selected=guarded_pool_access::prepare_borrowed(pool,which,prefix,borrowed);
        failure_probe::after=-1;ht::require(!failure_probe::hit,"borrowed selection allocates nothing");
        before.verify(true);
        if(selected)return commit(borrowed);
#endif
        auto plan=guarded_pool_access::prepare(pool,which,prefix);return commit(plan);
    };
    // Fill several blocks: exercise assigned-rank255, full-block unlink,
    // descriptor vector relocation and surviving neighbours.
    std::vector<word> small;for(unsigned i=0;i<520;++i)small.push_back(append(1,i));
    for(unsigned i:{3u,259u})release(small[i]);verify();
    auto reused=append(1,800);ht::require(reused==small[259],"selected returned rank preserved across cancel");
    // Abandon a stale selection after an ordinary mutation consumes that same
    // rank. This is not a valid commit sequence, but destruction remains inert.
    {
        observation before(pool);auto stale=guarded_pool_access::prepare(pool,0,{});before.verify(true);
        auto v=value<V>(801);auto ref=pool.create(0,{},v);expected[ref]={0,{v.begin(),v.end()}};
    }
    verify();
    // Exercise every growth family, including the bounded last reservation.
    for(unsigned count=2;count<Pool::max_values;count=std::min(count*2,Pool::max_values))append(count,910+count);
    append(Pool::max_values,1200);
    // An emptied block's descriptor is reusable without cancellation changing
    // the free-ID chain; payload and allocation counts are checked by validate.
    while(!expected.empty())release(expected.begin()->first);verify();
    append(1,1300);append(Pool::max_values,1400);
    observation unchanged(pool);bool refused=false;
    auto oversized=value<V>(1500);
    try{auto bad=guarded_pool_access::prepare(pool,0,oversized);(void)bad;}catch(const std::runtime_error&){refused=true;}
    ht::require(refused,"oversized prepared prefix refused");unchanged.verify(true);verify();
    std::cout<<"PREPARED_POOL_PASS,"<<V<<'\n';
}
}
#ifndef WRITE_ARCH_POOL_CHECK_NO_MAIN
int main(){check<8>();check<64>();}
#endif
