#include "small.h"
#include <map>

namespace ht {
template<unsigned K,container_kind Kind,unsigned W> void growth_contents(oa::Pool& pool) {
    small_table<K,Kind,W> table(pool,1771);
    using value=std::array<byte,W>;
    using oracle=std::map<word,std::vector<value>>;
    oracle expected;
    auto read=[](const auto& source) {
        oracle result;
        source.each_bytes([&](word k,auto bytes){value v;std::copy(bytes.begin(),bytes.end(),v.begin());result[k].push_back(v);});
        for(auto& [k,values]:result)std::sort(values.begin(),values.end());return result;
    };
    const auto key=[](unsigned i){return (word(i)*0xd6e8feb86659fd93ULL)&mask(K);};
    auto insert=[&](unsigned i,unsigned salt) {
        value v;for(unsigned j=0;j<W;++j)v[j]=byte(mix(i*71+salt*1009+word(j)*113));
        auto delta=table.set_bytes(key(i),v);
        if constexpr(Kind==container_kind::multimap)expected[key(i)].push_back(v);
        else expected[key(i)]={v};
        std::sort(expected[key(i)].begin(),expected[key(i)].end());return delta;
    };
    for(unsigned i=0;i<128;++i) {
        insert(i,0);
        if(std::has_single_bit(i+1))require(read(table)==expected,"private grow preserves all key/value bytes");
    }
    if constexpr(Kind==container_kind::multimap)for(unsigned i=0;i<128;i+=3)insert(i,1);
    auto old=table.clone();require(bool(old),"grow retained independent copy");const auto old_expected=expected;
    for(unsigned i=128;i<256;++i)insert(i,2);
    require(read(table)==expected&&read(*old)==old_expected,"grow through wide relocation with old copy");
    for(unsigned i=0;i<256;i+=2){table.erase(key(i));expected.erase(key(i));}
    require(read(table)==expected,"grown swap-last erase");
    for(unsigned i=0;i<256;i+=2)insert(i,3);
    require(read(table)==expected&&read(*old)==old_expected,"grown reuse keeps old version");
    std::array<byte,W> v{};small_journal no_room{{}};
    require(!table.set_bytes(key(0),v,no_room)&&read(table)==expected,"grow mutation admission refusal");
    table.clear();require(table.footprint().requested==0&&read(*old)==old_expected,"grown clear and retained copy");
}
template<unsigned K> void domain(oa::Pool& pool) {
    growth_contents<K,container_kind::set,0>(pool);
    growth_contents<K,container_kind::map,1>(pool);
    growth_contents<K,container_kind::map,64>(pool);
    growth_contents<K,container_kind::multimap,2>(pool);
    growth_contents<K,container_kind::multimap,64>(pool);
}
void growth_refusal(oa::Pool& pool) {
    // Explicitly constrain reservation here so an external Hold forces Orbital
    // to refuse relocation. This is a failure test, not a reservation policy.
    using owner=list_owner<64,container_kind::map,64>;
    owner list;constexpr unsigned count=128;
    auto storage=oa::allocate(pool,{.bytes=count*owner::view_type::entry_bytes,
        .reserved_bytes=count*owner::view_type::entry_bytes,.alignment=8});
    require(bool(storage),"bounded grow test allocation");list.allocation=std::move(*storage);
    for(std::size_t i=0;i<list.allocation.size;++i)list.allocation.address[i]=std::byte(mix(i));
    const std::vector<std::byte> before(list.allocation.address,list.allocation.address+list.allocation.size);
    unsigned observations=0;
    auto preserve=[&](const oa::Allocation& original)noexcept {
        ++observations;
        require(original.size==before.size()&&std::equal(before.begin(),before.end(),original.address),
            "growth observation precedes relocation and overlapping stores");
    };
    auto held=oa::hold(list.allocation,{0,list.allocation.size});require(bool(held),"grow refusal address Hold");
    require(!list.reserve(pool,count,256,preserve),"live Hold refuses required relocation");
    require(list.allocation.size==before.size()&&std::equal(before.begin(),before.end(),list.allocation.address),
        "failed private growth preserves allocation and every byte");
    oa::release(*held);
    require(list.reserve(pool,count,256,preserve),"growth after Hold retirement");
    require(observations==2,"successful and refused growth observations");
    const auto view=list.bind();
    for(unsigned i=0;i<count;++i) {
        require(view.data[i]==std::to_integer<byte>(before[i]),"relocated fingerprint plane");
        require(std::memcmp(view.suffix_at(i),before.data()+count+i*7,7)==0,"relocated suffix plane");
        require(std::memcmp(view.payload_at(i),before.data()+count*8+i*64,64)==0,"relocated payload plane");
    }
}
}
int main() {
    ht::pool_owner p;ht::growth_refusal(*p.pool);
    ht::domain<8>(*p.pool);ht::domain<16>(*p.pool);ht::domain<24>(*p.pool);ht::domain<32>(*p.pool);
    ht::domain<40>(*p.pool);ht::domain<48>(*p.pool);ht::domain<56>(*p.pool);ht::domain<64>(*p.pool);
    std::puts("Private list growth: all eight key domains, sets/maps/multimaps, W1/2/64, refusal, relocation and retained copies pass");
}
