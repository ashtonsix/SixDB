#pragma once
#include <engine/material.h>

namespace engine::material::detail {
struct Access {
  static orbital::Result<Definition> make(std::pmr::memory_resource&,std::uint64_t,
      std::uint32_t,Kind,std::span<const Geometry>,std::shared_ptr<const void> = {});
};
}
