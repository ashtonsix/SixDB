#include "prototype.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <numeric>
#include <string>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
void check(bool ok) { if (!ok) std::abort(); }
template<class T> T get(views::Result<T> result) { check(bool(result)); return std::move(*result); }
double ns(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::nano>(b-a).count();
}
struct Census { long fds{}, vmas{}, rss_kib{}, virtual_kib{}; };
Census census() {
  Census c;
  auto* dir = ::opendir("/proc/self/fd"); check(dir);
  while (const auto* entry = ::readdir(dir)) if (entry->d_name[0] != '.') ++c.fds;
  ::closedir(dir);
  std::ifstream maps("/proc/self/maps"); std::string line;
  while (std::getline(maps, line)) ++c.vmas;
  std::ifstream status("/proc/self/status");
  while (std::getline(status, line)) {
    long value{};
    if (std::sscanf(line.c_str(), "VmRSS: %ld kB", &value) == 1) c.rss_kib = value;
    if (std::sscanf(line.c_str(), "VmSize: %ld kB", &value) == 1) c.virtual_kib = value;
  }
  return c;
}
std::uint8_t value(std::size_t part, std::size_t byte) {
  return static_cast<std::uint8_t>((part*43 + byte*17 + 29) % 251);
}
views::Bytes bytes(std::size_t part, std::size_t size) {
  views::Bytes result(size);
  for (std::size_t i=0;i<size;++i) result[i]=value(part,i);
  return result;
}
struct Chunk {
  int fd{-1}; std::size_t size{}; std::uint8_t* mapped{};
  Chunk(std::size_t first, std::size_t parts, std::size_t part_bytes) : size(parts*part_bytes) {
    fd = static_cast<int>(::syscall(SYS_memfd_create, "orbital-packed-source", MFD_CLOEXEC|MFD_ALLOW_SEALING));
    check(fd>=0 && ::ftruncate(fd, static_cast<off_t>(size))==0);
    // Same initialized payload; one source file instead of one per logical part.
    for (std::size_t i=0;i<parts;++i) {
      auto input=bytes(first+i,part_bytes); std::size_t done{};
      while (done<input.size()) {
        auto n=::pwrite(fd,input.data()+done,input.size()-done,static_cast<off_t>(i*part_bytes+done));
        check(n>0); done+=static_cast<std::size_t>(n);
      }
    }
    check(::fcntl(fd,F_ADD_SEALS,F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL)==0);
  }
  void map() {
    mapped=static_cast<std::uint8_t*>(::mmap(nullptr,size,PROT_READ,MAP_PRIVATE,fd,0));
    check(mapped!=MAP_FAILED);
  }
  ~Chunk() { if(mapped) ::munmap(mapped,size); if(fd>=0) ::close(fd); }
};
void cell(std::size_t group, std::size_t n, std::size_t part_bytes, int shape, int loops) {
  // Warm library and /proc machinery before collecting process deltas.
  { views::Part warm(bytes(0,16)); auto held=get(warm.open(1,std::vector<std::size_t>{0})); }
  (void)census();
  const auto before=census(); const auto a=Clock::now();
  std::vector<std::unique_ptr<views::Part>> parts;
  std::vector<std::unique_ptr<Chunk>> chunks;
  if(group) for(std::size_t i=0;i<n;i+=group)
    chunks.push_back(std::make_unique<Chunk>(i,std::min(group,n-i),part_bytes));
  else {
    parts.reserve(n);
    for(std::size_t i=0;i<n;++i) parts.push_back(std::make_unique<views::Part>(bytes(i,part_bytes)));
  }
  const auto b=Clock::now();
  std::vector<views::Lease> uses; std::vector<const std::uint8_t*> pointers;
  std::vector<std::size_t> identities;
  const auto active=[&](std::size_t i) { return shape==0 || (shape==1 ? i%10==0 : i<(n+9)/10); };
  std::size_t backing{};
  if(group) for(std::size_t i=0;i<chunks.size();++i) {
    bool retained{};
    for(std::size_t j=i*group;j<std::min(n,(i+1)*group);++j) retained|=active(j);
    if(retained) { chunks[i]->map(); backing+=chunks[i]->size; }
    else chunks[i].reset();
  }
  for(std::size_t i=0;i<n;++i) {
    if(!active(i)) { if(!group) parts[i].reset(); continue; }
    identities.push_back(i);
    if(group) pointers.push_back(chunks[i/group]->mapped+(i%group)*part_bytes);
    else {
      uses.push_back(get(parts[i]->open(1,std::vector<std::size_t>{0})));
      pointers.push_back(uses.back().data());
    }
  }
  const auto c=Clock::now();
  const auto tail=std::min<std::size_t>(part_bytes-1,63);
  for(std::size_t i=0;i<pointers.size();++i)
    check(pointers[i][0]==value(identities[i],0) && pointers[i][tail]==value(identities[i],tail));
  const auto after=census(); const auto d=Clock::now();
  std::uint64_t checksum{};
  for(int repeat=0;repeat<loops;++repeat) {
    asm volatile("" ::: "memory");
    for(auto* p:pointers) checksum+=p[0]+p[tail];
  }
  const auto e=Clock::now();
  std::uint64_t expected{};
  for(auto id:identities) expected+=value(id,0)+value(id,tail);
  check(checksum==expected*static_cast<unsigned>(loops));
  const auto rounded=(part_bytes+views::page_size()-1)/views::page_size()*views::page_size();
  if(!group) backing=pointers.size()*rounded;
  std::printf("%s,%zu,%d,%zu,%zu,%zu,%.3f,%.3f,%.3f,%ld,%ld,%ld,%ld,%zu,%zu,%llu\n",
    group?"shared_extent_source":"per_part_source",group,shape,n,part_bytes,pointers.size(),ns(a,b),ns(b,c),
    ns(d,e)/(pointers.size()*loops),after.fds-before.fds,after.vmas-before.vmas,
    after.rss_kib-before.rss_kib,after.virtual_kib-before.virtual_kib,
    backing,pointers.size()*part_bytes,static_cast<unsigned long long>(checksum));
  std::fflush(stdout);
}
}
int main(int argc,char**argv) {
  const bool smoke=argc>1 && std::string(argv[1])=="--smoke";
  const bool reverse=argc>1 && std::string(argv[1])=="--reverse";
  const rlimit no_core{0,0};check(::setrlimit(RLIMIT_CORE,&no_core)==0);
  rlimit fd_budget{};check(::getrlimit(RLIMIT_NOFILE,&fd_budget)==0);
  const auto original_soft=fd_budget.rlim_cur;
  const rlim_t needed=smoke?96:1088;
  if(fd_budget.rlim_cur<needed) {
    check(fd_budget.rlim_max>=needed);fd_budget.rlim_cur=needed;
    check(::setrlimit(RLIMIT_NOFILE,&fd_budget)==0);
  }
  std::fprintf(stderr,"resource comparison FD soft limit: %llu -> %llu (hard %llu)\n",
    static_cast<unsigned long long>(original_soft),static_cast<unsigned long long>(fd_budget.rlim_cur),
    static_cast<unsigned long long>(fd_budget.rlim_max));
  std::puts("source,group_parts,retention_shape,parts,part_bytes,retained_parts,source_init_ns,prepare_and_drop_ns,native_pair_ns,fd_delta,vma_delta,rss_delta_kib,virtual_delta_kib,retained_backing_bytes,retained_useful_bytes,checksum");
  const std::size_t n=smoke?32:1024;
  auto sizes=smoke?std::vector<std::size_t>{16,4096}:std::vector<std::size_t>{16,256,4096,65536};
  auto groups=smoke?std::vector<std::size_t>{0,16,n}:std::vector<std::size_t>{0,16,128,n};
  std::vector<int> shapes{0,1,2};
  if(reverse) { std::reverse(sizes.begin(),sizes.end()); std::reverse(groups.begin(),groups.end()); std::reverse(shapes.begin(),shapes.end()); }
  for(auto size:sizes) for(int shape:shapes) for(auto group:groups) {
      std::fflush(stdout); auto pid=::fork(); check(pid>=0);
      if(!pid) { cell(group,n,size,shape,smoke?64:4096); ::_exit(0); }
      int status{}; check(::waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0);
    }
}
