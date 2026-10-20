#pragma once
#include "material.h"
#include <map>
#include <set>

namespace directory_spike {
struct Row {u64 id{},physical{},logical{};i64 value{};bool valid{true};};
struct Summary {u64 count{},not_null{};i64 minimum{},maximum{},sum{};};
static_assert(sizeof(Summary)==40);
struct Definition {std::uint32_t id{},key_bytes{},allowed_layers{},recipe{},summary_chunk{};};
Summary interpret_summary(Summary,unsigned recipe);
struct EntryState {
    u64 count{},not_null{};i64 sum{};
    std::map<u64,unsigned> logical;
    std::map<i64,unsigned> field;
    void edit(const Row&,bool add);
    Summary summary() const;
};
using SegmentKey=std::pair<unsigned,unsigned>;
using Segment=std::map<std::uint16_t,EntryState>;
struct Outcome {bool present{};u64 logical{},count{},not_null{};i64 sum{};unsigned prefix{},position{};u64 auxiliary{};};
// This borrows a selected snapshot's payload. Its Scope stays stationary/alive
// until finish_read; a pointer or this phase alone owns no backing.
struct PendingRead {
    const Summary* body{};u64 logical{};unsigned prefix{},position{},recipe{},overflow_block{};
    std::uint16_t overflow_rank{},flags{};
};
Outcome finish_read(PendingRead);
enum class Search { ordinary,boundlist };
struct EditStats : CopyStats {
    u64 summary_entries{},new_segments{},removed_segments{},bound_repairs{};
    u64 model_ns{},segment_image_ns{},segment_capture_ns{},directory_ns{},prepare_ns{},install_ns{};
};
class Index {
    Runtime& rt_;
    std::pmr::memory_resource& memory_;
    std::map<SegmentKey,Segment> segments_;
    std::map<u64,Row> rows_;
    struct DirectoryEntry {Dirent descriptor;u64 minimum{},maximum{};};
    std::array<std::map<unsigned,DirectoryEntry>,5> directory_entries_,page_entries_,branch_entries_;
    std::array<std::vector<unsigned>,5> overflow_prefixes_;
    std::map<SegmentKey,std::set<std::uint16_t>> dirty_positions_;
    unsigned j_{},key_bytes_{},generation_{},page_bits_{},aux_parts_{},definitions_{},summary_chunk_{};
    bool compact_tables_{},directory_branches_{},packed_pages_{},compact_hashes_{},indirect_parts_{};
    unsigned table_row_chunk_{},root_row_chunk_{},locator_mode_{};
    void edit_model(const Row&,bool,std::set<SegmentKey>&);
    Image segment_image(SegmentKey,unsigned representation) const;
    u64 extra_locator(Store::Scope&,unsigned,const Dirent&,unsigned)const;
    const Summary* summary_address(Store::Scope&,unsigned,const Dirent&,Selection,unsigned)const;
    Image schema_image()const;
    Selection selected_directory(Store::Scope&,unsigned,u64,Search) const;
    Selection physical_directory(Store::Scope&,unsigned,unsigned) const;
    std::vector<Selection> directory_pages(Store::Scope&,unsigned,u64,u64,Search,bool=true,bool=true)const;
    Outcome auxiliary(Store::Scope&,unsigned,const Dirent&,Outcome,unsigned) const;
public:
    Store store;
    Index(Runtime&,std::pmr::memory_resource&,unsigned j,unsigned key_bytes=8,unsigned page_bits=0,unsigned auxiliary_parts=0,unsigned definitions=3,unsigned summary_chunk=0,bool compact_tables=false,bool directory_branches=false,unsigned table_row_chunk=0,bool packed_pages=false,bool compact_hashes=false,unsigned root_row_chunk=0,unsigned locator_mode=0);
    void extend_schema(unsigned definitions);
    EditStats edit(std::span<const Row> additions,std::span<const u64> removals={},bool selective=true);
    Outcome seek(Store::Scope&,unsigned layer,u64 logical,Search=Search::boundlist,unsigned auxiliary_parts=0) const;
    PendingRead prepare(Store::Scope&,unsigned layer,u64 logical,Search=Search::boundlist)const;
    std::vector<Outcome> range(Store::Scope&,unsigned layer,u64 low,u64 high,Search=Search::boundlist,bool lower_closed=true,bool upper_closed=true) const;
    std::vector<Outcome> descend(Store::Scope&,unsigned source_layer,const Outcome&,unsigned destination_layer)const;
    void verify(Store::Scope&,std::span<const Row>,bool exhaustive=false) const;
    std::vector<Row> rows() const;
    u64 logical_bytes() const;
    u64 cache_bytes(Store::Scope&)const;
    u64 locator_bytes(Store::Scope&)const;
    unsigned segments() const {return segments_.size();}
};
std::vector<Row> dataset(unsigned count,unsigned j,unsigned duplicate=1,u64 spacing=257);
} // namespace directory_spike
