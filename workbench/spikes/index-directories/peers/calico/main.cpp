#include <calico/runtime.h>
#include <calico/runtime_telemetry.h>
#include <engine/wire.h>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <time.h>
#include <unistd.h>

namespace {
using Words=std::array<std::uint64_t,8>;
void check(bool yes,const char* why) {
  if (yes) return;
  std::fprintf(stderr,"calico-m3: %s\n",why); std::abort();
}
template<class Status> void checked(const Status& status) {
  if (status.ok) return;
  std::fprintf(stderr,"Calico: %s\n",status.error.c_str()); std::abort();
}
std::uint64_t ns(clockid_t clock) {
  timespec t{}; check(!clock_gettime(clock,&t),"clock_gettime");
  return std::uint64_t(t.tv_sec)*1000000000+t.tv_nsec;
}
struct Clock {
  std::uint64_t wall=ns(CLOCK_MONOTONIC_RAW),owner=ns(CLOCK_THREAD_CPUTIME_ID),
      process=ns(CLOCK_PROCESS_CPUTIME_ID);
};
struct Result { std::uint64_t wall{},owner{},process{},sum{}; };
Result stop(const Clock& start,std::uint64_t sum) {
  const auto wall=ns(CLOCK_MONOTONIC_RAW);
  const auto owner=ns(CLOCK_THREAD_CPUTIME_ID);
  const auto process=ns(CLOCK_PROCESS_CPUTIME_ID);
  return {wall-start.wall,owner-start.owner,process-start.process,sum};
}
void report(const char* operation,const char* arm,const char* stream,
    std::size_t population,std::size_t requests,const Result& r) {
  std::printf("%s,%s,%s,1,%zu,%zu,%.3f,%.3f,%.3f,%llu\n",operation,arm,stream,
      population,requests,double(r.wall)/requests,double(r.owner)/requests,
      double(r.process)/requests,static_cast<unsigned long long>(r.sum));
}
std::uint64_t mix(std::uint64_t x) {
  x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);
  x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);
  return x^(x>>31);
}
// Same logical M3 inputs as driver.cpp. No directory descriptors or locators.
std::vector<Words> records(std::size_t count) {
  std::vector<Words> rows(count);
  for (std::size_t i=0;i<count;++i)
    for (unsigned j=0;j<8;++j) rows[i][j]=mix(i*17+j+71);
  return rows;
}
std::uint32_t key(std::size_t index,bool sparse) {
  return std::uint32_t(sparse?index*16:index);
}
void load(calico::runtime& rt,const std::vector<Words>& rows,bool sparse) {
  engine::wire::wire_stream_schema schema;
  schema.k_bits=32;
  for (unsigned j=0;j<8;++j)
    schema.cols.push_back({engine::wire::wire_role::payload,engine::wire::wire_colkind::fixed,8});
  engine::wire::wire_writer writer;
  check(writer.begin(schema)==0,"wire begin");
  for (std::size_t first=0;first<rows.size();) {
    const auto high=key(first,sparse)>>16;
    auto end=first;
    while (end<rows.size() && (key(end,sparse)>>16)==high) ++end;
    std::vector<std::uint16_t> positions(end-first);
    std::array<std::vector<std::uint8_t>,8> columns;
    std::array<engine::wire::wire_pull_col,8> pull{};
    for (unsigned j=0;j<8;++j) {
      columns[j].resize((end-first)*8);
      for (std::size_t i=first;i<end;++i)
        for (unsigned b=0;b<8;++b) columns[j][(i-first)*8+b]=std::uint8_t(rows[i][j]>>(b*8));
      pull[j].fixed=columns[j].data();
    }
    for (std::size_t i=first;i<end;++i) positions[i-first]=std::uint16_t(key(i,sparse));
    const std::uint8_t prefix[]{std::uint8_t(high>>8),std::uint8_t(high)};
    check(writer.add_pull(prefix,2,positions.data(),std::uint32_t(end-first),pull.data(),8)==0,"wire window");
    first=end;
  }
  std::vector<std::uint8_t> bytes;
  check(writer.finish(bytes)==0,"wire finish");
  char path[]="/tmp/sixdb-calico-m3-XXXXXX";
  const int fd=mkstemp(path); check(fd>=0,"temporary wire");
  FILE* file=fdopen(fd,"wb"); check(file,"fdopen");
  check(std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size(),"wire write");
  check(!std::fclose(file),"wire close");
  const auto result=rt.write(std::string("COPY m3 (v0,v1,v2,v3,v4,v5,v6,v7) FROM '")+path+"'");
  check(!unlink(path),"wire unlink"); checked(result);
  check(result.affected==rows.size(),"COPY row count");
}
template<class Row> std::uint64_t consume(const Row& row,bool full) {
  check(row.found && (row.cells.size()==8 || (!full && row.cells.size()==1)),"point result shape");
  std::uint64_t value=row.cells[0].value;
  if (full) for (unsigned j=1;j<8;++j) value^=row.cells[j].value;
  return value;
}
struct Visit { bool called{}; std::uint64_t value{}; bool full{}; };
void visit(void* context,const calico::runtime_row_view& row) noexcept {
  auto& out=*static_cast<Visit*>(context);
  out.called=true; out.value=consume(row,out.full);
}
// Both real result APIs are checked field by field, before and after updates.
// The exact oracle does not share the timed XOR consumer.
void oracle(calico::runtime& rt,const calico::runtime_statement& full,
    const std::vector<Words>& rows,bool sparse) {
  calico::runtime_row_buffer buffer; buffer.reserve(8,0);
  calico::runtime_row_view view; view.reserve(8);
  for (std::size_t i=0;i<rows.size();++i) {
    checked(rt.execute(full,buffer,{key(i,sparse)}));
    check(buffer.found && buffer.cells.size()==8,"oracle result shape");
    for (unsigned j=0;j<8;++j)
      check(!buffer.cells[j].flags && buffer.cells[j].value==rows[i][j],"buffer exact material oracle");
    struct Capture { const Words* expected; bool called{}; } capture{&rows[i]};
    const auto exact=+[](void* context,const calico::runtime_row_view& row) noexcept {
      auto& c=*static_cast<Capture*>(context); c.called=true;
      check(row.found && row.cells.size()==8,"visitor oracle result shape");
      for (unsigned j=0;j<8;++j)
        check(!row.cells[j].flags && row.cells[j].value==(*c.expected)[j],"visitor exact material oracle");
    };
    checked(rt.execute(full,view,exact,&capture,{key(i,sparse)}));
    check(capture.called,"visitor not called");
  }
  checked(rt.execute(full,buffer,{UINT32_MAX}));
  check(!buffer.found,"absent key returned a row");
}
std::uint64_t expected(const std::vector<Words>& rows,std::span<const unsigned> indices,
    bool dependent,bool full) {
  std::uint64_t sum=0,state=0;
  for (std::size_t i=0;i<indices.size();++i) {
    const auto index=dependent?state&(rows.size()-1):indices[i];
    std::uint64_t value=rows[index][0];
    if (full) for (unsigned j=1;j<8;++j) value^=rows[index][j];
    sum+=value; state=mix(value+i);
  }
  return sum;
}
template<bool Visitor> Result read(calico::runtime& rt,const calico::runtime_statement& stmt,
    std::size_t population,std::span<const unsigned> indices,bool sparse,bool dependent,bool full) {
  calico::runtime_row_buffer buffer;
  calico::runtime_row_view view;
  if constexpr (Visitor) view.reserve(8);
  else buffer.reserve(8,0); // also covers the explicit full8 scalar fallback
  std::uint64_t sum=0,state=0;
  const Clock start;
  for (std::size_t i=0;i<indices.size();++i) {
    const auto index=dependent?state&(population-1):indices[i];
    std::uint64_t value;
    if constexpr (Visitor) {
      Visit out{false,0,full};
      checked(rt.execute(stmt,view,visit,&out,{key(index,sparse)}));
      check(out.called,"timed visitor not called"); value=out.value;
    } else {
      checked(rt.execute(stmt,buffer,{key(index,sparse)})); value=consume(buffer,full);
    }
    sum+=value; state=mix(value+i);
  }
  return stop(start,sum);
}
Result update(calico::runtime& rt,const calico::runtime_statement& stmt,
    std::span<const unsigned> indices,bool sparse) {
  std::uint64_t sum=0;
  const Clock start;
  for (std::size_t i=0;i<indices.size();++i) {
    const auto value=mix(i+UINT64_C(0x123456789));
    const auto result=rt.execute(stmt,{key(indices[i],sparse),value});
    checked(result); check(result.affected==1,"point assignment affected count"); sum+=value;
  }
  return stop(start,sum);
}
}
int main(int argc,char** argv) {
  check(argc==6 || argc==7,"usage: index_directory_calico POPULATION READS UPDATES dense|sparse buffer-first|visitor-first [recordwise|fieldwise]");
  const auto population=std::strtoull(argv[1],nullptr,10);
  const auto reads=std::strtoull(argv[2],nullptr,10),updates=std::strtoull(argv[3],nullptr,10);
  check(population && std::has_single_bit(population) && population<=(1u<<24),"population power-of-two limit");
  check(reads && !(reads%64) && updates,"reads positive multiple64; updates positive");
  check(!std::strcmp(argv[4],"dense") || !std::strcmp(argv[4],"sparse"),"dense|sparse");
  check(!std::strcmp(argv[5],"buffer-first") || !std::strcmp(argv[5],"visitor-first"),"buffer-first|visitor-first");
  check(argc==6 || !std::strcmp(argv[6],"recordwise") || !std::strcmp(argv[6],"fieldwise"),"recordwise|fieldwise");
  const bool sparse=!std::strcmp(argv[4],"sparse"),visitor_first=!std::strcmp(argv[5],"visitor-first");
  const bool fieldwise=argc==7 && !std::strcmp(argv[6],"fieldwise");
  calico::runtime::config config;
  config.arena_pages=1u<<20; config.location_mb=1;
  config.descriptor_preset=foyer::transactional_preset(); // actual YCSB posture
  if (fieldwise) {
    config.descriptor_preset.primary_payload=foyer::payload_layout::FIELDWISE;
    config.descriptor_preset.primary_for_anchor=foyer::for_anchor_policy::exact_min;
  }
  calico::runtime rt(config); check(rt.ok(),rt.error().c_str());
  checked(rt.apply_ddl("CREATE TABLE m3 (k INTEGER PRIMARY KEY, v0 BIGINT NOT NULL, "
      "v1 BIGINT NOT NULL, v2 BIGINT NOT NULL, v3 BIGINT NOT NULL, v4 BIGINT NOT NULL, "
      "v5 BIGINT NOT NULL, v6 BIGINT NOT NULL, v7 BIGINT NOT NULL)"));
  auto rows=records(population); load(rt,rows,sparse);
  auto scalar=rt.prepare("SELECT v0 FROM m3 WHERE k=$1");
  auto full=rt.prepare("SELECT v0,v1,v2,v3,v4,v5,v6,v7 FROM m3 WHERE k=$1");
  auto assignment=rt.prepare("UPDATE m3 SET v3=$2 WHERE k=$1");
  check(scalar.ok(),scalar.error().c_str()); check(full.ok(),full.error().c_str());
  check(assignment.ok(),assignment.error().c_str());
  // Exercise unsigned boundaries without changing the measured logical input.
  for (auto value:{UINT64_C(0),UINT64_MAX}) {
    const auto changed=rt.execute(assignment,{key(0,sparse),value});
    checked(changed); check(changed.affected==1,"boundary assignment");
    rows[0][3]=value; oracle(rt,full,rows,sparse);
  }
  rows[0][3]=mix(74); checked(rt.execute(assignment,{key(0,sparse),rows[0][3]}));
  const auto absent=rt.execute(assignment,{UINT32_MAX,UINT64_MAX});
  checked(absent); check(!absent.affected,"missing assignment changed a row");
  oracle(rt,full,rows,sparse);
  calico::runtime_row_buffer probe; probe.reserve(1,0);
  const auto narrow=rt.execute(scalar,probe,{key(0,sparse)});
  const bool scalar_full8=!narrow.ok;
  calico::runtime_row_view probe_view; probe_view.reserve(1);
  Visit probe_visit;
  const auto narrow_visit=rt.execute(scalar,probe_view,visit,&probe_visit,{key(0,sparse)});
  check(narrow_visit.ok==narrow.ok,"narrow sink capability differs");
  if (scalar_full8) {
    // Current numeric RECORDWISE partial-roster bind refuses this projection.
    // Keep a valid public SELECT fallback and expose the extra projected work.
    check(!fieldwise && narrow.error=="point SELECT f-array cursor open failed","unexpected scalar projection refusal");
    check(narrow_visit.error==narrow.error && !probe_visit.called,"unexpected visitor projection refusal");
    std::fprintf(stderr,"narrow_scalar_unavailable=%s; scalar executes full8 SELECT and consumes v0\n",narrow.error.c_str());
  } else check(probe.found && probe.cells.size()==1 && !probe.cells[0].flags &&
      probe.cells[0].value==rows[0][0] && probe_visit.called && probe_visit.value==rows[0][0],"narrow scalar oracle");
  std::fprintf(stderr,"Calico current-call M3; compiler=%s preset=transactional "
      "serving=transient-memfd logical=k+8u64; COPY/preparation/oracles untimed; "
      "reads=execute+numeric-consume; B1=execute-scalar-assignment; sink_order=%s "
      "payload=%s anchor=%s scalar_projected_fields=%u\n",
      __clang_version__,argv[5],fieldwise?"fieldwise":"recordwise",
      fieldwise?"exact_min":"zero",scalar_full8?8u:1u);
  std::fprintf(stderr,"unavailable: retained old-cut/cohort snapshot across publication; "
      "atomic64-key writes; prepared durable mutation. Cohort column1 means one current call.\n");
  std::fprintf(stderr,"native_api_bytes: read_status=%zu update_result=%zu buffer_cell=%zu visitor_cell=%zu\n",
      sizeof(calico::runtime_status),sizeof(calico::runtime_result),sizeof(foyer::point_cell),sizeof(foyer::point_view_cell));
  std::puts("operation,arm,stream,cohort,population,requests,wall_ns_op,owner_ns_op,process_ns_op,checksum");
  std::vector<unsigned> indices(reads);
  for (std::size_t i=0;i<reads;++i) indices[i]=unsigned(mix(i+17)&(population-1));
  for (bool all:{false,true}) for (bool dependent:{false,true}) {
    const auto& stmt=(all || scalar_full8)?full:scalar;
    Result buffer,visitor;
    if (visitor_first) visitor=read<true>(rt,stmt,population,indices,sparse,dependent,all);
    buffer=read<false>(rt,stmt,population,indices,sparse,dependent,all);
    if (!visitor_first) visitor=read<true>(rt,stmt,population,indices,sparse,dependent,all);
    const auto sum=expected(rows,indices,dependent,all);
    check(buffer.sum==sum && visitor.sum==sum,"complete read checksum");
    report(all?"material64":"project_u64","calico-buffer",dependent?"dependent":"independent",population,reads,buffer);
    report(all?"material64":"project_u64","calico-visitor",dependent?"dependent":"independent",population,reads,visitor);
  }
  indices.resize(updates);
  for (std::size_t i=0;i<updates;++i) indices[i]=unsigned(mix(i+37)&(population-1));
  const auto result=update(rt,assignment,indices,sparse);
  std::uint64_t sum=0;
  for (std::size_t i=0;i<updates;++i) {
    const auto value=mix(i+UINT64_C(0x123456789));
    rows[indices[i]][3]=value; sum+=value;
  }
  check(result.sum==sum,"assignment checksum"); oracle(rt,full,rows,sparse);
  report("move_material64","calico","independent",population,updates,result);
}
