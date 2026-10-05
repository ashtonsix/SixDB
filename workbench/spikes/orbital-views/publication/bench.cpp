#include "journal.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

using namespace publication;
using Clock=std::chrono::steady_clock;
namespace {
std::uint64_t consume=0;
double elapsed(Clock::time_point start) {return std::chrono::duration<double,std::nano>(Clock::now()-start).count();}
Bytes seed(std::size_t n) {Bytes b(n);for(std::size_t j=0;j<n;++j)b[j]=std::uint8_t((j*31+j/37)%251);return b;}
void same(std::span<const std::uint8_t> a,std::span<const std::uint8_t> b) {
  if(a.size()!=b.size()||!std::equal(a.begin(),a.end(),b.begin()))throw std::runtime_error("benchmark oracle mismatch");
}
std::size_t payload(const Batch& batch) {std::size_t n=0;for(const auto& p:batch)n+=p.bytes.size();return n;}
struct Variant {const char* label;Kind kind;unsigned compact;bool full_hash=false;};
constexpr std::array variants{
  Variant{"snapshot_full_hash",Kind::snapshot,0,true},
  Variant{"snapshot",Kind::snapshot,0},Variant{"flat_pages",Kind::flat_pages,0},
  Variant{"radix_pages",Kind::radix_pages,0},Variant{"block_pages",Kind::block_pages,0},Variant{"overlay",Kind::overlay,0},
  Variant{"overlay_compact8",Kind::overlay,8}};
void material(std::ostream& out,std::size_t size,std::string_view shape,unsigned epochs,Variant variant,bool roots,bool merged) {
  Counts c;Material m(variant.kind,size,roots,variant.compact,c,variant.full_hash);
  auto expected=seed(size);auto seed_at=Clock::now();auto current=m.seed(expected);const auto seed_ns=elapsed(seed_at);
  std::vector<Cut> cuts{current};const auto seeded=c;
  double update_ns=0,capture_ns=0,merge_ns=0;std::size_t input_bytes=0,input_records=0,output_bytes=0,output_records=0;
  std::size_t read_at=0;
  for(unsigned e=1;e<=epochs;++e) {
    auto input=changes(shape,size,e);input_bytes+=payload(input);input_records+=input.size();
    read_at=std::min(size-64,input.front().offset);
    const auto begin=Clock::now();auto batch=merged?capture_coalesced(input,c):capture(input,c);
    if(merged)merge_ns+=elapsed(begin);else capture_ns+=elapsed(begin);
    output_bytes+=payload(*batch);output_records+=batch->size();
    auto at=Clock::now();current=m.update(current,std::move(batch),e);update_ns+=elapsed(at);
    cuts.push_back(current);publication::apply(expected,input);
  }
  const auto updated=c;
  constexpr unsigned reads=128;
  auto at=Clock::now();
  for(unsigned j=0;j<reads;++j) {auto b=m.read(current,read_at,64);consume^=hash(b);}
  const auto latest_ns=elapsed(at)/reads;const auto latest_counts=c;
  at=Clock::now();
  for(unsigned j=0;j<reads;++j) {auto b=m.read(cuts[(j%3)*(epochs/2)],read_at,64);consume^=hash(b);}
  const auto retained_ns=elapsed(at)/reads;const auto read_counts=c;
  // Exact byte oracle outside the timed work, including an old cut before a collapse.
  same(m.read(current,0,size),expected);
  auto old=seed(size);const auto cut=epochs>8?7u:epochs/2;
  for(unsigned e=1;e<=cut;++e)publication::apply(old,changes(shape,size,e));same(m.read(cuts[cut],0,size),old);
  Counts oracle_counts;Material oracle(Kind::snapshot,size,true,0,oracle_counts,true);auto exact=oracle.seed(expected);
  if(roots&&m.digest(current)!=oracle.digest(exact))throw std::runtime_error("benchmark content root mismatch");
  out<<size<<','<<shape<<','<<epochs<<','<<variant.label<<','<<roots<<','<<merged<<','
     <<(capture_ns+merge_ns+update_ns)/epochs<<','<<capture_ns/epochs<<','<<merge_ns/epochs<<','<<update_ns/epochs<<','
     <<latest_ns<<','<<retained_ns<<','<<input_bytes<<','<<input_records<<','<<output_bytes<<','<<output_records<<','
     <<updated.payload_copied-seeded.payload_copied<<','<<updated.metadata_copied-seeded.metadata_copied<<','
     <<updated.hash_payload-seeded.hash_payload<<','<<updated.hash_metadata-seeded.hash_metadata<<','
     <<updated.compact_bytes<<','<<updated.allocations-seeded.allocations<<','<<updated.live_bytes<<','<<updated.peak_bytes<<','
     <<latest_counts.read_bytes-updated.read_bytes<<','<<latest_counts.lookup_entries-updated.lookup_entries<<','
     <<read_counts.read_bytes-latest_counts.read_bytes<<','<<read_counts.lookup_entries-latest_counts.lookup_entries<<','
     <<seeded.payload_copied<<','<<seeded.hash_payload<<','<<seeded.hash_metadata<<','<<seed_ns<<','<<seeded.live_bytes<<','<<seeded.allocations<<'\n';
}
void pressure(std::ostream& out,std::size_t size,std::string_view shape,unsigned epochs,Variant variant,std::string_view retention) {
  Counts c;Material m(variant.kind,size,false,variant.compact,c);
  auto expected=seed(size);auto current=m.seed(expected);Cut reader;std::vector<Cut> readers;Sink sink(size*2);
  if(retention=="reader_sink")reader=current;
  if(retention=="all_readers")readers.push_back(current);
  for(unsigned e=1;e<=epochs;++e) {
    auto b=changes(shape,size,e);current=m.update(current,capture(b,c),e);publication::apply(expected,b);
    if(retention=="reader_sink"&&e<=2)if(!sink.submit(e,current,size))throw std::runtime_error("pressure capacity");
    if(retention=="all_readers")readers.push_back(current);
  }
  const auto live=c.live_bytes,peak=c.peak_bytes,pending=sink.pending();
  same(m.read(current,0,size),expected);
  reader.reset();readers.clear();for(std::size_t i=0;i<sink.tickets();++i)sink.complete(i,false);
  const auto after=c.live_bytes;current.reset();if(c.live_bytes)throw std::runtime_error("pressure release");
  out<<size<<','<<shape<<','<<epochs<<','<<variant.label<<','<<retention<<','<<live<<','<<peak<<','<<pending<<','<<after<<'\n';
}
void recovery(std::ostream& out,const std::filesystem::path& path,std::size_t size,std::string_view shape,unsigned epochs,bool merged) {
  auto expected=seed(size);std::size_t input_bytes=0,output_bytes=0,records=0,read_at=0;
  {
    Journal journal(path,expected);
    for(unsigned e=1;e<=epochs;++e) {
      auto b=changes(shape,size,e);input_bytes+=payload(b);publication::apply(expected,b);
      read_at=std::min(size-64,b.front().offset);if(merged)b=coalesce(b);
      output_bytes+=payload(b);records+=b.size();journal.append(e,b);
    }
  }
  for(auto method:{"forward","latest_cover"}) {
    auto begin=Clock::now();Recovery r(path);auto startup=elapsed(begin);auto meta=r.counts.metadata_read;
    auto at=Clock::now();auto first=std::string_view(method)=="forward"?r.read(epochs,read_at,64):r.read_latest(epochs,read_at,64);
    auto first_ns=elapsed(at);same(first,std::span(expected).subspan(read_at,64));auto first_counts=r.counts;
    constexpr unsigned repeats=32;at=Clock::now();
    for(unsigned n=0;n<repeats;++n) {
      auto bytes=std::string_view(method)=="forward"?r.read(epochs,read_at,64):r.read_latest(epochs,read_at,64);consume^=hash(bytes);
    }
    auto warm_ns=elapsed(at)/repeats;
    const auto before_whole=r.counts;
    at=Clock::now();auto all=std::string_view(method)=="forward"?r.eager(epochs):r.read_latest(epochs,0,size);auto hydrate=elapsed(at);
    same(all,expected);const auto whole_counts=r.counts;
    at=Clock::now();
    for(unsigned n=0;n<128;++n) {Bytes b(all.begin()+read_at,all.begin()+read_at+64);consume^=hash(b);}
    auto hydrated_read=elapsed(at)/128;
    out<<size<<','<<shape<<','<<epochs<<','<<merged<<','<<method<<','<<startup<<','<<meta<<','<<first_ns<<','<<warm_ns<<','
       <<first_counts.payload_read<<','<<first_counts.patches_consulted<<','<<hydrate<<','<<hydrated_read<<','
       <<whole_counts.payload_read-before_whole.payload_read<<','
       <<input_bytes<<','<<output_bytes<<','<<records<<','<<std::filesystem::file_size(path)<<'\n';
  }
  std::filesystem::remove(path);
}
}
int main(int argc,char** argv) {
  try {
    if(argc<2||argc>5)throw std::invalid_argument("publication_bench OUT [epochs=16] [max_bytes=16777216] [reverse]");
    const std::filesystem::path output=argv[1];std::filesystem::create_directories(output);
    const auto epochs=argc>2?std::stoul(argv[2]):16u;
    const auto max=argc>3?std::stoull(argv[3]):16777216ull;
    if(epochs<2||epochs>64)throw std::invalid_argument("epochs must be 2..64");
    std::ofstream materials(output/"material.csv"),recoveries(output/"recovery.csv"),pressures(output/"pressure.csv");
    materials<<"part_bytes,shape,epochs,material,content_root,coalesced,front_ns_per_epoch,capture_ns_per_epoch,merge_ns_per_epoch,material_ns_per_epoch,latest_64B_ns,retained_64B_ns,input_bytes,input_records,captured_bytes,captured_records,payload_copied,metadata_copied,hash_payload_bytes,hash_metadata_bytes,compact_bytes,material_allocations,live_material_bytes,peak_material_bytes,latest_read_bytes,latest_lookup_entries,retained_read_bytes,retained_lookup_entries,seed_copy_bytes,seed_hash_payload_bytes,seed_hash_metadata_bytes,seed_ns,seed_live_bytes,seed_material_allocations\n";
    recoveries<<"part_bytes,shape,epochs,coalesced,reconstruction,lazy_start_ns,start_metadata_bytes,first_64B_ns,warm_64B_ns,first_payload_bytes,first_patch_visits,whole_hydrate_ns,hydrated_64B_ns,whole_payload_bytes,input_bytes,captured_bytes,captured_records,journal_bytes\n";
    pressures<<"part_bytes,shape,epochs,material,retention,live_material_bytes,peak_material_bytes,pending_sink_bytes,live_after_readers_sink_retire\n";
    struct Cell {std::size_t size;std::string_view shape;bool merged,roots;Variant variant;};
    struct RecoverCell {std::size_t size;std::string_view shape;bool merged;};
    struct PressureCell {std::size_t size;std::string_view shape;Variant variant;std::string_view retention;};
    std::vector<Cell> cells;std::vector<RecoverCell> recovery_cells;std::vector<PressureCell> pressure_cells;
    std::vector<std::size_t> sizes{4096,65536,1048576,16777216};
    for(auto size:sizes)if(size<=max)for(auto shape:{"sparse","dense","fragmented","overlap","adjacent"}) {
      for(bool merged:{false,true}) {
        for(bool roots:{false,true})for(auto variant:variants)if(roots||!variant.full_hash)cells.push_back({size,shape,merged,roots,variant});
        recovery_cells.push_back({size,shape,merged});
      }
      for(auto variant:variants)if(!variant.full_hash)for(auto retention:{"current","reader_sink","all_readers"})pressure_cells.push_back({size,shape,variant,retention});
    }
    if(argc>4) {std::reverse(cells.begin(),cells.end());std::reverse(recovery_cells.begin(),recovery_cells.end());std::reverse(pressure_cells.begin(),pressure_cells.end());}
    unsigned done=0;
    for(auto cell:cells) {
      material(materials,cell.size,cell.shape,epochs,cell.variant,cell.roots,cell.merged);
      if(++done%26==0) {materials.flush();std::cerr<<"checked material "<<done<<'/'<<cells.size()<<'\n';}
    }
    for(auto cell:recovery_cells) {recovery(recoveries,output/"temporary-journal",cell.size,cell.shape,epochs,cell.merged);recoveries.flush();}
    for(auto cell:pressure_cells)pressure(pressures,cell.size,cell.shape,epochs,cell.variant,cell.retention);
    if(!materials||!recoveries||!pressures)throw std::runtime_error("CSV write");std::cerr<<"checksum "<<consume<<'\n';return 0;
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
