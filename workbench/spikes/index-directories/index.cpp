#include "index.h"
#include "hash.h"
#include <ikea/boundlist/search.h>
#include <ikea/streampack.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <limits>
#include <numeric>

namespace directory_spike {
namespace {
namespace sp=ikea::streampack;
const sp::layout& overflow_format() {
    static const auto format=[] {auto l=sp::layout::prepare({sp::geometry::direct,sp::placement::grouped,65535,256u*65535,{},true,true});require(bool(l));return *l;}();
    return format;
}
Image pack_overflow(std::span<const Selection> segments) {
    const auto& f=overflow_format();Image image;
    unsigned total=0;for(auto s:segments)total+=(s.part_count()-6)*8;
    image[0].resize(total);image[1].resize(f.directory_bytes());image[2].resize(f.lengths_bytes());image[3].resize(f.control_bytes());
    auto bound=sp::view::bind(f,{image[0],image[1],image[2],image[3]});require(bool(bound));
    ikea::no_coverage effects;require(bool(sp::initialize(*bound,effects)));
    for(auto s:segments) {
        const auto extras=s.part_count()-6;std::vector<u64> pointers(extras);
        for(unsigned i=0;i<extras;++i)pointers[i]=part_locator(s,6+i);
        const auto payload=std::as_bytes(std::span(pointers));
        require(bool(sp::append(*bound,payload,payload.size(),effects)));
    }
    require(bool(bound->validate()));image[0].resize(bound->extent());return image;
}
template<class T> void append(std::vector<std::byte>& b,const T& value) {
    const auto n=b.size();b.resize(n+sizeof(T));std::memcpy(b.data()+n,&value,sizeof(T));
}
template<class T> void repeated(std::map<T,unsigned>& values,T value,bool add) {
    if(add)++values[value];else {auto it=values.find(value);require(it!=values.end());if(!--it->second)values.erase(it);}
}
unsigned prefix(u64 physical,unsigned layer) {return layer ? physical>>(48-8*layer):0;}
std::uint16_t position(u64 physical,unsigned layer) {return physical>>(32-8*layer);}
unsigned boundary(std::span<const u64> maxima,u64 key,Search method,bool strict=false) {
    if(method==Search::ordinary)return (strict?std::upper_bound(maxima.begin(),maxima.end(),key):std::lower_bound(maxima.begin(),maxima.end(),key))-maxima.begin();
    const auto source=ikea::boundlist::direct_source<u64>{maxima.data(),unsigned(maxima.size())};
    return strict?ikea::boundlist::upper_bound(source,key):ikea::boundlist::lower_bound(source,key);
}
const std::byte* located_address(Selection s,const Dirent& d,u64 locator,unsigned p) {
    // Keep the raw-address control's original pointer-load path. Only the
    // indirect kind performs cell validation and selected-Part resolution.
    return d.flags&4?take(resolve_locator(s,locator,p)):reinterpret_cast<const std::byte*>(locator);
}
template<class T> std::span<const T> located(Selection s,const Dirent& d,unsigned p,unsigned count) {
    return {reinterpret_cast<const T*>(located_address(s,d,d.parts[p],p)),count};
}
Definition definition(unsigned id,unsigned width,unsigned chunk=0) {return {id,width,id<=3?31u:((1u<<(id%5))|(1u<<((id+1)%5))),unsigned(id%2==0),chunk};}
unsigned chunks(unsigned n,unsigned grain){return grain ? (n+grain-1)/grain:1;}
unsigned summary_part(unsigned chunk){return chunk?5+chunk:3;}
unsigned interpretation(Selection s,const Dirent& d,Selection schema,unsigned layer,unsigned key_bytes) {
    const auto c=s.control();require(c.representation==d.representation && c.revision==d.revision && (d.flags&1));
    require(bool(d.flags&4)==c.indirect_parts);
    const auto definitions=values<Definition>(schema,0);require(d.representation && d.representation<=definitions.size());
    const auto def=definitions[d.representation-1];
    require(def.id==d.representation && def.key_bytes==key_bytes && (def.allowed_layers&(1u<<layer)) && def.summary_chunk==c.summary_chunk);
    return def.recipe;
}
Dirent describe(Selection s) {
    require(bool(s));Dirent d;
    for(unsigned p=0;p<6;++p)d.parts[p]=part_locator(s,p);
    const auto c=s.control();d.representation=c.representation;d.revision=c.revision;d.flags=1|(c.parts>6?2:0)|(c.indirect_parts?4:0);return d;
}
Dirent dirent_at(Selection directory,unsigned prefix,unsigned rank) {
    const auto c=directory.control();
    const unsigned slot=c.layer<2 && !c.compact_table?prefix:rank;
    return values<Dirent>(directory,summary_part(c.table_row_chunk?slot/c.table_row_chunk:0))[c.table_row_chunk?slot%c.table_row_chunk:slot];
}
Dirent known_dirent(Selection directory,unsigned prefix) {
    const auto c=directory.control();
    if(c.layer<2) {
        const auto rank=c.compact_table?values<std::uint16_t>(directory,4)[prefix]:prefix;
        return rank==UINT16_MAX?Dirent{}:dirent_at(directory,prefix,rank);
    }
    Dirent d;if(find_hash(c.hash,prefix,d))return d;
    const auto keys=values<unsigned>(directory,0);const auto it=std::lower_bound(keys.begin(),keys.end(),prefix);
    return it==keys.end() || *it!=prefix?Dirent{}:values<Dirent>(directory,3)[it-keys.begin()];
}
template<class Resolve> PendingRead local_prepare(Selection s,const Dirent& d,u64 key,Search search,unsigned recipe,Resolve resolve) {
    const auto n=s.control().count;
    const auto maxima=located<u64>(s,d,2,n);const auto rank=boundary(maxima,key,search);
    if(rank==maxima.size())return {};
    return {resolve(rank),located<u64>(s,d,1,n)[rank],s.control().prefix,located<std::uint16_t>(s,d,0,n)[rank],recipe,d.overflow_block,d.overflow_rank,d.flags};
}
} // namespace
Summary interpret_summary(Summary s,unsigned recipe) {
    require(recipe<=1);
    if(recipe){s.minimum=std::bit_cast<i64>(~std::bit_cast<u64>(s.minimum));s.maximum=std::bit_cast<i64>(~std::bit_cast<u64>(s.maximum));s.sum=std::bit_cast<i64>(~std::bit_cast<u64>(s.sum));}
    return s;
}
Outcome finish_read(PendingRead p) {
    if(!p.body)return {};
    const auto s=interpret_summary(*p.body,p.recipe);
    return {true,p.logical,s.count,s.not_null,s.sum,p.prefix,p.position};
}
void EntryState::edit(const Row& row,bool add) {
    if(add)++count;else {require(count);--count;}
    repeated(logical,row.logical,add);
    if(row.valid) {if(add)++not_null;else --not_null;sum+=add?row.value:-row.value;repeated(field,row.value,add);}
}
Summary EntryState::summary() const {return {count,not_null,field.empty()?0:field.begin()->first,field.empty()?0:field.rbegin()->first,sum};}
Index::Index(Runtime& rt,std::pmr::memory_resource& memory,unsigned j,unsigned key_bytes,unsigned page_bits,unsigned auxiliary_parts,unsigned definitions,unsigned summary_chunk,bool compact_tables,bool directory_branches,unsigned table_row_chunk,bool packed_pages,bool compact_hashes,unsigned root_row_chunk,unsigned locator_mode)
    :rt_(rt),memory_(memory),j_(j),key_bytes_(key_bytes),page_bits_(page_bits),aux_parts_(auxiliary_parts),definitions_(definitions),summary_chunk_(summary_chunk),compact_tables_(compact_tables),directory_branches_(directory_branches),packed_pages_(packed_pages),compact_hashes_(compact_hashes),indirect_parts_(locator_mode!=0),table_row_chunk_(table_row_chunk),root_row_chunk_(root_row_chunk),locator_mode_(locator_mode),store(memory) {require(locator_mode<=2 && j<=6 && key_bytes>=8 && page_bits<=8 && auxiliary_parts<=4 && definitions>=3 && (!summary_chunk || summary_chunk>=64) && (!directory_branches || page_bits) && (!packed_pages || (page_bits && !directory_branches)) && (!table_row_chunk || table_row_chunk<=256) && root_row_chunk<=256 && (!root_row_chunk || !directory_branches));}
void Index::edit_model(const Row& row,bool add,std::set<SegmentKey>& affected) {
    require(row.physical<(u64(1)<<48));
    if(j_)require((row.physical>>(48-8*j_))==(row.logical>>(64-8*j_)));
    for(unsigned layer=0;layer<5;++layer) {
        const SegmentKey key{layer,prefix(row.physical,layer)};affected.insert(key);
        auto& segment=segments_[key];const auto pos=position(row.physical,layer);
        dirty_positions_[key].insert(pos);
        auto& entry=segment[pos];entry.edit(row,add);
        if(!entry.count)segment.erase(pos);
        if(segment.empty())segments_.erase(key);
    }
}
Image Index::segment_image(SegmentKey key,unsigned representation) const {
    Image image;const auto& entries=segments_.at(key);unsigned rank=0;
    for(const auto& [pos,state]:entries) {
        append(image[0],pos);append(image[1],state.logical.begin()->first);append(image[2],state.logical.rbegin()->first);
        append(image[summary_part(summary_chunk_?rank/summary_chunk_:0)],interpret_summary(state.summary(),definition(representation,key_bytes_).recipe));
        // Long-key fixture: big-endian logical value followed by a common suffix.
        // All bytes are stored/read; bounds on the first eight are exact in THIS
        // fixture only. It is not a generic logical-key abbreviation policy.
        for(unsigned k=0;k<key_bytes_;++k)image[4].push_back(std::byte(k<8 ? (state.logical.begin()->first>>(56-k*8))&255:0x61));
        append(image[5],u64(state.count*17));++rank;
    }
    const auto first=5+chunks(entries.size(),summary_chunk_);
    for(unsigned p=first;p<first+aux_parts_;++p)append(image[p],u64(key.second)*71+p+entries.size());
    return image;
}
EditStats Index::edit(std::span<const Row> additions,std::span<const u64> removals,bool selective) {
    using Clock=std::chrono::steady_clock;
    auto elapsed=[](auto start){return u64(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());};
    const auto model_start=Clock::now();
    Store::Scope basis(store);EditStats stats;std::set<SegmentKey> affected;
    dirty_positions_.clear();
    const bool same_positions=removals.empty() && std::ranges::all_of(additions,[&](auto row) {
        const auto old=rows_.find(row.id);return old!=rows_.end() && old->second.physical==row.physical && old->second.logical==row.logical;
    });
    const bool value_only=selective && same_positions;
    for(auto id:removals) {const auto old=rows_.at(id);edit_model(old,false,affected);rows_.erase(id);}
    for(const auto& row:additions) {
        if(auto old=rows_.find(row.id);old!=rows_.end())edit_model(old->second,false,affected);
        rows_[row.id]=row;edit_model(row,true,affected);
    }
    ++generation_;
    stats.model_ns=elapsed(model_start);
    std::vector<Store::Change> changes;changes.reserve(affected.size()+6);
    std::map<SegmentKey,Selection> private_segments;
    std::set<SegmentKey> directory_changes;
    std::array<std::set<unsigned>,5> dirty_overflow;
    for(const auto key:affected) {
        const auto id=segment_id(key.first,key.second);auto old=basis.bind(id);
        if(!segments_.contains(key)) {changes.push_back({id,{}});directory_entries_[key.first].erase(key.second);directory_changes.insert(key);++stats.removed_segments;continue;}
        unsigned rep=1+(u64(key.second)*991+generation_+key.first)%definitions_;
        while(!(definition(rep,key_bytes_).allowed_layers&(1u<<key.first)))rep=rep%definitions_+1;
        if(old && same_positions)rep=old.control().representation;
        const auto image_start=Clock::now();
        Image image;
        if(value_only) {
            const auto new_recipe=definition(rep,key_bytes_).recipe;
            const auto positions=values<std::uint16_t>(old,0);
            for(auto pos:dirty_positions_.at(key)) {
                const unsigned rank=std::lower_bound(positions.begin(),positions.end(),pos)-positions.begin();
                const auto chunk=summary_chunk_ ? rank/summary_chunk_:0,part=summary_part(chunk);
                if(!image.supplied(part)){const auto old_body=bytes(old,part);image[part].assign(old_body.begin(),old_body.end());}
                const auto s=interpret_summary(segments_.at(key).at(pos).summary(),new_recipe);
                std::memcpy(image[part].data()+(summary_chunk_?rank%summary_chunk_:rank)*sizeof(Summary),&s,sizeof(s));
            }
        } else image=segment_image(key,rep);
        stats.segment_image_ns+=elapsed(image_start);
        Control c{&memory_,Kind::segment,key.first,key.second,unsigned(segments_.at(key).size()),
            rep,generation_,0,5+chunks(segments_.at(key).size(),summary_chunk_)+aux_parts_,key_bytes_};
        c.summary_chunk=summary_chunk_;
        c.indirect_parts=indirect_parts_;c.compact_locators=locator_mode_==2;
        // This fixture field checks locator/interpretation compatibility, not
        // the Object's MVCC cut. A value-only version can reuse indirect cells.
        if(indirect_parts_ && old && same_positions)c.revision=old.control().revision;
        stats.summary_entries+=c.count;
        const auto capture_start=Clock::now();
        auto owner=capture(rt_,store,c,image,old,selective,stats,value_only);stats.segment_capture_ns+=elapsed(capture_start);
        private_segments[key]=Physical::selection(owner.get());
        auto selected=Physical::selection(owner.get());auto d=describe(selected);
        auto& entries=directory_entries_[key.first];
        if(auto it=entries.find(key.second);it!=entries.end()){d.overflow_block=it->second.descriptor.overflow_block;d.overflow_rank=it->second.descriptor.overflow_rank;}
        const DirectoryEntry next{d,values<u64>(selected,1).front(),values<u64>(selected,2).back()};
        const auto prior=entries.find(key.second);
        if(prior==entries.end() || std::memcmp(&prior->second.descriptor,&d,sizeof(d)) || prior->second.minimum!=next.minimum || prior->second.maximum!=next.maximum)
            directory_changes.insert(key);
        entries[key.second]=next;
        for(unsigned p=6;p<selected.part_count();++p)if(!old || p>=old.part_count() || part_locator(selected,p)!=part_locator(old,p))dirty_overflow[key.first].insert(key.second);
        changes.push_back({id,std::move(owner)});if(!old)++stats.new_segments;
    }
    {const auto start=Clock::now();dirty_positions_.clear();stats.model_ns+=elapsed(start);}
    const auto directory_start=Clock::now();
    for(unsigned layer=0;layer<5;++layer) {
        if(std::ranges::none_of(directory_changes,[&](auto k){return k.first==layer;}) && dirty_overflow[layer].empty())continue;
        auto& entries=directory_entries_[layer];
        const auto root=basis.bind(directory_id(layer));
        std::set<unsigned> changed_pages;
        if(page_bits_ && layer>=2)for(auto k:affected)if(k.first==layer)changed_pages.insert(k.second>>page_bits_);
        if(aux_parts_ || summary_chunk_) {
            auto& prefixes=overflow_prefixes_[layer];std::set<unsigned> dirty_blocks;
            if(!value_only) {
                const auto old_segments=prefixes.size();
                prefixes.clear();prefixes.reserve(entries.size());unsigned rank=0;
                for(auto& [p,e]:entries) {
                    e.descriptor.overflow_block=rank/256;e.descriptor.overflow_rank=rank%256;
                    prefixes.push_back(p);dirty_blocks.insert(rank/256);++rank;
                    if(page_bits_ && layer>=2)changed_pages.insert(p>>page_bits_);
                }
                for(unsigned block=(prefixes.size()+255)/256;block<(old_segments+255)/256;++block)
                    if(basis.bind(overflow_id(layer,block)))changes.push_back({overflow_id(layer,block),{}});
            } else for(auto p:dirty_overflow[layer])dirty_blocks.insert(entries.at(p).descriptor.overflow_block);
            for(auto block:dirty_blocks) {
                std::vector<Selection> selected;
                for(unsigned r=block*256;r<std::min<unsigned>(prefixes.size(),(block+1)*256);++r) {
                    const SegmentKey key{layer,prefixes[r]};selected.push_back(private_segments.contains(key)?private_segments.at(key):basis.bind(segment_id(layer,key.second)));
                }
                const auto old=basis.bind(overflow_id(layer,block));
                if(std::ranges::none_of(selected,[](auto s){return s.part_count()>6;})) {
                    if(old)changes.push_back({overflow_id(layer,block),{}});continue;
                }
                const auto image=pack_overflow(selected);Control c{&memory_,Kind::overflow,layer,block,unsigned(selected.size()),1,generation_,0,4,key_bytes_};
                changes.push_back({overflow_id(layer,block),capture(rt_,store,c,image,old,selective,stats,value_only && old)});
            }
        }
        auto build_directory=[&](Kind kind,unsigned prefix,auto first,auto last,unsigned count,Selection old) {
            const bool table=kind==Kind::directory && layer<2;
            const bool compact=table && layer==1 && compact_tables_;
            const bool root_pages=kind==Kind::directory && page_bits_ && layer>=2;
            const bool bounds_changed=!value_only || !old;
            Image image;
            const unsigned capacity=table && !compact?(layer?256:1):count;
            const unsigned row_grain=table?table_row_chunk_:root_pages?root_row_chunk_:0;
            const unsigned row_chunks=chunks(capacity,row_grain);
            const bool delta_rows=row_grain && value_only && old;
            if(!delta_rows)for(unsigned chunk=0;chunk<row_chunks;++chunk)
                image[summary_part(chunk)].resize(std::size_t(row_grain?std::min(row_grain,capacity-chunk*row_grain):capacity)*sizeof(Dirent));
            if(compact && bounds_changed){image[4].resize(256*sizeof(std::uint16_t),std::byte{255});}
            if(bounds_changed){image[0].resize(count*sizeof(unsigned));image[1].resize(count*8);image[2].resize(count*8);}
            std::vector<unsigned> hash_keys;std::vector<Dirent> hash_rows;
            const bool hashed=!root_pages && kind!=Kind::directory_branch && layer>1 && layer<=j_;
            if(hashed){hash_keys.reserve(count);hash_rows.reserve(count);}
            unsigned rank=0;
            for(auto it=first;it!=last;++it,++rank) {
                const auto p=it->first;const auto& e=it->second;
                const unsigned slot=table && !compact?p:rank,chunk=row_grain?slot/row_grain:0,part=summary_part(chunk);
                const bool dirty_row=!delta_rows || (table?affected.contains({layer,p}):changed_pages.contains(p));
                if(dirty_row) {
                    if(delta_rows && !image.supplied(part)){const auto prior=bytes(old,part);image[part].assign(prior.begin(),prior.end());}
                    std::memcpy(image[part].data()+(row_grain?slot%row_grain:slot)*sizeof(Dirent),&e.descriptor,sizeof(Dirent));
                }
                if(compact && bounds_changed){const auto r=std::uint16_t(rank);std::memcpy(image[4].data()+p*sizeof(r),&r,sizeof(r));}
                if(bounds_changed){std::memcpy(image[0].data()+rank*sizeof(p),&p,sizeof(p));std::memcpy(image[1].data()+rank*8,&e.minimum,8);std::memcpy(image[2].data()+rank*8,&e.maximum,8);}
                if(hashed){hash_keys.push_back(p);hash_rows.push_back(e.descriptor);}
            }
            require(rank==count);
            Control c{&memory_,kind,layer,prefix,count,1,generation_,0,5+row_chunks,key_bytes_};
            c.compact_table=compact;
            c.table_row_chunk=row_grain;
            if(hashed){c.hash=make_hash(hash_keys,hash_rows,compact_hashes_);stats.hash_created_bytes+=hash_bytes(c.hash);}
            auto owner=capture(rt_,store,c,image,old,selective,stats,value_only && old);release_hash(c.hash);
            if(bounds_changed)++stats.bound_repairs;
            return owner;
        };
        if(page_bits_ && layer>=2) {
            if(packed_pages_) {
                // Occupied-rank partition control. Value edits touch one page;
                // structural edits repartition the roster and retire old page
                // IDs. This is not a stable split/merge implementation.
                changed_pages.clear();
                if(value_only) {
                    for(auto key:affected)if(key.first==layer) {
                        auto page=page_entries_[layer].upper_bound(key.second);require(page!=page_entries_[layer].begin());
                        changed_pages.insert(std::prev(page)->first);
                    }
                } else {
                    std::set<unsigned> current;
                    for(auto it=entries.begin();it!=entries.end();) {
                        current.insert(it->first);for(unsigned n=0;n<(1u<<page_bits_) && it!=entries.end();++n)++it;
                    }
                    for(auto it=page_entries_[layer].begin();it!=page_entries_[layer].end();) {
                        if(!current.contains(it->first)){changes.push_back({page_id(layer,it->first),{}});it=page_entries_[layer].erase(it);}else ++it;
                    }
                    changed_pages=std::move(current);
                }
            }
            for(auto page:changed_pages) {
                const auto low=u64(page)<<page_bits_,high=low+(u64(1)<<page_bits_);
                auto first=entries.lower_bound(unsigned(low));auto last=high>(u64(1)<<32)-1?entries.end():entries.lower_bound(unsigned(high));
                if(packed_pages_) {
                    first=entries.lower_bound(page);last=first;
                    for(unsigned n=0;n<(1u<<page_bits_) && last!=entries.end();++n)++last;
                }
                if(first==last) {
                    if(page_entries_[layer].erase(page))changes.push_back({page_id(layer,page),{}});
                    continue;
                }
                const auto count=unsigned(std::distance(first,last));
                auto owner=build_directory(Kind::directory_page,page,first,last,count,basis.bind(page_id(layer,page)));
                const auto s=Physical::selection(owner.get());page_entries_[layer][page]={describe(s),first->second.minimum,std::prev(last)->second.maximum};
                changes.push_back({page_id(layer,page),std::move(owner)});
            }
            auto& pages=page_entries_[layer];
            if(directory_branches_) {
                std::set<unsigned> dirty;for(auto p:changed_pages)dirty.insert(p>>page_bits_);
                auto& branches=branch_entries_[layer];
                for(auto b:dirty) {
                    auto first=pages.lower_bound(b<<page_bits_),last=pages.lower_bound((b+1)<<page_bits_);
                    if(first==last){if(branches.erase(b))changes.push_back({branch_id(layer,b),{}});continue;}
                    auto owner=build_directory(Kind::directory_branch,b,first,last,std::distance(first,last),basis.bind(branch_id(layer,b)));
                    branches[b]={describe(Physical::selection(owner.get())),first->second.minimum,std::prev(last)->second.maximum};
                    changes.push_back({branch_id(layer,b),std::move(owner)});
                }
                if(branches.empty()){if(root)changes.push_back({directory_id(layer),{}});}
                else changes.push_back({directory_id(layer),build_directory(Kind::directory,0,branches.begin(),branches.end(),branches.size(),root)});
            } else {
                if(pages.empty()){if(root)changes.push_back({directory_id(layer),{}});}
                else changes.push_back({directory_id(layer),build_directory(Kind::directory,0,pages.begin(),pages.end(),pages.size(),root)});
            }
        } else {
            if(entries.empty()){if(root)changes.push_back({directory_id(layer),{}});}
            else changes.push_back({directory_id(layer),build_directory(Kind::directory,0,entries.begin(),entries.end(),entries.size(),root)});
        }
    }
    // Explicit schema Object, including historical representations and per-layer
    // admission. Readers bind it under the same Scope as directory and segments.
    if(!basis.bind(schema_id())) {
        Control c{&memory_,Kind::schema,0,0,definitions_,1,1,0,6,key_bytes_};
        changes.push_back({schema_id(),capture(rt_,store,c,schema_image(),{},false,stats)});
    }
    stats.directory_ns=elapsed(directory_start);
    if(!changes.empty()) {
        Store::Prepared packet(store);auto start=Clock::now();
        take(store.prepare(basis,changes,packet));stats.prepare_ns=elapsed(start);
        start=Clock::now();take(store.install(packet));stats.install_ns=elapsed(start);
    }
    return stats;
}
Image Index::schema_image()const {
    Image image;for(unsigned rep=1;rep<=definitions_;++rep)append(image[0],definition(rep,key_bytes_,summary_chunk_));
    const auto wire=overflow_format().encode();image[1].assign(wire.begin(),wire.end());return image;
}
void Index::extend_schema(unsigned definitions) {
    require(definitions>=definitions_);Store::Scope basis(store);definitions_=definitions;
    Control c{&memory_,Kind::schema,0,0,definitions_,1,++generation_,0,6,key_bytes_};CopyStats stats;
    const std::array<Store::Change,1> changes{{{schema_id(),capture(rt_,store,c,schema_image(),basis.bind(schema_id()),true,stats)}}};
    publish(store,basis,changes);
}
Selection Index::selected_directory(Store::Scope& scope,unsigned layer,u64 key,Search search) const {
    const auto root=scope.bind(directory_id(layer));
    if(!root || !page_bits_ || layer<2)return root;
    auto maxima=values<u64>(root,2);auto prefixes=values<unsigned>(root,0);std::span<const Dirent> rows;
    const unsigned depth=directory_branches_?2:1;
    for(unsigned step=0;step<depth;++step) {
        const auto rank=boundary(maxima,key,search);if(rank==maxima.size())return {};
        const auto selected=scope.bind(step+1<depth?branch_id(layer,prefixes[rank]):page_id(layer,prefixes[rank]));require(bool(selected));
        const auto c=selected.control();const auto d=step?rows[rank]:dirent_at(root,prefixes[rank],rank);require(c.revision==d.revision);
        if(step+1==depth)return selected;
        maxima=located<u64>(selected,d,2,c.count);prefixes=located<unsigned>(selected,d,0,c.count);rows=located<Dirent>(selected,d,3,c.count);
    }
    return {};
}
Selection Index::physical_directory(Store::Scope& scope,unsigned layer,unsigned prefix)const {
    const auto root=scope.bind(directory_id(layer));if(!root || !page_bits_ || layer<2)return root;
    if(!packed_pages_)return scope.bind(page_id(layer,prefix>>page_bits_));
    const auto keys=values<unsigned>(root,0);auto it=std::upper_bound(keys.begin(),keys.end(),prefix);
    return it==keys.begin()?Selection{}:scope.bind(page_id(layer,*std::prev(it)));
}
std::vector<Selection> Index::directory_pages(Store::Scope& scope,unsigned layer,u64 low,u64 high,Search search,bool lower_closed,bool upper_closed)const {
    const auto root=scope.bind(directory_id(layer));if(!root)return {};
    if(!page_bits_ || layer<2)return {root};
    auto children=[&](Selection parent,bool branches) {
        const auto maxima=values<u64>(parent,2),minima=values<u64>(parent,1);const auto prefixes=values<unsigned>(parent,0);
        std::vector<Selection> result;
        for(unsigned r=boundary(maxima,low,search,!lower_closed);r<maxima.size() && (upper_closed?minima[r]<=high:minima[r]<high);++r) {
            const auto selected=scope.bind(branches?branch_id(layer,prefixes[r]):page_id(layer,prefixes[r]));require(bool(selected));result.push_back(selected);
        }
        return result;
    };
    if(!directory_branches_)return children(root,false);
    std::vector<Selection> result;
    for(auto branch:children(root,true)){auto pages=children(branch,false);result.insert(result.end(),pages.begin(),pages.end());}
    return result;
}
Outcome Index::auxiliary(Store::Scope& scope,unsigned layer,const Dirent& d,Outcome outcome,unsigned parts) const {
    require(parts<=aux_parts_);
    if(!parts || !outcome.present)return outcome;
    const auto s=scope.bind(segment_id(layer,outcome.prefix));const auto c=s.control();
    const unsigned first=chunks(c.count,c.summary_chunk)-1;
    for(unsigned p=0;p<parts;++p)outcome.auxiliary+=*reinterpret_cast<const u64*>(located_address(s,d,extra_locator(scope,layer,d,first+p),6+first+p));
    return outcome;
}
u64 Index::extra_locator(Store::Scope& scope,unsigned layer,const Dirent& d,unsigned ordinal)const {
    require(d.flags&2);const auto selected=scope.bind(overflow_id(layer,d.overflow_block));require(bool(selected));
    const auto f=sp::const_view::assume_valid(overflow_format(),{bytes(selected,0),bytes(selected,1),bytes(selected,2),bytes(selected,3)});
    auto value=sp::value(f,d.overflow_rank);require(bool(value) && value->size()>=(ordinal+1)*8);
    u64 address;std::memcpy(&address,value->data()+ordinal*8,8);return address;
}
const Summary* Index::summary_address(Store::Scope& scope,unsigned layer,const Dirent& d,Selection s,unsigned rank)const {
    const auto c=s.control();
    require(rank<c.count);const auto grain=c.summary_chunk?c.summary_chunk:c.count,chunk=rank/grain;
    const auto address=chunk?extra_locator(scope,layer,d,chunk-1):d.parts[3];
    return reinterpret_cast<const Summary*>(located_address(s,d,address,summary_part(chunk)))+rank%grain;
}
PendingRead Index::prepare(Store::Scope& scope,unsigned layer,u64 key,Search search) const {
    require(layer<5);
    const auto schema=scope.bind(schema_id());if(!schema)return {};
    const auto directory=selected_directory(scope,layer,key,search);if(!directory)return {};
    const auto maxima=values<u64>(directory,2);
    const auto prefixes=values<unsigned>(directory,0);
    // Known shared prefixes permit direct table/hash routing. Missing prefixes,
    // and queries beyond a present segment's maximum, still owe logical seek.
    if(search==Search::boundlist && (layer==0 || layer<=j_)) {
        const unsigned p=layer ? key>>(64-8*layer):0;Dirent d;
        d=known_dirent(directory,p);const bool found=d.flags&1;
        if(found) {
            const auto segment=scope.bind(segment_id(layer,p));require(bool(segment));
            const auto recipe=interpretation(segment,d,schema,layer,key_bytes_);
            auto answer=local_prepare(segment,d,key,search,recipe,[&](unsigned r){return summary_address(scope,layer,d,segment,r);});if(answer.body)return answer;
        }
    }
    const auto rank=boundary(maxima,key,search);if(rank==maxima.size())return {};
    const auto p=prefixes[rank];
    const auto d=dirent_at(directory,p,rank);require(d.flags&1);
    const auto segment=scope.bind(segment_id(layer,p));require(bool(segment));
    const auto recipe=interpretation(segment,d,schema,layer,key_bytes_);
    return local_prepare(segment,d,key,search,recipe,[&](unsigned r){return summary_address(scope,layer,d,segment,r);});
}
Outcome Index::seek(Store::Scope& scope,unsigned layer,u64 key,Search search,unsigned auxiliary_parts)const {
    const auto p=prepare(scope,layer,key,search);Dirent d;
    d.flags=p.flags;d.overflow_rank=p.overflow_rank;d.overflow_block=p.overflow_block;
    return auxiliary(scope,layer,d,finish_read(p),auxiliary_parts);
}
std::vector<Outcome> Index::range(Store::Scope& scope,unsigned layer,u64 low,u64 high,Search search,bool lower_closed,bool upper_closed) const {
    std::vector<Outcome> result;if(high<low || (high==low && !(lower_closed && upper_closed)))return result;
    const auto schema=scope.bind(schema_id());if(!schema)return result;
    for(const auto directory:directory_pages(scope,layer,low,high,search,lower_closed,upper_closed)) {
    const auto maxima=values<u64>(directory,2),minima=values<u64>(directory,1);
    const auto prefixes=values<unsigned>(directory,0);
    // Same-layer continuation uses the selected directory's order; it never
    // increments a rank into another layer.
    for(unsigned r=boundary(maxima,low,search,!lower_closed);r<maxima.size() && (upper_closed?minima[r]<=high:minima[r]<high);++r) {
        auto segment=scope.bind(segment_id(layer,prefixes[r]));require(bool(segment));
        const auto d=dirent_at(directory,prefixes[r],r);const auto recipe=interpretation(segment,d,schema,layer,key_bytes_);
        const auto count=segment.control().count;
        const auto mins=located<u64>(segment,d,1,count),maxs=located<u64>(segment,d,2,count);
        const auto positions=located<std::uint16_t>(segment,d,0,count);
        for(unsigned e=boundary(maxs,low,search,!lower_closed);e<mins.size() && (upper_closed?mins[e]<=high:mins[e]<high);++e) {
            const auto summary=interpret_summary(*summary_address(scope,layer,d,segment,e),recipe);
            result.push_back({true,mins[e],summary.count,summary.not_null,summary.sum,prefixes[r],positions[e]});
        }
    }
    }
    return result;
}
std::vector<Outcome> Index::descend(Store::Scope& scope,unsigned source_layer,const Outcome& parent,unsigned layer)const {
    require(parent.present && layer<=4 && layer>source_layer && layer-source_layer<=2);
    const bool one=layer-source_layer==1;
    const unsigned prefix=one ? (parent.prefix<<8)|(parent.position>>8):(parent.prefix<<16)|parent.position;
    const unsigned low=one ? (parent.position&255)<<8:0,high=one?low+256:65536;
    auto directory=physical_directory(scope,layer,prefix);
    if(!directory)return {};
    const auto d=known_dirent(directory,prefix);
    if(!(d.flags&1))return {};
    auto segment=scope.bind(segment_id(layer,prefix));require(bool(segment));
    const auto recipe=interpretation(segment,d,scope.bind(schema_id()),layer,key_bytes_);
    const auto n=segment.control().count;const auto positions=located<std::uint16_t>(segment,d,0,n);
    std::vector<Outcome> result;
    for(auto it=std::lower_bound(positions.begin(),positions.end(),low);it!=positions.end() && *it<high;++it) {
        const auto r=it-positions.begin();const auto s=interpret_summary(*summary_address(scope,layer,d,segment,r),recipe);
        result.push_back({true,located<u64>(segment,d,1,n)[r],s.count,s.not_null,s.sum,prefix,*it});
    }
    return result;
}
std::vector<Row> Index::rows() const {std::vector<Row> result;for(auto [id,row]:rows_)result.push_back(row);return result;}
u64 Index::logical_bytes() const {
    u64 n=0;for(const auto& [key,s]:segments_)for(const auto& [pos,e]:s) {
        n+=sizeof(e)+e.logical.size()*(sizeof(u64)+sizeof(unsigned))+e.field.size()*(sizeof(i64)+sizeof(unsigned));
    }
    return n; // logical mutable-model operands, not std::map allocation bytes
}
u64 Index::cache_bytes(Store::Scope& scope)const {
    u64 bytes=0;
    for(unsigned l=0;l<5;++l)if(auto root=scope.bind(directory_id(l))) {
        bytes+=hash_bytes(root.control().hash);
        if(page_bits_ && l>=2)for(auto page:directory_pages(scope,l,0,UINT64_MAX,Search::ordinary))bytes+=hash_bytes(page.control().hash);
    }
    return bytes;
}
u64 Index::locator_bytes(Store::Scope& scope)const {
    // Current segment cell sets, counted once per Object. Historical sets and
    // exported proofs remain charged to their actual memory resource.
    u64 total=0;for(const auto& [key,segment]:segments_)if(auto s=scope.bind(segment_id(key.first,key.second)))
        if(auto* cells=s.control().locators)total+=cells->bytes();
    return total;
}
void Index::verify(Store::Scope& scope,std::span<const Row> rows,bool exhaustive) const {
    // Oracle derives each layer by shifting the ORIGINAL records, independently
    // of mutable accumulators, captured images, directory rank and search bodies.
    for(unsigned layer=0;layer<5;++layer) {
        if(page_bits_ && layer>=2) {
            // Internal directory pages are storage for this same index layer.
            // Check their parent locators as well as the leaf-to-segment links.
            auto check_children=[&](Selection parent,bool branches) {
                std::vector<Selection> children;if(!parent)return children;
                const auto keys=values<unsigned>(parent,0);
                require(keys.size()==parent.control().count);
                for(unsigned r=0;r<keys.size();++r) {
                    auto child=scope.bind(branches?branch_id(layer,keys[r]):page_id(layer,keys[r]));require(bool(child));
                    const auto descriptor=dirent_at(parent,keys[r],r);
                    require(descriptor.revision==child.control().revision);
                    for(unsigned p=0;p<6;++p)require(descriptor.parts[p]==reinterpret_cast<u64>(take(child.part(p)).address()));
                    require(values<u64>(parent,1)[r]==values<u64>(child,1).front() && values<u64>(parent,2)[r]==values<u64>(child,2).back());
                    children.push_back(child);
                }
                return children;
            };
            const auto children=check_children(scope.bind(directory_id(layer)),directory_branches_);
            if(directory_branches_)for(auto child:children)check_children(child,false);
        }
        using Key=std::pair<unsigned,unsigned>;
        std::map<Key,std::vector<Row>> groups;
        for(const auto& row:rows)groups[{layer?unsigned(row.physical>>(48-8*layer)):0,unsigned((row.physical>>(32-8*layer))&65535)}].push_back(row);
        auto all=range(scope,layer,0,UINT64_MAX);
        require(all.size()==groups.size());unsigned rank=0;
        for(const auto& [where,records]:groups) {
            auto s=scope.bind(segment_id(layer,where.first));require(bool(s));
            const auto directory=physical_directory(scope,layer,where.first);
            const auto prefixes=values<unsigned>(directory,0);
            const auto directory_rank=std::size_t(std::ranges::find(prefixes,where.first)-prefixes.begin());
            require(directory_rank<prefixes.size());const auto d=dirent_at(directory,where.first,directory_rank);
            for(unsigned part=0;part<s.part_count();++part) {
                const auto locator=part<6?d.parts[part]:extra_locator(scope,layer,d,part-6);
                require(locator==part_locator(s,part));
                require(take(resolve_locator(s,locator,part))==take(s.part(part)).address());
            }
            if(aux_parts_) {
                const auto o=auxiliary(scope,layer,d,Outcome{.present=true,.prefix=where.first},aux_parts_);u64 expected=0;
                for(unsigned p=5+chunks(s.control().count,s.control().summary_chunk);p<s.part_count();++p)expected+=values<u64>(s,p).front();
                require(o.auxiliary==expected);
            }
            const auto positions=values<std::uint16_t>(s,0);const auto found=std::lower_bound(positions.begin(),positions.end(),where.second);require(found!=positions.end() && *found==where.second);
            const auto r=found-positions.begin();const auto defs=values<Definition>(scope.bind(schema_id()),0);
            const auto actual=interpret_summary(*summary_address(scope,layer,d,s,r),defs[s.control().representation-1].recipe);
            Summary expected;u64 lo=UINT64_MAX,hi=0;expected.minimum=INT64_MAX;expected.maximum=INT64_MIN;
            for(const auto& row:records) {++expected.count;lo=std::min(lo,row.logical);hi=std::max(hi,row.logical);if(row.valid){++expected.not_null;expected.sum+=row.value;expected.minimum=std::min(expected.minimum,row.value);expected.maximum=std::max(expected.maximum,row.value);}}
            if(!expected.not_null)expected.minimum=expected.maximum=0;
            require(actual.count==expected.count && actual.not_null==expected.not_null && actual.minimum==expected.minimum && actual.maximum==expected.maximum && actual.sum==expected.sum);
            require(values<u64>(s,1)[r]==lo && values<u64>(s,2)[r]==hi);
            require(all[rank].prefix==where.first && all[rank].position==where.second);++rank;
        }
        std::vector<u64> queries{0,UINT64_MAX};
        for(unsigned i=0;i<rows.size();i+=std::max(1u,unsigned(rows.size()/512))) {
            const auto& row=rows[i];queries.push_back(row.logical);if(exhaustive && row.logical)queries.push_back(row.logical-1);if(exhaustive && row.logical!=UINT64_MAX)queries.push_back(row.logical+1);
        }
        for(auto q:queries) {
            auto a=seek(scope,layer,q),b=seek(scope,layer,q,Search::ordinary);
            auto expected=groups.end();for(auto it=groups.begin();it!=groups.end();++it) {
                u64 hi=0;for(const auto& row:it->second)hi=std::max(hi,row.logical);
                if(hi>=q){expected=it;break;}
            }
            require(a.present==(expected!=groups.end()) && a.present==b.present);
            if(a.present)require(a.prefix==expected->first.first && a.position==expected->first.second && a.sum==b.sum);
        }
    }
    std::vector<Row> ordered(rows.begin(),rows.end());std::ranges::sort(ordered,{},&Row::physical);
    for(unsigned i=1;i<ordered.size();++i)require(ordered[i-1].physical<ordered[i].physical && ordered[i-1].logical<=ordered[i].logical);
}
std::vector<Row> dataset(unsigned count,unsigned j,unsigned duplicate,u64 spacing) {
    require(j==0 || j==2);std::vector<Row> rows;
    for(unsigned i=0;i<count;++i) {
        const u64 tenant=j==2 ? u64(i)*16/count:0;
        const u64 local=j==2 ? i-((tenant*count+15)/16):i;
        const u64 logical=(tenant<<48)+(local/duplicate)*4;
        const u64 physical=(tenant<<32)+local*spacing;
        require(local*spacing<(j==2?(u64(1)<<32):(u64(1)<<48)));
        rows.push_back({i+1,physical,logical,i64((i*37)%1009)-500,i%7!=0});
    }
    return rows;
}
} // namespace directory_spike
