#include "../algebra.h"
#include "../analyse.h"
#include "../storage_decision.h"
#include "../bench_support.h"
#include "../fixture.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/resource.h>
using namespace ikea::heterogeneous;
using namespace ikea::heterogeneous::measurement;
namespace {
using Window = std::array<PlainBlock, 256>;
struct Triple { Window a{}, b{}, c{}; };
struct Dataset { std::string name; std::vector<Triple> triples; };

std::vector<Dataset> load_inputs(const std::filesystem::path &directory) {
  std::vector<Dataset> result;
  for (auto name : {"structural", "random_dense", "terminal_mix"}) {
    Dataset dataset{name, {}};
    for (unsigned row = 0; row < 8; ++row) {
      Triple t;
      for (unsigned i = 0; i < 256; ++i) {
        for (unsigned j = 0; j < 32; ++j) {
          t.a[i][j] = mix(row * 8192 + i * 32 + j);
          t.b[i][j] = mix(987654 + row * 8192 + i * 32 + j);
          t.c[i][j] = (i % 8 == row) ? 255 : 0;
        }
        if (dataset.name == "structural") {
          t.a[i] = structural_blocks(1, (i + 29 * row) % 257)[0];
          t.b[i] = structural_blocks(1, (173 * i + row) % 257)[0];
        } else if (dataset.name == "terminal_mix") {
          if (i % 5) t.a[i].fill(i % 5 == 1 ? 255 : 0);
          if (i % 7) t.b[i].fill(i % 7 == 1 ? 255 : 0);
        }
      }
      dataset.triples.push_back(t);
    }
    result.push_back(std::move(dataset));
  }
  if (!directory.empty()) {
    std::vector<std::filesystem::path> files;
    for (auto p : std::filesystem::directory_iterator(directory))
      if (p.path().extension() == ".pairs") files.push_back(p.path());
    std::sort(files.begin(), files.end());
    for (auto p : files) {
      require(std::filesystem::file_size(p) % 24576 == 0, "pair file extent");
      Dataset dataset{p.stem().string(), {}};
      std::ifstream input(p, std::ios::binary);
      for (unsigned i = 0; i < std::filesystem::file_size(p) / 24576; ++i) {
        Triple t;
        input.read(reinterpret_cast<char *>(t.a.data()), 8192);
        input.read(reinterpret_cast<char *>(t.b.data()), 8192);
        input.read(reinterpret_cast<char *>(t.c.data()), 8192);
        require(bool(input), "pair read");
        dataset.triples.push_back(t);
      }
      require(!dataset.triples.empty(), "empty dataset");
      result.push_back(std::move(dataset));
    }
  }
  return result;
}

SliceMask make_mask(const std::string &name, const Window &third) {
  SliceMask mask{};
  for (unsigned i = 0; i < 256; ++i) {
    const bool active = name == "all" || (name == "single255" && i == 255) ||
      (name == "boundary7" && (i == 15 || i == 16 || i == 63 || i == 64 || i == 127 || i == 128 || i == 255)) ||
      (name == "cluster32" && i >= 112 && i < 144) ||
      (name == "dispersed32" && i % 8 == 3) ||
      (name == "alternating128" && i % 2 == 0) ||
      (name == "third_candidates" && std::any_of(third[i].begin(), third[i].end(), [](auto v) { return v != 0; }));
    if (active) mask[i / 64] |= std::uint64_t{1} << (i % 64);
  }
  return mask;
}
U hash_bytes(const std::uint8_t *data, unsigned size) {
  U sum = 0;
  for (unsigned i = 0; i < size; ++i) sum = (sum ^ data[i]) * 1099511628211ULL;
  return sum;
}
unsigned population(const PlainBlock &b) {
  unsigned result = 0; for (auto x : b) result += std::popcount(x); return result;
}
struct Sources {
  std::shared_ptr<AlignedBytes> plain;
  EncodedSource encoded;
  std::array<AdmittedBitset, 4> admitted;
  std::array<U, 4> footprint{};
  explicit Sources(const Window &bits) : plain(std::make_shared<AlignedBytes>(8192)), encoded(encode_source(bits)) {
    std::memcpy(plain->data(), bits.data(), 8192);
    require(admit_plain(plain, admitted[0]) == AdmissionError::none, "plain admission"); footprint[0] = 8192;
    for (unsigned i = 0; i < 3; ++i) {
      auto metadata = std::make_shared<MetadataOwner>(encoded, MetadataKind(i));
      footprint[i + 1] = metadata->bytes().size() + encoded.body()->bytes.size();
      require(admit_bec(metadata, encoded.body(), admitted[i + 1]) == AdmissionError::none, "BEC admission");
    }
  }
};
struct Configuration {
  const char *name; unsigned left, right;
  Resolution left_resolution, right_resolution;
  AlgebraExecution execution;
  constexpr Configuration(const char *n, unsigned l, unsigned r, Resolution mode, AlgebraExecution e)
      : name(n), left(l), right(r), left_resolution(mode), right_resolution(mode), execution(e) {}
  constexpr Configuration(const char *n, unsigned l, unsigned r, Resolution lm, Resolution rm, AlgebraExecution e)
      : name(n), left(l), right(r), left_resolution(lm), right_resolution(rm), execution(e) {}
};
const Configuration configurations[] = {
  {"plain",0,0,Resolution::point,AlgebraExecution::factored},
  {"direct",1,1,Resolution::point,AlgebraExecution::factored},
  {"local-point",2,2,Resolution::point,AlgebraExecution::factored},
  {"local-frame",2,2,Resolution::cached16,AlgebraExecution::factored},
  {"local-inline",2,2,Resolution::cached16,AlgebraExecution::local_inline},
  {"scan-point",3,3,Resolution::point,AlgebraExecution::factored},
  {"scan-frame",3,3,Resolution::cached16,AlgebraExecution::factored},
  {"mixed-point",2,3,Resolution::point,AlgebraExecution::factored},
  {"mixed-frame",2,3,Resolution::cached16,AlgebraExecution::factored},
  {"mixed-point-frame",2,3,Resolution::point,Resolution::cached16,AlgebraExecution::factored},
  {"mixed-frame-point",2,3,Resolution::cached16,Resolution::point,AlgebraExecution::factored},
  {"bec-plain-point",2,0,Resolution::point,AlgebraExecution::factored},
  {"bec-plain-frame",2,0,Resolution::cached16,AlgebraExecution::factored},
};
struct AlgebraItem { PreparedAlgebra prepared; SliceMask mask; std::shared_ptr<AlignedBytes> output; U expected; };
struct Accounting { U selected=0, bec=0, plain=0, frames=0, left_bytes=0, right_bytes=0; };
Accounting account(const Triple &t, const SliceMask &mask, const Configuration &config, bool is_union) {
  Accounting a;
  std::array<bool,16> visited{};
  for (unsigned i = 0; i < 256; ++i) if ((mask[i/64] >> (i%64)) & 1) {
    ++a.selected; visited[i/16]=true;
    const bool lc=config.left!=0, rc=config.right!=0;
    const unsigned p=population(t.a[i]), q=population(t.b[i]);
    bool read_left=true, read_right=true;
    if ((lc && p==(is_union?256u:0u)) || (rc && q==(is_union?256u:0u))) read_left=read_right=false;
    else if (lc && p==(is_union?0u:256u)) { read_left=false; if(rc && (q==0 || q==256)) read_right=false; }
    else if (rc && q==(is_union?0u:256u)) read_right=false;
    a.bec += (read_left && lc) + (read_right && rc);
    a.plain += (read_left && !lc) + (read_right && !rc);
  }
  a.frames = std::count(visited.begin(),visited.end(),true) *
      ((config.left > 1 && config.left_resolution == Resolution::cached16) +
       (config.right > 1 && config.right_resolution == Resolution::cached16));
  return a;
}
__attribute__((noinline)) void repeat_algebra(const std::vector<AlgebraItem> &items, U passes, bool selected_only) {
  for (U pass=0; pass<passes; ++pass) for (const auto &item:items) {
    if (selected_only) item.prepared.apply_selected(item.mask); else item.prepared.apply(item.mask);
  }
}
U output_checksum(const std::vector<AlgebraItem> &items) {
  U sum=0; for(const auto &item:items) { const auto h=hash_bytes(item.output->data(),8192); require(h==item.expected,"algebra checksum");sum+=h; }return sum;
}
void algebra_bench(const std::vector<Dataset> &datasets, unsigned repetitions, U minimum, bool pmu, int cpu) {
  std::cout << "dataset,mask,operation,configuration,policy,repetition,pairs,passes,calls,selected_slices,bec_decodes,plain_loads,metadata_frames,left_bytes,right_bytes,output_bytes,checksum,elapsed_ns,ns_per_call,cycles_raw,instructions_raw,enabled_ns,running_ns,pmu_status,major_faults,minor_faults,residence\n";
  for (const auto &dataset:datasets) {
    std::vector<Sources> left,right;
    for(const auto &t:dataset.triples) { left.emplace_back(t.a); right.emplace_back(t.b); }
    for(auto name:{"none","all","single255","boundary7","cluster32","dispersed32","alternating128","third_candidates"})
      for(bool is_union:{false,true}) for(const auto &config:configurations) for(bool selected_only:{false,true}) {
        // Active-only controls are limited to the direct plain and shared Local
        // frame routes; their output obligation differs from a complete result.
        if(selected_only && std::string(config.name)!="plain" && std::string(config.name)!="local-frame") continue;
        std::vector<AlgebraItem> items;
        Accounting total;
        for(unsigned i=0;i<dataset.triples.size();++i) {
          const auto &t=dataset.triples[i]; auto mask=make_mask(name,t.c);
          auto output=std::make_shared<AlignedBytes>(8192); std::memset(output->data(),0xa5,8192);
          auto expected=std::make_unique<AlignedBytes>(8192);
          std::memset(expected->data(),selected_only?0xa5:0,8192);
          for(unsigned s=0;s<256;++s) if((mask[s/64]>>(s%64))&1)
            for(unsigned b=0;b<32;++b) expected->data()[32*s+b]=is_union?(t.a[s][b]|t.b[s][b]):(t.a[s][b]&t.b[s][b]);
          PreparedAlgebra prepared;
          require(prepare_algebra(left[i].admitted[config.left],right[i].admitted[config.right],output,
              is_union?SetOperation::set_union:SetOperation::intersection,config.left_resolution,config.right_resolution,config.execution,prepared)==AlgebraError::none,"algebra preparation");
          items.push_back({std::move(prepared),mask,output,hash_bytes(expected->data(),8192)});
          const auto counters=account(t,mask,config,is_union);
          total.selected+=counters.selected;total.bec+=counters.bec;total.plain+=counters.plain;total.frames+=counters.frames;
          total.left_bytes+=left[i].footprint[config.left];total.right_bytes+=right[i].footprint[config.right];
        }
        U passes=1;
        for(;;) {auto start=now();repeat_algebra(items,passes,selected_only);auto elapsed=now()-start;output_checksum(items);if(elapsed>=minimum)break;passes*=2;}
        repeat_algebra(items,1,selected_only);output_checksum(items);
        for(unsigned rep=0;rep<repetitions;++rep) {
          Counters perf(pmu);rusage before{},after{};getrusage(RUSAGE_SELF,&before);require(sched_getcpu()==cpu,"CPU before");
          perf.start();auto start=now();repeat_algebra(items,passes,selected_only);auto elapsed=now()-start;perf.stop();
          require(sched_getcpu()==cpu,"CPU after");getrusage(RUSAGE_SELF,&after);auto checksum=output_checksum(items);
          const U calls=passes*items.size();
          std::cout<<dataset.name<<','<<name<<','<<(is_union?"union":"intersection")<<','<<config.name<<','<<(selected_only?"selected":"complete")<<','<<rep<<','<<items.size()<<','<<passes<<','<<calls<<','<<total.selected*passes<<','<<total.bec*passes<<','<<total.plain*passes<<','<<total.frames*passes<<','<<total.left_bytes<<','<<total.right_bytes<<','<<items.size()*8192<<','<<checksum<<','<<elapsed<<','<<double(elapsed)/calls<<','<<perf.cycles<<','<<perf.instructions<<','<<perf.enabled<<','<<perf.running<<','<<perf.status<<','<<after.ru_majflt-before.ru_majflt<<','<<after.ru_minflt-before.ru_minflt<<",unestablished\n";
        }
      }
    std::cerr<<"algebra "<<dataset.name<<" complete\n";
  }
}

struct AnalyseItem { const std::uint8_t *plain; unsigned actual; unsigned estimate; };
__attribute__((noinline)) U repeat_analysis(const std::vector<AnalyseItem> &items,U passes,
    AnalyseModel model,AnalyseScan scan,unsigned stage,std::uint8_t *scratch) {
  U total=0;
  for(U pass=0;pass<passes;++pass) for(const auto &item:items) {
    if(stage==0) total+=predict_body_bytes(item.plain,model,scan);
    else if(stage==1) total+=encode_body_bytes(item.plain,scratch);
    else {
      const unsigned estimate=predict_body_bytes(item.plain,model,scan);
      if(prefer_bec(bec_storage_bytes(estimate, MetadataKind::local16, ByteScope::readable_extent)))
        total+=encode_body_bytes(item.plain,scratch)+512+64;
      else total+=8192;
    }
  }
  return total;
}
void analyser_bench(const std::vector<Dataset> &datasets,unsigned repetitions,U minimum,bool pmu,int cpu) {
  std::cout<<"dataset,model,scan,stage,repetition,windows,passes,calls,conversions,checksum,elapsed_ns,ns_per_call,cycles_raw,instructions_raw,enabled_ns,running_ns,pmu_status,major_faults,minor_faults,residence\n";
  AlignedBytes scratch(analyse_encode_writable_bytes);
  for(const auto &dataset:datasets) for(auto model:{AnalyseModel::cheap,AnalyseModel::quadrants})
    for(auto scan:{AnalyseScan::full,AnalyseScan::sample32}) for(unsigned stage=0;stage<3;++stage) {
      if(stage==1 && (model!=AnalyseModel::cheap || scan!=AnalyseScan::full)) continue;
      std::vector<AnalyseItem> items;U expected=0,conversions=0;
      for(const auto &t:dataset.triples) for(const auto *window:{&t.a,&t.b}) {
        const auto *plain=reinterpret_cast<const std::uint8_t *>(window->data());
        const unsigned actual=encode_body_bytes(plain,scratch.data()), estimate=predict_body_bytes(plain,model,scan);
        items.push_back({plain,actual,estimate});
        const bool convert=stage==1 || (stage==2 && prefer_bec(bec_storage_bytes(
            estimate, MetadataKind::local16, ByteScope::readable_extent)));conversions+=convert;
        expected+=stage==0?estimate:stage==1?actual:convert?actual+512+64:8192;
      }
      U passes=1;
      for(;;) {auto start=now();auto value=repeat_analysis(items,passes,model,scan,stage,scratch.data());auto elapsed=now()-start;require(value==expected*passes,"analysis calibration");if(elapsed>=minimum)break;passes*=2;}
      require(repeat_analysis(items,1,model,scan,stage,scratch.data())==expected,"analysis warm");
      for(unsigned rep=0;rep<repetitions;++rep) {
        Counters perf(pmu);rusage before{},after{};getrusage(RUSAGE_SELF,&before);require(sched_getcpu()==cpu,"CPU before");
        perf.start();auto start=now();auto checksum=repeat_analysis(items,passes,model,scan,stage,scratch.data());auto elapsed=now()-start;perf.stop();
        require(sched_getcpu()==cpu,"CPU after");getrusage(RUSAGE_SELF,&after);require(checksum==expected*passes,"analysis checksum");
        U calls=passes*items.size();
        std::cout<<dataset.name<<','<<(model==AnalyseModel::cheap?"cheap":"quadrants")<<','<<analyse_scan_name(scan)<<','<<(stage==0?"predict":stage==1?"encode":"predict-then-encode")<<','<<rep<<','<<items.size()<<','<<passes<<','<<calls<<','<<conversions*passes<<','<<checksum<<','<<elapsed<<','<<double(elapsed)/calls<<','<<perf.cycles<<','<<perf.instructions<<','<<perf.enabled<<','<<perf.running<<','<<perf.status<<','<<after.ru_majflt-before.ru_majflt<<','<<after.ru_minflt-before.ru_minflt<<",unestablished\n";
      }
      std::cerr<<"analyser "<<dataset.name<<' '<<stage<<" complete\n";
    }
}
} // namespace
int main(int argc,char **argv) {
  try {
    unsigned repetitions=5;U minimum=5000000;bool pmu=false;std::filesystem::path data;std::string part="algebra";
    for(int i=1;i<argc;++i) {std::string arg=argv[i];
      if(arg=="--data"&&i+1<argc)data=argv[++i];
      else if(arg=="--part"&&i+1<argc)part=argv[++i];
      else if(arg=="--pmu")pmu=true;
      else if(arg=="--quick"){repetitions=1;minimum=100000;}
      else if(arg=="--repetitions"&&i+1<argc)repetitions=std::stoul(argv[++i]);
      else throw std::runtime_error("unknown argument: "+arg);
    }
    require(repetitions>0,"zero repetitions");int cpu=pin();auto datasets=load_inputs(data);
    if(part=="algebra")algebra_bench(datasets,repetitions,minimum,pmu,cpu);
    else if(part=="analyser")analyser_bench(datasets,repetitions,minimum,pmu,cpu);
    else throw std::runtime_error("unknown part");
    return 0;
  } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
