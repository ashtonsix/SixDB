#pragma once
#include "promoted.h"

namespace ht {
/// A private index replacement, with bounded source traversal. Keep the source
/// optional at a stable address and immutable until finish or cancellation. The
/// source owns every bag throughout preparation; opaque refs acquire their owner
/// only at finish. This is an exclusive-owner transition, not a published-reader
/// mutation: retained versions need their own owner before starting migration.
template<class Source,class Destination> class hierarchy_migration {
    static_assert(std::is_same_v<typename Source::source_type,typename Destination::source_type>);
    static constexpr bool from_list=std::is_same_v<Source,typename Destination::source_type>;
    static auto initial_cursor() {
        if constexpr(from_list)return typename Source::scan_cursor{};
        else return typename Source::index_type::scan_cursor{};
    }
    std::optional<Source>* source_;
    Destination replacement_;
    decltype(initial_cursor()) cursor_;
    bool ready_=false;
    hierarchy_migration(std::optional<Source>& source,word a,word b)
        :source_(&source),replacement_(*source->pool_,a,b) {}
  public:
    hierarchy_migration(hierarchy_migration&&)noexcept=default;
    hierarchy_migration& operator=(hierarchy_migration&&)noexcept=default;
    static std::optional<hierarchy_migration> prepare(std::optional<Source>& source,word a,word b) {
        require(bool(source),"migration source");
        // Reuse the same owning Pool: transferred bags keep its affinity and
        // lifetime. Moving a table to a different worker is a separate operation.
        try {return hierarchy_migration(source,a,b);}
        catch(const std::bad_alloc&) {return {};}
    }
    record_scan_result step(unsigned budget) {
        require(bool(*source_),"live migration source");
        auto copy=[&](word key,const auto& row) {
            return replacement_.index_.insert_unique_record(key,row);
        };
        auto result=[&] {
            if constexpr(from_list)return (*source_)->scan_records(cursor_,budget,copy);
            else return (*source_)->index_.scan(cursor_,budget,copy);
        }();
        ready_=result.state==record_scan_state::done;return result;
    }
    bool ready()const{return ready_;}
    /// Transfer bags once, then destroy the consumed source index. A coordinator
    /// may hold several ready migrations before advancing their shared epoch.
    Destination finish() {
        require(ready_ && bool(*source_),"complete migration");
        if constexpr(from_list)replacement_.owner_=(*source_)->release_for_transition();
        else replacement_.owner_=std::move((*source_)->owner_);
        source_->reset();return std::move(replacement_);
    }
    memory footprint()const {
        auto m=replacement_.index_.footprint();
        m.metadata+=sizeof(*this)-sizeof(typename Destination::index_type);return m;
    }
};
} // namespace ht
