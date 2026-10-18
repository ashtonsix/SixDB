#define REBUILD_MM_CHECK_NO_MAIN
#include "mm-check.cpp"
#include <set>
#ifndef MM_MERGE_REUSE_FITTING
#define MM_MERGE_REUSE_FITTING 0
#endif
#ifndef MM_MERGE_BULK_JOIN
#define MM_MERGE_BULK_JOIN 0
#endif
#ifndef MM_MERGE_REUSE_RIGHT
#define MM_MERGE_REUSE_RIGHT 0
#endif
#ifndef MM_MERGE_LIVE_CELLS
#define MM_MERGE_LIVE_CELLS 0
#endif
#ifndef MM_MERGE_KEEP_BUCKETS
#define MM_MERGE_KEEP_BUCKETS 0
#endif

namespace write_arch {
struct multimap_probe {
    template<class MM> static auto snapshot(const MM& owner){std::map<word,std::vector<byte>> result;
        owner.index_.visit([&](word key,const auto& cell){
            auto& bytes=result[key];bytes.assign(cell.begin(),cell.end());const auto meta=ht::load<8>(cell.data());
            if(MM::is_external(meta)){const auto which=MM::family(meta);const auto ref=ht::load<8>(cell.data()+8);
                const auto* data=owner.buffers_.data(ref,which);
                bytes.insert(bytes.end(),data,data+std::size_t(owner.buffers_.capacity(which))*sizeof(typename MM::value_type));}
        });return result;
    }
    template<class MM> static auto references(const MM& owner){std::set<word> refs;
        owner.index_.visit([&](word,const auto& cell){if(MM::is_external(ht::load<8>(cell.data())))refs.insert(ht::load<8>(cell.data()+8));});
        return refs;
    }
};
}
template<unsigned W,unsigned V> void check_merge(){
    namespace ht=ikea::hashtable;using ht::word;
    constexpr unsigned b=10,K=W-b;using MM=write_arch::multimap<W,b,V,false>;
    MM left(713),right(713);ht::permutation<W,K> mix{713};
    std::map<word,std::vector<ht::bytes<V>>> a,z;
    const unsigned lengths[]={1,2,3,7,8,9,16,17,65};
    auto put=[&](MM& owner,auto& oracle,unsigned i,unsigned generation){
        const auto key=mix.recover(i%4,(i/4)&ht::mask<K>);
        for(unsigned rank=0;rank<lengths[i%9];++rank){auto v=study::payload<V>(key,generation+rank);owner.insert(key,v);oracle[key].push_back(v);}
    };
    constexpr unsigned n=K>9?600:unsigned(std::min(word{600},(word{1}<<K)*2));
    for(unsigned i=0;i<n;++i)put(left,a,i,0);
    for(unsigned i=n+n/2;i-->n/2;)put(right,z,i,1000);
    for(unsigned i=0;i<std::min(16u,n/2);++i)put(right,z,i,2000);
    auto expected=a;for(const auto& [key,bag]:z){auto& target=expected[key];target.insert(target.end(),bag.begin(),bag.end());}
    const auto ar=write_arch::multimap_probe::references(left),zr=write_arch::multimap_probe::references(right);
    std::size_t overlap=0;for(auto ref:ar)overlap+=zr.contains(ref);
    if(K>9)ht::require(overlap>0,"worker fixture shares numeric block/rank values");
    auto result=write_arch::multimap_rebuild::merge(left,right);
    study::verify(*result,expected);study::verify(left,a);study::verify(right,z);
    auto self=write_arch::multimap_rebuild::merge(left,left);auto doubled=a;
    for(auto& [key,bag]:doubled){const auto copy=bag;bag.insert(bag.end(),copy.begin(),copy.end());}
    study::verify(*self,doubled);
    auto hashed=write_arch::multimap_rebuild::merge<false>(left,right);study::verify(*hashed,expected);
    auto consuming_left=std::make_unique<MM>(713),consuming_right=std::make_unique<MM>(713);
    for(const auto& [key,bag]:a)for(const auto& value:bag)consuming_left->insert(key,value);
    for(const auto& [key,bag]:z)for(const auto& value:bag)consuming_right->insert(key,value);
    std::map<word,const ht::byte*> unique_addresses;
    write_arch::multimap_rebuild::visit_bags(*consuming_left,[&](word key,std::span<const ht::byte> bag){
        if(!z.contains(key)&&bag.size()>2*V)unique_addresses[key]=bag.data();});
    write_arch::multimap_rebuild::visit_bags(*consuming_right,[&](word key,std::span<const ht::byte> bag){
        if(!a.contains(key)&&bag.size()>2*V)unique_addresses[key]=bag.data();});
    auto consumed=write_arch::multimap_rebuild::merge_consuming<true,MM_MERGE_REUSE_FITTING,MM_MERGE_BULK_JOIN,MM_MERGE_KEEP_BUCKETS,MM_MERGE_REUSE_RIGHT,MM_MERGE_LIVE_CELLS>(consuming_left,consuming_right);
    ht::require(!consuming_left&&!consuming_right,"consuming merge clears both sources");study::verify(*consumed,expected);
    write_arch::multimap_rebuild::visit_bags(*consumed,[&](word key,std::span<const ht::byte> bag){
        if(unique_addresses.contains(key))ht::require(bag.data()==unique_addresses[key],"unique worker payload address survives merge");});
    auto consumed_expected=expected;
    for(auto it=consumed_expected.begin();it!=consumed_expected.end();){consumed->erase(it->first);it=consumed_expected.erase(it);if(it!=consumed_expected.end())++it;}
    for(auto& [key,bag]:consumed_expected)for(unsigned rank=0;rank<33;++rank){auto v=study::payload<V>(key,5000+rank);consumed->insert(key,v);bag.push_back(v);}
    for(unsigned i=0;i<std::min(word{128},word{1}<<std::min(K,10u));++i){const auto key=mix.recover(7,i);
        for(unsigned rank=0;rank<lengths[i%9];++rank){auto v=study::payload<V>(key,6000+rank);consumed->insert(key,v);consumed_expected[key].push_back(v);}}
    study::verify(*consumed,consumed_expected);
    for(const auto& [key,bag]:consumed_expected){(void)bag;consumed->erase(key);}consumed_expected.clear();
    study::verify(*consumed,consumed_expected);ht::require(consumed->live_buffers()==0,"consumed pool full release");
    for(const auto& [key,bag]:a)for(const auto& v:bag)consumed->insert(key,v);
    study::verify(*consumed,a);
    for(auto& [key,bag]:expected){auto v=study::payload<V>(key,4000);result->insert(key,v);bag.push_back(v);}
    study::verify(*result,expected);study::verify(left,a);study::verify(right,z);
    for(auto it=expected.begin();it!=expected.end();){result->erase(it->first);it=expected.erase(it);if(it!=expected.end())++it;}
    study::verify(*result,expected);study::verify(left,a);study::verify(right,z);
    MM full(713),one(713);const auto key=mix.recover(0,0);constexpr unsigned maximum=65535/V;
    for(unsigned rank=0;rank<maximum;++rank)full.insert(key,study::payload<V>(key,rank));
    one.insert(key,study::payload<V>(key,maximum));bool refused=false;
    try{auto bad=write_arch::multimap_rebuild::merge(full,one);(void)bad;}catch(const std::runtime_error&){refused=true;}
    ht::require(refused,"oversized merged bag refuses");full.validate();one.validate();
    unsigned count=0;full.find(key,[&](const auto& value){ht::require(value==study::payload<V>(key,count++),"refused merge source unchanged");});
    ht::require(count==maximum,"refused merge source length");
    auto full_owned=std::make_unique<MM>(713),one_owned=std::make_unique<MM>(713);
    std::map<word,std::vector<ht::bytes<V>>> full_values,one_value;
    for(unsigned rank=0;rank<maximum;++rank){auto v=study::payload<V>(key,rank);full_owned->insert(key,v);full_values[key].push_back(v);}
    auto last=study::payload<V>(key,maximum);one_owned->insert(key,last);one_value[key].push_back(last);refused=false;
    try{auto bad=write_arch::multimap_rebuild::merge_consuming<true,MM_MERGE_REUSE_FITTING,MM_MERGE_BULK_JOIN,MM_MERGE_KEEP_BUCKETS,MM_MERGE_REUSE_RIGHT,MM_MERGE_LIVE_CELLS>(full_owned,one_owned);(void)bad;}catch(const std::runtime_error&){refused=true;}
    ht::require(refused&&full_owned&&one_owned,"failed consuming merge preserves owners");study::verify(*full_owned,full_values);study::verify(*one_owned,one_value);
    // Bucket 0 prepares a duplicate buffer before bucket 1 refuses. This
    // exercises destruction of prepared state while both inputs stay intact.
    for(const auto counts:std::array<std::array<unsigned,2>,4>{{{3,1},{3,3},{1,3},{3,5}}}){
    auto early=std::make_unique<MM>(713),late=std::make_unique<MM>(713);
    std::map<word,std::vector<ht::bytes<V>>> early_values,late_values;
    const auto first=mix.recover(0,0),bad=mix.recover(1,0);
    for(unsigned rank=0;rank<counts[0];++rank){auto v=study::payload<V>(first,rank);early->insert(first,v);early_values[first].push_back(v);}
    for(unsigned rank=0;rank<counts[1];++rank){auto v=study::payload<V>(first,100+rank);late->insert(first,v);late_values[first].push_back(v);}
    for(unsigned rank=0;rank<maximum;++rank){auto v=study::payload<V>(bad,rank);early->insert(bad,v);early_values[bad].push_back(v);}
    auto overflow_value=study::payload<V>(bad,maximum);late->insert(bad,overflow_value);late_values[bad].push_back(overflow_value);refused=false;
    const auto early_before=write_arch::multimap_probe::snapshot(*early),late_before=write_arch::multimap_probe::snapshot(*late);
    try{auto bad_result=write_arch::multimap_rebuild::merge_consuming<true,MM_MERGE_REUSE_FITTING,MM_MERGE_BULK_JOIN,MM_MERGE_KEEP_BUCKETS,MM_MERGE_REUSE_RIGHT,MM_MERGE_LIVE_CELLS>(early,late);(void)bad_result;}catch(const std::runtime_error&){refused=true;}
    ht::require(refused&&early&&late,"late consuming failure retains owners");study::verify(*early,early_values);study::verify(*late,late_values);
    ht::require(early_before==write_arch::multimap_probe::snapshot(*early)&&late_before==write_arch::multimap_probe::snapshot(*late),"late failure preserves full cells and assigned capacity including slack");
    }
    // Unequal worker bags cover fitting appends and mixed inline/external
    // combinations independently of the overlapping population above.
    for(const auto lengths:std::array<std::array<unsigned,2>,8>{{{3,1},{5,3},{1,3},{1,1},{3,5},{2,5},{7,9},{9,17}}}){
        MM a(713),z(713);std::map<word,std::vector<ht::bytes<V>>> aa,zz;
        for(unsigned i=0;i<lengths[0];++i){auto v=study::payload<V>(key,i);a.insert(key,v);aa[key].push_back(v);}
        for(unsigned i=0;i<lengths[1];++i){auto v=study::payload<V>(key,100+i);z.insert(key,v);zz[key].push_back(v);}
        auto wanted=aa;wanted[key].insert(wanted[key].end(),zz[key].begin(),zz[key].end());
        auto joined=write_arch::multimap_rebuild::merge(a,z);study::verify(*joined,wanted);study::verify(a,aa);study::verify(z,zz);
        auto ca=std::make_unique<MM>(713),cz=std::make_unique<MM>(713);
        for(const auto& value:aa[key])ca->insert(key,value);for(const auto& value:zz[key])cz->insert(key,value);
        const ht::byte* old_payload=nullptr;const ht::byte* right_payload=nullptr;
        write_arch::multimap_rebuild::visit_bags(*ca,[&](word,std::span<const ht::byte> bag){old_payload=bag.data();});
        write_arch::multimap_rebuild::visit_bags(*cz,[&](word,std::span<const ht::byte> bag){right_payload=bag.data();});
        auto cj=write_arch::multimap_rebuild::merge_consuming<true,MM_MERGE_REUSE_FITTING,MM_MERGE_BULK_JOIN,MM_MERGE_KEEP_BUCKETS,MM_MERGE_REUSE_RIGHT,MM_MERGE_LIVE_CELLS>(ca,cz);study::verify(*cj,wanted);
        constexpr unsigned inline_values=V==8?2:1;
        const bool left_fits=MM_MERGE_REUSE_FITTING&&lengths[0]>inline_values&&std::bit_ceil(lengths[0])>=lengths[0]+lengths[1];
        const bool right_fits=MM_MERGE_REUSE_RIGHT&&!left_fits&&lengths[1]>inline_values&&std::bit_ceil(lengths[1])>=lengths[0]+lengths[1];
        if(left_fits||right_fits)write_arch::multimap_rebuild::visit_bags(*cj,[&](word,std::span<const ht::byte> bag){
            ht::require(bag.data()==(left_fits?old_payload:right_payload),"fitting merged reservation keeps selected payload address");});
    }
    if constexpr(MM_MERGE_REUSE_RIGHT){
        // Three right reservations share one block: keep one by right reuse,
        // release one after left reuse, and transfer one untouched. A new bag
        // must reuse the released rank without damaging either survivor.
        auto l=std::make_unique<MM>(713),r=std::make_unique<MM>(713);
        std::map<word,std::vector<ht::bytes<V>>> expected;
        const word ka=mix.recover(0,0),kb=mix.recover(0,1),kc=mix.recover(0,2),kd=mix.recover(0,3);
        auto fill=[&](auto& owner,word k,unsigned n,unsigned salt){
            for(unsigned i=0;i<n;++i){auto v=study::payload<V>(k,salt+i);owner.insert(k,v);expected[k].push_back(v);}
        };
        fill(*l,ka,3,0);fill(*l,kb,9,0);
        fill(*r,ka,5,100);fill(*r,kb,5,100);fill(*r,kc,5,100);
        const auto refs=write_arch::multimap_probe::references(*r);
        ht::require(refs.size()==3&&(*refs.begin()>>8)==(*refs.rbegin()>>8),"right-reuse fixture shares a block");
        std::map<word,const ht::byte*> la,ra;
        write_arch::multimap_rebuild::visit_bags(*l,[&](word k,std::span<const ht::byte> bag){la[k]=bag.data();});
        write_arch::multimap_rebuild::visit_bags(*r,[&](word k,std::span<const ht::byte> bag){ra[k]=bag.data();});
        auto joined=write_arch::multimap_rebuild::merge_consuming<true,true,true,MM_MERGE_KEEP_BUCKETS,true,MM_MERGE_LIVE_CELLS>(l,r);
        study::verify(*joined,expected);
        write_arch::multimap_rebuild::visit_bags(*joined,[&](word k,std::span<const ht::byte> bag){
            ht::require(bag.data()==(k==kb?la[k]:ra[k]),"mixed right block retains the chosen addresses");
        });
        fill(*joined,kd,5,200);study::verify(*joined,expected);
        write_arch::multimap_rebuild::visit_bags(*joined,[&](word k,std::span<const ht::byte> bag){
            if(k==kd)ht::require(bag.data()==ra[kb],"displaced right rank is reusable with surviving neighbours");
        });
        for(word k:{ka,kb,kd}){ht::require(joined->erase(k),"mixed reuse erase");expected.erase(k);}
        fill(*joined,kc,4,300);study::verify(*joined,expected);
    }
    std::cout<<"MM_MERGE_PASS,"<<W<<','<<V<<','<<overlap<<'\n';
}
#if MM_MERGE_KEEP_BUCKETS
template<unsigned V> void check_complete_domain(){
    namespace ht=ikea::hashtable;using MM=write_arch::multimap<4,4,V,false>;
    auto a=std::make_unique<MM>(7),z=std::make_unique<MM>(7);
    std::map<ht::word,std::vector<ht::bytes<V>>> expected;
    for(unsigned key=0;key<16;++key){auto v=study::payload<V>(key,0);a->insert(key,v);expected[key].push_back(v);}
    for(unsigned key=16;key-->0;){auto v=study::payload<V>(key,1);z->insert(key,v);expected[key].push_back(v);}
    auto result=write_arch::multimap_rebuild::merge_consuming<true,true,true,true,MM_MERGE_REUSE_RIGHT,MM_MERGE_LIVE_CELLS>(a,z);
    ht::require(!a&&!z,"full-domain merge consumes owners");study::verify(*result,expected);
    std::cout<<"MM_MERGE_PASS,4,"<<V<<",16\n";
}
#endif
int main(){check_merge<24,8>();check_merge<24,64>();check_merge<40,8>();check_merge<40,64>();
    check_merge<20,8>();check_merge<20,64>();check_merge<12,8>();check_merge<12,64>();
#if MM_MERGE_KEEP_BUCKETS
    check_complete_domain<8>();check_complete_domain<64>();
#endif
}
