#include "journal.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace publication {
namespace {
constexpr std::uint64_t magic=0x31505542524f3653ull, frame_magic=0x31544e564536534bull, complete=0x31444e454536534bull;
void put(Bytes& out,std::uint64_t value) { for(unsigned i=0;i<8;++i)out.push_back(value>>(i*8)); }
std::uint64_t get(std::span<const std::uint8_t> b,std::size_t& at) {
  if(at>b.size() || b.size()-at<8) throw std::runtime_error("short metadata");
  std::uint64_t n=0; for(unsigned i=0;i<8;++i)n|=std::uint64_t(b[at++])<<(i*8); return n;
}
void exact_write(int fd,std::span<const std::uint8_t> bytes) {
  while(!bytes.empty()) { auto n=::write(fd,bytes.data(),bytes.size()); if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("write");bytes=bytes.subspan(n); }
}
Bytes exact_read(int fd,std::uint64_t offset,std::size_t length) {
  Bytes bytes(length); std::size_t at=0;
  while(at<length) { auto n=::pread(fd,bytes.data()+at,length-at,offset+at);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("read");at+=n; }
  return bytes;
}
void sync(int fd) { if(::fdatasync(fd))throw std::runtime_error("fdatasync"); }
Bytes metadata(const Batch& patches,std::size_t size) {
  Bytes meta;
  for(const auto& p:patches) {
    if(p.offset>size || p.bytes.size()>size-p.offset)throw std::out_of_range("journal patch");
    put(meta,p.offset);put(meta,p.bytes.size());put(meta,(p.bytes.size()+block_bytes-1)/block_bytes);
    for(std::size_t i=0;i<p.bytes.size();i+=block_bytes)put(meta,hash(std::span(p.bytes).subspan(i,std::min(block_bytes,p.bytes.size()-i))));
  }
  return meta;
}
Bytes encoded(std::uint64_t epoch,const Batch& patches,std::size_t size) {
  auto meta=metadata(patches,size);std::size_t payload=0;for(const auto& p:patches)payload+=p.bytes.size();
  Bytes bytes;put(bytes,frame_magic);put(bytes,epoch);put(bytes,patches.size());put(bytes,meta.size());put(bytes,payload);put(bytes,hash(bytes));put(bytes,hash(meta));
  bytes.insert(bytes.end(),meta.begin(),meta.end());for(const auto& p:patches)bytes.insert(bytes.end(),p.bytes.begin(),p.bytes.end());put(bytes,complete);return bytes;
}
}
Journal::Journal(const std::filesystem::path& path,std::span<const std::uint8_t> seed):size_(seed.size()) {
  fd_=::open(path.c_str(),O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600);if(fd_<0)throw std::runtime_error("create journal");
  try {
  Bytes header;put(header,magic);put(header,size_);put(header,block_bytes);put(header,(size_+block_bytes-1)/block_bytes);
  for(std::size_t at=0;at<size_;at+=block_bytes)put(header,hash(seed.subspan(at,std::min(block_bytes,size_-at))));
  put(header,hash(header));exact_write(fd_,header);exact_write(fd_,seed);sync(fd_);
  } catch(...) {::close(fd_);fd_=-1;throw;}
}
Journal::~Journal(){if(fd_>=0)::close(fd_);}
void Journal::append(std::uint64_t epoch,const Batch& patches) {
  if(epoch!=epoch_+1)throw std::invalid_argument("noncontiguous epoch");auto bytes=encoded(epoch,patches,size_);exact_write(fd_,bytes);sync(fd_);epoch_=epoch;
}
void Journal::partial_tail(std::uint64_t epoch,const Batch& patches,std::size_t keep) {
  auto bytes=encoded(epoch,patches,size_);if(keep>=bytes.size())throw std::invalid_argument("tail must be partial");exact_write(fd_,std::span(bytes).first(keep));sync(fd_);
}
Recovery::Recovery(const std::filesystem::path& path) {
  fd_=::open(path.c_str(),O_RDONLY|O_CLOEXEC);if(fd_<0)throw std::runtime_error("open journal");
  try {
  struct stat st{};if(::fstat(fd_,&st))throw std::runtime_error("stat");const auto end=std::uint64_t(st.st_size);
  auto fixed=exact_read(fd_,0,32);counts.metadata_read+=fixed.size();std::size_t at=0;
  if(get(fixed,at)!=magic)throw std::runtime_error("magic");size_=get(fixed,at);if(get(fixed,at)!=block_bytes)throw std::runtime_error("block size");const auto blocks=get(fixed,at);
  if(!size_ || blocks!=(size_+block_bytes-1)/block_bytes || blocks>(end-32)/8)throw std::runtime_error("base shape");
  auto table=exact_read(fd_,32,(blocks+1)*8);counts.metadata_read+=table.size();Bytes header=fixed;header.insert(header.end(),table.begin(),table.end()-8);std::size_t table_at=0;
  base_={0,size_,32+table.size(),{}};for(std::uint64_t i=0;i<blocks;++i)base_.hashes.push_back(get(table,table_at));if(get(table,table_at)!=hash(header))throw std::runtime_error("header hash");
  std::uint64_t cursor=base_.location+size_;if(cursor>end)throw std::runtime_error("short base");
  while(cursor<end) {
    if(end-cursor<56)break;auto frame=exact_read(fd_,cursor,56);counts.metadata_read+=frame.size();std::size_t p=0;
    if(get(frame,p)!=frame_magic)throw std::runtime_error("frame magic");const auto epoch=get(frame,p),npatch=get(frame,p),meta_size=get(frame,p),payload=get(frame,p),header_hash=get(frame,p),digest=get(frame,p);
    if(hash(std::span(frame).first(40))!=header_hash)throw std::runtime_error("frame header hash");
    if(meta_size>end-cursor-56 || payload>end-cursor-56-meta_size || end-cursor-56-meta_size-payload<8)break;
    auto footer=exact_read(fd_,cursor+56+meta_size+payload,8);counts.metadata_read+=8;std::size_t f=0;if(get(footer,f)!=complete)throw std::runtime_error("footer");
    if(epoch!=latest()+1 || npatch>meta_size/24)throw std::runtime_error("epoch/index");
    auto meta=exact_read(fd_,cursor+56,meta_size);counts.metadata_read+=meta.size();if(hash(meta)!=digest)throw std::runtime_error("metadata hash");
    Frame result{epoch,{}};std::size_t m=0;std::uint64_t location=cursor+56+meta_size;
    for(std::uint64_t i=0;i<npatch;++i) {
      Range range{std::size_t(get(meta,m)),std::size_t(get(meta,m)),location,{}};const auto n=get(meta,m);
      if(range.offset>size_ || range.length>size_-range.offset || n!=(range.length+block_bytes-1)/block_bytes || n>(meta.size()-m)/8 || range.length>cursor+56+meta_size+payload-location)throw std::runtime_error("patch index");
      for(std::uint64_t b=0;b<n;++b)range.hashes.push_back(get(meta,m));location+=range.length;result.patches.push_back(std::move(range));
    }
    if(m!=meta.size() || location!=cursor+56+meta_size+payload)throw std::runtime_error("index extent");frames_.push_back(std::move(result));cursor=location+8;
  }
  } catch(...) {::close(fd_);fd_=-1;throw;}
}
Recovery::~Recovery(){if(fd_>=0)::close(fd_);}
Bytes Recovery::block(const Range& range,std::size_t at) {
  const auto length=std::min(block_bytes,range.length-at*block_bytes);auto bytes=exact_read(fd_,range.location+at*block_bytes,length);counts.payload_read+=length;counts.verified_bytes+=length;
  if(hash(bytes)!=range.hashes[at])throw std::runtime_error("payload hash");return bytes;
}
void Recovery::overlay(Bytes& out,std::size_t offset,const Range& range) {
  const auto begin=std::max(offset,range.offset),end=std::min(offset+out.size(),range.offset+range.length);if(begin>=end)return;
  for(std::size_t at=(begin-range.offset)/block_bytes;at<=(end-range.offset-1)/block_bytes;++at) {
    auto bytes=block(range,at);const auto start=range.offset+at*block_bytes;
    const auto lo=std::max(begin,start),hi=std::min(end,start+bytes.size());std::copy_n(bytes.data()+lo-start,hi-lo,out.data()+lo-offset);
  }
}
Bytes Recovery::read(std::uint64_t cut,std::size_t offset,std::size_t length) {
  if(cut>latest() || offset>size_ || length>size_-offset)throw std::out_of_range("recovery cut/range");
  Bytes out(length);overlay(out,offset,base_);
  for(const auto& frame:frames_) {if(frame.epoch>cut)break;for(const auto& patch:frame.patches){++counts.patches_consulted;overlay(out,offset,patch);}}
  return out;
}
Bytes Recovery::read_latest(std::uint64_t cut,std::size_t offset,std::size_t length) {
  if(cut>latest() || offset>size_ || length>size_-offset)throw std::out_of_range("recovery cut/range");
  Bytes out(length),needed(length,1);std::size_t remaining=length;
  auto take=[&](const Range& range) {
    const auto begin=std::max(offset,range.offset),end=std::min(offset+length,range.offset+range.length);
    if(begin>=end)return;
    for(std::size_t b=(begin-range.offset)/block_bytes;b<=(end-range.offset-1)/block_bytes;++b) {
      const auto start=range.offset+b*block_bytes,lo=std::max(begin,start),hi=std::min(end,start+block_bytes);
      bool useful=false;for(auto at=lo;at<hi;++at)useful|=bool(needed[at-offset]);
      if(!useful)continue;
      auto bytes=block(range,b);
      for(auto at=lo;at<hi;++at)if(needed[at-offset]) {out[at-offset]=bytes[at-start];needed[at-offset]=0;--remaining;}
    }
  };
  for(auto f=frames_.rbegin();f!=frames_.rend() && remaining;++f) {
    if(f->epoch>cut)continue;
    for(auto p=f->patches.rbegin();p!=f->patches.rend() && remaining;++p) {++counts.patches_consulted;take(*p);}
  }
  if(remaining)take(base_);
  return out;
}
}
