#pragma once
#include <loom/worker.h>
#include <orbital/object.h>
#include <orbital/representation.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <memory_resource>
#include <source_location>
#include <span>
#include <vector>

namespace directory_spike {
using u64 = std::uint64_t;
using i64 = std::int64_t;
namespace oa = orbital::allocation;
inline void require(bool value, std::source_location at=std::source_location::current()) {
    if (!value) { std::fprintf(stderr,"%s:%u: directory check failed\n",at.file_name(),at.line()); std::abort(); }
}
template<class T> T take(orbital::Result<T> r, std::source_location at=std::source_location::current()) {
    if (!r) { std::fprintf(stderr,"%s:%u: Orbital error %u\n",at.file_name(),at.line(),unsigned(r.error())); std::abort(); }
    return std::move(*r);
}
inline void take(orbital::Result<void> r, std::source_location at=std::source_location::current()) {
    if (!r) { std::fprintf(stderr,"%s:%u: Orbital error %u\n",at.file_name(),at.line(),unsigned(r.error())); std::abort(); }
}
struct alignas(64) Dirent {
    std::array<u64,6> parts{};
    std::uint32_t representation{};
    std::uint16_t flags{}, overflow_rank{};
    std::uint32_t overflow_block{}, revision{};
};
static_assert(sizeof(Dirent)==64);
enum class Kind : unsigned { segment, directory, schema, directory_page, overflow, directory_branch };
struct PrefixHash;
inline orbital::ObjectId segment_id(unsigned,unsigned);
struct Geometry {
    std::pmr::memory_resource* memory;
    unsigned references{1};
    std::pmr::vector<std::size_t> extents;
    Geometry(std::pmr::memory_resource& r,std::span<const std::size_t> e):memory(&r),extents(e.begin(),e.end(),&r){}
};
struct PartReference {orbital::ObjectId object;unsigned ordinal;};
// An experimental memory locator points at one immutable cell. The cell names
// the existing segment Object and Part; the selected snapshot supplies bytes.
// Every segment realization using these addresses owns the cell set. Coupled
// directory/segment publication and the same Core/Scope protect late discovery.
struct LocatorCells {
    std::pmr::memory_resource* memory;
    unsigned references{1};
    unsigned count;
    orbital::ObjectId object;
    void* data;
    bool compact;
    LocatorCells(std::pmr::memory_resource& r,orbital::ObjectId id,unsigned n,bool small):memory(&r),count(n),object(id),
        data(r.allocate(n*(small?sizeof(unsigned):sizeof(PartReference)),small?alignof(unsigned):alignof(PartReference))),compact(small) {
        for(unsigned p=0;p<count;++p) {
            if(compact)std::construct_at(static_cast<unsigned*>(data)+p,p);
            else std::construct_at(static_cast<PartReference*>(data)+p,PartReference{object,p});
        }
    }
    ~LocatorCells(){memory->deallocate(data,count*stride(),compact?alignof(unsigned):alignof(PartReference));}
    std::size_t stride()const{return compact?sizeof(unsigned):sizeof(PartReference);}
    const void* address(unsigned p)const{return static_cast<const std::byte*>(data)+p*stride();}
    PartReference reference(unsigned p)const{return compact?PartReference{object,static_cast<const unsigned*>(data)[p]}:static_cast<const PartReference*>(data)[p];}
    std::size_t bytes()const{return sizeof(LocatorCells)+count*stride();}
};
// Prototype interpretation, not a wire format. Geometry has no fixed Part count.
// Segment: positions, minima, maxima, summaries, keys, auxiliary payload.
// Directory: prefixes, minima, maxima, dirents; tables reserve the full top layers.
struct Control {
    std::pmr::memory_resource* memory{};
    Kind kind{};
    unsigned layer{}, prefix{}, count{}, representation{1}, revision{1}, first{}, parts{6};
    unsigned key_bytes{8};
    Geometry* geometry{};
    PrefixHash* hash{}; // Coherent reconstructed accelerator; bytes charged separately.
    unsigned summary_chunk{};
    bool compact_table{};
    unsigned table_row_chunk{};
    bool indirect_parts{};
    bool compact_locators{};
    LocatorCells* locators{};
    bool valid(std::pmr::memory_resource& r) const noexcept {
        return memory==&r && geometry && geometry->memory==&r && parts && first<geometry->extents.size() && parts<=geometry->extents.size()-first && layer<5 && representation && key_bytes>=8 &&
            (indirect_parts ? (kind==Kind::segment && locators && locators->memory==&r && first<=locators->count && parts<=locators->count-first &&
                locators->compact==compact_locators && locators->object==segment_id(layer,prefix)):!locators);
    }
    auto* resource() const noexcept { return memory; }
    std::size_t part_count() const noexcept { return parts; }
    orbital::PartId id(std::size_t i) const noexcept { return {unsigned(kind)+1,first+i}; }
    orbital::Result<std::size_t> extent(std::size_t i) const noexcept {
        if(i>=parts) return std::unexpected(orbital::Error::invalid_argument);
        return geometry->extents[first+i];
    }
    std::size_t alignment(std::size_t) const noexcept { return 64; }
    std::uint32_t padding(std::size_t) const noexcept { return 0; }
    void retain() const noexcept;
    void release() const noexcept;
    std::size_t selected_bytes(std::size_t) const noexcept { return 0; }
    Control copy_selected(void*,std::size_t i) const noexcept { auto c=*this;c.first+=i;c.parts=1;c.retain();return c; }
    std::size_t metadata_bytes() const noexcept { return 0; }
    void destroy_selected() const noexcept {release();}
};
using Selection=orbital::RepresentationSelection<Control>;
struct Physical {
    using Value=orbital::Representation<Control>;
    using Selection=directory_spike::Selection;
    using Specification=Control;
    using PartSource=orbital::PartSource;
    using ExportPolicy=orbital::PartRetention;
    struct Footprint { std::size_t requested_bytes{}, copied_metadata{}; };
    static void retain(Value* p) noexcept;
    static void release(Value* p) noexcept;
    static Value* get(Selection s) noexcept {return s.realization;}
    static Selection selection(Value* p) noexcept {return {p};}
    static orbital::Result<Value*> capture(std::pmr::memory_resource&,const Control&,std::span<const PartSource>) noexcept;
    static bool accepts(Value*,orbital::ObjectId,u64) noexcept;
    static bool accepts_resource(Value*,std::pmr::memory_resource&) noexcept;
    static bool compatible(Value*,Value*) noexcept;
    static Footprint footprint(Value*) noexcept;
    static orbital::Result<orbital::Part<Control>*> export_part(Selection,std::size_t,orbital::ObjectBinding,ExportPolicy) noexcept;
};
using Store=orbital::ObjectStore<Physical>;
constexpr u64 index_id=0x494e4458;
inline orbital::ObjectId segment_id(unsigned layer,unsigned prefix) {return {index_id,(u64(layer+1)<<56)|prefix};}
inline orbital::ObjectId directory_id(unsigned layer) {return {index_id,u64(16+layer)<<56};}
inline orbital::ObjectId page_id(unsigned layer,unsigned prefix) {return {index_id,(u64(48+layer)<<56)|prefix};}
inline orbital::ObjectId overflow_id(unsigned layer,unsigned block) {return {index_id,(u64(64+layer)<<56)|block};}
inline orbital::ObjectId branch_id(unsigned layer,unsigned prefix) {return {index_id,(u64(80+layer)<<56)|prefix};}
inline orbital::ObjectId schema_id() {return {index_id,u64(32)<<56};}
struct Runtime {
    loom::Worker worker;
    orbital::Ref<oa::Arena> arena;
    // One synchronous writer in this fixture. Capacity is reused; certificates
    // are reconstructed from each live basis and cleared before capture returns.
    std::vector<oa::Allocation> allocations;
    std::vector<orbital::PartSource> sources;
    std::vector<std::size_t> extents;
    Runtime(); ~Runtime();
};
std::span<const std::byte> bytes(Selection,unsigned);
u64 part_locator(Selection,unsigned);
orbital::Result<const std::byte*> resolve_locator(Selection,u64,unsigned);
// Borrowed proof; the escaping Part, rather than a Scope, protects this copy.
Control part_control(const orbital::Part<Control>*);
template<class T> std::span<const T> values(Selection s,unsigned p) {
    const auto b=bytes(s,p); require(b.size()%sizeof(T)==0);
    return {reinterpret_cast<const T*>(b.data()),b.size()/sizeof(T)};
}
class Image {
    std::vector<std::vector<std::byte>> parts_{6};
    std::vector<bool> supplied_=std::vector<bool>(6);
public:
    std::vector<std::byte>& operator[](std::size_t p) {
        if(p>=parts_.size()){parts_.resize(p+1);supplied_.resize(p+1);}
        supplied_[p]=true;return parts_[p];
    }
    const std::vector<std::byte>& operator[](std::size_t p)const {
        static const std::vector<std::byte> empty;
        return p<parts_.size()?parts_[p]:empty;
    }
    bool supplied(std::size_t p)const{return p<supplied_.size() && supplied_[p];}
};
struct CopyStats {u64 allocated{},copied{},shared{},objects{},dirent_bytes{},hash_created_bytes{},small_requests{},large_requests{},realization_ns{},locator_created_bytes{};};
Store::Owner capture(Runtime&,Store&,Control,const Image&,Selection old,bool selective,CopyStats&,bool same_geometry=false);
void publish(Store&,const Store::Scope&,std::span<const Store::Change>);
} // namespace directory_spike
extern template class orbital::ObjectStore<directory_spike::Physical>;
extern template struct orbital::RepresentationSelection<directory_spike::Control>;
