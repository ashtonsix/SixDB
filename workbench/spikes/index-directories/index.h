#pragma once
#include "material.h"
#include "undo.h"
#include <map>
#include <set>

namespace directory_spike {
struct Row {u64 id{},physical{},logical{};i64 value{};bool valid{true};};
struct Summary {u64 count{},not_null{};i64 minimum{},maximum{},sum{};};
static_assert(sizeof(Summary)==40);
struct Definition {std::uint32_t id{},key_bytes{},allowed_layers{},recipe{},summary_chunk{},auxiliary_parts{};};
orbital::Result<Definition> schema_definition(Selection,unsigned id,unsigned layer);
Summary interpret_summary(Summary,unsigned recipe);
struct EntryState {
    u64 count{},not_null{};i64 sum{};
    std::map<u64,unsigned> logical;
    std::map<i64,unsigned> field;
    void edit(const Row&,bool add,UndoJournal* undo=nullptr);
    void replace(const Row& before,const Row& after,UndoJournal* undo=nullptr);
    Summary summary() const;
};
using SegmentKey=std::pair<unsigned,unsigned>;
struct Migration {SegmentKey segment;unsigned representation;};
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
    std::map<unsigned,Definition> definition_overrides_;
    unsigned j_{},key_bytes_{},generation_{},page_bits_{},aux_parts_{},definitions_{},summary_chunk_{};
    bool compact_tables_{},directory_branches_{},packed_pages_{},compact_hashes_{},indirect_parts_{};
    unsigned table_row_chunk_{},root_row_chunk_{},locator_mode_{};
    UndoJournal* undo_{};
    template<class F>auto transaction(F f,EditFaults* faults)->orbital::Result<decltype(f())> {
        require(!undo_ && !rt_.faults);UndoJournal undo(faults && faults->journal_memory?faults->journal_memory:std::pmr::new_delete_resource(),faults?faults->journal_local_bytes:8192);undo_=&undo;rt_.faults=faults;
        struct Reset {Index& index;UndoJournal& undo;EditFaults* faults;~Reset(){
            if(faults){faults->journal_records=undo.records();faults->journal_record_bytes=undo.record_bytes();faults->journal_heap_bytes=undo.heap_bytes();}
            index.undo_=nullptr;index.rt_.faults=nullptr;index.dirty_positions_.clear();
        }} reset{*this,undo,faults};
        try{auto stats=f();stats.journal_records=undo.records();stats.journal_record_bytes=undo.record_bytes();stats.journal_heap_bytes=undo.heap_bytes();undo.commit();return stats;}
        catch(const EditFailure& e){return std::unexpected(e.error);}
        catch(const std::bad_alloc&){return std::unexpected(orbital::Error::capacity);}
    }
    void edit_model(const Row&,bool,std::set<SegmentKey>&);
    void replace_value_model(const Row&,const Row&,std::set<SegmentKey>&);
    Image segment_image(SegmentKey,unsigned representation) const;
    u64 extra_locator(Store::Scope&,unsigned,const Dirent&,unsigned)const;
    const Summary* summary_address(Store::Scope&,unsigned,const Dirent&,Selection,unsigned)const;
    Image schema_image()const;
    CopyStats extend_schema_impl(unsigned,std::span<const Definition>);
    void install_changes(const Store::Scope&,std::span<const Store::Change>,EditStats* stats=nullptr);
    Definition representation_definition(unsigned)const;
    EditStats apply(std::span<const Row>,std::span<const u64>,bool,std::span<const Migration>,bool=true,bool=true);
    Selection selected_directory(Store::Scope&,unsigned,u64,Search) const;
    Selection physical_directory(Store::Scope&,unsigned,unsigned) const;
    std::vector<Selection> directory_pages(Store::Scope&,unsigned,u64,u64,Search,bool=true,bool=true)const;
    Outcome auxiliary(Store::Scope&,unsigned,const Dirent&,Outcome,unsigned) const;
public:
    Store store;
    Index(Runtime&,std::pmr::memory_resource&,unsigned j,unsigned key_bytes=8,unsigned page_bits=0,unsigned auxiliary_parts=0,unsigned definitions=3,unsigned summary_chunk=0,bool compact_tables=false,bool directory_branches=false,unsigned table_row_chunk=0,bool packed_pages=false,bool compact_hashes=false,unsigned root_row_chunk=0,unsigned locator_mode=0);
    CopyStats extend_schema(unsigned definitions,std::span<const Definition> additions={});
    orbital::Result<CopyStats> try_extend_schema(unsigned definitions,std::span<const Definition> additions={},EditFaults* faults=nullptr);
    EditStats edit(std::span<const Row> additions,std::span<const u64> removals={},bool selective=true);
    orbital::Result<EditStats> try_edit(std::span<const Row> additions,std::span<const u64> removals={},bool selective=true,EditFaults* faults=nullptr,bool value_patch=true);
    EditStats edit_unprotected(std::span<const Row>,std::span<const u64> removals={},bool selective=true,bool value_patch=true);
    orbital::Result<EditStats> migrate(std::span<const Migration>,bool source_transcode=true,EditFaults* faults=nullptr);
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
