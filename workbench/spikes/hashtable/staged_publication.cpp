#include "promoted.h"
#include "publication.h"
#include <object/store_impl.h>
#include <loom/ring.h>

namespace ht::retained_staged {
constexpr unsigned N=400;
word key(unsigned i){return word(i)*0xd6e8feb86659fd93ULL&mask(40);}
template<class Read> struct job {
    typename Read::probe probe;word key=0;unsigned expected=0,seen=0;
    bool present=false,finished=false;std::array<std::array<byte,64>,80> values{};
};
template<class Read> struct context {
    Read read;std::vector<std::span<const byte>> extents;
    void window(const byte* p,std::size_t n)const {
        const auto at=reinterpret_cast<std::uintptr_t>(p);bool contained=false;
        for(auto w:extents){const auto start=reinterpret_cast<std::uintptr_t>(w.data());
            if(at>=start&&at-start<=w.size()&&n<=w.size()-(at-start))contained=true;}
        require(n&&contained,"retained stage belongs to old material extent");
    }
};
template<class Read> using Ring=typename loom::Ring<job<Read>*>::template WithShared<const context<Read>*>;
template<class Read> LOOM_NATIVE_CC void visit(typename Ring<Read>::Cursor ring,typename Ring<Read>::Slot* slot,
        const context<Read>* c,job<Read>* j) {
    bool found=false;
    if(c->read.advance_read(j->probe,j->key,found,[&](auto bytes){
        require(j->seen<j->expected,"retained value multiplicity bound");
        require(std::equal(bytes.begin(),bytes.end(),j->values[j->seen++].begin()),"retained complete value bytes");
    },[&](const byte* p,std::size_t n){c->window(p,n);})) {
        require(found==j->present&&j->seen==j->expected,"retained completion");j->finished=true;
        [[clang::musttail]] return Ring<Read>::finish(ring,slot,c,j);
    }
    [[clang::musttail]] return Ring<Read>::yield_unchanged(ring,slot,c,j);
}
template<class T> T accepted(orbital::Result<T> r){require(bool(r),"retained ObjectStore result");return std::move(*r);}
template<container_kind Kind,class Bags=linked_bags<8,true,16>,unsigned Width=8> void check(loom::Worker& worker) {
    constexpr unsigned W=Kind==container_kind::set?0:Width;
    static_assert(W<=64);
    using Bytes=std::array<byte,W>;
    using Small=small_table<40,Kind,W,true,Bags,16>;
    using Body=promoted_table<40,Kind,W,3,0,96,96,record_format::exact,false,record_access::both,true,Bags,16>;
    using Material=publication_material<Body>;using Store=orbital::ObjectStore<Material>;
    catalog_resource resource;
    {
        Store store(resource);constexpr orbital::ObjectId id{119,3};
        auto publish=[&](Body& candidate,typename Store::Scope& basis) {
            auto owner=accepted(store.capture({&candidate},{}));
            const std::array<typename Store::Change,1> changes{{{id,std::move(owner)}}};
            typename Store::Prepared prepared(store);
            require(bool(store.prepare(basis,changes,prepared)),"retained prepare");
            require(bool(store.install(prepared)),"retained install");
        };
        auto count=[](unsigned i){return Kind==container_kind::multimap?(i%16==0?80u:2u):1u;};
        auto value=[](unsigned i,unsigned j){Bytes v;
            for(unsigned b=0;b<W;++b)v[b]=byte(mix(word(i)+(word(j)<<32)+word(b)*719));return v;};
        auto inverted=[&](unsigned i){auto v=value(i,0);for(auto& b:v)b=byte(~b);return v;};
        {
            Small empty(*worker.memory,19);auto body=Body::promote(empty,*worker.memory,41,73);require(bool(body),"retained promote");
            for(unsigned i=0;i<N;++i)for(unsigned j=0;j<count(i);++j)body->set_bytes(key(i),value(i,j));
            const auto population=body->population();require(population[0]&&population[1]&&population[2],"retained all key tiers");
            typename Store::Scope basis(store);publish(*body,basis);
        }
        {
            typename Store::Scope original(store),unbound(store);
            const auto selected=original.bind(id);const auto& old=selected.value->body;
            auto read=old.bind_read();using Read=decltype(read);context<Read> c{read,{}};
            old.read_storage([&](auto w){if(w.size())c.extents.push_back(w);});
            std::array<job<Read>,33> jobs{};
            for(unsigned j=0;j<32;++j){auto i=j*12;jobs[j].key=key(i);jobs[j].present=true;jobs[j].expected=count(i);
                for(unsigned v=0;v<count(i);++v){auto expected=value(i,v);std::copy(expected.begin(),expected.end(),jobs[j].values[v].begin());}}
            jobs.back().key=key(N+1);
            Ring<Read> ring;require(bool(ring.configure(worker,{64,0},{&c})),"retained Ring configure");
            for(auto& j:jobs){read.prepare_read(j.probe,j.key,[&](const byte* p,std::size_t n){c.window(p,n);});
                require(ring.push(visit<Read>,{&j}),"retained Ring admission");}
            // Return from one bounded Ring visit with borrowers still queued.
            ring.spin({1});require(ring.pending(),"borrowers remain across publication");
            if constexpr(HT_RECORD_READ_WINDOWS==2 && Kind!=container_kind::set) {
                auto located=[](const auto& p) {
                    using Stage=decltype(p.next);return p.next==Stage::primary_value || p.next==Stage::secondary_value;
                };
                if constexpr(Kind==container_kind::multimap)require(located(jobs[0].probe.index),"located bag descriptor retained across publication");
                else require(located(jobs[0].probe),"located value retained across publication");
            }
            {
                typename Store::Scope basis(store);auto changed=basis.bind(id).value->body.clone();require(bool(changed),"retained clone");
                for(unsigned i=0;i<N;++i){changed->erase(key(i));changed->set_bytes(key(i),inverted(i));}
                if constexpr(Kind==container_kind::multimap)for(unsigned j=0;j<100;++j)changed->set_bytes(key(0),value(0,j+1000));
                resource.fail_at=resource.calls;auto refused=store.capture({&*changed},{});resource.fail_at=SIZE_MAX;
                require(!refused&&refused.error()==orbital::Error::capacity,"retained publication refusal");
                publish(*changed,basis);
            }
            store.drain();
            unsigned steps=0;while(ring.pending()){ring.spin({1});require(++steps<10000,"retained Ring progress");}
            for(auto& j:jobs)require(j.finished,"all retained jobs complete");
            require(bool(ring.close()),"finish borrowers before releasing selected material");
            // A retained but previously unbound scope must still find all old bags.
            const auto late=unbound.bind(id);word seen=0;
            late.value->body.get_bytes(key(0),[&](auto v){auto expected=value(0,unsigned(seen));
                require(std::equal(v.begin(),v.end(),expected.begin()),"late retained discovery");++seen;});
            require(seen==count(0),"late retained multiplicity");
            typename Store::Scope current(store);word current_seen=0;
            current.bind(id).value->body.get_bytes(key(0),[&](auto v){
                auto expected=current_seen?value(0,unsigned(current_seen)+999):inverted(0);
                require(std::equal(v.begin(),v.end(),expected.begin()),"new version independent complete bytes");++current_seen;});
            require(current_seen==(Kind==container_kind::multimap?101:1),"new version multiplicity");
        }
        store.drain();
    }
    require(resource.live==0&&Material::live==nullptr,"retained catalog and material retirement");
}
}
int main(){using namespace ht;loom::Worker worker;require(bool(loom::adopt(worker)),"retained worker adoption");
    auto arena=oa::open_arena(4ULL<<30);require(bool(arena),"retained arena");
    require(bool(loom::configure(worker,std::move(*arena),0)),"retained worker configuration");
    using namespace retained_staged;
    check<container_kind::set>(worker);check<container_kind::map>(worker);
    check<container_kind::map,linked_bags<8,true,16>,64>(worker);
    check<container_kind::multimap>(worker);
    check<container_kind::multimap,buffer_bags<8>>(worker);
    check<container_kind::multimap,pooled_vector_bags<8,true,16>>(worker);
    auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(loom::shutdown(worker).kind()!=orbital::Progress::Kind::done){require(clock_type::now()<deadline,"retained worker drain");loom::poll(worker,{1024,0});}
    loom::relinquish(worker);
    std::puts("retained staged: actual ObjectStore publication/refusal, queued Loom reads, full 64-byte maps, all key tiers, linked/buffer/vector bags and balanced retirement passed");
}
