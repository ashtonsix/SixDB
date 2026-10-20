#pragma once
#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <memory>
#include <memory_resource>
#include <type_traits>
#include <utility>
#include <vector>

namespace directory_spike {
// Writer-local undo storage. Map extraction keeps nested mapped objects at
// stable addresses; reverse replay restores parents before their older edits.
class UndoJournal {
    struct Action {void(*finish)(Action*,bool) noexcept;};
    struct Upstream : std::pmr::memory_resource {
        std::size_t bytes{};
        std::pmr::memory_resource* parent;
        explicit Upstream(std::pmr::memory_resource* p):parent(p){}
        void* do_allocate(std::size_t n,std::size_t a)override{auto* p=parent->allocate(n,a);bytes+=n;return p;}
        void do_deallocate(void* p,std::size_t n,std::size_t a)override{parent->deallocate(p,n,a);}
        bool do_is_equal(const std::pmr::memory_resource& r)const noexcept override{return this==&r;}
    } upstream_;
    alignas(std::max_align_t) std::array<std::byte,8192> buffer_;
    std::pmr::monotonic_buffer_resource memory_;
    std::pmr::vector<Action*> actions_;
    std::size_t record_bytes_{};
    bool finished_{};
    template<class T,class... Args>T& push(Args&&... args) {
        auto* p=std::construct_at(static_cast<T*>(memory_.allocate(sizeof(T),alignof(T))),std::forward<Args>(args)...);
        try{actions_.push_back(p);}catch(...){std::destroy_at(p);throw;}
        record_bytes_+=sizeof(T);return *p;
    }
    template<class T>struct Saved : Action {
        T* value;T old;
        explicit Saved(T& v):Action{[](Action* a,bool rollback)noexcept{auto* p=static_cast<Saved*>(a);if(rollback)*p->value=p->old;std::destroy_at(p);}},value(&v),old(v){}
    };
    template<class Map>struct Inserted : Action {
        Map* map;typename Map::key_type key;
        Inserted(Map& m,const typename Map::key_type& k):Action{[](Action* a,bool rollback)noexcept{auto* p=static_cast<Inserted*>(a);if(rollback)p->map->erase(p->key);std::destroy_at(p);}},map(&m),key(k){}
    };
    template<class Map>struct Erased : Action {
        Map* map;typename Map::node_type node;
        explicit Erased(Map& m):Action{[](Action* a,bool rollback)noexcept{auto* p=static_cast<Erased*>(a);if(rollback && p->node)p->map->insert(std::move(p->node));std::destroy_at(p);}},map(&m){}
    };
    template<class T>struct Vector : Action {
        std::vector<T>* value;std::vector<T> old;
        explicit Vector(std::vector<T>& v):Action{[](Action* a,bool rollback)noexcept{auto* p=static_cast<Vector*>(a);if(rollback)p->value->swap(p->old);std::destroy_at(p);}},value(&v){}
    };
    void finish(bool rollback)noexcept{for(auto i=actions_.rbegin();i!=actions_.rend();++i)(*i)->finish(*i,rollback);finished_=true;}
public:
    explicit UndoJournal(std::pmr::memory_resource* parent=std::pmr::new_delete_resource(),std::size_t local_bytes=8192)
        :upstream_(parent),memory_(buffer_.data(),std::min(local_bytes,buffer_.size()),&upstream_),actions_(&memory_){}
    ~UndoJournal(){if(!finished_)finish(true);}
    void commit()noexcept{finish(false);}
    std::size_t records()const{return actions_.size();}
    std::size_t record_bytes()const{return record_bytes_;}
    std::size_t heap_bytes()const{return upstream_.bytes;}
    template<class T>void save(T& value){static_assert(std::is_trivially_copyable_v<T>);push<Saved<T>>(value);}
    template<class Map>auto& get(Map& map,const typename Map::key_type& key) {
        auto it=map.find(key);if(it!=map.end())return it->second;
        push<Inserted<Map>>(map,key);return map.try_emplace(key).first->second;
    }
    template<class Map>auto erase(Map& map,typename Map::iterator it) {
        auto next=std::next(it);auto& undo=push<Erased<Map>>(map);undo.node=map.extract(it);return next;
    }
    template<class T>void replace(std::vector<T>& value){auto& undo=push<Vector<T>>(value);undo.old.swap(value);}
};
template<class Map>auto& edit_get(UndoJournal* undo,Map& map,const typename Map::key_type& key){return undo?undo->get(map,key):map[key];}
template<class Map,class Value>void edit_set(UndoJournal* undo,Map& map,const typename Map::key_type& key,const Value& value){auto& v=edit_get(undo,map,key);if(undo)undo->save(v);v=value;}
template<class Map>auto edit_erase(UndoJournal* undo,Map& map,typename Map::iterator it){return undo?undo->erase(map,it):map.erase(it);}
template<class Map>bool edit_erase(UndoJournal* undo,Map& map,const typename Map::key_type& key){auto it=map.find(key);if(it==map.end())return false;edit_erase(undo,map,it);return true;}
} // namespace directory_spike
