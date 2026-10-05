#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <span>
#include <string>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
using Bytes=std::vector<unsigned char>;
using Clock=std::chrono::steady_clock;
void check(bool ok) { if(!ok) std::abort(); }
void number(Bytes& bytes,std::size_t offset,std::uint64_t value,std::size_t n) {
  for(std::size_t i=0;i<n;++i) bytes[offset+i]=static_cast<unsigned char>(value>>(8*i));
}
std::uint64_t number(std::span<const unsigned char> bytes,std::size_t offset,std::size_t n) {
  std::uint64_t result{};
  for(std::size_t i=0;i<n;++i) result|=std::uint64_t(bytes[offset+i])<<(8*i);
  return result;
}
unsigned char value(std::size_t event,std::size_t byte) {
  return static_cast<unsigned char>((event*13+byte*17+23)%251);
}
std::uint32_t hash(std::span<const unsigned char> bytes) {
  std::uint32_t h=2166136261U;
  for(auto byte:bytes) h=(h^byte)*16777619U;
  return h;
}
Bytes record(std::size_t event,std::size_t body) {
  Bytes bytes(16+body);
  for(std::size_t i=0;i<body;++i) bytes[16+i]=value(event,i);
  number(bytes,0,event,8); number(bytes,8,body,4);
  number(bytes,12,hash(std::span(bytes).subspan(16)),4); return bytes;
}
struct File {
  char path[64]="/tmp/orbital-journal-cost-XXXXXX"; int fd;
  File():fd(::mkstemp(path)) { check(fd>=0); }
  ~File() { ::close(fd); ::unlink(path); }
};
struct Work { std::size_t writes{},syncs{},copied{},bytes{}; };
void scatter(int fd,std::vector<iovec> vectors,Work& work,
             std::size_t cap=std::numeric_limits<std::size_t>::max()) {
  std::size_t first{};
  std::vector<iovec> request; request.reserve(vectors.size());
  while(first<vectors.size()) {
    request.clear(); auto left=cap;
    for(std::size_t i=first;i<vectors.size() && left;++i) {
      const auto size=std::min(left,vectors[i].iov_len);
      request.push_back({vectors[i].iov_base,size}); left-=size;
    }
    const auto n=::writev(fd,request.data(),static_cast<int>(request.size())); ++work.writes;
    if(n<0 && errno==EINTR) continue;
    check(n>0); work.bytes+=static_cast<std::size_t>(n);
    auto remaining=static_cast<std::size_t>(n);
    while(remaining) {
      auto& current=vectors[first];
      const auto consumed=std::min(remaining,current.iov_len);
      current.iov_base=static_cast<unsigned char*>(current.iov_base)+consumed;
      current.iov_len-=consumed; remaining-=consumed;
      if(!current.iov_len) ++first;
    }
  }
}
Bytes read_file(int fd) {
  struct stat st{}; check(::fstat(fd,&st)==0); Bytes bytes(static_cast<std::size_t>(st.st_size));
  std::size_t offset{};
  while(offset<bytes.size()) {
    const auto n=::pread(fd,bytes.data()+offset,bytes.size()-offset,static_cast<off_t>(offset));
    if(n<0 && errno==EINTR) continue;
    check(n>0); offset+=static_cast<std::size_t>(n);
  }
  return bytes;
}
struct Parsed { std::size_t events{},complete_bytes{}; bool corrupt{}; };
Parsed parse(std::span<const unsigned char> bytes) {
  Parsed result;
  while(bytes.size()-result.complete_bytes>=16) {
    const auto offset=result.complete_bytes;
    const auto event=number(bytes,offset,8),size=number(bytes,offset+8,4);
    if(size>(1U<<20) || event!=result.events) { result.corrupt=true; break; }
    if(size>bytes.size()-offset-16) break;
    const auto body=bytes.subspan(offset+16,static_cast<std::size_t>(size));
    if(hash(body)!=number(bytes,offset+12,4)) { result.corrupt=true; break; }
    for(std::size_t i=0;i<body.size();++i)
      if(body[i]!=value(result.events,i)) { result.corrupt=true; return result; }
    result.complete_bytes+=16+size; ++result.events;
  }
  return result;
}
void checks() {
  File file; Work work;
  auto a=record(0,47),b=record(1,65);
  scatter(file.fd,{{a.data(),13},{a.data()+13,a.size()-13},{b.data(),b.size()}},work,7);
  check(work.bytes==a.size()+b.size() && work.writes>1);
  check(::fdatasync(file.fd)==0);
  const auto child=::fork(); check(child>=0);
  if(!child) {
    const int recovered=::open(file.path,O_RDONLY); check(recovered>=0);
    const auto replay=parse(read_file(recovered)); ::close(recovered);
    check(replay.events==2 && !replay.corrupt); ::_exit(0);
  }
  int status{}; check(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
  auto saved=read_file(file.fd); saved[19]^=1; check(parse(saved).corrupt);
  saved=read_file(file.fd); auto tail=record(2,47); saved.insert(saved.end(),tail.begin(),tail.end()-11);
  const auto partial=parse(saved); check(!partial.corrupt && partial.events==2 && partial.complete_bytes==a.size()+b.size());
}
void cell(std::size_t body,std::size_t batch,std::size_t events,bool gather) {
  std::vector<Bytes> records; records.reserve(events);
  for(std::size_t i=0;i<events;++i) records.push_back(record(i,body));
  Bytes scratch(batch*(16+body)); std::vector<iovec> vectors; vectors.reserve(2*batch);
  File file; Work work; double max_batch_ns{};
  const auto start=Clock::now();
  for(std::size_t first=0;first<events;first+=batch) {
    const auto at=Clock::now(); const auto count=std::min(batch,events-first); vectors.clear();
    if(gather) {
      for(std::size_t i=0;i<count;++i) {
        auto& bytes=records[first+i]; vectors.push_back({bytes.data(),16});
        vectors.push_back({bytes.data()+16,body});
      }
    } else {
      std::size_t offset{};
      for(std::size_t i=0;i<count;++i) {
        auto& bytes=records[first+i]; std::memcpy(scratch.data()+offset,bytes.data(),bytes.size()); offset+=bytes.size();
      }
      work.copied+=offset; vectors.push_back({scratch.data(),offset});
    }
    scatter(file.fd,vectors,work); check(::fdatasync(file.fd)==0); ++work.syncs;
    max_batch_ns=std::max(max_batch_ns,std::chrono::duration<double,std::nano>(Clock::now()-at).count());
  }
  const auto finish=Clock::now();
  const auto replay=parse(read_file(file.fd)); check(replay.events==events && !replay.corrupt);
  check(work.bytes==events*(body+16) && work.syncs==(events+batch-1)/batch);
  const auto elapsed=std::chrono::duration<double,std::nano>(finish-start).count();
  std::printf("%s,%zu,%zu,%zu,%.3f,%.3f,%zu,%zu,%zu,%zu\n",gather?"gather":"concatenate",body,batch,events,
    elapsed/events,max_batch_ns,work.writes,work.syncs,work.copied,work.bytes);
}
}
int main(int argc,char**argv) {
  const bool smoke=argc>1 && std::string(argv[1])=="--smoke";
  const bool reverse=argc>1 && std::string(argv[1])=="--reverse";
  checks();
  std::puts("packing,event_bytes,batch,events,port_ns_per_event,max_service_batch_ns,write_syscalls,sync_syscalls,user_copied_bytes,wire_bytes");
  std::vector<std::size_t> batches=smoke?std::vector<std::size_t>{1,4}:std::vector<std::size_t>{1,16,64};
  if(reverse) std::reverse(batches.begin(),batches.end());
  for(auto size:smoke?std::vector<std::size_t>{47}:std::vector<std::size_t>{64,4096,65536})
    for(auto batch:batches) for(bool gather:reverse?std::vector<bool>{true,false}:std::vector<bool>{false,true})
      cell(size,batch,smoke?8:256,gather);
}
