#include "hash.h"
#include <ikea/hashtable.h>
#include <cstring>
#include <memory>
namespace directory_spike {
struct PrefixHash {
    unsigned references{1};
    using Small=ikea::hashtable::table<32,2,64,30>;
    using Large=ikea::hashtable::table<32,8,64,24>;
    std::unique_ptr<Small> small;
    std::unique_ptr<Large> large;
    template<class F> decltype(auto) visit(F&& f)const {if(small)return f(*small);return f(*large);}
};
PrefixHash* make_hash(std::span<const unsigned> prefixes,std::span<const Dirent> descriptors,bool compact) {
    require(prefixes.size()==descriptors.size());auto* h=new PrefixHash;
    if(compact && prefixes.size()<=64)h->small=std::make_unique<PrefixHash::Small>(713);
    else h->large=std::make_unique<PrefixHash::Large>(713);
    for(unsigned i=0;i<prefixes.size();++i) {
        ikea::hashtable::bytes<64> b{};std::memcpy(b.data(),&descriptors[i],64);h->visit([&](auto& table){table.insert(prefixes[i],b);});
    }
    return h;
}
void retain_hash(PrefixHash* h) noexcept {if(h)++h->references;}
void release_hash(PrefixHash* h) noexcept {if(h && !--h->references)delete h;}
bool find_hash(const PrefixHash* h,unsigned prefix,Dirent& d) noexcept {
    return h && h->visit([&](const auto& table){return table.find(prefix,[&](const auto& b){std::memcpy(&d,b.data(),64);});});
}
std::size_t hash_bytes(const PrefixHash* h) noexcept {return h?h->visit([](const auto& table){return table.bytes_used();}):0;}
std::size_t hash_spills(const PrefixHash* h) noexcept {return h?h->visit([](const auto& table){return table.spill_size();}):0;}
}
