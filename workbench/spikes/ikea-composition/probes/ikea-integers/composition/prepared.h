#pragma once
#include "graph.h"
#include <memory>

namespace ikea::integers::composition {
// Allocation and cold description are target-independent. Admitted storage
// must have no mutable aliases during execution; preparation retains ownership.
struct Storage {
    struct Free { void operator()(std::uint8_t*) const; };
    std::unique_ptr<std::uint8_t,Free> bytes;
    std::size_t size;
    explicit Storage(std::size_t);
};
struct ChildPlacement {
    std::shared_ptr<const Storage> owner;
    std::size_t offset;
    unsigned repeat_stride,values_per_repeat,bytes_per_repeat;
};
struct Placement {
    ParentKind parent;
    std::uint64_t source_id;
    std::size_t offset,tiles;
    unsigned tile_stride=96,values_per_tile=64;
    ChildPlacement body,tail;
};
Placement place(std::shared_ptr<const Storage>,ParentKind,std::uint64_t source_id,
                std::size_t offset,std::size_t tiles);
struct Program;
struct Binding {
    const std::uint8_t* first;
    std::size_t tiles;
    unsigned cutoff;
    const Program* program;
};
enum class Execution { authored_inline,cps };
using Entry=std::uint64_t(*)(const void*);
struct Prepared {
    std::shared_ptr<const Storage> owner;
    std::shared_ptr<const Program> program;
    Graph graph;
    Binding binding{};
    Entry entry=nullptr;
    std::uint64_t operator()() const {return entry(&binding);}
};
bool prepare_impl(TailKind,std::uint64_t,Graph,const Placement&,unsigned,Execution,Prepared&,std::string&);
template<class Tail>
bool prepare(const Column<Tail>& source,const Placement& placement,unsigned cutoff,
             Execution execution,Prepared& result,std::string& error) {
    return prepare_impl(Tail::kind,source.source_id,record(source),placement,cutoff,execution,result,error);
}
extern "C" std::uint64_t ikea_i12_inline_local(const void*);
extern "C" std::uint64_t ikea_i12_inline_scan(const void*);
extern "C" std::uint64_t ikea_i12_inline_scan_middle(const void*);
extern "C" std::uint64_t ikea_i12_cps(const void*);
} // namespace ikea::integers::composition
