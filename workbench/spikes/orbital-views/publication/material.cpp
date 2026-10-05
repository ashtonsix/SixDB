#include "material.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>

namespace publication {
std::string_view name(Kind k) {
  switch(k) {
    case Kind::snapshot: return "snapshot";
    case Kind::flat_pages: return "flat_pages";
    case Kind::radix_pages: return "radix_pages";
    case Kind::block_pages: return "block_pages";
    case Kind::overlay: return "overlay";
  }
  std::terminate();
}
std::uint64_t hash(std::span<const std::uint8_t> data) {
  std::uint64_t value=1469598103934665603ull;
  for(auto byte:data) value=(value^byte)*1099511628211ull;
  return value;
}
void Counts::allocated(std::size_t n) { ++allocations; live_bytes+=n; peak_bytes=std::max(peak_bytes,live_bytes); }
void Counts::freed(std::size_t n) { assert(n<=live_bytes); live_bytes-=n; }
template<class T> std::shared_ptr<T> owned(Counts& c, std::size_t extra=0) {
  const auto bytes=sizeof(T)+extra;
  c.allocated(bytes);
  return {new T, [&c,bytes](T* p){delete p;c.freed(bytes);}};
}
std::shared_ptr<const Batch> capture(const Batch& input,Counts& c) {
  std::size_t payload=0; for(const auto& patch:input) payload+=patch.bytes.size();
  auto batch=owned<Batch>(c,input.size()*sizeof(Patch)+payload); *batch=input;
  c.payload_copied+=payload; c.metadata_copied+=input.size()*sizeof(Patch);
  return batch;
}
std::shared_ptr<const Batch> capture_coalesced(const Batch& input,Counts& c) {
  auto merged=coalesce(input);std::size_t retained=0,copied=0;
  for(const auto& p:merged)retained+=p.bytes.size();for(const auto& p:input)copied+=p.bytes.size();
  auto result=owned<Batch>(c,merged.size()*sizeof(Patch)+retained);*result=std::move(merged);
  c.payload_copied+=copied;c.metadata_copied+=input.size()*2*sizeof(std::size_t)+result->size()*sizeof(Patch);
  return result;
}
void apply(Bytes& bytes,const Batch& patches) {
  for(const auto& p:patches) {
    if(p.offset>bytes.size() || p.bytes.size()>bytes.size()-p.offset) throw std::out_of_range("patch");
    std::copy(p.bytes.begin(),p.bytes.end(),bytes.begin()+p.offset);
  }
}
Batch coalesce(const Batch& patches) {
  std::vector<std::pair<std::size_t,std::size_t>> spans;
  for(const auto& p:patches) if(!p.bytes.empty()) spans.push_back({p.offset,p.offset+p.bytes.size()});
  std::sort(spans.begin(),spans.end());
  Batch result;
  for(auto [begin,end]:spans) {
    if(!result.empty() && begin<=result.back().offset+result.back().bytes.size())
      result.back().bytes.resize(std::max(end,result.back().offset+result.back().bytes.size())-result.back().offset);
    else result.push_back({begin,Bytes(end-begin)});
  }
  for(const auto& p:patches) {
    auto it=std::upper_bound(result.begin(),result.end(),p.offset,[](auto at,const Patch& x){return at<x.offset;});
    if(it!=result.begin()) --it;
    for(;it!=result.end() && it->offset<p.offset+p.bytes.size();++it) {
      const auto begin=std::max(it->offset,p.offset), end=std::min(it->offset+it->bytes.size(),p.offset+p.bytes.size());
      if(begin<end) std::copy_n(p.bytes.data()+begin-p.offset,end-begin,it->bytes.data()+begin-it->offset);
    }
  }
  return result;
}
Batch changes(std::string_view shape,std::size_t size,unsigned epoch) {
  Batch out;
  auto emit=[&](std::size_t offset,std::size_t n,unsigned salt) {
    Bytes bytes(n);
    for(std::size_t i=0;i<n;++i) bytes[i]=std::uint8_t((epoch*29+salt*71+i*17)%251+1);
    out.push_back({offset,std::move(bytes)});
  };
  if(shape=="sparse") emit((epoch*7919u)%(size-64),64,1);
  else if(shape=="dense") emit(0,size/2,1);
  else if(shape=="fragmented") for(unsigned j=0;j<128;++j) emit((j*(size/128)+epoch*7)%(size-1),1,j);
  else if(shape=="overlap") for(unsigned j=0;j<64;++j) emit((epoch*64)%(size-128)+j%16,64,j);
  else if(shape=="adjacent") for(unsigned j=0;j<64;++j) emit((size==4096?0:(epoch*4096)%(size-4096))+j*64,64,j);
  else throw std::invalid_argument("shape");
  return out;
}
struct Page {
  std::array<std::uint8_t,page_bytes> bytes{};
  std::array<std::uint64_t,page_bytes/block_bytes> blocks{};
  std::uint64_t digest=0;
};
using PagePtr=std::shared_ptr<Page>;
struct Flat { std::vector<PagePtr> pages; };
struct Node { std::vector<PagePtr> pages; std::uint64_t digest=0; };
struct Radix { std::vector<std::shared_ptr<Node>> nodes; };
struct Block { std::array<std::uint8_t,block_bytes> bytes{}; };
struct BlockPage {
  std::array<std::shared_ptr<Block>,page_bytes/block_bytes> blocks;
  std::array<std::uint64_t,page_bytes/block_bytes> hashes{};
  std::uint64_t digest=0;
};
struct BlockNode { std::vector<std::shared_ptr<BlockPage>> pages;std::uint64_t digest=0; };
struct BlockRadix {std::vector<std::shared_ptr<BlockNode>> nodes;};
struct Overlay { Cut parent; std::shared_ptr<const Batch> patches; std::shared_ptr<Bytes> base; };
struct State {
  std::uint64_t epoch=0,digest=0;
  std::shared_ptr<std::vector<Page>> snapshot;
  std::shared_ptr<Flat> flat;
  std::shared_ptr<Radix> radix;
  std::shared_ptr<BlockRadix> blocked;
  std::shared_ptr<Overlay> overlay;
};
std::uint64_t combine(std::span<const std::uint64_t> hashes,Counts& c) {
  c.hash_metadata+=hashes.size_bytes();
  return hash({reinterpret_cast<const std::uint8_t*>(hashes.data()),hashes.size_bytes()});
}
void seal(Page& p,std::uint16_t mask,Counts& c) {
  for(unsigned b=0;b<p.blocks.size();++b) if(mask&(1u<<b)) {
    p.blocks[b]=hash({p.bytes.data()+b*block_bytes,block_bytes}); c.hash_payload+=block_bytes;
  }
  p.digest=combine(p.blocks,c);
}
std::uint64_t root(std::span<const Page> pages,Counts& c) {
  std::vector<std::uint64_t> groups;
  for(std::size_t at=0;at<pages.size();at+=fanout) {
    std::array<std::uint64_t,fanout> hashes{};
    const auto count=std::min(fanout,pages.size()-at);
    for(std::size_t j=0;j<count;++j) hashes[j]=pages[at+j].digest;
    groups.push_back(combine({hashes.data(),count},c));
  }
  return combine(groups,c);
}
Material::Material(Kind k,std::size_t bytes,bool roots,unsigned compact,Counts& counts,bool full_hash)
 :kind_(k),bytes_(bytes),roots_(roots),full_hash_(full_hash),compact_every_(compact),counts_(counts) {
  if(!bytes || bytes%page_bytes) throw std::invalid_argument("whole pages required");
  if(full_hash && k!=Kind::snapshot)throw std::invalid_argument("full hash requires snapshot");
}
Cut Material::seed(std::span<const std::uint8_t> input) {
  if(input.size()!=bytes_) throw std::invalid_argument("seed size");
  auto state=owned<State>(counts_);
  counts_.payload_copied+=bytes_;
  const auto n=bytes_/page_bytes;
  if(kind_==Kind::block_pages) {
    const auto groups=(n+fanout-1)/fanout;
    state->blocked=owned<BlockRadix>(counts_,groups*sizeof(std::shared_ptr<BlockNode>));
    std::vector<std::uint64_t> group_hashes;
    for(std::size_t i=0;i<n;i+=fanout) {
      const auto count=std::min(fanout,n-i);
      auto node=owned<BlockNode>(counts_,count*sizeof(std::shared_ptr<BlockPage>));
      std::vector<std::uint64_t> page_hashes;
      for(std::size_t j=0;j<count;++j) {
        auto page=owned<BlockPage>(counts_);
        for(std::size_t b=0;b<page->blocks.size();++b) {
          auto block=owned<Block>(counts_);std::copy_n(input.data()+(i+j)*page_bytes+b*block_bytes,block_bytes,block->bytes.data());
          if(roots_) {page->hashes[b]=hash(block->bytes);counts_.hash_payload+=block_bytes;}
          page->blocks[b]=std::move(block);
        }
        if(roots_) {page->digest=combine(page->hashes,counts_);page_hashes.push_back(page->digest);}
        node->pages.push_back(std::move(page));
      }
      if(roots_) {node->digest=combine(page_hashes,counts_);group_hashes.push_back(node->digest);}
      state->blocked->nodes.push_back(std::move(node));
    }
    if(roots_)state->digest=combine(group_hashes,counts_);
  } else if(kind_==Kind::overlay) {
    state->overlay=owned<Overlay>(counts_);
    state->overlay->base=owned<Bytes>(counts_,bytes_);
    state->overlay->base->assign(input.begin(),input.end());
  } else if(kind_==Kind::snapshot) {
    state->snapshot=owned<std::vector<Page>>(counts_,n*sizeof(Page)); state->snapshot->resize(n);
    for(std::size_t i=0;i<n;++i) {
      std::copy_n(input.data()+i*page_bytes,page_bytes,(*state->snapshot)[i].bytes.data());
      if(roots_) seal((*state->snapshot)[i],0xffff,counts_);
    }
    if(roots_) state->digest=root(*state->snapshot,counts_);
  } else {
    if(kind_==Kind::flat_pages) {
      state->flat=owned<Flat>(counts_,n*sizeof(PagePtr)); state->flat->pages.reserve(n);
    } else {
      const auto groups=(n+fanout-1)/fanout;
      state->radix=owned<Radix>(counts_,groups*sizeof(std::shared_ptr<Node>));
      state->radix->nodes.reserve(groups);
      for(std::size_t i=0;i<n;i+=fanout) {
        auto node=owned<Node>(counts_,std::min(fanout,n-i)*sizeof(PagePtr));
        node->pages.reserve(std::min(fanout,n-i)); state->radix->nodes.push_back(std::move(node));
      }
    }
    for(std::size_t i=0;i<n;++i) {
      auto page=owned<Page>(counts_); std::copy_n(input.data()+i*page_bytes,page_bytes,page->bytes.data());
      if(roots_) seal(*page,0xffff,counts_);
      if(state->flat) state->flat->pages.push_back(std::move(page));
      else state->radix->nodes[i/fanout]->pages.push_back(std::move(page));
    }
    if(roots_) {
      std::vector<std::uint64_t> groups;
      for(std::size_t i=0;i<n;i+=fanout) {
        std::array<std::uint64_t,fanout> hashes{}; const auto count=std::min(fanout,n-i);
        for(std::size_t j=0;j<count;++j) hashes[j]=state->flat?state->flat->pages[i+j]->digest:state->radix->nodes[i/fanout]->pages[j]->digest;
        auto digest=combine({hashes.data(),count},counts_); groups.push_back(digest);
        if(state->radix) state->radix->nodes[i/fanout]->digest=digest;
      }
      state->digest=combine(groups,counts_);
    }
  }
  if(kind_==Kind::overlay && roots_) {
    std::vector<Page> pages(n);
    for(std::size_t i=0;i<n;++i) { std::copy_n(input.data()+i*page_bytes,page_bytes,pages[i].bytes.data()); seal(pages[i],0xffff,counts_); }
    state->digest=root(pages,counts_);
  }
  return state;
}
Cut Material::update(Cut before,std::shared_ptr<const Batch> patches,std::uint64_t epoch) {
  if(!before || !patches || epoch<=before->epoch) throw std::invalid_argument("epoch/capture");
  for(const auto& p:*patches) if(p.offset>bytes_ || p.bytes.size()>bytes_-p.offset) throw std::out_of_range("patch");
  auto state=owned<State>(counts_); state->epoch=epoch;
  if(kind_==Kind::block_pages) {
    state->blocked=owned<BlockRadix>(counts_,before->blocked->nodes.size()*sizeof(std::shared_ptr<BlockNode>));
    state->blocked->nodes=before->blocked->nodes;
    counts_.metadata_copied+=state->blocked->nodes.size()*sizeof(std::shared_ptr<BlockNode>);
    std::vector<std::pair<std::size_t,std::uint16_t>> damaged;
    for(const auto& patch:*patches)for(std::size_t offset=0;offset<patch.bytes.size();) {
      const auto at=patch.offset+offset,p=at/page_bytes,b=at%page_bytes/block_bytes,within=at%block_bytes;
      const auto length=std::min(block_bytes-within,patch.bytes.size()-offset);
      auto& node=state->blocked->nodes[p/fanout];const auto& previous=before->blocked->nodes[p/fanout];
      if(node==previous) {
        auto clone=owned<BlockNode>(counts_,node->pages.size()*sizeof(std::shared_ptr<BlockPage>));*clone=*node;
        counts_.metadata_copied+=node->pages.size()*sizeof(std::shared_ptr<BlockPage>);node=std::move(clone);
      }
      auto& page=node->pages[p%fanout];const auto& old=previous->pages[p%fanout];
      if(page==old) {auto clone=owned<BlockPage>(counts_);*clone=*page;counts_.metadata_copied+=sizeof(BlockPage);page=std::move(clone);}
      if(page->blocks[b]==old->blocks[b]) {
        auto clone=owned<Block>(counts_);
        if(within || length!=block_bytes) {*clone=*page->blocks[b];counts_.payload_copied+=block_bytes;}
        page->blocks[b]=std::move(clone);
      }
      std::copy_n(patch.bytes.data()+offset,length,page->blocks[b]->bytes.data()+within);counts_.payload_copied+=length;
      damaged.push_back({p,std::uint16_t(1u<<b)});offset+=length;
    }
    counts_.metadata_copied+=damaged.size()*sizeof(damaged.front());
    if(roots_) {
      std::sort(damaged.begin(),damaged.end());
      for(std::size_t at=0;at<damaged.size();) {
        auto p=damaged[at].first;std::uint16_t mask=0;
        while(at<damaged.size()&&damaged[at].first==p)mask|=damaged[at++].second;
        auto& page=*state->blocked->nodes[p/fanout]->pages[p%fanout];
        for(unsigned b=0;b<page.blocks.size();++b)if(mask&(1u<<b)) {page.hashes[b]=hash(page.blocks[b]->bytes);counts_.hash_payload+=block_bytes;}
        page.digest=combine(page.hashes,counts_);
      }
      std::vector<std::uint64_t> groups;
      for(std::size_t i=0;i<state->blocked->nodes.size();++i) {
        auto& node=*state->blocked->nodes[i];
        if(state->blocked->nodes[i]!=before->blocked->nodes[i]) {
          std::vector<std::uint64_t> hashes;for(const auto& page:node.pages)hashes.push_back(page->digest);
          node.digest=combine(hashes,counts_);
        }
        groups.push_back(node.digest);
      }
      state->digest=combine(groups,counts_);
    }
    return state;
  }
  if(kind_==Kind::overlay) {
    state->overlay=owned<Overlay>(counts_); state->overlay->parent=before; state->overlay->patches=std::move(patches);
    if(compact_every_ && epoch%compact_every_==0) {
      auto bytes=read(state,0,bytes_); counts_.compact_bytes+=bytes_;
      auto base=owned<Bytes>(counts_,bytes_); *base=std::move(bytes);
      state->overlay->parent.reset(); state->overlay->patches.reset(); state->overlay->base=std::move(base);
    }
    if(roots_) {
      auto bytes=read(state,0,bytes_); counts_.metadata_copied+=(bytes_/page_bytes)*(sizeof(Page)-page_bytes);
      std::vector<Page> pages(bytes_/page_bytes);
      for(std::size_t i=0;i<pages.size();++i) { std::copy_n(bytes.data()+i*page_bytes,page_bytes,pages[i].bytes.data()); counts_.payload_copied+=page_bytes; seal(pages[i],0xffff,counts_); }
      state->digest=root(pages,counts_);
    }
    return state;
  }
  const auto n=bytes_/page_bytes;
  if(kind_==Kind::snapshot) {
    state->snapshot=owned<std::vector<Page>>(counts_,n*sizeof(Page)); *state->snapshot=*before->snapshot;
    counts_.payload_copied+=bytes_; counts_.metadata_copied+=n*(sizeof(Page)-page_bytes);
  } else if(kind_==Kind::flat_pages) {
    state->flat=owned<Flat>(counts_,n*sizeof(PagePtr)); state->flat->pages=before->flat->pages;
    counts_.metadata_copied+=n*sizeof(PagePtr);
  } else {
    state->radix=owned<Radix>(counts_,before->radix->nodes.size()*sizeof(std::shared_ptr<Node>)); state->radix->nodes=before->radix->nodes;
    counts_.metadata_copied+=state->radix->nodes.size()*sizeof(std::shared_ptr<Node>);
  }
  std::vector<std::pair<std::size_t,std::uint16_t>> damaged;
  for(const auto& patch:*patches) {
    for(std::size_t offset=0;offset<patch.bytes.size();) {
      const auto at=patch.offset+offset,p=at/page_bytes,within=at%page_bytes;
      const auto length=std::min(page_bytes-within,patch.bytes.size()-offset);
      Page* page;
      if(state->snapshot) page=&(*state->snapshot)[p];
      else {
        PagePtr* slot; const PagePtr* old;
        if(state->flat) { slot=&state->flat->pages[p]; old=&before->flat->pages[p]; }
        else {
          auto& node=state->radix->nodes[p/fanout]; const auto& previous=before->radix->nodes[p/fanout];
          if(node==previous) { auto clone=owned<Node>(counts_,node->pages.size()*sizeof(PagePtr)); *clone=*node; counts_.metadata_copied+=node->pages.size()*sizeof(PagePtr); node=std::move(clone); }
          slot=&node->pages[p%fanout]; old=&previous->pages[p%fanout];
        }
        if(*slot==*old) {
          auto clone=owned<Page>(counts_);
          if(within || length!=page_bytes) {*clone=**slot;counts_.payload_copied+=page_bytes;}
          *slot=std::move(clone);counts_.metadata_copied+=sizeof(Page)-page_bytes;
        }
        page=slot->get();
      }
      std::copy_n(patch.bytes.data()+offset,length,page->bytes.data()+within); counts_.payload_copied+=length;
      const auto begin=within/block_bytes,end=(within+length-1)/block_bytes;
      damaged.push_back({p,std::uint16_t(((std::uint32_t{1}<<(end-begin+1))-1)<<begin)});
      offset+=length;
    }
  }
  counts_.metadata_copied+=damaged.size()*sizeof(damaged.front());
  if(roots_) {
    if(full_hash_) {
      for(std::size_t p=0;p<n;++p) {
        auto& page=state->snapshot?(*state->snapshot)[p]:state->flat?*state->flat->pages[p]:*state->radix->nodes[p/fanout]->pages[p%fanout];
        // This option is used only with full snapshots: resealing shared pages
        // would mutate a retained predecessor even though values are equal.
        if(!state->snapshot) throw std::invalid_argument("full hash requires snapshot");
        seal(page,0xffff,counts_);
      }
    } else {
    std::sort(damaged.begin(),damaged.end());
    for(std::size_t at=0;at<damaged.size();) {
      const auto p=damaged[at].first; std::uint16_t mask=0;
      while(at<damaged.size() && damaged[at].first==p) mask|=damaged[at++].second;
      auto& page=state->snapshot?(*state->snapshot)[p]:state->flat?*state->flat->pages[p]:*state->radix->nodes[p/fanout]->pages[p%fanout];
      seal(page,mask,counts_);
    }
    }
    std::vector<std::uint64_t> groups;
    for(std::size_t i=0;i<n;i+=fanout) {
      if(state->radix && state->radix->nodes[i/fanout]==before->radix->nodes[i/fanout]) { groups.push_back(state->radix->nodes[i/fanout]->digest); continue; }
      std::array<std::uint64_t,fanout> hashes{}; const auto count=std::min(fanout,n-i);
      for(std::size_t j=0;j<count;++j) hashes[j]=state->snapshot?(*state->snapshot)[i+j].digest:state->flat?state->flat->pages[i+j]->digest:state->radix->nodes[i/fanout]->pages[j]->digest;
      auto digest=combine({hashes.data(),count},counts_); groups.push_back(digest);
      if(state->radix) state->radix->nodes[i/fanout]->digest=digest;
    }
    state->digest=combine(groups,counts_);
  }
  return state;
}
Bytes Material::read(Cut state,std::size_t offset,std::size_t length) {
  if(!state || offset>bytes_ || length>bytes_-offset) throw std::out_of_range("read");
  Bytes out(length); counts_.read_bytes+=length;
  if(state->overlay) {
    const auto& overlay=*state->overlay;
    if(overlay.base) std::copy_n(overlay.base->data()+offset,length,out.data());
    else {
      out=read(overlay.parent,offset,length);
      for(const auto& patch:*overlay.patches) {
        ++counts_.lookup_entries;
        const auto begin=std::max(offset,patch.offset),end=std::min(offset+length,patch.offset+patch.bytes.size());
        if(begin<end) { std::copy_n(patch.bytes.data()+begin-patch.offset,end-begin,out.data()+begin-offset); counts_.payload_copied+=end-begin; }
      }
    }
  } else if(state->blocked)for(std::size_t done=0;done<length;) {
    const auto at=offset+done,p=at/page_bytes,b=at%page_bytes/block_bytes,within=at%block_bytes,n=std::min(length-done,block_bytes-within);
    const auto& block=*state->blocked->nodes[p/fanout]->pages[p%fanout]->blocks[b];
    ++counts_.lookup_entries;std::copy_n(block.bytes.data()+within,n,out.data()+done);done+=n;
  } else for(std::size_t done=0;done<length;) {
    const auto at=offset+done,p=at/page_bytes,within=at%page_bytes,n=std::min(length-done,page_bytes-within);
    const auto& page=state->snapshot?(*state->snapshot)[p]:state->flat?*state->flat->pages[p]:*state->radix->nodes[p/fanout]->pages[p%fanout];
    ++counts_.lookup_entries; std::copy_n(page.bytes.data()+within,n,out.data()+done); done+=n;
  }
  return out;
}
std::uint64_t Material::digest(Cut cut) const { return cut->digest; }
bool Sink::submit(std::uint64_t epoch,Cut cut,std::size_t bytes) {
  if(!cut || cut->epoch!=epoch) throw std::invalid_argument("sink source epoch");
  if(bytes>limit_ || pending_>limit_-bytes) return false;
  requests_.push_back({epoch,std::move(cut),bytes,true}); pending_+=bytes; return true;
}
bool Sink::complete(std::size_t ticket,bool success) {
  if(ticket>=requests_.size() || !requests_[ticket].live) return false;
  auto& request=requests_[ticket]; request.live=false; pending_-=request.bytes;
  if(success && request.epoch>installed_) installed_=request.epoch;
  request.cut.reset(); return true;
}
}
