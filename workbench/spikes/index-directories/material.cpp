#include "material.h"
#include "hash.h"
#include <orbital/instantiate/object.h>
#include <orbital/instantiate/representation.h>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <thread>

namespace directory_spike {
void retain_geometry(Geometry* p) noexcept {if(p)++p->references;}
void release_geometry(Geometry* p) noexcept {
    if(!p || --p->references)return;auto* r=p->memory;
    std::destroy_at(p);r->deallocate(p,sizeof(Geometry),alignof(Geometry));
}
void Control::retain() const noexcept {retain_hash(hash);retain_geometry(geometry);}
void Control::release() const noexcept {release_hash(hash);release_geometry(geometry);}
void Physical::retain(Value* p) noexcept {orbital::retain(p);}
void Physical::release(Value* p) noexcept {orbital::release(p);}
orbital::Result<Physical::Value*> Physical::capture(std::pmr::memory_resource& m,const Control& c,std::span<const PartSource> parts) noexcept {
    return orbital::capture_representation(m,c,orbital::PartOwners::independent_allocation,parts);
}
bool Physical::accepts(Value* v,orbital::ObjectId id,u64) noexcept {
    const auto c=Selection{v}.control();
    const auto expected=c.kind==Kind::segment ? segment_id(c.layer,c.prefix) : c.kind==Kind::directory ? directory_id(c.layer):
        c.kind==Kind::directory_page?page_id(c.layer,c.prefix):c.kind==Kind::directory_branch?branch_id(c.layer,c.prefix):c.kind==Kind::overflow?overflow_id(c.layer,c.prefix):schema_id();
    return id==expected;
}
bool Physical::accepts_resource(Value* v,std::pmr::memory_resource& m) noexcept {return v && Selection{v}.control().memory==&m;}
bool Physical::compatible(Value* a,Value* b) noexcept {
    const auto x=Selection{a}.control(),y=Selection{b}.control();
    return x.kind==y.kind && x.layer==y.layer && x.prefix==y.prefix && x.count==y.count && x.key_bytes==y.key_bytes;
}
Physical::Footprint Physical::footprint(Value* p) noexcept {
    return {orbital::object_detail::representation_bytes(Selection{p}.control(),orbital::PartOwners::independent_allocation),0};
}
orbital::Result<orbital::Part<Control>*> Physical::export_part(Selection s,std::size_t p,orbital::ObjectBinding b,ExportPolicy policy) noexcept {
    return orbital::object_detail::retain_part(s,p,b,policy);
}
Runtime::Runtime() {
    take(loom::adopt(worker)); arena=take(oa::open_arena(512ull<<20));take(loom::configure(worker,arena,0));
}
Runtime::~Runtime() {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    for (;;) {
        auto p=loom::shutdown(worker);
        if(p.kind()==orbital::Progress::Kind::done)break;
        require(p.kind()!=orbital::Progress::Kind::failed && std::chrono::steady_clock::now()<deadline);
        loom::poll(worker,{256,0});std::this_thread::yield();
    }
    require(!arena->charged_bytes.load());loom::relinquish(worker);
}
std::span<const std::byte> bytes(Selection s,unsigned p) {
    require(s && p<s.part_count());return {take(s.part(p)).address(),take(s.control().extent(p))};
}
Store::Owner capture(Runtime& rt,Store& store,Control c,const Image& image,Selection old,bool selective,CopyStats& stats,bool same_geometry) {
    auto& allocation=rt.allocations;auto& sources=rt.sources;auto& extents=rt.extents;
    allocation.resize(c.parts);sources.resize(c.parts);
    if(same_geometry)require(old && old.part_count()==c.parts);
    else extents.resize(c.parts);
    for(unsigned p=0;p<c.parts;++p) {
        const bool prior=old && p<old.part_count();
        const bool retained=prior && selective && !image.supplied(p);
        const auto extent=retained ? take(old.control().extent(p)):image[p].size();
        if(same_geometry)require(extent==take(old.control().extent(p)));else extents[p]=extent;
        if(retained || (selective && prior && std::ranges::equal(bytes(old,p),image[p]))) {
            sources[p]=take(orbital::source(old,p));stats.shared+=extent;
        } else if(!extent) {
            sources[p]={c.id(p),{}}; // Zero body/padding requires no native owner.
        } else {
            // The supplied image initializes the entire admitted body. There is
            // no readable padding, so clearing before memcpy adds no evidence.
            allocation[p]=take(loom::allocate(rt.worker,{std::max<std::size_t>(64,extent),0,64,0,oa::Initialization::uninitialized}));
            // Request-size diagnostic for this caller's zero-padding, 64-aligned
            // Parts. Orbital currently routes <=3072 bytes to finite storage.
            if(image[p].size()<=3072)++stats.small_requests;else ++stats.large_requests;
            if(!image[p].empty())std::memcpy(allocation[p].address,image[p].data(),image[p].size());
            sources[p]={c.id(p),{allocation[p].address,image[p].size()},{},&allocation[p]};
            stats.allocated+=allocation[p].size;stats.copied+=image[p].size();
        }
    }
    const bool reused=same_geometry || (old && std::ranges::equal(old.control().geometry->extents,extents));
    if(reused)c.geometry=old.control().geometry; // The source Scope protects it.
    else c.geometry=std::construct_at(static_cast<Geometry*>(c.memory->allocate(sizeof(Geometry),alignof(Geometry))),*c.memory,extents);
    ++stats.objects;
    if(c.kind==Kind::directory || c.kind==Kind::directory_page || c.kind==Kind::directory_branch) {
        stats.dirent_bytes+=image[3].size();
        if(c.table_row_chunk)for(unsigned p=6;p<c.parts;++p)stats.dirent_bytes+=image[p].size();
    }
    const auto start=std::chrono::steady_clock::now();auto owner=take(store.capture(c,sources));
    stats.realization_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    if(!reused)release_geometry(c.geometry);
    sources.clear();allocation.clear();extents.clear();return owner;
}
void publish(Store& store,const Store::Scope& basis,std::span<const Store::Change> changes) {
    Store::Prepared packet(store);take(store.prepare(basis,changes,packet));take(store.install(packet));
}
} // namespace directory_spike
template struct orbital::object_detail::History<directory_spike::Physical>;
template class orbital::ObjectStore<directory_spike::Physical>;
template struct orbital::RepresentationSelection<directory_spike::Control>;
template void orbital::retain(orbital::Representation<directory_spike::Control>*) noexcept;
template void orbital::release(orbital::Representation<directory_spike::Control>*) noexcept;
template void orbital::release(orbital::Part<directory_spike::Control>*) noexcept;
template orbital::View orbital::view(const orbital::Part<directory_spike::Control>*) noexcept;
template orbital::ObjectBinding orbital::binding(const orbital::Part<directory_spike::Control>*) noexcept;
template orbital::Result<orbital::PartSource> orbital::source(orbital::RepresentationSelection<directory_spike::Control>,std::size_t) noexcept;
