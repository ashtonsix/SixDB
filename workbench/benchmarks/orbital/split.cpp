#include "workload.h"
#include "packed-page.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>
namespace b=orbital_bench;
namespace {
std::atomic<std::size_t> rejected_size{};
std::atomic<unsigned> failures{};
void require(bool ok,const char* message) {
    if(!ok) { std::fprintf(stderr,"page publication check: %s\n",message);std::abort(); }
}
std::vector<std::uintptr_t> page_addresses(b::Store& store) {
    std::vector<std::uintptr_t> result;
    for(const auto& region:store.memory_regions())
        if(region.kind=="page" || region.kind=="bindings")result.push_back(region.address);
    return result;
}
void ordered_rows(b::Store& store,std::span<const b::Row> rows) {
    std::array<b::Row,37> got;
    b::Key next=0;
    std::size_t position=0;
    for(;;) {
        store.begin(false);const auto count=store.scan(next,got);store.end();
        if(!count)break;
        require(position+count<=rows.size() && std::equal(got.begin(),got.begin()+count,rows.begin()+position),
                "failed preparation/publication changed original ordered rows");
        position+=count;next=got[count-1].key+1;
    }
    require(position==rows.size(),"failed preparation/publication lost original rows");
}
enum class Boundary { split_fence, page, append_fence };
}
void* operator new(std::size_t bytes) {
    auto expected=bytes;
    if(bytes && rejected_size.compare_exchange_strong(expected,0)) {
        ++failures;
        throw std::bad_alloc();
    }
    if(auto* p=std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void failure_boundary(b::Access access,Boundary boundary) {
    constexpr std::size_t pages=17;
    const auto rows=b::initial_rows(pages*256,123);
    auto store=b::packed_store(64u<<20,access,true);
    store->load(rows);
    const auto before=store->counters();
    const auto addresses=page_addresses(*store);
    const bool append=boundary==Boundary::append_fence;
    const auto key=append ? rows.back().key+1 : rows[127].key+1;
    const auto allocation_bytes=boundary==Boundary::page ? sizeof(b::packed::Page)
        : 34*sizeof(b::packed::Entry);
    const auto prior_failures=failures.load();
    bool rejected=false;
    rejected_size=allocation_bytes;
    store->begin(true);
    try { (void)store->insert(key,rows[127].record); }
    catch(const std::bad_alloc&) { rejected=true; }
    store->end();
    rejected_size=0;
    require(rejected && failures==prior_failures+1,"target allocation failure was not exercised");
    const auto after=store->counters();
    require(after.splits==before.splits && after.logical_live_rows==before.logical_live_rows &&
            after.live_payload_bytes==before.live_payload_bytes && page_addresses(*store)==addresses,
            "failed preparation/publication changed live pages, bindings or visibility");
    const bool prepared=append;
    require(after.pool_allocations==before.pool_allocations+unsigned(prepared),
            "failure did not occur at the intended backing preparation boundary");
    if(!prepared)
        require(after.allocated_pages==before.allocated_pages && after.payload_bytes==before.payload_bytes &&
                after.issued_write_bytes==before.issued_write_bytes && after.index_write_bytes==before.index_write_bytes,
                "preflight refusal changed backing/effects");
    else
        require(after.allocated_pages==before.allocated_pages+1 && after.payload_bytes>before.payload_bytes,
                "append publication did not fail with an actual unpublished prepared Page");
    store->drain();store->audit();ordered_rows(*store,rows);
    const auto retry_before=store->counters();
    const auto result=b::execute(*store,{b::Kind::insert,key,rows[127].record,1});
    require(result.changes==1,"retry did not complete actual insertion");
    store->drain();store->audit();
    if(prepared)require(store->counters().pool_reuses>retry_before.pool_reuses,
                        "failed unpublished Page did not return its backing for ordinary reuse");
    store->begin(false);const auto inserted=store->get(key);store->end();
    require(inserted && *inserted==rows[127].record,"retry record differs");
    const char* name=boundary==Boundary::split_fence ? "split fence" : boundary==Boundary::page ? "Page"
        : "append fence";
    std::printf("publication: %s failure preserves original rows/addresses; access=%u backing retry passed\n",name,unsigned(access));
}
void partial_load(b::Access access) {
    auto rows=b::initial_rows(513,321);
    rows[300].key=rows[299].key;
    auto store=b::packed_store(64u<<20,access,true);
    bool rejected=false;
    try { store->load(rows); }
    catch(const std::runtime_error&) { rejected=true; }
    require(rejected,"malformed partial load was accepted");
    store->drain();store->audit();ordered_rows(*store,std::span(rows).first(256));
    require(store->counters().logical_live_rows==256 && page_addresses(*store).size()==2,
            "partial load retained an unpublished Page or lost its published prefix");
    store.reset(); // Actual destruction must retire the raw owned prefix and drain captured storage.
    auto healthy=b::packed_store(64u<<20,access,true);
    const auto valid=b::initial_rows(257,321);
    healthy->load(valid);healthy->audit();ordered_rows(*healthy,valid);
    std::printf("publication: partial load retained only its ordered prefix; access=%u teardown/retry passed\n",unsigned(access));
}
int main() {
    for(auto access:{b::Access::owner_ready,b::Access::fresh_hold,b::Access::retained_hold}) {
        for(auto boundary:{Boundary::split_fence,Boundary::page,Boundary::append_fence})
            failure_boundary(access,boundary);
        partial_load(access);
    }
}
