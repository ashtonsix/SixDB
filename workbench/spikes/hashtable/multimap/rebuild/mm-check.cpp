#include "mm-grow.h"
#include <iostream>
#include <map>

namespace study {
namespace ht=ikea::hashtable;
using ht::word;
template<unsigned V> ht::bytes<V> payload(word key,unsigned rank){
    ht::bytes<V> v;for(unsigned i=0;i<V;++i)v[i]=std::byte((key>>(i%8)*8)^rank*131^i*79);return v;
}
template<class MM,class Oracle> void verify(const MM& owner,const Oracle& expected){
    owner.validate();ht::require(owner.size()==expected.size(),"multimap grow distinct count");
    for(const auto& [key,bag]:expected){std::size_t rank=0;
        ht::require(owner.find(key,[&](const auto& value){ht::require(rank<bag.size()&&value==bag[rank++],"multimap grow ordered values");}),"multimap grow membership");
        ht::require(rank==bag.size(),"multimap grow bag length");}
}
template<unsigned W,unsigned V> void check(){
    constexpr unsigned b=10,K=W-b;using MM=write_arch::multimap<W,b,V,false>;
    MM source(713);ht::permutation<W,K> mix{713};
    std::map<word,std::vector<ht::bytes<V>>> expected;
    const unsigned lengths[]={1,2,3,7,8,9,16,17,64,65,129,257};
    constexpr unsigned initial=K>9?600:unsigned((word{1}<<K)*2);
    constexpr unsigned refill=initial/4;
    for(unsigned i=0;i<initial;++i){const auto key=mix.recover(i%3,i/3);
        for(unsigned j=0;j<lengths[i%12];++j){auto v=payload<V>(key,j);source.insert(key,v);expected[key].push_back(v);}}
    // Reuse both buffer ranks and whole block directory IDs before copying.
    for(unsigned i=0;i<initial;i+=3){const auto key=mix.recover(i%3,i/3);source.erase(key);expected.erase(key);}
    for(unsigned i=initial;i<initial+refill;++i){const auto key=mix.recover(i%3,i/3);
        for(unsigned j=0;j<lengths[i%12];++j){auto v=payload<V>(key,j);source.insert(key,v);expected[key].push_back(v);}}
    verify(source,expected);auto grown=write_arch::multimap_rebuild::grow(source);verify(*grown,expected);
    ht::require(source.live_buffers()==grown->live_buffers(),"multimap grow reference census");
    auto changed=expected;
    unsigned i=0;for(auto& [key,bag]:changed){auto v=payload<V>(key,1000+i++);grown->insert(key,v);bag.push_back(v);}
    verify(*grown,changed);verify(source,expected);
    i=0;for(auto& [key,bag]:expected){auto v=payload<V>(key,2000+i++);source.insert(key,v);bag.push_back(v);}
    verify(source,expected);verify(*grown,changed);
    for(auto it=changed.begin();it!=changed.end();){grown->erase(it->first);it=changed.erase(it);if(it!=changed.end())++it;}
    verify(*grown,changed);verify(source,expected);
    auto enumerated=write_arch::multimap_rebuild::enumerate<false>(source);verify(*enumerated,expected);
    auto appended=write_arch::multimap_rebuild::enumerate<true>(source);verify(*appended,expected);
    auto image=write_arch::multimap_rebuild::grow<true>(source);verify(*image,expected);
    if constexpr(K>=2){auto again=write_arch::multimap_rebuild::grow<true>(*image);verify(*again,expected);}
    auto image_expected=expected;for(auto& [key,bag]:image_expected){auto v=payload<V>(key,3000);image->insert(key,v);bag.push_back(v);}
    verify(*image,image_expected);verify(source,expected);
    auto consuming=std::make_unique<MM>(713);
    for(const auto& [key,bag]:expected)for(const auto& value:bag)consuming->insert(key,value);
    std::map<word,const std::byte*> before;
    write_arch::multimap_rebuild::visit_bags(*consuming,[&](word key,std::span<const std::byte> values){
        if(values.size()>2*V)before[key]=values.data();});
    auto moved=write_arch::multimap_rebuild::grow_consuming(consuming);
    ht::require(!consuming,"consuming growth clears source owner");verify(*moved,expected);
    write_arch::multimap_rebuild::visit_bags(*moved,[&](word key,std::span<const std::byte> values){
        if(before.contains(key))ht::require(before[key]==values.data(),"consuming growth preserves payload addresses");});
    if constexpr(K>=2){auto image_moved=write_arch::multimap_rebuild::grow_consuming<true>(moved);
        ht::require(!moved,"image consuming growth clears source owner");verify(*image_moved,expected);
        write_arch::multimap_rebuild::visit_bags(*image_moved,[&](word key,std::span<const std::byte> values){
            if(before.contains(key))ht::require(before[key]==values.data(),"image consuming growth preserves payload addresses");});}
    std::cout<<"MM_GROW_PASS,"<<W<<','<<V<<'\n';
}
}
#ifndef REBUILD_MM_CHECK_NO_MAIN
int main(){study::check<24,8>();study::check<24,64>();study::check<40,8>();study::check<40,64>();
    study::check<20,8>();study::check<20,64>();study::check<11,8>();study::check<11,64>();}
#endif
