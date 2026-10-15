#include "promoted.h"
#include <map>
using namespace ht;
namespace {
template<class Body> auto contents(const Body& body) {
    std::map<word,std::vector<word>> out;
    body.each([&](word k,word v){out[k].push_back(v);});return out;
}
template<unsigned K,class Bags=links<64,false,0,true>> void small_history() {
    pool_owner pool;using Small=small_table<K,container_kind::multimap,8,true,Bags,16>;
    Small body(*pool.pool,19);
    const auto first=body.set_value(0,~word(0));require(first.keys==1&&first.values==1,"first inline delta");
    const auto before=body.footprint();const auto allocations=oa::statistics(*pool.pool).allocations;
    std::array<small_event,32> events;small_journal journal{events};
    const auto second_result=body.set_value(0,~word(0),journal);require(bool(second_result),"observed second value");
    const auto second=*second_result;const auto after=body.footprint();
    for(unsigned i=0;i<journal.used;++i)require(events[i].type==small_event::kind::list_write ||
        events[i].type==small_event::kind::caller_delta,"second inline emits only record and caller stores");
    require(second.keys==0&&second.values==1&&body.value_count()==2,"second inline delta");
    require(before.requested==after.requested && before.metadata==after.metadata &&
        allocations==oa::statistics(*pool.pool).allocations,"second inline has no bag owner or allocation");
    body.each_record([](word,const auto& r){require(r.tag==2,"distinct two-inline state");});
    auto old=body.clone();require(bool(old),"retained two-inline clone");
    std::array<small_event,0> none;small_journal denied{none};
    require(!body.set_value(0,0,denied)&&body.value_count()==2,"spill observer refusal");
    journal.used=0;require(bool(body.set_value(0,0,journal)),"observed third value");
    bool bag_write=false,record_write=false;unsigned deltas=0;
    for(unsigned i=0;i<journal.used;++i) {
        bag_write|=events[i].type==small_event::kind::bag_write;
        if(events[i].type==small_event::kind::list_write){require(bag_write,"spill bag commit precedes record publication");record_write=true;}
        if(events[i].type==small_event::kind::caller_delta){require(record_write&&events[i].delta.keys==0&&events[i].delta.values==1,"single spill delta after record");++deltas;}
    }
    require(bag_write&&record_write&&deltas==1,"spill publishes bag, record and one value delta");
    body.each_record([](word,const auto& r){require(r.tag==1,"third value spills");});
    require(contents(body).at(0)==std::vector<word>({~word(0),~word(0),0}),"full-word duplicates survive three-value spill");
    for(word i=3;i<257;++i)body.set_value(0,mix(i));
    const std::array<word,4> keys{1,1,1,1},values{0,~word(0),~word(0),0};
    require(body.template setN<4>(keys,values,15)==15,"ordered packet crosses both inline and spill boundaries");
    unsigned seen=0;require(body.template getN<4>(keys,5,[&](unsigned lane,word v){
        require((lane==0||lane==2)&&v==values[seen%4],"packet exact duplicate values");++seen;})==5&&seen==8,"packet active lanes");
    require(contents(*old).at(0)==std::vector<word>({~word(0),~word(0)}),"retained two-inline bytes unchanged");
    auto delta=body.erase(1);require(delta.keys==-1&&delta.values==-4,"erase spilled count");
    body.set_value(1,17);body.set_value(1,18);delta=body.erase(1);
    require(delta.keys==-1&&delta.values==-2,"erase two-inline count");
    delta=body.clear();require(delta.keys==-1&&delta.values==-257,"clear values across link growth");
    require(contents(*old).at(0).size()==2,"retained clone survives source clear");
}
template<unsigned K,record_format Format> void hierarchy() {
    pool_owner pool;using Small=small_table<K,container_kind::multimap,8,true,links<64,false,0,true>,16>;
    using Body=promoted_table<K,container_kind::multimap,8,1,0,64,64,Format,false,record_access::both,true,links<64,false,0,true>,16>;
    Small source(*pool.pool,19);std::map<word,std::vector<word>> expected;
    for(word i=0;i<32;++i)for(word j=0;j<1+i%5;++j){source.set_value(i,j);expected[i].push_back(j);}
    auto old=source.clone();require(bool(old),"pre-promotion clone");const auto retained=contents(*old);
    auto body=Body::promote(source,*pool.pool,41,73);require(bool(body)&&source.size()==0,"consume all inline states and bag refs");
    for(word i=32;i<240;++i)for(word j=0;j<1+i%3;++j){body->set_value(i,j);expected[i].push_back(j);}
    require(body->population()[0]&&body->population()[1]&&body->population()[2],"all three key tiers exercised");
    for(word i=0;i<240;i+=2){const auto delta=body->erase(i);require(delta.values==-std::int64_t(expected[i].size()),"hierarchy erase count");expected.erase(i);}
    for(word i=0;i<240;i+=2)for(word j=0;j<3;++j){body->set_value(i,~j);expected[i].push_back(~j);}
    body->validate();require(contents(*body)==expected,"all-tier swap, spill and reuse values");
    auto clone=body->clone();require(clone&&contents(*clone)==expected,"logical hierarchy clone preserves inline counts");
    body.reset();require(contents(*clone)==expected&&contents(*old)==retained,"retained clones survive hierarchy retirement");
}
void allocation_refusal() {
    pool_owner pool(1u<<20);using Small=small_table<40,container_kind::multimap,8,true,links<64,false,0,true>,16>;
    Small body(*pool.pool,19);body.set_value(7,19);body.set_value(7,23);
    std::vector<oa::Allocation> pressure;
    for(std::size_t bytes=4096;bytes>=8;bytes/=2)
        while(auto a=oa::allocate(*pool.pool,{.bytes=bytes,.alignment=8}))pressure.push_back(std::move(*a));
    no_small_effects effects;auto result=body.set_value(7,29,effects);
    require(!result&&body.size()==1&&body.value_count()==2,"spill allocation refusal keeps cardinality");
    require(contents(body).at(7)==std::vector<word>({19,23}),"spill refusal preserves both inline values");
    pressure.clear();auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*pool.pool,0).kind()!=orbital::Progress::Kind::done){require(clock_type::now()<deadline,"inline pressure trim");orbital::worker::poll(pool.lane,1024);}
    body.set_value(7,29);require(contents(body).at(7)==std::vector<word>({19,23,29}),"spill retry applies exactly once");
}
}
int main() {
    lanes<8>([](auto i){small_history<8*(i+1)>();hierarchy<8*(i+1),record_format::exact>();});
    small_history<40,buffer_bags<>>();
    hierarchy<40,record_format::tag_bits>();hierarchy<40,record_format::tag_plane>();
    allocation_refusal();std::puts("two-inline boundaries, all-key-width ownership/transition, packet order, tag planes and spill refusal passed");
}
