#pragma once
#include "material.h"
namespace directory_spike {
struct PrefixHash;
PrefixHash* make_hash(std::span<const unsigned>,std::span<const Dirent>,bool compact=false);
void retain_hash(PrefixHash*) noexcept;
void release_hash(PrefixHash*) noexcept;
bool find_hash(const PrefixHash*,unsigned,Dirent&) noexcept;
std::size_t hash_bytes(const PrefixHash*) noexcept;
std::size_t hash_spills(const PrefixHash*) noexcept;
}
