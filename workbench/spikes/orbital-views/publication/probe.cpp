#include "journal.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

using namespace publication;
namespace {
void require(bool value,const char* what) {if(!value)throw std::runtime_error(what);}
template<class F> void rejects(F&& f) {bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,"invalid input accepted");}
Bytes seed(std::size_t n) {Bytes b(n);for(std::size_t j=0;j<n;++j)b[j]=std::uint8_t((j*31+j/37)%251);return b;}
void same(std::span<const std::uint8_t> got,std::span<const std::uint8_t> wanted) {
  require(got.size()==wanted.size()&&std::equal(got.begin(),got.end(),wanted.begin()),"different retained bytes");
}
void material_checks() {
  for(auto bytes:{4096u,65536u,1048576u})for(auto shape:{"sparse","dense","fragmented","overlap","adjacent"}) {
    auto initial=seed(bytes),expected=initial;std::vector<Bytes> histories{initial};
    std::array<Counts,7> counts;
    std::array<Material,7> stores{
      Material(Kind::snapshot,bytes,true,0,counts[0],true),
      Material(Kind::snapshot,bytes,true,0,counts[1]),
      Material(Kind::flat_pages,bytes,true,0,counts[2]),
      Material(Kind::radix_pages,bytes,true,0,counts[3]),
      Material(Kind::block_pages,bytes,true,0,counts[4]),
      Material(Kind::overlay,bytes,true,0,counts[5]),
      Material(Kind::overlay,bytes,true,4,counts[6])};
    std::array<std::vector<Cut>,7> cuts;
    for(std::size_t i=0;i<stores.size();++i)cuts[i].push_back(stores[i].seed(initial));
    for(unsigned e=1;e<=12;++e) {
      auto raw=changes(shape,bytes,e),merged=coalesce(raw);auto alternative=expected;
      publication::apply(expected,raw);publication::apply(alternative,merged);same(expected,alternative);histories.push_back(expected);
      for(std::size_t i=0;i<stores.size();++i) {
        // Alternate raw and merged effects. No candidate shares mutable captures.
        cuts[i].push_back(stores[i].update(cuts[i].back(),capture(e%2?raw:merged,counts[i]),e));
        same(stores[i].read(cuts[i].back(),0,bytes),expected);
        require(stores[i].digest(cuts[i].back())==stores[0].digest(cuts[0].back()),"noncanonical content root");
      }
    }
    for(std::size_t i=0;i<stores.size();++i)for(unsigned e=0;e<cuts[i].size();++e) {
      same(stores[i].read(cuts[i][e],bytes-67,67),std::span(histories[e]).last(67));
      same(stores[i].read(cuts[i][e],0,bytes),histories[e]);
    }
    for(std::size_t i=0;i<stores.size();++i) {
      rejects([&]{stores[i].update(cuts[i].back(),capture({},counts[i]),12);});
      rejects([&]{stores[i].update(cuts[i].back(),capture({Patch{bytes,Bytes{1}}},counts[i]),13);});
      rejects([&]{stores[i].read(cuts[i].back(),bytes+1,0);});
      cuts[i].clear();require(counts[i].live_bytes==0,"captured material leaked");
    }
  }
  // Empty effects and a cross-page, overlapping union retain untouched guards.
  auto bytes=seed(8192),expected=bytes;
  Batch patches{{4094,Bytes{1,2,3,4,5}},{4096,Bytes{9,8}},{0,{}}};
  publication::apply(expected,patches);publication::apply(bytes,coalesce(patches));same(bytes,expected);
  require(coalesce({}).empty(),"empty coalescing");
}
void flip(const std::filesystem::path& path,std::uint64_t offset) {
  int fd=::open(path.c_str(),O_RDWR);require(fd>=0,"corrupt open");std::uint8_t byte;
  require(::pread(fd,&byte,1,offset)==1,"corrupt read");byte^=0x80;
  require(::pwrite(fd,&byte,1,offset)==1,"corrupt write");::close(fd);
}
void fresh(const std::filesystem::path& path,std::uint64_t epoch,std::uint64_t digest) {
  const auto child=::fork();require(child>=0,"fork");
  if(!child) {
    auto e=std::to_string(epoch),h=std::to_string(digest);
    ::execl("/proc/self/exe","publication_probe","--recover",path.c_str(),e.c_str(),h.c_str(),nullptr);::_exit(123);
  }
  int status=0;require(::waitpid(child,&status,0)==child,"wait");require(WIFEXITED(status)&&WEXITSTATUS(status)==0,"fresh-process recovery failed");
}
void recovery_checks() {
  const auto dir=std::filesystem::temp_directory_path()/std::filesystem::path("sixdb-publication-"+std::to_string(::getpid()));
  std::filesystem::create_directories(dir);
  try {
    auto initial=seed(65536),expected=initial;std::vector<Bytes> cuts{initial};
    const auto path=dir/"journal";
    Counts counts;Material material(Kind::radix_pages,initial.size(),true,0,counts);auto current=material.seed(initial);
    auto old_reader=current;Sink sink(initial.size()*2);std::uint64_t consumer=0;
    {
      Journal journal(path,initial);
      for(unsigned e=1;e<=12;++e) {
        const auto batch=changes(e%3==0?"overlap":e%3==1?"fragmented":"dense",initial.size(),e);
        journal.append(e,batch); // Admission retains actual replayable bytes.
        current=material.update(current,capture(batch,counts),e);
        publication::apply(expected,batch);cuts.push_back(expected);consumer=e;
        require(journal.admitted()==consumer,"consumer passed admitted basis");
        if(e<=2)require(sink.submit(e,current,initial.size()),"initial sink submit");
        else if(e==3) {
          require(!sink.submit(e,current,initial.size()),"unbounded pending sink");
          require(sink.installed()==0&&sink.pending()==initial.size()*2,"stalled sink no longer pending");
          fresh(path,e,hash(expected)); // No derived sink has succeeded.
        }
        else if(e==4) {
          require(sink.complete(1,true),"newer sink completion");
          require(sink.installed()==2&&consumer==4,"sink completion changed consumer");
          require(sink.complete(0,false),"failed older sink completion");
          require(sink.installed()==2&&consumer==4,"failed sink changed frontier");
          require(!sink.complete(1,true),"duplicate completion accepted");
          require(sink.submit(e,current,initial.size()),"retry sink submit");
        } else if(e==6) {
          require(sink.submit(e,current,initial.size()),"concurrent newer sink submit");
          require(sink.complete(3,true),"out-of-order sink completion");
          require(sink.complete(2,true),"late older sink completion");
          require(sink.installed()==6&&consumer==6,"late completion regressed frontier");
        }
      }
      rejects([&]{sink.submit(13,current,1);});
      rejects([&]{journal.append(14,{});});
      // Only 80B of the next event survive; it must not publish epoch 13.
      journal.partial_tail(13,changes("dense",initial.size(),13),80);
      require(journal.admitted()==12,"incomplete event admitted");
      same(material.read(old_reader,0,initial.size()),initial);
    }
    fresh(path,12,hash(expected));
    {
      Recovery lazy(path);require(lazy.latest()==12,"wrong recovered head");
      require(lazy.counts.payload_read==0,"lazy startup hydrated payload");
      for(unsigned e=0;e<cuts.size();++e) {
        same(lazy.eager(e),cuts[e]);same(lazy.read_latest(e,0,initial.size()),cuts[e]);
        same(lazy.read(e,4093,73),std::span(cuts[e]).subspan(4093,73));
        same(lazy.read_latest(e,4093,73),std::span(cuts[e]).subspan(4093,73));
      }
      rejects([&]{lazy.read(13,0,1);});rejects([&]{lazy.read(12,65536,1);});
    }
    auto corrupt=dir/"corrupt";
    // Header/table extent is known independently of Recovery's parser.
    const auto first=32+(initial.size()/block_bytes+1)*8+initial.size();
    std::filesystem::copy_file(path,corrupt);flip(corrupt,first+24);rejects([&]{Recovery r(corrupt);});
    std::filesystem::copy_file(path,corrupt,std::filesystem::copy_options::overwrite_existing);
    flip(corrupt,first+56+2);rejects([&]{Recovery r(corrupt);});
    // Epoch 1 is fragmented: 128 descriptors of 32B, each with one payload byte.
    std::filesystem::copy_file(path,corrupt,std::filesystem::copy_options::overwrite_existing);
    flip(corrupt,first+56+128*32);
    rejects([&]{Recovery r(corrupt);r.eager(1);});
    current.reset();old_reader.reset();require(sink.pending()==0,"sink pins after completion");
    require(counts.live_bytes==0,"retained source after readers and sink retire");
  } catch(...) {std::filesystem::remove_all(dir);throw;}
  std::filesystem::remove_all(dir);
}
}
int main(int argc,char** argv) {
  try {
    if(argc==5&&std::string_view(argv[1])=="--recover") {
      Recovery r(argv[2]);const auto e=std::stoull(argv[3]);require(r.latest()==e,"fresh recovered head");
      auto a=r.eager(e),b=r.read_latest(e,0,r.size());same(a,b);require(hash(a)==std::stoull(argv[4]),"fresh recovered bytes");
      return 0;
    }
    material_checks();recovery_checks();std::cout<<"retained cuts, canonical roots, bounded sink, torn tail and fresh recovery: ok\n";
    return 0;
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
