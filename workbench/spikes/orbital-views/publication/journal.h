#pragma once
#include "material.h"
#include <filesystem>
#include <map>

namespace publication {
struct RecoveryCounts {
  std::uint64_t metadata_read=0,payload_read=0,verified_bytes=0,patches_consulted=0;
};
// Experimental single-writer replayable afterimages. This checks material and
// ordering, not SQL semantic admission or a consensus/security format.
class Journal {
  int fd_=-1;
  std::size_t size_=0;
  std::uint64_t epoch_=0;
public:
  Journal(const std::filesystem::path&,std::span<const std::uint8_t> seed);
  ~Journal();
  Journal(const Journal&)=delete;
  void append(std::uint64_t,const Batch&);
  void partial_tail(std::uint64_t,const Batch&,std::size_t keep);
  std::uint64_t admitted() const { return epoch_; }
};
class Recovery {
  struct Range { std::size_t offset,length; std::uint64_t location; std::vector<std::uint64_t> hashes; };
  struct Frame { std::uint64_t epoch; std::vector<Range> patches; };
  int fd_=-1;
  std::size_t size_=0;
  Range base_;
  std::vector<Frame> frames_;
  Bytes block(const Range&,std::size_t at);
  void overlay(Bytes&,std::size_t,const Range&);
public:
  RecoveryCounts counts;
  explicit Recovery(const std::filesystem::path&);
  ~Recovery();
  Recovery(const Recovery&)=delete;
  std::uint64_t latest() const { return frames_.empty()?0:frames_.back().epoch; }
  std::size_t size() const { return size_; }
  Bytes read(std::uint64_t cut,std::size_t offset,std::size_t length);
  Bytes read_latest(std::uint64_t cut,std::size_t offset,std::size_t length);
  Bytes eager(std::uint64_t cut) { return read(cut,0,size_); }
};
}
