#pragma once
#include "store.h"
#include <engine/material.h>
#include <loom/worker.h>
#include <map>
#include <memory_resource>
#include <optional>

// Publishes a model Store's current Objects as engine::material Objects and
// reads them back through one Scope, so a visit is Orbital's own name
// resolution (Scope::bind) and Part access goes through the Representation.
namespace segdir {
class OrbitalSource {
public:
  using Handle=engine::material::Selection;
  OrbitalSource(loom::Worker&,std::pmr::memory_resource&,const Store& model,std::uint64_t space);
  ~OrbitalSource();
  OrbitalSource(const OrbitalSource&)=delete;
  OrbitalSource& operator=(const OrbitalSource&)=delete;
  Handle get(std::uint64_t name) const {return scope_->bind({space_,name});}
  const std::byte* part(Handle h,unsigned i) const {return h.part(i)->address();}
  unsigned parts(Handle h) const {return unsigned(h.part_count());}
  std::size_t definitions() const {return definitions_.size();}
private:
  std::uint64_t space_;
  std::map<std::vector<std::uint64_t>,engine::material::Definition> definitions_;
  engine::material::Store store_;
  std::optional<engine::material::Store::Scope> scope_;
};
}
