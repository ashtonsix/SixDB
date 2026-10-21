#pragma once
#include <engine/directory.h>
namespace engine::directory::detail {
bool valid_prefix(unsigned,std::uint32_t) noexcept;
orbital::Result<void> admit_entries(unsigned,std::span<const Entry>,std::optional<KeyShape>) noexcept;
}
